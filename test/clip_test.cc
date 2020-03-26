#include "catch2/catch.hpp"

#include "tiles/fixed/algo/clip.h"

using namespace tiles;

TEST_CASE("fixed point clip") {
  fixed_box box{{10, 10}, {20, 20}};

  auto const null_index = fixed_geometry{fixed_null{}}.index();
  auto const point_index = fixed_geometry{fixed_point{}}.index();

  {
    fixed_point test_case{{42, 23}};
    auto result = clip(test_case, box);
    REQUIRE(result.index() == null_index);
  }

  {
    fixed_point test_case{{15, 15}};
    auto result = clip(test_case, box);
    REQUIRE(result.index() == point_index);
    CHECK(mpark::get<fixed_point>(result) == test_case);
  }

  {
    fixed_point test_case{{10, 10}};
    auto result = clip(test_case, box);
    REQUIRE(result.index() == null_index);
  }

  {
    fixed_point test_case{{20, 12}};
    auto result = clip(test_case, box);
    REQUIRE(result.index() == null_index);
  }
}

TEST_CASE("fixed point polyline") {
  fixed_box box{{10, 10}, {20, 20}};

  auto const null_index = fixed_geometry{fixed_null{}}.index();
  auto const polyline_index = fixed_geometry{fixed_polyline{}}.index();

  {
    fixed_polyline input{{{{0, 0}, {0, 30}}}};
    auto result = clip(input, box);
    REQUIRE(result.index() == null_index);
  }

  {
    fixed_polyline input{{{{12, 12}, {18, 18}}}};
    auto result = clip(input, box);
    REQUIRE(result.index() == polyline_index);
    CHECK(mpark::get<fixed_polyline>(result) == input);
  }

  {
    fixed_polyline input{{{{12, 8}, {12, 12}}}};
    auto result = clip(input, box);
    REQUIRE(result.index() == polyline_index);

    fixed_polyline expected{{{{12, 10}, {12, 12}}}};
    CHECK(mpark::get<fixed_polyline>(result) == expected);
  }
}