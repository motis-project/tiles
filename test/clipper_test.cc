#include "gtest/gtest.h"

#include <algorithm>

#include "clipper2/clipper.h"

namespace c2 = Clipper2Lib;

auto const subject = c2::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

TEST(clipper, orientation) {
  ASSERT_TRUE(c2::IsPositive(subject));
  auto s2 = subject;
  std::reverse(begin(s2), end(s2));
  ASSERT_TRUE(!c2::IsPositive(s2));
}

TEST(clipper, in_polygon) {
  ASSERT_TRUE(c2::PointInPolygon(c2::Point64{5, 5}, subject) ==
              c2::PointInPolygonResult::IsInside);
  ASSERT_TRUE(c2::PointInPolygon(c2::Point64{0, 0}, subject) ==
              c2::PointInPolygonResult::IsOn);
  ASSERT_TRUE(c2::PointInPolygon(c2::Point64{15, 15}, subject) ==
              c2::PointInPolygonResult::IsOutside);
}

TEST(clipper, intersection) {
  auto const clip = c2::Path64{{0, 0}, {5, 0}, {5, 5}, {0, 5}};

  c2::Clipper64 clpr;
  clpr.AddSubject(c2::Paths64{subject});
  clpr.AddClip(c2::Paths64{clip});

  c2::Paths64 solution;
  clpr.Execute(c2::ClipType::Intersection, c2::FillRule::EvenOdd, solution);

  ASSERT_TRUE(solution.size() == 1);
  ASSERT_TRUE(solution[0].size() == clip.size());
  ASSERT_TRUE(std::all_of(
      begin(solution[0]), end(solution[0]), [&clip](auto const& pt) {
        return end(clip) != std::find(begin(clip), end(clip), pt);
      }));
}

TEST(clipper, intersection_empty) {
  auto const clip = c2::Path64{{20, 20}, {22, 20}, {22, 22}, {20, 22}};

  c2::Clipper64 clpr;
  clpr.AddSubject(c2::Paths64{subject});
  clpr.AddClip(c2::Paths64{clip});

  c2::Paths64 solution;
  clpr.Execute(c2::ClipType::Intersection, c2::FillRule::EvenOdd, solution);

  ASSERT_TRUE(solution.empty());
}
