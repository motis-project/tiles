#pragma once

#include "tiles/db/tile_database.h"

namespace tiles {

inline void clear_database(tile_db_handle& handle, lmdb::txn& txn) {
  auto meta_dbi = handle.meta_dbi(txn, lmdb::dbi_flags::CREATE);
  txn.dbi_clear(meta_dbi);

  auto features_dbi = handle.features_dbi(txn, lmdb::dbi_flags::CREATE);
  txn.dbi_clear(features_dbi);

  auto tiles_dbi = handle.tiles_dbi(txn, lmdb::dbi_flags::CREATE);
  txn.dbi_clear(tiles_dbi);
}

inline void clear_database(std::string const& db_fname) {
  lmdb::env db_env = make_tile_database(db_fname.c_str());
  tile_db_handle handle{db_env};

  lmdb::txn txn{handle.env_};
  clear_database(handle, txn);
  txn.commit();
}

}  // namespace tiles
