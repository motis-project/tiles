#include "gtest/gtest.h"

#include "boost/geometry.hpp"

#include "tiles/fixed/algo/clip.h"

using namespace tiles;

TEST(clip, fixed_point_clip) {
  auto const box = fixed_box{{10, 10}, {20, 20}};

  auto const null_index = fixed_geometry{fixed_null{}}.index();
  auto const point_index = fixed_geometry{fixed_point{}}.index();

  {
    fixed_point test_case{{42, 23}};
    auto result = clip(test_case, box);
    ASSERT_TRUE(result.index() == null_index);
  }

  {
    fixed_point test_case{{15, 15}};
    auto result = clip(test_case, box);
    ASSERT_TRUE(result.index() == point_index);
    EXPECT_TRUE(mpark::get<fixed_point>(result) == test_case);
  }

  {
    fixed_point test_case{{10, 10}};
    auto result = clip(test_case, box);
    ASSERT_TRUE(result.index() == null_index);
  }

  {
    fixed_point test_case{{20, 12}};
    auto result = clip(test_case, box);
    ASSERT_TRUE(result.index() == null_index);
  }
}

TEST(clip, fixed_point_polyline) {
  auto const box = fixed_box{{10, 10}, {20, 20}};

  auto const null_index = fixed_geometry{fixed_null{}}.index();
  auto const polyline_index = fixed_geometry{fixed_polyline{}}.index();

  {
    fixed_polyline input{{{{0, 0}, {0, 30}}}};
    auto result = clip(input, box);
    ASSERT_TRUE(result.index() == null_index);
  }

  {
    fixed_polyline input{{{{12, 12}, {18, 18}}}};
    auto result = clip(input, box);
    ASSERT_TRUE(result.index() == polyline_index);
    EXPECT_TRUE(mpark::get<fixed_polyline>(result) == input);
  }

  {
    fixed_polyline input{{{{12, 8}, {12, 12}}}};
    auto result = clip(input, box);
    ASSERT_TRUE(result.index() == polyline_index);

    auto const expected = fixed_polyline{{{{12, 10}, {12, 12}}}};
    EXPECT_TRUE(mpark::get<fixed_polyline>(result) == expected);
  }
}

TEST(clip, fixed_polygon_degenerate) {
  auto const box = fixed_box{{0, 0}, {100, 100}};
  auto input = fixed_polygon{};
  input.resize(1);
  input[0].outer() = {{10, 10}, {20, 10}, {30, 10}, {10, 10}};
  EXPECT_TRUE(mpark::holds_alternative<fixed_null>(clip(input, box)));
}

TEST(clip, fixed_polygon_island_in_hole) {
  auto const box = fixed_box{{-10, -10}, {110, 110}};
  auto input = fixed_polygon{};
  input.resize(2);
  input[0].outer() = {{0, 0}, {0, 100}, {100, 100}, {100, 0}, {0, 0}};
  input[0].inners() = {{{20, 20}, {80, 20}, {80, 80}, {20, 80}, {20, 20}},
                       {{85, 85}, {95, 85}, {95, 95}, {85, 95}, {85, 85}}};
  input[1].outer() = {{40, 40}, {40, 60}, {60, 60}, {60, 40}, {40, 40}};
  input[1].inners() = {{{45, 45}, {55, 45}, {55, 55}, {45, 55}, {45, 45}}};

  auto const result = clip(input, box);
  ASSERT_TRUE(mpark::holds_alternative<fixed_polygon>(result));
  auto const& out = mpark::get<fixed_polygon>(result);

  ASSERT_EQ(2, out.size());
  EXPECT_EQ(400, boost::geometry::area(out[0].outer()));
  ASSERT_EQ(1, out[0].inners().size());
  EXPECT_EQ(-100, boost::geometry::area(out[0].inners()[0]));
  EXPECT_EQ(10000, boost::geometry::area(out[1].outer()));
  ASSERT_EQ(2, out[1].inners().size());
  EXPECT_EQ(-3700, boost::geometry::area(out[1].inners()[0]) +
                       boost::geometry::area(out[1].inners()[1]));
}
