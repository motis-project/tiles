#include "tiles/db/feature_pack.h"

#include <condition_variable>
#include <algorithm>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "zlib.h"

#include "utl/concat.h"
#include "utl/equal_ranges.h"
#include "utl/equal_ranges_linear.h"
#include "utl/erase_if.h"
#include "utl/parallel_for.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "tiles/bin_utils.h"
#include "tiles/db/feature_pack_quadtree.h"
#include "tiles/db/feature_shard.h"
#include "tiles/db/pack_file.h"
#include "tiles/db/quad_tree.h"
#include "tiles/db/repack_features.h"
#include "tiles/db/shared_metadata.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/deserialize.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/fixed/io/dump.h"
#include "tiles/mvt/tile_spec.h"
#include "tiles/util_parallel.h"

namespace tiles {

void feature_packer::finish() {
  auto const crc = static_cast<std::uint32_t>(::crc32_z(
      0UL, reinterpret_cast<unsigned char const*>(buf_.data()), buf_.size()));
  tiles::append<uint32_t>(buf_, crc);
}

bool feature_pack_valid(std::string_view const sv) {
  if (sv.size() < sizeof(uint32_t)) {
    return false;
  }
  auto const crc = static_cast<std::uint32_t>(
      ::crc32_z(0UL, reinterpret_cast<unsigned char const*>(sv.data()),
                sv.size() - sizeof(uint32_t)));
  return tiles::read<uint32_t>(sv.data(), sv.size() - sizeof(uint32_t)) == crc;
}

std::string pack_features(std::vector<std::string> const& serialized_features) {
  feature_packer p;
  p.finish_header(serialized_features.size());
  p.append_features(begin(serialized_features), end(serialized_features));
  p.finish();
  return p.buf_;
}

std::string pack_features(geo::tile const& tile,
                          shared_metadata_coder const& metadata_coder,
                          std::vector<std::string> const& packs) {
  quadtree_feature_packer p{tile, metadata_coder};
  p.pack_features(packs);
  p.finish();
  return p.packer_.buf_;
}

void pack_features(tile_db_handle& db_handle, pack_handle& pack_handle) {
  auto const metadata_coder = make_shared_metadata_coder(db_handle);
  pack_features(db_handle, pack_handle,
                [&](auto const tile, auto const& packs) {
                  return pack_features(tile, metadata_coder, packs);
                });
}

void pack_features(
    tile_db_handle& db_handle, pack_handle& pack_handle,
    std::function<std::string(geo::tile, std::vector<std::string> const&)>
        pack_fn) {
  std::vector<tile_record> tasks;
  {
    auto txn = db_handle.make_txn();
    auto feature_dbi = db_handle.features_dbi(txn);
    lmdb::cursor c{txn, feature_dbi};

    for (auto el = c.get<tile_key_t>(lmdb::cursor_op::FIRST); el;
         el = c.get<tile_key_t>(lmdb::cursor_op::NEXT)) {
      auto tile = key_to_tile(el->first);
      auto records = pack_records_deserialize(el->second);
      utl::verify(!records.empty(), "pack_features: empty pack_records");

      if (!tasks.empty() && tasks.back().tile_ == tile) {
        utl::concat(tasks.back().records_, records);
      } else {
        tasks.push_back({tile, records});
      }
    }

    txn.dbi_clear(feature_dbi);
    txn.commit();
  }

  repack_features<std::string>(pack_handle, std::move(tasks),
                               std::move(pack_fn), [&](auto const& updates) {
                                 if (updates.empty()) {
                                   return;
                                 }

                                 auto txn = db_handle.make_txn();
                                 auto feature_dbi = db_handle.features_dbi(txn);
                                 for (auto const& [tile, records] : updates) {
                                   txn.put(feature_dbi, tile_to_key(tile),
                                           pack_records_serialize(records));
                                 }
                                 txn.commit();
                               });
}

void merge_shards(shard_pool& pool, tile_db_handle& db_handle,
                  pack_handle& pack_handle) {
  auto& shards = pool.shards();
  if (shards.empty()) {
    return;
  }
  // Drain any remaining in-memory buckets so `entries()` reflects all data.
  for (auto& s : shards) {
    s.flush(0U, 0U);
  }

  auto const metadata_coder = make_shared_metadata_coder(db_handle);

  // Per-shard sort of entries by tile_key, in parallel.
  using shard_entry = feature_shard::entry;
  auto sorted_per_shard = std::vector<std::vector<shard_entry>>(shards.size());
  utl::parallel_for_run(shards.size(), [&](std::size_t const i) {
    auto const span = shards[i].entries();
    sorted_per_shard[i].assign(span.begin(), span.end());
    std::stable_sort(
        begin(sorted_per_shard[i]), end(sorted_per_shard[i]),
        [](auto const& a, auto const& b) { return a.tile_ < b.tile_; });
  });

  // K-way merge: collect (tile_key -> [(shard_idx, entry)]).
  struct heap_node {
    tile_key_t tile_;
    std::size_t shard_idx_;
    std::size_t pos_;
  };
  auto cmp = [](heap_node const& a, heap_node const& b) {
    return a.tile_ > b.tile_;  // min-heap
  };
  auto heap =
      std::priority_queue<heap_node, std::vector<heap_node>, decltype(cmp)>{
          cmp};
  for (auto i = std::size_t{0U}; i < sorted_per_shard.size(); ++i) {
    if (!sorted_per_shard[i].empty()) {
      heap.push({sorted_per_shard[i].front().tile_, i, 0U});
    }
  }

  struct tile_task {
    tile_key_t tile_;
    std::vector<std::string> blobs_;
    std::size_t bytes_;
  };

  // The k-way merge on this thread is the producer: it hands one complete tile
  // at a time to a bounded queue. A pool of worker threads pops tiles, packs
  // them concurrently (CPU), and appends the packed blob to the pack file under
  // `write_mtx`. `pack_handle.append` assigns sequential offsets, so it must be
  // serialized -- but it now overlaps the *other* workers' packing instead of
  // running in a separate barrier phase, keeping compression (CPU) and the
  // pack-file writes (disk) busy at the same time.
  //
  // The queue is bounded by in-flight blob bytes: backpressure that caps peak
  // memory usage. The LMDB feature_dbi entries are just 16-byte (offset,size)
  // records; they are collected here and written in one serial pass at the end
  // (see below), which keeps every LMDB txn on a single thread -- LMDB's write
  // lock is a pthread mutex that must be released by the thread that took it.
  // On-disk tile order is no longer tile-sorted (tiles land in pack-completion
  // order); that is fine because each tile's LMDB record points at its own
  // offset, so lookups are unaffected.
  constexpr auto kMaxInFlightBytes = std::size_t{1} << 30;  // ~1 GiB in flight

  auto q_mtx = std::mutex{};
  auto q_can_push = std::condition_variable{};
  auto q_can_pop = std::condition_variable{};
  auto queue = std::queue<tile_task>{};
  auto queue_bytes = std::size_t{0};
  auto queue_closed = false;

  auto const push_task = [&](tile_task&& t) {
    auto lock = std::unique_lock{q_mtx};
    q_can_push.wait(lock, [&] { return queue_bytes < kMaxInFlightBytes; });
    queue_bytes += t.bytes_;
    queue.push(std::move(t));
    q_can_pop.notify_one();
  };
  auto const pop_task = [&]() -> std::optional<tile_task> {
    auto lock = std::unique_lock{q_mtx};
    q_can_pop.wait(lock, [&] { return !queue.empty() || queue_closed; });
    if (queue.empty()) {
      return std::nullopt;
    }
    auto t = std::move(queue.front());
    queue.pop();
    queue_bytes -= t.bytes_;
    q_can_push.notify_one();
    return t;
  };

  auto write_mtx = std::mutex{};
  auto results = std::vector<std::pair<geo::tile, pack_record>>{};

  auto const n_workers = std::max(1U, std::thread::hardware_concurrency());
  auto workers = std::vector<std::thread>{};
  workers.reserve(n_workers);
  for (auto w = 0U; w < n_workers; ++w) {
    workers.emplace_back([&] {
      for (auto t = pop_task(); t.has_value(); t = pop_task()) {
        auto const tile = key_to_tile(t->tile_);
        auto const blob = pack_features(tile, metadata_coder, t->blobs_);
        auto lock = std::lock_guard{write_mtx};
        results.emplace_back(tile, pack_handle.append(blob));
      }
    });
  }

  // Producer: k-way merge emitting one tile_task per output tile.
  auto cur = tile_task{};
  auto have_cur = false;
  while (!heap.empty()) {
    auto const top = heap.top();
    heap.pop();
    auto const& span = sorted_per_shard[top.shard_idx_];
    auto const& e = span[top.pos_];

    if (!have_cur || cur.tile_ != e.tile_) {
      if (have_cur) {
        push_task(std::move(cur));
      }
      cur = tile_task{e.tile_, {}, 0U};
      have_cur = true;
    }
    auto blob = shards[top.shard_idx_].blob(e);
    cur.bytes_ += blob.size();
    cur.blobs_.emplace_back(std::move(blob));

    auto const next = top.pos_ + 1U;
    if (next < span.size()) {
      heap.push({span[next].tile_, top.shard_idx_, next});
    }
  }
  if (have_cur) {
    push_task(std::move(cur));
  }

  {
    auto lock = std::lock_guard{q_mtx};
    queue_closed = true;
  }
  q_can_pop.notify_all();
  for (auto& worker : workers) {
    worker.join();
  }

  // Serial LMDB write pass: one thread owns every write txn. The 16-byte
  // records are cheap, so this tail is negligible next to packing.
  auto txn = db_handle.make_txn();
  auto feature_dbi = db_handle.features_dbi(txn);
  auto batch_count = std::size_t{0};
  for (auto const& [tile, rec] : results) {
    txn.put(feature_dbi, tile_to_key(tile), pack_records_serialize(rec));
    if (++batch_count >= 1024U) {
      txn.commit();
      batch_count = 0;
      txn = db_handle.make_txn();
      feature_dbi = db_handle.features_dbi(txn);
    }
  }
  txn.commit();
}

}  // namespace tiles
