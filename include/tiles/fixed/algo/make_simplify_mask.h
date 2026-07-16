#pragma once

#include "utl/to_vec.h"

#include "geo/simplify_mask.h"

namespace tiles {

inline std::vector<std::string> make_simplify_mask(fixed_null const&) {
  return {};
}
inline std::vector<std::string> make_simplify_mask(fixed_point const&) {
  return {};
}

inline std::vector<std::string> make_simplify_mask(fixed_polyline const& geo) {
  return utl::to_vec(geo, [](auto const& line) {
    return geo::make_serialize_simplify_mask(line);
  });
}

inline std::vector<std::string> make_simplify_mask(fixed_polygon const& geo) {
  std::vector<std::string> masks;
  for (auto const& polygon : geo) {
    masks.emplace_back(geo::make_serialize_simplify_mask(polygon.outer()));

    for (auto const& inner : polygon.inners()) {
      masks.emplace_back(geo::make_serialize_simplify_mask(inner));
    }
  }
  return masks;
}

inline std::vector<std::string> make_simplify_mask(fixed_geometry const& geo) {
  return mpark::visit([&](auto const& g) { return make_simplify_mask(g); },
                      geo);
}

}  // namespace tiles
