#pragma once

#include "utl/erase_if.h"

#include "tiles/fixed/fixed_geometry.h"
#include "tiles/util.h"

namespace tiles {

inline fixed_geometry shift(fixed_null, uint32_t const) { return fixed_null{}; }

inline void shift(fixed_xy& pt, uint32_t const z) {
  uint32_t delta_z = 20 - z;
  pt.x(pt.x() >> delta_z);
  pt.y(pt.y() >> delta_z);
}

inline void shift(fixed_box& box, uint32_t const z) {
  shift(box.min_corner(), z);
  shift(box.max_corner(), z);
}

template <typename Container>
inline void shift_container(Container& c, uint32_t const z) {
  transform_erase(c, [&](auto& e) { shift(e, z); });
}

inline fixed_geometry shift(fixed_point multi_point, uint32_t const z) {
  shift_container(multi_point, z);
  if (multi_point.empty()) {
    return fixed_null{};
  } else {
    return multi_point;
  }
}

inline fixed_geometry shift(fixed_polyline multi_polyline, uint32_t const z) {
  for (auto& polyline : multi_polyline) {
    shift_container(polyline, z);
  }

  utl::erase_if(multi_polyline, [](auto const& p) { return p.size() < 2; });

  if (multi_polyline.empty()) {
    return fixed_null{};
  } else {
    return multi_polyline;
  }
}

inline fixed_geometry shift(fixed_polygon multi_polygon, uint32_t const z) {
  for (auto& polygon : multi_polygon) {
    shift_container(polygon.outer(), z);
    for (auto& ring : polygon.inners()) {
      shift_container(ring, z);
    }

    utl::erase_if(polygon.inners(), [](auto const& r) { return r.size() < 3; });
  }

  utl::erase_if(multi_polygon,
                [](auto const& p) { return p.outer().size() < 3; });

  if (multi_polygon.empty()) {
    return fixed_null{};
  } else {
    return multi_polygon;
  }
}

inline fixed_geometry shift(fixed_geometry geometry, uint32_t const z) {
  return mpark::visit([&](auto arg) { return shift(std::move(arg), z); },
                      std::move(geometry));
}

}  // namespace tiles
