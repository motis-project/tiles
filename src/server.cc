#include <exception>
#include <iomanip>
#include <iostream>

#include "boost/asio/io_service.hpp"
#include "boost/beast/version.hpp"
#include "boost/filesystem.hpp"

#include "net/stop_handler.h"
#include "net/web_server/enable_cors.h"
#include "net/web_server/query_router.h"
#include "net/web_server/responses.h"
#include "net/web_server/web_server.h"

#include "tiles/db/get_tile.h"
#include "tiles/db/tile_database.h"
#include "tiles/perf_counter.h"

using namespace net;

int main() {
  lmdb::env db_env = tiles::make_tile_database("./tiles.mdb");
  tiles::tile_db_handle handle{db_env};
  auto const render_ctx = make_render_ctx(handle);

  boost::asio::io_service ios;
  web_server server{ios};

  query_router router;
  router.route("OPTIONS", ".*", [](auto const& req, auto cb, bool) {
    auto rep = web_server::http_res_t{empty_response(req)};
    enable_cors(rep);
    cb(std::move(rep));
  });

  // z, x, y
  router.route(
      "GET", "^\\/(\\d+)\\/(\\d+)\\/(\\d+).mvt$",
      [&](auto const& req, auto cb, bool /* is_ssl */) {
        if (auto const it =
                req.base().find(boost::beast::http::field::accept_encoding);
            it != req.base().end() &&
            it->value().find("deflate") == std::string_view::npos) {
          return cb(server_error_response(req, "deflate required"));
        }

        try {
          tiles::t_log("received a request: {}", req.base().target());
          auto const tile =
              geo::tile{static_cast<uint32_t>(std::stoul(req.path_params[1])),
                        static_cast<uint32_t>(std::stoul(req.path_params[2])),
                        static_cast<uint32_t>(std::stoul(req.path_params[0]))};

          tiles::perf_counter pc;
          auto rendered_tile = tiles::get_tile(handle, render_ctx, tile, pc);
          tiles::perf_report_get_tile(pc);

          if (rendered_tile) {
            auto res = string_response(req, *rendered_tile);
            res.set(boost::beast::http::field::content_encoding, "deflate");
            return cb(std::move(res));
          } else {
            auto rep = web_server::http_res_t{empty_response(req)};
            enable_cors(rep);
            return cb(std::move(rep));
          }
        } catch (std::exception const& e) {
          tiles::t_log("unhandled error: {}", e.what());
        } catch (...) {
          tiles::t_log("unhandled unknown error");
        }
      });

  boost::system::error_code ec;
  server.init("0.0.0.0", "8888", ec);
  if (ec) {
    std::cout << "init error: " << ec.message() << "\n";
    return 1;
  }

  stop_handler stop(ios, [&]() {
    server.stop();
    ios.stop();
  });

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
