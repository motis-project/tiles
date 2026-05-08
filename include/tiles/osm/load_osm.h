#pragma once

#include <string>

namespace tiles {

struct tile_db_handle;
struct shard_pool;

void load_osm(tile_db_handle&, shard_pool&, std::string const& osm_fname,
              std::string const& osm_profile, std::string const& tmp_dname,
              size_t flush_threshold);

}  // namespace tiles
