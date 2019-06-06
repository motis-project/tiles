#pragma once

#include <string>

namespace tiles {

struct tile_db_handle;

void load_coastlines(tile_db_handle&, std::string const& fname);

}  // namespace tiles
