#include "catch2/catch.hpp"

#include "tiles/db/shared_strings.h"

TEST_CASE("shared_strings") {
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
