#pragma once

#include <map>

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

namespace tiles {

constexpr auto kDefaultMeta = "default_meta";
constexpr auto kDefaultFeatures = "default_features";
constexpr auto kDefaultTiles = "default_tiles";

constexpr auto kMetaKeyMaxPreparedZoomLevel = "max-prepared-zoomlevel";
constexpr auto kMetaKeyFullySeasideTree = "fully-seaside-tree";
constexpr auto kMetaKeyLayerNames = "layer-names";
constexpr auto kMetaKeyFeatureMetaCoding = "feature-meta-coding";

inline lmdb::env make_tile_database(
    char const* db_fname,
    lmdb::env_open_flags flags = lmdb::env_open_flags::NOSUBDIR) {
  lmdb::env e;
  e.set_mapsize(1024ul * 1024 * 1024 * 1024);
  e.set_maxdbs(8);
  e.open(db_fname, flags);
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
        dbi_name_tiles_{dbi_name_tiles} {}

  lmdb::txn::dbi meta_dbi(lmdb::txn& txn,
                          lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) {
    return txn.dbi_open(dbi_name_meta_, flags);
  }

  lmdb::txn::dbi features_dbi(lmdb::txn& txn,
                              lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) {
    return txn.dbi_open(dbi_name_features_,
                        flags | lmdb::dbi_flags::INTEGERKEY);
  }

  lmdb::txn::dbi tiles_dbi(lmdb::txn& txn,
                           lmdb::dbi_flags flags = lmdb::dbi_flags::NONE) {
    return txn.dbi_open(dbi_name_tiles_, flags | lmdb::dbi_flags::INTEGERKEY);
  }

  lmdb::env& env_;
  char const* dbi_name_meta_;
  char const* dbi_name_features_;
  char const* dbi_name_tiles_;
};

struct batch_inserter {
  batch_inserter(tile_db_handle& handle,
                 lmdb::txn::dbi (tile_db_handle::*dbi_opener)(lmdb::txn&,
                                                              lmdb::dbi_flags),
                 lmdb::dbi_flags flags = lmdb::dbi_flags::CREATE)
      : txn_{handle.env_},
        dbi_{(handle.*dbi_opener)(txn_, flags)},
        done_{false} {}

  batch_inserter(lmdb::env& env, char const* dbname,
                 lmdb::dbi_flags flags = lmdb::dbi_flags::CREATE)
      : txn_{env}, dbi_{txn_.dbi_open(dbname, flags)}, done_{false} {}

  ~batch_inserter() {
    if (!txn_.committed_ && !done_) {
      done_ = true;
      txn_.commit();
    }
  }

  template <typename... T>
  void insert(T&&... t) {
    txn_.put(dbi_, std::forward<T>(t)...);
  }

  lmdb::txn txn_;
  lmdb::txn::dbi dbi_;
  bool done_;
};

}  // namespace tiles