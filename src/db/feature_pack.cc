#include "tiles/db/feature_pack.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <queue>
#include <utility>

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
  auto const crc = static_cast<std::uint32_t>(
      ::crc32_z(0UL, reinterpret_cast<unsigned char const*>(buf_.data()),
                buf_.size()));
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
    std::stable_sort(begin(sorted_per_shard[i]), end(sorted_per_shard[i]),
                     [](auto const& a, auto const& b) {
                       return a.tile_ < b.tile_;
                     });
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
  };
  auto tasks = std::vector<tile_task>{};
  while (!heap.empty()) {
    auto const top = heap.top();
    heap.pop();
    auto const& span = sorted_per_shard[top.shard_idx_];
    auto const& e = span[top.pos_];

    if (tasks.empty() || tasks.back().tile_ != e.tile_) {
      tasks.push_back({e.tile_, {}});
    }
    tasks.back().blobs_.emplace_back(shards[top.shard_idx_].blob(e));

    auto const next = top.pos_ + 1U;
    if (next < span.size()) {
      heap.push({span[next].tile_, top.shard_idx_, next});
    }
  }

  // Parallel repack; serial append + LMDB write.
  auto txn = db_handle.make_txn();
  auto feature_dbi = db_handle.features_dbi(txn);
  std::size_t batch_count = 0;

  utl::parallel_ordered_collect_threadlocal<std::monostate>(
      tasks.size(),
      [&](auto& /*local*/, std::size_t const i)
          -> std::pair<geo::tile, std::string> {
        auto const& t = tasks[i];
        auto const tile = key_to_tile(t.tile_);
        return {tile, pack_features(tile, metadata_coder, t.blobs_)};
      },
      [&](std::size_t /*idx*/, std::pair<geo::tile, std::string>&& result) {
        auto const& [tile, blob] = result;
        auto const rec = pack_handle.append(blob);
        txn.put(feature_dbi, tile_to_key(tile), pack_records_serialize(rec));

        if (++batch_count >= 1024U) {
          txn.commit();
          batch_count = 0;
          txn = db_handle.make_txn();
          feature_dbi = db_handle.features_dbi(txn);
        }
      });

  txn.commit();
}

}  // namespace tiles
