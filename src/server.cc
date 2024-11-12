#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "boost/algorithm/string/predicate.hpp"
#include "boost/asio.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/http.hpp"
#include "boost/beast/version.hpp"

#include "conf/configuration.h"
#include "conf/options_parser.h"

#include "utl/parser/mmap_reader.h"

#include "tiles/db/tile_database.h"
#include "tiles/get_tile.h"
#include "tiles/parse_tile_url.h"
#include "tiles/perf_counter.h"
#include "tiles/util.h"

#include "pbf_sdf_fonts_res.h"
#include "tiles_server_res.h"

// Generic server code adapted from Boost ASIO and Boost BEAST examples.
// Distributed under the Boost Software License, Version 1.0.

namespace beast = boost::beast;  // from "boost/beast.hpp"
namespace http = beast::http;  // from "boost/beast/http.hpp"
namespace net = boost::asio;  // from "boost/asio.hpp"
using tcp = boost::asio::ip::tcp;  // from "boost/asio/ip/tcp.hpp"

namespace tiles {

using request_t = http::request<http::dynamic_body>;
using response_t = http::response<http::string_body>;
using callback_t = std::function<void(request_t const&, response_t&)>;

std::string url_decode(request_t const& req) {
  auto const& in = req.target();
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%') {
      utl::verify(i + 3 <= in.size(), "invalid url");
      int value = 0;
      std::istringstream is{std::string{in.substr(i + 1, 2)}};
      utl::verify(static_cast<bool>(is >> std::hex >> value), "invalid url");
      out += static_cast<char>(value);
      i += 2;
    } else if (in[i] == '+') {
      out += ' ';
    } else {
      out += in[i];
    }
  }
  return out;
}

struct http_connection : public std::enable_shared_from_this<http_connection> {
  http_connection(tcp::socket socket, callback_t const& callback)
      : socket_{std::move(socket)}, callback_{callback} {}

  void start() {
    auto self = shared_from_this();
    http::async_read(
        socket_, buffer_, request_, [self](beast::error_code ec, std::size_t) {
          if (!ec) {
            self->response_.version(self->request_.version());
            self->response_.keep_alive(false);

            try {
              self->callback_(self->request_, self->response_);
            } catch (std::exception const& e) {
              tiles::t_log("unhandled error: {}", e.what());
              self->response_.result(http::status::internal_server_error);
            } catch (...) {
              tiles::t_log("unhandled unknown error");
              self->response_.result(http::status::internal_server_error);
            }
            self->response_.set(http::field::content_length,
                                std::to_string(self->response_.body().size()));
            http::async_write(self->socket_, self->response_,
                              [self](beast::error_code ec, std::size_t) {
                                self->socket_.shutdown(
                                    tcp::socket::shutdown_send, ec);
                                self->deadline_.cancel();
                              });
          }
        });
    check_deadline();
  }

  void check_deadline() {
    auto self = shared_from_this();
    deadline_.async_wait([self](beast::error_code ec) {
      if (!ec) {
        self->socket_.close(ec);
      }
    });
  }

  tcp::socket socket_;
  beast::flat_buffer buffer_{8192};
  request_t request_;
  response_t response_;
  callback_t const& callback_;
  net::steady_timer deadline_{socket_.get_executor(), std::chrono::seconds(60)};
};

void http_server(tcp::acceptor& acceptor, tcp::socket& socket,
                 callback_t const& cb) {
  acceptor.async_accept(socket, [&](beast::error_code ec) {
    if (!ec) {
      std::make_shared<http_connection>(std::move(socket), cb)->start();
    }
    http_server(acceptor, socket, cb);
  });
}

void serve_forever(std::string const& address, uint16_t port, callback_t&& cb) {
  try {
    net::io_context ioc{static_cast<int>(std::thread::hardware_concurrency())};
    tcp::acceptor acceptor{ioc, {net::ip::make_address(address), port}};
    tcp::socket socket{ioc};
    http_server(acceptor, socket, cb);

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](boost::system::error_code const&, int) { ioc.stop(); });

    std::vector<std::thread> threads;
    for (auto i = 1ULL; i < std::thread::hardware_concurrency(); ++i) {
      threads.emplace_back([&ioc] { ioc.run(); });
    }

    t_log("tiles-server started on {}:{}", address, port);
    ioc.run();

    std::for_each(begin(threads), end(threads), [](auto& t) { t.join(); });
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }
}

struct server_settings : public conf::configuration {
  server_settings() : configuration("tiles-server options", "") {
    param(db_fname_, "db_fname", "/path/to/tiles.mdb");
    param(res_dname_, "res_dname", "tiles/client");
    param(port_, "port", "the http port of the server");
  }

  std::string db_fname_{"tiles.mdb"};
  std::string res_dname_;
  uint16_t port_{8080};
};

