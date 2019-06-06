#include <exception>
#include <iomanip>
#include <iostream>

#include "boost/filesystem.hpp"

#include "net/http/server/enable_cors.hpp"
#include "net/http/server/query_router.hpp"
#include "net/http/server/server.hpp"
#include "net/http/server/shutdown_handler.hpp"

#include "tiles/db/get_tile.h"
#include "tiles/db/tile_database.h"
#include "tiles/perf_counter.h"

using namespace net::http::server;

int main() {
  lmdb::env db_env = tiles::make_tile_database("./tiles.mdb");
  tiles::tile_db_handle handle{db_env};
  auto const render_ctx = make_render_ctx(handle);

  boost::asio::io_service ios;
  server server{ios};

  query_router router;
  router.route("OPTIONS", ".*", [](auto const&, auto cb) {
    reply rep = reply::stock_reply(reply::ok);
    add_cors_headers(rep);
    cb(rep);
  });

  // z, x, y
  router.route(
      "GET", "^\\/(\\d+)\\/(\\d+)\\/(\\d+).mvt$",
      [&](auto const& req, auto cb) {
        if (std::find_if(begin(req.headers), end(req.headers),
                         [](auto const& h) {
                           return h.name == "Accept-Encoding" &&
                                  h.value.find("deflate") != std::string::npos;
                         }) == end(req.headers)) {
          return cb(reply::stock_reply(reply::not_implemented));
        }

        try {
          tiles::t_log("received a request: {}", req.uri);
          auto const tile =
              geo::tile{static_cast<uint32_t>(std::stoul(req.path_params[1])),
                        static_cast<uint32_t>(std::stoul(req.path_params[2])),
                        static_cast<uint32_t>(std::stoul(req.path_params[0]))};

          tiles::perf_counter pc;
          auto rendered_tile = tiles::get_tile(handle, render_ctx, tile, pc);
          tiles::perf_report_get_tile(pc);

          reply rep = reply::stock_reply(reply::ok);
          if (rendered_tile) {
            rep.headers.emplace_back("Content-Encoding", "deflate");
            rep.content = std::move(*rendered_tile);
          } else {
            rep.status = reply::no_content;
          }

          add_cors_headers(rep);
          return cb(rep);
        } catch (std::exception const& e) {
          tiles::t_log("unhandled error: {}", e.what());
        } catch (...) {
          tiles::t_log("unhandled unknown error");
        }
      });

  server.listen("0.0.0.0", "8888", router);

  io_service_shutdown shutd(ios);
  shutdown_handler<io_service_shutdown> shutdown(ios, shutd);

  tiles::t_log(">>> tiles-server up and running!");
  while (true) {
    try {
      ios.run();
      break;
    } catch (std::exception const& e) {
      tiles::t_log("unhandled error: {}", e.what());
    } catch (...) {
      tiles::t_log("unhandled unknown error");
    }
  }
}
