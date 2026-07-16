#pragma once

#include <string>

namespace tiles {

struct tile_db_handle;
struct feature_shard;

void load_coastlines(tile_db_handle&, feature_shard&, std::string const& fname);

}  // namespace tiles
