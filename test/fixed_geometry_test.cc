#include "gtest/gtest.h"

#include <random>

#include "boost/geometry.hpp"

#include "tiles/fixed/algo/shift.h"
#include "tiles/fixed/fixed_geometry.h"
#include "tiles/fixed/io/deserialize.h"
#include "tiles/fixed/io/serialize.h"

using namespace tiles;

TEST(fixed_geometry_test, fixed_point_io) {
  std::vector<fixed_point> test_cases;
  test_cases.push_back({{kFixedCoordMin, kFixedCoordMin}});
  test_cases.push_back({{kFixedCoordMax, kFixedCoordMax}});

  std::mt19937 gen{0};
  std::uniform_int_distribution<fixed_coord_t> dist{kFixedCoordMin,
                                                    kFixedCoordMax};
  for (auto i = 0; i < 10000; ++i) {
    test_cases.push_back({{dist(gen), dist(gen)}});
  }

  for (auto const& test_case : test_cases) {
    auto const serialized = serialize(test_case);
    auto const deserialized = deserialize(serialized);

    EXPECT_TRUE(test_case == mpark::get<fixed_point>(deserialized));
  }
}

TEST(fixed_geometry_test, fixed_polyline_io) {
  std::vector<fixed_polyline> test_cases;

  test_cases.push_back({{{{10, 10}, {20, 20}}}});
  test_cases.push_back({{{{10, 10}, {10, 10}}}});

  test_cases.push_back(
      {{{{kFixedCoordMin, kFixedCoordMin}, {kFixedCoordMax, kFixedCoordMax}}}});
  test_cases.push_back(
      {{{{kFixedCoordMax, kFixedCoordMax}, {kFixedCoordMin, kFixedCoordMin}}}});

  std::mt19937 gen{0};
  std::uniform_int_distribution<fixed_coord_t> coord_dist{kFixedCoordMin,
                                                          kFixedCoordMax};
  std::uniform_int_distribution<fixed_coord_t> len_dist{1, 10000};

  for (auto i = 0; i < 1000; ++i) {
    auto len = len_dist(gen);

    fixed_polyline line;
    line.emplace_back();
    for (auto j = 0LL; j < len; ++j) {
      line[0].emplace_back(coord_dist(gen), coord_dist(gen));
    }
    test_cases.emplace_back(std::move(line));
  }

  for (auto const& test_case : test_cases) {
    auto const serialized = serialize(test_case);
    auto const deserialized = deserialize(serialized);

    EXPECT_TRUE(test_case == mpark::get<fixed_polyline>(deserialized));
  }
}

TEST(fixed_geometry_test, shift_polygon_winding) {
  // z18: coords / 4 -> first outer flips, two of the holes collapse
  fixed_polygon input;
  input.resize(2);
  input[0].outer() = {{7, 0}, {6, 7}, {12, 11}, {11, 14}, {7, 0}};
  input[1].outer() = {
      {100, 100}, {100, 200}, {200, 200}, {200, 100}, {100, 100}};
  input[1].inners() = {
      {{120, 120}, {180, 120}, {180, 180}, {120, 180}, {120, 120}},
      {{130, 130}, {131, 130}, {131, 131}, {130, 131}, {130, 130}},
      {{140, 140}, {160, 140}, {160, 141}, {140, 141}, {140, 140}}};

  ASSERT_GT(boost::geometry::area(input[0].outer()), 0);

  auto const result = shift(input, 18);
  ASSERT_TRUE(mpark::holds_alternative<fixed_polygon>(result));
  auto const& out = mpark::get<fixed_polygon>(result);

  ASSERT_EQ(2, out.size());
  EXPECT_GT(boost::geometry::area(out[0].outer()), 0);
  EXPECT_GT(boost::geometry::area(out[1].outer()), 0);
  ASSERT_EQ(1, out[1].inners().size());
  EXPECT_LT(boost::geometry::area(out[1].inners()[0]), 0);
}
