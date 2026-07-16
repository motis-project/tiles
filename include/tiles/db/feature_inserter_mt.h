#pragma once

#include <atomic>
#include <map>
#include <mutex>

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

#include "tiles/db/feature_pack.h"
#include "tiles/db/pack_file.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/fixed/io/dump.h"
#include "tiles/util.h"

namespace tiles {

struct cache_bucket {
  geo::tile tile_{};

  std::mutex mutex_;
  size_t mem_size_{0};
  std::vector<std::string> mem_;
};

struct feature_inserter_mt {
  static constexpr size_t kCacheThresholdUpper = 2ULL * 1024 * 1024 * 1024;
  static constexpr size_t kCacheThresholdLower = kCacheThresholdUpper / 4 * 3;

  feature_inserter_mt(dbi_handle dbi_handle, pack_handle& pack_handle)
      : dbi_handle_{std::move(dbi_handle)},
        pack_handle_{pack_handle},
        cache_((1ULL << kTileDefaultIndexZoomLvl) *
               (1ULL << kTileDefaultIndexZoomLvl)) {
    auto it = geo::tile_iterator{kTileDefaultIndexZoomLvl};
    for (auto& c : cache_) {
      utl::verify(it->z_ == kTileDefaultIndexZoomLvl, "it broken");
      c.tile_ = *it;
      ++it;
    }
    utl::verify(it->z_ == kTileDefaultIndexZoomLvl + 1, "it broken");
  }

  ~feature_inserter_mt() { flush(0, 0); }

  feature_inserter_mt(feature_inserter_mt const&) = delete;
  feature_inserter_mt(feature_inserter_mt&&) noexcept = delete;
  feature_inserter_mt& operator=(feature_inserter_mt const&) = delete;
  feature_inserter_mt& operator=(feature_inserter_mt&&) noexcept = delete;

  geo::tile insert(feature const& f) {
    auto const box = bounding_box(f.geometry_);
    auto const range = make_tile_range(box);
    utl::verify(range.begin() != range.end(), "inserter: no tile for feature");

    auto const value = serialize_feature(f);
    for (auto const& tile : range) {
      insert(tile, value);
    }

    flush();
    return *range.begin();
  }

  void insert(geo::tile const& tile, std::string const& value) {
    cache_size_ += value.size();
    auto& bucket = get_bucket(tile);

    std::lock_guard<std::mutex> l(bucket.mutex_);
    bucket.mem_size_ += value.size();
    bucket.mem_.push_back(value);
  }

  void flush(size_t threshold_upper = kCacheThresholdUpper,
             size_t threshold_lower = kCacheThresholdLower) {
    size_t persisted_packs = 0;
    size_t persisted_features = 0;
    size_t persisted_size = 0;

    int64_t min_x = std::numeric_limits<int64_t>::max();
    int64_t max_x = std::numeric_limits<int64_t>::min();
    int64_t min_y = std::numeric_limits<int64_t>::max();
    int64_t max_y = std::numeric_limits<int64_t>::min();

    std::vector<std::pair<cache_bucket*, std::vector<std::string>>> queue;
    // Single-flusher discipline: one thread at a time runs phase 1 *and*
    // phase 2. Other workers see `flush_mutex_` is held and skip — they
    // keep inserting while the active flusher works. Holding the lock
    // across phase 2 also avoids N concurrent LMDB write txns serializing
    // inside LMDB's writer lock. If the cache reaches a hard cap (2 ×
    // threshold_upper), late callers DO block here for backpressure.
    if (cache_size_ <= threshold_upper) {
      return;
    }
    auto flush_lock =
        std::unique_lock<std::mutex>{flush_mutex_, std::try_to_lock};
    if (!flush_lock.owns_lock()) {
      if (cache_size_ <= 2U * threshold_upper) {
        return;
      }
      flush_lock.lock();
    }
    if (cache_size_ <= threshold_upper) {
      return;
    }

    {  // phase 1: build queue
      std::vector<std::pair<size_t, cache_bucket*>> buckets;
      buckets.reserve(cache_.size());
      for (auto& b : cache_) {
        if (b.mem_size_ > 0) {
          buckets.emplace_back(b.mem_size_, &b);
        }
      }
      std::sort(begin(buckets), end(buckets));
      if (buckets.empty()) {
        return;
      }

      for (auto const& [size, bucket_ptr] : buckets) {
        if (cache_size_ < threshold_lower) {
          break;
        }

        std::lock_guard<std::mutex> bucket_lock{bucket_ptr->mutex_};
        ++persisted_packs;
        persisted_features += bucket_ptr->mem_.size();

        min_x = std::min(min_x, static_cast<int64_t>(bucket_ptr->tile_.x_));
        max_x = std::max(max_x, static_cast<int64_t>(bucket_ptr->tile_.x_));
        min_y = std::min(min_y, static_cast<int64_t>(bucket_ptr->tile_.y_));
        max_y = std::max(max_y, static_cast<int64_t>(bucket_ptr->tile_.y_));

        cache_size_ -= bucket_ptr->mem_size_;
        persisted_size += bucket_ptr->mem_size_;
        bucket_ptr->mem_size_ = 0;

        queue.emplace_back(bucket_ptr, std::vector<std::string>{});
        std::swap(queue.back().second, bucket_ptr->mem_);
      }
    }
    {  // phase 2: write to pack_file and update database
      auto txn_dbi = dbi_handle_.begin_txn();
      lmdb::cursor c{txn_dbi.first, txn_dbi.second};

      for (auto const& [bucket_ptr, features] : queue) {
        auto key = tile_to_key(bucket_ptr->tile_);
        auto pack_record = pack_handle_.append(pack_features(features));

        if (auto el = c.get(lmdb::cursor_op::SET_KEY, key); el) {
          std::string pack_records{el->second};
          pack_records_update(pack_records, pack_record);
          c.put(key, pack_records);
        } else {
          c.put(key, pack_records_serialize(pack_record));
        }
      }
      txn_dbi.first.commit();
    }

    t_log("persisted {} packs with {} features ({}) in ({}, {} / {}, {})",
          printable_num{persisted_packs}, printable_num{persisted_features},
          printable_bytes{persisted_size}, min_x, min_y, max_x, max_y);
  }

  cache_bucket& get_bucket(geo::tile const tile) {
    auto it = std::lower_bound(
        begin(cache_), end(cache_), tile, [](auto const& a, auto const& b) {
          return std::tie(a.tile_.y_, a.tile_.x_) < std::tie(b.y_, b.x_);
        });

    utl::verify(it != end(cache_) && tile == it->tile_,
                "requested invalid tile");
    return *it;
  }

  dbi_handle dbi_handle_;
  pack_handle& pack_handle_;

  std::mutex flush_mutex_;
  std::atomic_size_t cache_size_{0};
  std::vector<cache_bucket> cache_;
};

}  // namespace tiles
