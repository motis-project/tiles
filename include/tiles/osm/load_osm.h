#pragma once

#include <string>

namespace tiles {

struct tile_db_handle;
struct feature_inserter_mt;

void load_osm(tile_db_handle&, feature_inserter_mt&,
              std::string const& osm_fname, std::string const& osm_profile,
              std::string const& tmp_dname, size_t flush_threshold);

}  // namespace tiles
