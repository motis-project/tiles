#include <iostream>

#include "conf/configuration.h"
#include "conf/options_parser.h"

#include "tiles/db/clear_database.h"
#include "tiles/db/database_stats.h"
#include "tiles/db/feature_inserter_mt.h"
#include "tiles/db/feature_pack.h"
#include "tiles/db/pack_file.h"
#include "tiles/db/prepare_tiles.h"
#include "tiles/db/tile_database.h"
#include "tiles/osm/feature_handler.h"
#include "tiles/osm/load_coastlines.h"
#include "tiles/osm/load_osm.h"

namespace tiles {

struct import_settings : public conf::configuration {
  import_settings() : conf::configuration("tiles-import options", "") {
    param(db_fname_, "db_fname", "/path/to/tiles.mdb");
    param(osm_fname_, "osm_fname", "/path/to/latest.osm.pbf");
    param(osm_profile_, "osm_profile", "/path/to/profile.lua");
    param(coastlines_fname_, "coastlines_fname", "/path/to/coastlines.zip");
    param(tmp_dname_, "tmp_dname", "/path/to/tmp/directory");
    param(tasks_, "tasks",
          "'all' or any combination of: 'coastlines', "
          "'features', 'stats', 'pack', 'tiles'");
  }

  bool has_any_task(std::vector<std::string> const& query) const {
    return std::find(begin(tasks_), end(tasks_), "all") != end(tasks_) ||
           std::any_of(begin(query), end(query), [this](auto const& q) {
             return std::find(begin(tasks_), end(tasks_), q) != end(tasks_);
           });
  }

  std::string db_fname_{"tiles.mdb"};
  std::string osm_fname_{"planet-latest.osm.pbf"};
  std::string osm_profile_{"tiles/profile/profile.lua"};
  std::string coastlines_fname_{"land-polygons-complete-4326.zip"};
  std::string tmp_dname_{"."};
  std::vector<std::string> tasks_{{"all"}};
};

int run_tiles_import(int argc, char const** argv) {
  import_settings opt;

  try {
    conf::options_parser parser({&opt});
    parser.read_command_line_args(argc, argv, false);

    if (parser.help() || parser.version()) {
      std::cout << "tiles-import\n\n";
      parser.print_help(std::cout);
      return 0;
    }

    parser.read_configuration_file(false);
    parser.print_used(std::cout);
  } catch (std::exception const& e) {
    std::cout << "options error: " << e.what() << "\n";
    return 1;
  }

  if (opt.has_any_task({"features"})) {
    check_profile(opt.osm_profile_);
  }

  if (opt.has_any_task({"coastlines", "features"})) {
    t_log("clear database");
    clear_database(opt.db_fname_, kDefaultSize);
    clear_pack_file(opt.db_fname_.c_str());
  }

  lmdb::env db_env = make_tile_database(opt.db_fname_.c_str(), kDefaultSize);
  tile_db_handle db_handle{db_env};
  pack_handle pack_handle{opt.db_fname_.c_str()};

  {
    feature_inserter_mt inserter{
        dbi_handle{db_handle, db_handle.features_dbi_opener()}, pack_handle};

    if (opt.has_any_task({"coastlines"})) {
      auto const t = scoped_timer{"load coastlines"};
      load_coastlines(db_handle, inserter, opt.coastlines_fname_);
    }

    if (opt.has_any_task({"features"})) {
      t_log("load features");
      load_osm(db_handle, inserter, opt.osm_fname_, opt.osm_profile_,
               opt.tmp_dname_);
    }
  }

  if (opt.has_any_task({"stats"})) {
    database_stats(db_handle, pack_handle);
  }

  if (opt.has_any_task({"pack"})) {
    t_log("pack features");
    pack_features(db_handle, pack_handle);
  }

  if (opt.has_any_task({"tiles"})) {
    t_log("prepare tiles");
    prepare_tiles(db_handle, pack_handle, 10);
  }

  t_log("import done!");
  return 0;
}

}  // namespace tiles

int main(int argc, char const** argv) {
  try {
    return tiles::run_tiles_import(argc, argv);
  } catch (std::exception const& e) {
    tiles::t_log("exception caught: {}", e.what());
    return 1;
  } catch (...) {
    tiles::t_log("unknown exception caught");
    return 1;
  }
}
