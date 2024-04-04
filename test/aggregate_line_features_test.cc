#include "gtest/gtest.h"

#include "tiles/feature/aggregate_line_features.h"
#include "tiles/feature/feature.h"

TEST(aggregate_line_features, to_from) {
  tiles::feature f1;
  f1.id_ = 1;
  f1.geometry_ = tiles::fixed_polyline{{{10, 10}, {11, 11}}};

  tiles::feature f2;
  f2.id_ = 2;
  f2.geometry_ = tiles::fixed_polyline{{{11, 11}, {12, 12}}};

  auto result = tiles::aggregate_line_features({f1, f2}, 99);
  ASSERT_TRUE(result.size() == 1);

  auto geo = mpark::get<tiles::fixed_polyline>(result.at(0).geometry_);
  ASSERT_TRUE(geo.size() == 1);
  ASSERT_TRUE(geo.front().size() == 3);

  EXPECT_TRUE(geo.front()[0] == tiles::fixed_xy(10, 10));
  EXPECT_TRUE(geo.front()[1] == tiles::fixed_xy(11, 11));
  EXPECT_TRUE(geo.front()[2] == tiles::fixed_xy(12, 12));
}

TEST(aggregate_line_features, to_to) {
  tiles::feature f1;
  f1.id_ = 1;
  f1.geometry_ = tiles::fixed_polyline{{{10, 10}, {11, 11}}};

  tiles::feature f2;
  f2.id_ = 2;
  f2.geometry_ = tiles::fixed_polyline{{{12, 12}, {11, 11}}};

  auto result = tiles::aggregate_line_features({f1, f2}, 99);
  ASSERT_TRUE(result.size() == 1);

  auto geo = mpark::get<tiles::fixed_polyline>(result.at(0).geometry_);
  ASSERT_TRUE(geo.size() == 1);
  ASSERT_TRUE(geo.front().size() == 3);

  EXPECT_TRUE(geo.front()[0] == tiles::fixed_xy(10, 10));
  EXPECT_TRUE(geo.front()[1] == tiles::fixed_xy(11, 11));
  EXPECT_TRUE(geo.front()[2] == tiles::fixed_xy(12, 12));
}

TEST(aggregate_line_features, from_to) {
  tiles::feature f1;
  f1.id_ = 1;
  f1.geometry_ = tiles::fixed_polyline{{{10, 10}, {11, 11}}};

  tiles::feature f2;
  f2.id_ = 2;
  f2.geometry_ = tiles::fixed_polyline{{{12, 12}, {10, 10}}};

  auto result = tiles::aggregate_line_features({f1, f2}, 99);
  ASSERT_TRUE(result.size() == 1);

  auto geo = mpark::get<tiles::fixed_polyline>(result.at(0).geometry_);
  ASSERT_TRUE(geo.size() == 1);
  ASSERT_TRUE(geo.front().size() == 3);

  EXPECT_TRUE(geo.front()[0] == tiles::fixed_xy(12, 12));
  EXPECT_TRUE(geo.front()[1] == tiles::fixed_xy(10, 10));
  EXPECT_TRUE(geo.front()[2] == tiles::fixed_xy(11, 11));
}

TEST(aggregate_line_features, from_from) {
  tiles::feature f1;
  f1.id_ = 1;
  f1.geometry_ = tiles::fixed_polyline{{{10, 10}, {11, 11}}};

  tiles::feature f2;
  f2.id_ = 2;
  f2.geometry_ = tiles::fixed_polyline{{{10, 10}, {12, 12}}};

  auto result = tiles::aggregate_line_features({f1, f2}, 99);
  ASSERT_TRUE(result.size() == 1);

  auto geo = mpark::get<tiles::fixed_polyline>(result.at(0).geometry_);
  ASSERT_TRUE(geo.size() == 1);
  ASSERT_TRUE(geo.front().size() == 3);

  EXPECT_TRUE(geo.front()[0] == tiles::fixed_xy(12, 12));
  EXPECT_TRUE(geo.front()[1] == tiles::fixed_xy(10, 10));
  EXPECT_TRUE(geo.front()[2] == tiles::fixed_xy(11, 11));
}

TEST(aggregate_line_features, to_from_to_from) {
  tiles::feature f1;
  f1.id_ = 1;
  f1.geometry_ = tiles::fixed_polyline{{{10, 10}, {11, 11}}};

  tiles::feature f2;
  f2.id_ = 2;
  f2.geometry_ = tiles::fixed_polyline{{{11, 11}, {12, 12}}};

  tiles::feature f3;
  f3.id_ = 3;
  f3.geometry_ = tiles::fixed_polyline{{{12, 12}, {13, 13}}};

  auto result = tiles::aggregate_line_features({f1, f2, f3}, 99);
  ASSERT_TRUE(result.size() == 1);

  auto geo = mpark::get<tiles::fixed_polyline>(result.at(0).geometry_);
  ASSERT_TRUE(geo.size() == 1);
  ASSERT_TRUE(geo.front().size() == 4);

  EXPECT_TRUE(geo.front()[0] == tiles::fixed_xy(10, 10));
  EXPECT_TRUE(geo.front()[1] == tiles::fixed_xy(11, 11));
  EXPECT_TRUE(geo.front()[2] == tiles::fixed_xy(12, 12));
  EXPECT_TRUE(geo.front()[3] == tiles::fixed_xy(13, 13));
}
