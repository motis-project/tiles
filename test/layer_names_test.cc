#include "gtest/gtest.h"

#include "tiles/db/layer_names.h"

TEST(layer_names, empty) {
  std::vector<std::string> vec_in;
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}

TEST(layer_names, one) {
  std::vector<std::string> vec_in{"yolo"};
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}

TEST(layer_names, two) {
  std::vector<std::string> vec_in{"road", "rail"};
  auto const buf = tiles::write_layer_names(vec_in);
  auto const vec_out = tiles::read_layer_names(buf);

  EXPECT_TRUE(vec_in == vec_out);
}
