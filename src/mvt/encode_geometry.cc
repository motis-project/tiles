#include "tiles/mvt/encode_geometry.h"

#include <iostream>

#include "boost/geometry.hpp"

#include "tiles/fixed/algo/delta.h"
#include "tiles/mvt/tags.h"
#include "tiles/util.h"

using namespace geo;
using namespace protozero;
namespace pz = protozero;
namespace ttm = tiles::tags::mvt;

namespace tiles {

enum command { MOVE_TO = 1, LINE_TO = 2, CLOSE_PATH = 7 };

uint32_t encode_command(command cmd, uint32_t count) {
  return (cmd & 0x7) | (count << 3);
}

constexpr auto geometry_tag =
    static_cast<pz::pbf_tag_type>(ttm::Feature::packed_uint32_geometry);

std::pair<delta_encoder, delta_encoder> delta_encoders(fixed_box const& box) {
  return {delta_encoder{static_cast<fixed_coord_t>(box.min_corner().x())},
          delta_encoder{static_cast<fixed_coord_t>(box.min_corner().y())}};
}

void encode(pz::pbf_builder<ttm::Feature>&, fixed_null const&,
            tile_spec const&) {}

void encode(pz::pbf_builder<ttm::Feature>& pb, fixed_point const& point,
            tile_spec const& spec) {
  pb.add_enum(ttm::Feature::optional_GeomType_type, ttm::GeomType::POINT);

  auto [x_enc, y_enc] = delta_encoders(spec.px_bounds_);
  {
    pz::packed_field_uint32 sw{pb, geometry_tag};
    sw.add_element(encode_command(MOVE_TO, point.size()));
    for (auto const& p : point) {
      sw.add_element(encode_zigzag32(x_enc.encode(p.x())));
      sw.add_element(encode_zigzag32(y_enc.encode(p.y())));
    }
  }
}

template <bool ClosePath, typename Container>
void encode_path(pz::packed_field_uint32& sw, delta_encoder& x_enc,
                 delta_encoder& y_enc, Container const& c) {
  utl::verify(c.size() > 1, "container polyline");

  sw.add_element(encode_command(MOVE_TO, 1));
  sw.add_element(encode_zigzag32(x_enc.encode(c.front().x())));
  sw.add_element(encode_zigzag32(y_enc.encode(c.front().y())));

  auto const limit = ClosePath ? c.size() - 2 : c.size() - 1;
  sw.add_element(encode_command(LINE_TO, limit));
  for (auto i = 1u; i <= limit; ++i) {
    sw.add_element(encode_zigzag32(x_enc.encode(c[i].x())));
    sw.add_element(encode_zigzag32(y_enc.encode(c[i].y())));
  }

  if (ClosePath) {
    sw.add_element(encode_command(CLOSE_PATH, 1));
  }
}

void encode(pz::pbf_builder<ttm::Feature>& pb,
            fixed_polyline const& multi_polyline, tile_spec const& spec) {
  pb.add_enum(ttm::Feature::optional_GeomType_type, ttm::GeomType::LINESTRING);

  auto [x_enc, y_enc] = delta_encoders(spec.px_bounds_);
  {
    pz::packed_field_uint32 sw{pb, geometry_tag};

    for (auto const& polyline : multi_polyline) {
      encode_path<false>(sw, x_enc, y_enc, polyline);
    }
  }
}

void encode(pz::pbf_builder<ttm::Feature>& pb,
            fixed_polygon const& multi_polygon, tile_spec const& spec) {
  pb.add_enum(ttm::Feature::optional_GeomType_type, ttm::GeomType::POLYGON);
  utl::verify(!multi_polygon.empty(), "multi_polygon empty");

  auto [x_enc, y_enc] = delta_encoders(spec.px_bounds_);
  {
    pz::packed_field_uint32 sw{pb, geometry_tag};

    for (auto const& polygon : multi_polygon) {
      encode_path<true>(sw, x_enc, y_enc, polygon.outer());
      for (auto const& inner : polygon.inners()) {
        encode_path<true>(sw, x_enc, y_enc, inner);
      }
    }
  }
}

void encode_geometry(pz::pbf_builder<ttm::Feature>& pb,
                     fixed_geometry const& geometry, tile_spec const& spec) {
  mpark::visit([&](auto const& arg) { encode(pb, arg, spec); }, geometry);
}

}  // namespace tiles