int run_tiles_server(int argc, char const** argv) {
  server_settings opt;

  try {
    conf::options_parser parser({&opt});
    parser.read_command_line_args(argc, argv, false);

    if (parser.help() || parser.version()) {
      std::cout << "tiles-server\n\n";
      parser.print_help(std::cout);
      return 0;
    }

    parser.read_configuration_file(false);
    parser.print_used(std::cout);
  } catch (std::exception const& e) {
    std::cout << "options error: " << e.what() << "\n";
    return 1;
  }

  utl::verify(std::filesystem::is_regular_file(opt.db_fname_.c_str()),
              "tiles database file not found: {}", opt.db_fname_);

  lmdb::env db_env = make_tile_database(opt.db_fname_.c_str(), kDefaultSize);
  tile_db_handle handle{db_env};
  auto const render_ctx = make_render_ctx(handle);
  pack_handle pack_handle{opt.db_fname_.c_str()};

  auto const maybe_serve_tile = [&](auto const& req, auto& res) -> bool {
    auto const decoded_url = url_decode(req);
    auto const match = parse_tile_url(decoded_url);
    if (!match) {
      return false;
    }

    if (req[http::field::accept_encoding]  //
            .find("deflate") == std::string_view::npos) {
      res.result(http::status::not_implemented);
      return true;
    }

    t_log("received a request: {}", req.target());
    auto const tile = *match;

    perf_counter pc;
    auto rendered_tile = get_tile(handle, pack_handle, render_ctx, tile, pc);
    perf_report_get_tile(pc);

    if (rendered_tile) {
      res.body() = std::move(*rendered_tile);
      res.set(http::field::content_encoding, "deflate");
      res.result(http::status::ok);
    } else {
      res.result(http::status::no_content);
    }
    return true;
  };

  auto const maybe_serve_glyphs = [&](auto const& req, auto& res) -> bool {
    static auto const matcher = regex_matcher{"^\\/glyphs/(.+)$"};
    auto const decoded_url = url_decode(req);
    auto const match = matcher.match(decoded_url);
    if (!match) {
      return false;
    }

    try {
      auto const mem =
          pbf_sdf_fonts_res::get_resource(std::string{match->at(1)});
      res.body() =
          std::string{reinterpret_cast<char const*>(mem.ptr_), mem.size_};
      res.result(http::status::ok);
    } catch (std::out_of_range const&) {
      res.result(http::status::not_found);
    }
    return true;
  };

  auto const maybe_serve_file = [&](auto const& req, auto& res) -> bool {
    static auto const matcher = regex_matcher{"^\\/(.+)$"};
    auto const decoded_url = url_decode(req);
    auto const match = matcher.match(decoded_url);
    if (!match && req.target() != "/") {
      res.result(http::status::not_found);
      return false;
    }

    bool found = false;
    auto const fname = std::string{match ? match->at(1) : "index.html"};
    if (!opt.res_dname_.empty()) {
      auto p = std::filesystem::path{opt.res_dname_} / fname;
      if (std::filesystem::exists(p)) {
        auto const mem = utl::mmap_reader{p.string().c_str()};
        res.body() = std::string{mem.m_.ptr(), mem.m_.size()};
        found = true;
      }
    }

    if (!found) {
      try {
        auto const mem = tiles_server_res::get_resource(fname);
        res.body() =
            std::string{reinterpret_cast<char const*>(mem.ptr_), mem.size_};
        found = true;
      } catch (std::out_of_range const&) {
        // tough luck
      }
    }

    if (found) {
      res.result(http::status::ok);
      if (boost::algorithm::ends_with(fname, ".html")) {
        res.set(http::field::content_type, "text/html");
      } else if (boost::algorithm::ends_with(fname, ".css")) {
        res.set(http::field::content_type, "text/css");
      } else if (boost::algorithm::ends_with(fname, ".js")) {
        res.set(http::field::content_type, "text/javascript");
      }
    } else {
      res.result(http::status::not_found);
    }

    return true;
  };

  serve_forever("0.0.0.0", opt.port_, [&](auto const& req, auto& res) {
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_headers,
            "X-Requested-With, Content-Type, Accept, Authorization");
    res.set(http::field::access_control_allow_methods,
            "GET, POST, PUT, DELETE, OPTIONS, HEAD");

    switch (req.method()) {
      case http::verb::options: res.result(http::status::no_content); break;
      case http::verb::get:
      case http::verb::head:
        if (!(maybe_serve_tile(req, res) ||  //
              maybe_serve_glyphs(req, res) ||  //
              maybe_serve_file(req, res))) {
          res.result(http::status::not_found);
        }
        break;
      default: res.result(http::status::method_not_allowed);
    }
  });

  return 0;
}

}  // namespace tiles

int main(int argc, char const** argv) {
  try {
    return tiles::run_tiles_server(argc, argv);
  } catch (std::exception const& e) {
    tiles::t_log("exception caught: {}", e.what());
    return 1;
  } catch (...) {
    tiles::t_log("unknown exception caught");
    return 1;
  }
}
