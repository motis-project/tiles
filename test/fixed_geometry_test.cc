#include "catch2/catch.hpp"

#include <random>

#include "tiles/fixed/fixed_geometry.h"
#include "tiles/fixed/io/deserialize.h"
#include "tiles/fixed/io/serialize.h"

using namespace tiles;

TEST_CASE("fixed point io") {
  std::vector<fixed_point> test_cases;
  test_cases.push_back({{kFixedCoordMin, kFixedCoordMin}});
  test_cases.push_back({{kFixedCoordMax, kFixedCoordMax}});

  std::mt19937 gen{0};
  std::uniform_int_distribution<fixed_coord_t> dist{kFixedCoordMin,
                                                    kFixedCoordMax};
  for (auto i = 0u; i < 10000; ++i) {
    test_cases.push_back({{dist(gen), dist(gen)}});
  }

  for (auto const& test_case : test_cases) {
    auto const serialized = serialize(test_case);
    auto const deserialized = deserialize(serialized);

    CHECK(test_case == mpark::get<fixed_point>(deserialized));
  }
}

TEST_CASE("fixed polyline io") {
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

  for (auto i = 0u; i < 1000; ++i) {
    auto len = len_dist(gen);

    fixed_polyline line;
    line.emplace_back();
    for (auto j = 0u; j < len; ++j) {
      line[0].emplace_back(coord_dist(gen), coord_dist(gen));
    }
    test_cases.emplace_back(std::move(line));
  }

  for (auto const& test_case : test_cases) {
    auto const serialized = serialize(test_case);
    auto const deserialized = deserialize(serialized);

    CHECK(test_case == mpark::get<fixed_polyline>(deserialized));
  }
}
