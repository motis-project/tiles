#include "tiles/db/prepare_tiles.h"

#include <chrono>
#include <mutex>
#include <numeric>
#include <thread>

#include "geo/tile.h"

#include "tiles/db/pack_file.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/get_tile.h"
#include "tiles/perf_counter.h"
#include "tiles/util.h"

namespace tiles {

struct prepare_task {
  explicit prepare_task(geo::tile tile) : tile_{tile} {}
  geo::tile tile_;
  std::vector<std::pair<geo::tile, pack_record>> packs_;
  std::optional<std::string> result_;
};

struct prepare_stats {
  uint64_t n_total_{0};
  uint64_t n_finished_{0};
  uint64_t n_empty_{0};
  uint64_t sum_size_{0};
  uint64_t sum_dur_{0};
};

struct prepare_manager {
  prepare_manager(geo::tile_range base_range, uint32_t max_zoomlevel)
      : max_zoomlevel_{max_zoomlevel},
        curr_zoomlevel_{0},
        base_range_{base_range},
        curr_range_{geo::tile_range_on_z(base_range_, curr_zoomlevel_)},
        stats_(max_zoomlevel + 1) {
#ifdef TILES_GLOBAL_PROGRESS_TRACKER
    utl::get_active_progress_tracker()->in_high(max_zoomlevel);
#endif
  }

  std::vector<prepare_task> get_batch() {
    auto const lock = std::lock_guard<std::mutex>{mutex_};
    // do not process all expensive low-z tiles in one thread
    auto const batch_size = 1U << 9U;
    auto const batch_inc = 1U << static_cast<uint32_t>(std::max(
                               9 - static_cast<int>(curr_zoomlevel_), 0));

    std::vector<prepare_task> batch;
    for (auto i = 0U; i < batch_size; i += batch_inc) {
      if (curr_zoomlevel_ > max_zoomlevel_) {
        break;
      }

      ++stats_[curr_zoomlevel_].n_total_;
      batch.emplace_back(*curr_range_.begin_);
      ++curr_range_.begin_;

      if (curr_range_.begin() == curr_range_.end()) {
        ++curr_zoomlevel_;
        curr_range_ = geo::tile_range_on_z(base_range_, curr_zoomlevel_);

#ifdef TILES_GLOBAL_PROGRESS_TRACKER
        utl::get_active_progress_tracker()->increment();
#endif
      }
    }
    return batch;
  }

  void finish(geo::tile tile, uint64_t size, uint64_t dur) {
    auto const lock = std::lock_guard<std::mutex>{mutex_};
    auto& stats = stats_.at(tile.z_);

    stats.sum_size_ += size;
    stats.sum_dur_ += dur;
    ++stats.n_finished_;

    if (size != 0) {
      ++stats.n_empty_;
    }

    if (tile.z_ == curr_zoomlevel_ || stats.n_finished_ < stats.n_total_) {
      return;
    }

    t_log("tiles lvl {:>2} | {} | {} total (avg. {} excl. {} empty)", tile.z_,
          printable_ns{stats.sum_dur_}, printable_num{stats.n_total_},
          printable_bytes{stats.n_total_ == stats.n_empty_
                              ? 0.
                              : static_cast<double>(stats.sum_size_) /
                                    (stats.n_total_ - stats.n_empty_)},
          printable_num{stats.n_empty_});
  }

  std::mutex mutex_;
  std::uint32_t max_zoomlevel_, curr_zoomlevel_;
  geo::tile_range base_range_, curr_range_;
  std::vector<prepare_stats> stats_;
};

prepare_manager make_prepare_manager(tile_db_handle& db_handle,
                                     uint32_t max_zoomlevel) {
  auto minx = std::numeric_limits<uint32_t>::max();
  auto miny = std::numeric_limits<uint32_t>::max();
  auto maxx = std::numeric_limits<uint32_t>::min();
  auto maxy = std::numeric_limits<uint32_t>::min();

  auto txn = db_handle.make_txn();
  auto feature_dbi = db_handle.features_dbi(txn);
  auto c = lmdb::cursor{txn, feature_dbi};
  for (auto el = c.get<tile_key_t>(lmdb::cursor_op::FIRST); el;
       el = c.get<tile_key_t>(lmdb::cursor_op::NEXT)) {
    auto const tile = key_to_tile(el->first);
    minx = std::min(minx, tile.x_);
    miny = std::min(miny, tile.y_);
    maxx = std::max(maxx, tile.x_);
    maxy = std::max(maxy, tile.y_);
  }

  utl::verify(minx != std::numeric_limits<uint32_t>::max() &&
                  miny != std::numeric_limits<uint32_t>::max(),
              "prepare_tiles: invalid bounds (no features in database?)");

  return prepare_manager{
      geo::make_tile_range(minx, miny, maxx, maxy, kTileDefaultIndexZoomLvl),
      max_zoomlevel};
}

void prepare_tiles(tile_db_handle& db_handle, pack_handle& pack_handle,
                   uint32_t max_zoomlevel) {
  auto m = make_prepare_manager(db_handle, max_zoomlevel);

  auto render_ctx = make_render_ctx(db_handle);
  render_ctx.ignore_fully_seaside_ = true;
  render_ctx.tb_aggregate_lines_ = true;
  render_ctx.tb_aggregate_polygons_ = true;
  null_perf_counter npc;

  std::vector<std::thread> threads;
  threads.reserve(std::thread::hardware_concurrency());
  for (auto i = 0U; i < std::thread::hardware_concurrency(); ++i) {
    threads.emplace_back([&] {
      while (true) {
        auto batch = m.get_batch();
        if (batch.empty()) {
          break;
        }

        {
          auto txn = db_handle.make_txn();
          auto feature_dbi = db_handle.features_dbi(txn);
          auto c = lmdb::cursor{txn, feature_dbi};

          for (auto& task : batch) {
            pack_records_foreach(c, task.tile_, [&](auto t, auto r) {
              task.packs_.emplace_back(t, r);
            });
          }
        }

        for (auto& task : batch) {
          using namespace std::chrono;
          auto start = steady_clock::now();
          task.result_ = get_tile(
              render_ctx, task.tile_,
              [&](auto&& fn) {
                std::for_each(begin(task.packs_), end(task.packs_),
                              [&](auto const& p) {
                                fn(p.first, pack_handle.get(p.second));
                              });
              },
              npc);
          auto finish = steady_clock::now();

          m.finish(task.tile_, task.result_ ? task.result_->size() : 0,
                   duration_cast<nanoseconds>(finish - start).count());
        }

        {
          if (std::none_of(begin(batch), end(batch),
                           [](auto const& t) { return t.result_; })) {
            continue;
          }

          auto txn = db_handle.make_txn();
          auto tiles_dbi = db_handle.tiles_dbi(txn);
          for (auto& task : batch) {
            if (task.result_) {
              txn.put(tiles_dbi, tile_to_key(task.tile_), *task.result_);
            }
          }
          txn.commit();
        }
      }
    });
  }
  std::for_each(begin(threads), end(threads), [](auto& t) { t.join(); });

  auto txn = db_handle.make_txn();
  auto meta_dbi = db_handle.meta_dbi(txn);
  txn.put(meta_dbi, kMetaKeyMaxPreparedZoomLevel,
          std::to_string(max_zoomlevel));
  txn.commit();
}

}  // namespace tiles
