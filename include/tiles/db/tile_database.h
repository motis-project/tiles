#pragma once

#include <map>

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

#include "tiles/util.h"

namespace tiles {

constexpr auto kDefaultMeta = "default_meta";
constexpr auto kDefaultFeatures = "default_features";
constexpr auto kDefaultTiles = "default_tiles";

constexpr auto kMetaKeyMaxPreparedZoomLevel = "max-prepared-zoomlevel";
constexpr auto kMetaKeyFullySeasideTree = "fully-seaside-tree";
constexpr auto kMetaKeyLayerNames = "layer-names";
constexpr auto kMetaKeyFeatureMetaCoding = "feature-meta-coding";

using dbi_opener_fn =
    std::function<lmdb::txn::dbi(lmdb::txn&, lmdb::dbi_flags)>;

inline lmdb::env make_tile_database(
    char const* db_fname,
    lmdb::env_open_flags flags = lmdb::env_open_flags::NOSUBDIR) {
  lmdb::env e;
  e.set_mapsize(1024ULL * 1024 * 1024 * 1024);
  e.set_maxdbs(8);
  try {
    e.open(db_fname, flags);
  } catch (...) {
    t_log("make_tile_database failed [file={}]", db_fname);
    throw;
  }
  return e;
}

struct tile_db_handle {
  explicit tile_db_handle(lmdb::env& env,
                          char const* dbi_name_meta = kDefaultMeta,
                          char const* dbi_name_features = kDefaultFeatures,
                          char const* dbi_name_tiles = kDefaultTiles)
      : env_{env},
        dbi_name_meta_{dbi_name_meta},
        dbi_name_features_{dbi_name_features},
        dbi_name_tiles_{dbi_name_tiles} {
    auto txn = make_txn();
    meta_dbi(txn, lmdb::dbi_flags::CREATE);
    features_dbi(txn, lmdb::dbi_flags::CREATE);
    tiles_dbi(txn, lmdb::dbi_flags::CREATE);
    txn.commit();
  }

  lmdb::txn make_txn() {
    auto env_flags = env_.get_flags();

    auto txn_flags = lmdb::txn_flags::NONE;
    if ((env_flags & lmdb::env_open_flags::RDONLY) !=
        lmdb::env_open_flags::NONE) {
      txn_flags = lmdb::txn_flags::RDONLY;
    }

    return lmdb::txn{env_, txn_flags};
  }

  lmdb::txn::dbi meta_dbi(lmdb::txn& txn,
                          lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) const {
    return txn.dbi_open(dbi_name_meta_, flags);
  }

  lmdb::txn::dbi features_dbi(
      lmdb::txn& txn, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) const {
    return txn.dbi_open(dbi_name_features_,
                        flags | lmdb::dbi_flags::INTEGERKEY);
  }

  lmdb::txn::dbi tiles_dbi(
      lmdb::txn& txn, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) const {
    return txn.dbi_open(dbi_name_tiles_, flags | lmdb::dbi_flags::INTEGERKEY);
  }

  auto meta_dbi_opener() {
    return [this](lmdb::txn& txn, lmdb::dbi_flags flags) {
      return meta_dbi(txn, flags);
    };
  }

  auto features_dbi_opener() {
    return [this](lmdb::txn& txn, lmdb::dbi_flags flags) {
      return features_dbi(txn, flags);
    };
  }

  auto tiles_dbi_opener() {
    return [this](lmdb::txn& txn, lmdb::dbi_flags flags) {
      return tiles_dbi(txn, flags);
    };
  }

  lmdb::env& env_;
  char const* dbi_name_meta_;
  char const* dbi_name_features_;
  char const* dbi_name_tiles_;
};

struct dbi_handle {
  dbi_handle(tile_db_handle& handle, dbi_opener_fn dbi_opener,
             lmdb::dbi_flags flags = lmdb::dbi_flags::CREATE)
      : env_{handle.env_}, dbi_opener_{std::move(dbi_opener)}, flags_{flags} {}

  dbi_handle(lmdb::env& env, std::string dbiname,
             lmdb::dbi_flags flags = lmdb::dbi_flags::CREATE)
      : env_{env},
        dbi_opener_{
            [dbiname{std::move(dbiname)}](
                lmdb::txn& txn, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) {
              return txn.dbi_open(dbiname.c_str(), flags);
            }},
        flags_{flags} {}

  std::pair<lmdb::txn, lmdb::txn::dbi> begin_txn() {
    lmdb::txn txn{env_};
    lmdb::txn::dbi dbi = dbi_opener_(txn, flags_);
    return {std::move(txn), dbi};
  }

  lmdb::env& env_;
  dbi_opener_fn dbi_opener_;
  lmdb::dbi_flags flags_;
};

}  // namespace tiles
