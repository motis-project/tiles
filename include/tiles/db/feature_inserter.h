#pragma once

#include <map>

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

#include "tiles/db/feature_pack.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/util.h"

namespace tiles {

struct feature_inserter : public batch_inserter {
  feature_inserter(tile_db_handle& handle,
                   lmdb::txn::dbi (tile_db_handle::*dbi_opener)(lmdb::txn&, lmdb::dbi_flags),
                   lmdb::dbi_flags flags = lmdb::dbi_flags::CREATE)
      : batch_inserter(handle, dbi_opener, flags) {
    init_fill_state();
  }

  feature_inserter(lmdb::env& env, char const* dbname, lmdb::dbi_flags flags)
      : batch_inserter(env, dbname, flags) {
    init_fill_state();
  }

  ~feature_inserter() {
    if (!txn_.committed_ && !done_) {
      flush();
    }
  }

  void insert(feature const& f) {
    auto const box = bounding_box(f.geometry_);
    auto const value = serialize_feature(f);

    for (auto const& tile : make_tile_range(box)) {
      pending_size_ += value.size();

      auto& pending = pending_features_[tile];
      pending.first = pending_idx_++;
      pending.second.push_back(value);
    }

    constexpr size_t kCacheThresholdUpper = 64ull * 1024 * 1024;
    constexpr size_t kCacheThresholdLower = kCacheThresholdUpper / 2;

    if (pending_size_ < kCacheThresholdUpper) {
      return;
    }

    auto entries = utl::to_vec(pending_features_, [](auto const& pair) {
      return std::make_pair(pair.second.first, pair.first);
    });
    std::sort(begin(entries), end(entries));

    size_t persisted_packs = 0;
    size_t persisted_features = 0;
    size_t persisted_size = 0;
    for (auto const& [index, tile] : entries) {
      if (pending_size_ < kCacheThresholdLower) {
        break;
      }

      auto it = pending_features_.find(tile);
      utl::verify(it != end(pending_features_), "cannot happen");

      auto const size =
          std::accumulate(begin(it->second.second), end(it->second.second), 0ull,
                          [](auto const acc, auto const str) { return acc + str.size(); });
      pending_size_ -= size;

      ++persisted_packs;
      persisted_features += it->second.second.size();
      persisted_size += size;

      persist(tile, it->second.second);
      pending_features_.erase(it);
    }

    t_log("persisted {} packs with {} features ({})", printable_num{persisted_packs},
          printable_num{persisted_features}, printable_bytes{persisted_size});
  }

  void insert_unbuffered(geo::tile const& tile, std::string const& str) { persist(tile, {str}); }

  void flush() {
    for (auto const& [tile, features] : pending_features_) {
      persist(tile, features.second);
    }
    pending_features_ = {};
  }

  void persist(geo::tile const& tile, std::vector<std::string> const& features) {
    auto const idx = ++fill_state_[tile];
    auto const key = make_feature_key(tile, idx);
    batch_inserter::insert(key, pack_features(features));
  }

private:
  void init_fill_state() {
    auto c = lmdb::cursor{txn_, dbi_};
    for (auto el = c.get<tile_index_t>(lmdb::cursor_op::FIRST); el;
         el = c.get<tile_index_t>(lmdb::cursor_op::NEXT)) {
      auto const tile = feature_key_to_tile(el->first);
      fill_state_[tile] = feature_key_to_idx(el->first);
    }
  }

  // (x, y) -> idx
  std::map<geo::tile, size_t> fill_state_;

  size_t pending_idx_{0};
  size_t pending_size_{0};

  // (x, y) -> feature string
  std::map<geo::tile, std::pair<size_t, std::vector<std::string>>> pending_features_;
};

}  // namespace tiles
