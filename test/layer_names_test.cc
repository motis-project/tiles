#include "catch2/catch_all.hpp"

#include "tiles/db/layer_names.h"

TEST_CASE("layer_names") {
  SECTION("empty") {
    std::vector<std::string> vec_in;
    auto const buf = tiles::write_layer_names(vec_in);
    auto const vec_out = tiles::read_layer_names(buf);

    CHECK(vec_in == vec_out);
  }

  SECTION("one") {
    std::vector<std::string> vec_in{"yolo"};
    auto const buf = tiles::write_layer_names(vec_in);
    auto const vec_out = tiles::read_layer_names(buf);

    CHECK(vec_in == vec_out);
  }

  SECTION("two") {
    std::vector<std::string> vec_in{"road", "rail"};
    auto const buf = tiles::write_layer_names(vec_in);
    auto const vec_out = tiles::read_layer_names(buf);

    CHECK(vec_in == vec_out);
  }
}
