#include "gtest/gtest.h"

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
