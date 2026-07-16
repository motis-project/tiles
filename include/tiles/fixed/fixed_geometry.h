#pragma once

#include <variant>

#include "boost/geometry/geometries/box.hpp"
#include "boost/geometry/geometries/linestring.hpp"
#include "boost/geometry/geometries/multi_linestring.hpp"
#include "boost/geometry/geometries/multi_point.hpp"
#include "boost/geometry/geometries/multi_polygon.hpp"
#include "boost/geometry/geometries/point_xy.hpp"
#include "boost/geometry/geometries/polygon.hpp"

#include "mpark/variant.hpp"

#include "tiles/constants.h"

namespace tiles {

using fixed_coord_t = int64_t;

using fixed_xy = boost::geometry::model::d2::point_xy<fixed_coord_t>;

const fixed_xy invalid_xy{std::numeric_limits<fixed_coord_t>::max(),
                          std::numeric_limits<fixed_coord_t>::max()};

using fixed_box = boost::geometry::model::box<fixed_xy>;
using fixed_line = boost::geometry::model::linestring<fixed_xy>;
using fixed_simple_polygon = boost::geometry::model::polygon<fixed_xy>;
using fixed_ring = fixed_simple_polygon::ring_type;

constexpr fixed_coord_t kFixedCoordMin = 0;
constexpr fixed_coord_t kFixedCoordMax = proj::map_size(kMaxZoomLevel) - 1;
constexpr fixed_coord_t kFixedCoordMagicOffset = kFixedCoordMax / 2ULL;

constexpr auto kFixedDefaultZoomLevel = 20ULL;
static_assert(kFixedDefaultZoomLevel <= kMaxZoomLevel, "invalid default zoom");

using fixed_delta_t = int64_t;

using fixed_null = std::monostate;
using fixed_point = boost::geometry::model::multi_point<fixed_xy>;
using fixed_polyline = boost::geometry::model::multi_linestring<fixed_line>;
using fixed_polygon =
    boost::geometry::model::multi_polygon<fixed_simple_polygon>;

using fixed_geometry =
    mpark::variant<fixed_null, fixed_point, fixed_polyline, fixed_polygon>;

}  // namespace tiles

namespace boost {
namespace geometry {
namespace model {
namespace d2 {

#ifndef BOOST_GEOMETRY_POINT_XY_LONG_EQUAL_DEFINED
#define BOOST_GEOMETRY_POINT_XY_LONG_EQUAL_DEFINED
inline bool operator==(point_xy<tiles::fixed_coord_t> const& lhs,
                       point_xy<tiles::fixed_coord_t> const& rhs) {
  return std::tie(lhs.x(), lhs.y()) == std::tie(rhs.x(), rhs.y());
}
#endif

}  // namespace d2
}  // namespace model
}  // namespace geometry
}  // namespace boost
