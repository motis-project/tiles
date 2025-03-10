#include "gtest/gtest.h"

#include <algorithm>

#include "clipper/clipper.hpp"

namespace cl = ClipperLib;

auto const subject = cl::Path{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

TEST(clipper, orientation) {
  ASSERT_TRUE(cl::Orientation(subject));
  auto s2 = subject;
  cl::ReversePath(s2);
  ASSERT_TRUE(!cl::Orientation(s2));
}

TEST(clipper, in_polygon) {
  ASSERT_TRUE(cl::PointInPolygon(cl::IntPoint{5, 5}, subject) == 1);
  ASSERT_TRUE(cl::PointInPolygon(cl::IntPoint{0, 0}, subject) == -1);
  ASSERT_TRUE(cl::PointInPolygon(cl::IntPoint{15, 15}, subject) == 0);
}

TEST(clipper, intersection) {
  auto const clip = cl::Path{{0, 0}, {5, 0}, {5, 5}, {0, 5}};
  auto solution = cl::Paths{};

  cl::Clipper clpr;
  clpr.AddPath(subject, cl::ptSubject, true);
  clpr.AddPath(clip, cl::ptClip, true);
  clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd, cl::pftEvenOdd);

  ASSERT_TRUE(solution.size() == 1);
  ASSERT_TRUE(solution[0].size() == clip.size());
  ASSERT_TRUE(std::all_of(
      begin(solution[0]), end(solution[0]), [&clip](auto const& pt) {
        return end(clip) != std::find(begin(clip), end(clip), pt);
      }));
}

TEST(clipper, intersection_empty) {
  auto const clip = cl::Path{{20, 20}, {22, 20}, {22, 22}, {20, 22}};
  auto solution = cl::Paths{};

  cl::Clipper clpr;
  clpr.AddPath(subject, cl::ptSubject, true);
  clpr.AddPath(clip, cl::ptClip, true);
  clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd, cl::pftEvenOdd);

  ASSERT_TRUE(solution.empty());
}
