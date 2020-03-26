#pragma once

#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "protozero/pbf_reader.hpp"
#include "protozero/pbf_writer.hpp"

#include "protozero/pbf_message.hpp"

#include "utl/equal_ranges.h"
#include "utl/get_or_create_index.h"

#include "tiles/db/feature_pack.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/util.h"

namespace tiles {

constexpr auto const kLayerCoastlineIdx = 0ull;
constexpr auto const kLayerCoastlineName = "coastline";

template <typename Vec>
inline std::string write_layer_names(Vec&& vec) {
  std::string buf;
  protozero::pbf_writer writer{buf};
  for (auto const& name : vec) {
    writer.add_string(1, name);
  }
  return buf;
}

inline std::vector<std::string> read_layer_names(std::string_view const& str) {
  std::vector<std::string> vec;
  protozero::pbf_reader reader{str};
  while (reader.next()) {
    vec.emplace_back(reader.get_string());
  }
  return vec;
}

struct layer_names_builder {
  layer_names_builder() {
    layer_names_[kLayerCoastlineName] = kLayerCoastlineIdx;
  }

  size_t get_layer_idx(std::string const& name) {
    return utl::get_or_create_index(layer_names_, name);
  }

  void store(tile_db_handle& handle, lmdb::txn& txn) const {
    std::vector<std::string_view> sorted;
    sorted.resize(layer_names_.size());
    for (auto const& [name, idx] : layer_names_) {
      sorted.at(idx) = name;
    }

    auto buf = write_layer_names(sorted);
    auto meta_dbi = handle.meta_dbi(txn);
    txn.put(meta_dbi, kMetaKeyLayerNames, buf);
  }

  std::map<std::string, size_t> layer_names_;
};

inline std::vector<std::string> get_layer_names(tile_db_handle& handle,
                                                lmdb::txn& txn) {
  auto meta_dbi = handle.meta_dbi(txn);
  auto const opt_names = txn.get(meta_dbi, kMetaKeyLayerNames);
  if (!opt_names) {
    return {{kLayerCoastlineName}};
  }

  return read_layer_names(*opt_names);
}

struct pair_freq {
  explicit pair_freq(std::string key) : key_{std::move(key)} {}
  pair_freq(std::string key, std::string val, size_t freq)
      : key_{std::move(key)}, val_{std::move(val)}, freq_{freq} {}

  friend bool operator==(pair_freq const& a, pair_freq const& b) {
    return std::tie(a.key_, a.val_) == std::tie(b.key_, b.val_);
  }

  friend bool operator<(pair_freq const& a, pair_freq const& b) {
    return std::tie(a.key_, a.val_) < std::tie(b.key_, b.val_);
  }

  std::string key_, val_;
  size_t freq_ = 1;
};

inline void make_meta_coding(tile_db_handle& handle) {
  auto txn = handle.make_txn();
  auto features_dbi = handle.features_dbi(txn);
  auto c = lmdb::cursor{txn, features_dbi};

  std::vector<pair_freq> meta_acc;
  for (auto el = c.get<tile_index_t>(lmdb::cursor_op::FIRST); el;
       el = c.get<tile_index_t>(lmdb::cursor_op::NEXT)) {
    size_t meta_fill = 0;
    std::vector<pair_freq> tile_meta;

    unpack_features(el->second, [&](auto const& str) {
      protozero::pbf_message<tags::Feature> msg{str};
      while (msg.next()) {
        switch (msg.tag()) {
          case tags::Feature::repeated_string_keys:
            tile_meta.emplace_back(msg.get_string());
            break;
          case tags::Feature::repeated_string_values:
            utl::verify(meta_fill < tile_meta.size(),
                        "tile_meta data imbalance! (a)");
            tile_meta[meta_fill++].val_ = msg.get_string();
            break;
          default: msg.skip();
        }
      }
    });
    utl::verify(meta_fill == tile_meta.size(), "meta data imbalance! (b)");

    utl::equal_ranges(tile_meta, [&](auto lb, auto ub) {
      meta_acc.emplace_back(
          lb->key_, lb->val_,
          std::accumulate(lb, ub, 0ull, [](auto const acc, auto const& e) {
            return acc + e.freq_;
          }));
    });
  }

  std::vector<pair_freq> metas;
  utl::equal_ranges(meta_acc, [&](auto lb, auto ub) {
    auto const freq = std::accumulate(
        lb, ub, 0ull,
        [](auto const acc, auto const& e) { return acc + e.freq_; });
    if (freq > 1) {  // XXX
      metas.emplace_back(lb->key_, lb->val_, freq);
    }
  });

  std::sort(begin(metas), end(metas), [](auto const& lhs, auto const& rhs) {
    return lhs.freq_ > rhs.freq_;
  });

  std::string buf;
  protozero::pbf_writer writer{buf};
  for (auto const& meta : metas) {
    writer.add_string(1, meta.key_);
    writer.add_string(1, meta.val_);
  }

  auto meta_dbi = handle.meta_dbi(txn);
  txn.put(meta_dbi, kMetaKeyFeatureMetaCoding, buf);
  txn.commit();
}

using meta_coding_map_t = std::map<std::pair<std::string, std::string>, size_t>;

inline meta_coding_map_t load_meta_coding_map(tile_db_handle& handle) {
  auto txn = handle.make_txn();
  auto meta_dbi = handle.meta_dbi(txn);
  auto const opt_coding = txn.get(meta_dbi, kMetaKeyFeatureMetaCoding);
  if (!opt_coding) {
    return {};
  }

  meta_coding_map_t coding_map;
  protozero::pbf_reader reader{*opt_coding};
  while (reader.next()) {
    auto const size = coding_map.size();
    auto key = reader.get_string();
    utl::verify(reader.next(), "invalid meta coding (map)");
    auto value = reader.get_string();

    coding_map[{key, value}] = size;
  }
  return coding_map;
}

using meta_coding_vec_t = std::vector<std::pair<std::string, std::string>>;

inline meta_coding_vec_t load_meta_coding_vec(tile_db_handle& handle,
                                              lmdb::txn& txn) {
  auto meta_dbi = handle.meta_dbi(txn);
  auto const opt_coding = txn.get(meta_dbi, kMetaKeyFeatureMetaCoding);
  if (!opt_coding) {
    return {};
  }

  meta_coding_vec_t coding_vec;
  protozero::pbf_reader reader{*opt_coding};
  while (reader.next()) {
    auto key = reader.get_string();
    utl::verify(reader.next(), "invalid meta coding (vec)");
    auto value = reader.get_string();

    coding_vec.emplace_back(std::move(key), std::move(value));
  }
  return coding_vec;
}

inline meta_coding_vec_t load_meta_coding_vec(tile_db_handle& handle) {
  auto txn = handle.make_txn();
  return load_meta_coding_vec(handle, txn);
}

}  // namespace tiles
