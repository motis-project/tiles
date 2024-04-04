#include "gtest/gtest.h"

#include "tiles/db/layer_names.h"

TEST(layer_names, empty) {
  auto const vec_in = std::vector<std::string>{};
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}

TEST(layer_names, one) {
  auto const vec_in = std::vector<std::string>{"yolo"};
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}

TEST(layer_names, two) {
  auto const vec_in = std::vector<std::string>{"road", "rail"};
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}
