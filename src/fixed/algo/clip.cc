#include "tiles/fixed/algo/clip.h"

#include "boost/geometry.hpp"

#include "clipper/clipper.hpp"

#include "utl/erase_if.h"

#include "tiles/fixed/io/to_svg.h"
#include "tiles/util.h"

namespace cl = ClipperLib;

namespace tiles {

fixed_geometry clip(fixed_null const&, fixed_box const&) {
  return fixed_null{};
}

fixed_geometry clip(fixed_point const& in, fixed_box const& box) {
  fixed_point out;

  for (auto const& point : in) {
    if (boost::geometry::within(point, box)) {
      out.push_back(point);
    }
  }

  if (out.empty()) {
    return fixed_null{};
  } else {
    return out;
  }
}

fixed_geometry clip(fixed_polyline const& in, fixed_box const& box) {
  fixed_polyline out;
  boost::geometry::intersection(box, in, out);

  utl::erase_if(out, [](auto const& line) { return line.size() < 2; });
  if (out.empty()) {
    return fixed_null{};
  } else {
    return out;
  }
}

void to_fixed_polygon2(fixed_polygon& polygon, cl::PolyNodes const& nodes) {
  auto const path_to_ring = [](auto const& path) {
    utl::verify(!path.empty(), "path empty");
    fixed_ring ring;
    ring.reserve(path.size() + 1);
    for (auto const& pt : path) {
      ring.emplace_back(pt.X, pt.Y);
    }
    ring.emplace_back(path[0].X, path[0].Y);
    return ring;
  };

  for (auto const* outer : nodes) {
    utl::verify(!outer->IsHole(), "outer ring is hole");
    fixed_simple_polygon simple;
    simple.outer() = path_to_ring(outer->Contour);

    for (auto const* inner : outer->Childs) {
      utl::verify(inner->IsHole(), "inner ring is no hole");
      simple.inners().emplace_back(path_to_ring(inner->Contour));

      to_fixed_polygon2(polygon, inner->Childs);
    }

    polygon.emplace_back(std::move(simple));
  }
}

fixed_geometry clip(fixed_polygon const& in, fixed_box const& box) {
  auto const clip = cl::Path{{box.min_corner().x(), box.min_corner().y()},
                             {box.max_corner().x(), box.min_corner().y()},
                             {box.max_corner().x(), box.max_corner().y()},
                             {box.min_corner().x(), box.max_corner().y()}};
  cl::Paths subject;
  for (auto const& poly : in) {
    subject.emplace_back();

    for (auto const& pt : poly.outer()) {
      subject.back().emplace_back(pt.x(), pt.y());
    }
    subject.back().pop_back();

    for (auto const& inner : poly.inners()) {
      subject.emplace_back();
      for (auto const& pt : inner) {
        subject.back().emplace_back(pt.x(), pt.y());
      }
      subject.back().pop_back();
    }
  }

  cl::Clipper clpr;
  utl::verify(clpr.AddPaths(subject, cl::ptSubject, true), "AddPath1 failed");
  utl::verify(clpr.AddPath(clip, cl::ptClip, true), "AddPath2 failed");

  cl::PolyTree solution;
  clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd, cl::pftEvenOdd);
  if (solution.Childs.empty()) {
    return fixed_null{};
  }

  fixed_polygon out;
  to_fixed_polygon2(out, solution.Childs);

  boost::geometry::correct(out);
  return out;
}

fixed_geometry clip(fixed_geometry const& in, fixed_box const& box) {
  return mpark::visit([&](auto const& arg) { return clip(arg, box); }, in);
}

}  // namespace tiles
