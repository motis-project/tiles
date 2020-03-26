#include "catch2/catch.hpp"

#include "clipper/clipper.hpp"

namespace cl = ClipperLib;

TEST_CASE("clipper_simple") {
  cl::Path subject{{0, 0}, {10, 0}, {10, 10}, {0, 10}};

  SECTION("orientation") {
    REQUIRE(cl::Orientation(subject));
    auto s2 = subject;
    cl::ReversePath(s2);
    REQUIRE(!cl::Orientation(s2));
  }

  SECTION("in polygon") {
    REQUIRE(cl::PointInPolygon(cl::IntPoint{5, 5}, subject) == 1);
    REQUIRE(cl::PointInPolygon(cl::IntPoint{0, 0}, subject) == -1);
    REQUIRE(cl::PointInPolygon(cl::IntPoint{15, 15}, subject) == 0);
  }

  SECTION("intersection") {
    cl::Path clip{{0, 0}, {5, 0}, {5, 5}, {0, 5}};
    cl::Paths solution;

    cl::Clipper clpr;
    clpr.AddPath(subject, cl::ptSubject, true);
    clpr.AddPath(clip, cl::ptClip, true);
    clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd, cl::pftEvenOdd);

    REQUIRE(solution.size() == 1);
    REQUIRE(solution[0].size() == clip.size());
    REQUIRE(std::all_of(
        begin(solution[0]), end(solution[0]), [&clip](auto const& pt) {
          return end(clip) != std::find(begin(clip), end(clip), pt);
        }));
  }

  SECTION("intersection empty") {
    cl::Path clip{{20, 20}, {22, 20}, {22, 22}, {20, 22}};
    cl::Paths solution;

    cl::Clipper clpr;
    clpr.AddPath(subject, cl::ptSubject, true);
    clpr.AddPath(clip, cl::ptClip, true);
    clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd, cl::pftEvenOdd);

    REQUIRE(solution.empty());
  }
}
