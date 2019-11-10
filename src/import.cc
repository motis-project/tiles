#include <iostream>

#include "conf/configuration.h"
#include "conf/options_parser.h"

#include "tiles/db/clear_database.h"
#include "tiles/db/database_stats.h"
#include "tiles/db/prepare_tiles.h"
#include "tiles/db/shared_strings.h"
#include "tiles/db/tile_database.h"
#include "tiles/osm/load_coastlines.h"
#include "tiles/osm/load_osm.h"

namespace tiles {

// "/home/sebastian/Downloads/land-polygons-complete-4326.zip"
// "/data/osm/2017-10-29/hessen-171029.osm.pbf"

struct import_settings : public conf::configuration {
  import_settings() : conf::configuration("tiles-import options", "") {
    param(db_fname_, "db_fname", "/path/to/tiles.mdb");
    param(osm_fname_, "osm_fname", "/path/to/latest.osm.pbf");
    param(coastlines_fname_, "coastlines_fname", "/path/to/coastlines.zip");
    param(tasks_, "tasks",
          "'all' or any combination of: 'coastlines', "
          "'features', 'stats', 'tiles'");
  }

  bool has_any_task(std::vector<std::string> const& query) {
    return std::find(begin(tasks_), end(tasks_), "all") != end(tasks_) ||
           std::any_of(begin(query), end(query), [this](auto const& q) {
             return std::find(begin(tasks_), end(tasks_), q) != end(tasks_);
           });
  }

  std::string db_fname_{"tiles.mdb"};
  std::string osm_fname_{"latest.osm.pbf"};
  std::string coastlines_fname_{"coastlines.zip"};

  std::vector<std::string> tasks_{{"all"}};
};

}  // namespace tiles

int main(int argc, char** argv) {
  tiles::import_settings opt;

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

  if (opt.has_any_task({"coastlines", "features"})) {
    tiles::t_log("clear database");
    tiles::clear_database(opt.db_fname_);
  }

  lmdb::env db_env = tiles::make_tile_database(opt.db_fname_.c_str());
  tiles::tile_db_handle handle{db_env};

  if (opt.has_any_task({"coastlines"})) {
    tiles::scoped_timer t{"load coastlines"};
    tiles::load_coastlines(handle, opt.coastlines_fname_);
    tiles::t_log("sync db");
    db_env.sync();
  }

  if (opt.has_any_task({"features"})) {
    tiles::t_log("load features");
    tiles::load_osm(handle, opt.osm_fname_);
    tiles::t_log("sync db");
    db_env.sync();
  }

  if (opt.has_any_task({"stats"})) {
    tiles::database_stats(handle);
  }

  if (opt.has_any_task({"pack"})) {
    tiles::t_log("feature meta pair coding");
    tiles::make_meta_coding(handle);

    tiles::t_log("pack features");
    tiles::pack_features(handle);
  }

  if (opt.has_any_task({"tiles"})) {
    tiles::t_log("prepare tiles");
    tiles::prepare_tiles(handle, 10);
  }

  tiles::t_log("import done!");
}
