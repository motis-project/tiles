#include "tiles/fixed/algo/clip.h"

#include <cmath>
#include <limits>
#include <vector>

#include "boost/geometry.hpp"

#include "clipper2/clipper.h"

#include "utl/erase_if.h"
#include "utl/verify.h"

#include "tiles/util.h"

namespace c2 = Clipper2Lib;

namespace tiles {

// Polygon-clip backend:
//   1 = Clipper2 RectClip64  (specialized, fast rectangle clipping)
//   0 = Clipper2 Clipper64 + PolyTree64  (general boolean, faithful to v1)
#ifndef TILES_CLIP_RECTCLIP
#define TILES_CLIP_RECTCLIP 1
#endif

fixed_geometry clip(fixed_null const&, fixed_box const&) { return fixed_null{}; }

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

namespace {

fixed_ring to_ring(c2::Path64 const& path) {
  fixed_ring ring;
  if (path.empty()) {
    return ring;
  }
  ring.reserve(path.size() + 1);
  for (auto const& pt : path) {
    ring.emplace_back(pt.x, pt.y);
  }
  ring.emplace_back(path.front().x, path.front().y);  // close ring
  return ring;
}

c2::Paths64 to_subject(fixed_polygon const& in) {
  c2::Paths64 subject;
  for (auto const& poly : in) {
    subject.emplace_back();
    for (auto const& pt : poly.outer()) {
      subject.back().emplace_back(pt.x(), pt.y());
    }
    if (!subject.back().empty()) {
      subject.back().pop_back();  // Clipper2 paths are implicitly closed
    }

    for (auto const& inner : poly.inners()) {
      subject.emplace_back();
      for (auto const& pt : inner) {
        subject.back().emplace_back(pt.x(), pt.y());
      }
      if (!subject.back().empty()) {
        subject.back().pop_back();
      }
    }
  }
  return subject;
}

#if TILES_CLIP_RECTCLIP
// Rebuild the boost multi-polygon hierarchy from the flat ring set returned by
// RectClip. Ring role is determined by containment depth (even depth = outer
// ring, odd depth = hole of its immediate parent). This is orientation
// agnostic; boost::geometry::correct() normalizes winding afterwards.
void to_fixed_polygon(fixed_polygon& out, c2::Paths64 const& paths) {
  auto const n = paths.size();

  std::vector<double> area(n, 0.0);
  for (auto i = 0U; i < n; ++i) {
    area[i] = std::abs(c2::Area(paths[i]));
  }

  // parent[i] = index of the smallest-area ring strictly containing ring i.
  std::vector<int> parent(n, -1);
  for (auto i = 0U; i < n; ++i) {
    if (paths[i].empty()) {
      continue;
    }
    auto const pt = paths[i].front();
    auto best = std::numeric_limits<double>::max();
    for (auto j = 0U; j < n; ++j) {
      if (i == j || paths[j].size() < 3 || area[j] <= area[i]) {
        continue;
      }
      if (c2::PointInPolygon(pt, paths[j]) !=
          c2::PointInPolygonResult::IsInside) {
        continue;
      }
      if (area[j] < best) {
        best = area[j];
        parent[i] = static_cast<int>(j);
      }
    }
  }

  auto const depth = [&](std::size_t i) {
    auto d = 0;
    for (auto p = parent[i]; p != -1; p = parent[static_cast<std::size_t>(p)]) {
      ++d;
    }
    return d;
  };

  std::vector<int> out_idx(n, -1);
  for (auto i = 0U; i < n; ++i) {  // outer rings (even depth)
    if (paths[i].size() < 3 || (depth(i) & 1) != 0) {
      continue;
    }
    fixed_simple_polygon simple;
    simple.outer() = to_ring(paths[i]);
    out_idx[i] = static_cast<int>(out.size());
    out.emplace_back(std::move(simple));
  }
  for (auto i = 0U; i < n; ++i) {  // holes (odd depth) -> attach to parent outer
    if (paths[i].size() < 3 || (depth(i) & 1) != 1) {
      continue;
    }
    auto const p = parent[i];
    if (p >= 0 && out_idx[static_cast<std::size_t>(p)] >= 0) {
      out[static_cast<std::size_t>(out_idx[static_cast<std::size_t>(p)])]
          .inners()
          .emplace_back(to_ring(paths[i]));
    }
  }
}
#else
void to_fixed_polygon(fixed_polygon& polygon, c2::PolyPath64 const& parent) {
  for (auto const& child : parent) {  // top-level children are outer rings
    auto const& outer = *child;
    fixed_simple_polygon simple;
    simple.outer() = to_ring(outer.Polygon());

    for (auto const& grand_child : outer) {  // children of an outer are holes
      auto const& inner = *grand_child;
      simple.inners().emplace_back(to_ring(inner.Polygon()));
      to_fixed_polygon(polygon, inner);  // islands nested inside holes
    }

    polygon.emplace_back(std::move(simple));
  }
}
#endif

}  // namespace

fixed_geometry clip(fixed_polygon const& in, fixed_box const& box) {
  auto const subject = to_subject(in);

  auto const rect = c2::Rect64{box.min_corner().x(), box.min_corner().y(),
                               box.max_corner().x(), box.max_corner().y()};

  fixed_polygon out;

#if TILES_CLIP_RECTCLIP
  auto const solution = c2::RectClip(rect, subject);
  if (solution.empty()) {
    return fixed_null{};
  }
  to_fixed_polygon(out, solution);
#else
  c2::Clipper64 clpr;
  clpr.AddSubject(subject);
  clpr.AddClip(c2::Paths64{c2::Path64{{rect.left, rect.top},
                                      {rect.right, rect.top},
                                      {rect.right, rect.bottom},
                                      {rect.left, rect.bottom}}});
  c2::PolyTree64 solution;
  clpr.Execute(c2::ClipType::Intersection, c2::FillRule::EvenOdd, solution);
  if (solution.Count() == 0) {
    return fixed_null{};
  }
  to_fixed_polygon(out, solution);
#endif

  if (out.empty()) {
    return fixed_null{};
  }

  boost::geometry::correct(out);
  return out;
}

fixed_geometry clip(fixed_geometry const& in, fixed_box const& box) {
  return mpark::visit([&](auto const& arg) { return clip(arg, box); }, in);
}

}  // namespace tiles
