#include "tiles/osm/read_osm_geometry.h"

#include "osmium/osm.hpp"

#include "utl/to_vec.h"

#include "tiles/fixed/convert.h"
#include "tiles/util.h"

namespace tiles {

fixed_geometry read_osm_geometry(osmium::Node const& node) {
  return fixed_point{
      {latlng_to_fixed({node.location().lat(), node.location().lon()})}};
}

template <typename In, typename Out>
void nodes_to_fixed(In const& in, Out& out) {
  out.reserve(in.size());

  for (auto const& node : in) {
    out.emplace_back(latlng_to_fixed({node.lat(), node.lon()}));
  }
}

fixed_geometry read_osm_geometry(osmium::Way const& way) {
  // TODO verify that distances fit into int32_t (or clipping will not
  // work)
  fixed_polyline polyline;
  polyline.emplace_back();
  nodes_to_fixed(way.nodes(), polyline.back());

  return polyline;
}

fixed_geometry read_osm_geometry(osmium::Area const& area) {
  fixed_polygon polygon;

  // TODO check first is not last!

  for (auto const& item : area) {
    switch (item.type()) {
      case osmium::item_type::outer_ring:
        polygon.emplace_back();
        nodes_to_fixed(*reinterpret_cast<osmium::OuterRing const*>(item.data()),
                       polygon.back().outer());
        break;
      case osmium::item_type::inner_ring:
        utl::verify(!polygon.empty(), "inner ring first!");

        polygon.back().inners().emplace_back();
        nodes_to_fixed(*reinterpret_cast<osmium::InnerRing const*>(item.data()),
                       polygon.back().inners().back());
        break;
      default: break;
    }
  }

  if (polygon.empty()) {
    return fixed_null{};
  }

  return polygon;
}

}  // namespace tiles
