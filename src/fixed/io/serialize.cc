#include "tiles/fixed/io/serialize.h"

#include <numeric>

#include "boost/numeric/conversion/cast.hpp"

#include "utl/zip.h"

#include "tiles/fixed/algo/delta.h"
#include "tiles/fixed/io/tags.h"
#include "tiles/util.h"

namespace pz = protozero;

namespace tiles {

std::string serialize(fixed_null const&) {
  throw utl::fail("tries to serialize null geometry!");
}

template <typename SW, typename Points>
void serialize_points(SW& sw, delta_encoder& x_enc, delta_encoder& y_enc,
                      Points const& points) {
  sw.add_element(boost::numeric_cast<fixed_delta_t>(points.size()));
  for (auto const& point : points) {
    sw.add_element(x_enc.encode(point.x()));
    sw.add_element(y_enc.encode(point.y()));
  }
}

std::string serialize(fixed_point const& point) {
  std::string buffer;
  pz::pbf_builder<tags::FixedGeometry> pb(buffer);

  pb.add_enum(tags::FixedGeometry::required_FixedGeometryType_type,
              tags::FixedGeometryType::POINT);

  {
    pz::packed_field_sint64 sw{
        pb, static_cast<pz::pbf_tag_type>(
                tags::FixedGeometry::packed_sint64_geometry)};

    delta_encoder x_encoder{kFixedCoordMagicOffset};
    delta_encoder y_encoder{kFixedCoordMagicOffset};

    utl::verify(!point.empty(), "empty point");
    serialize_points(sw, x_encoder, y_encoder, point);
  }
  return buffer;
}

std::string serialize(fixed_polyline const& polyline) {
  std::string buffer;
  pz::pbf_builder<tags::FixedGeometry> pb(buffer);

  pb.add_enum(tags::FixedGeometry::required_FixedGeometryType_type,
              tags::FixedGeometryType::POLYLINE);

  {
    pz::packed_field_sint64 sw{
        pb, static_cast<pz::pbf_tag_type>(
                tags::FixedGeometry::packed_sint64_geometry)};

    delta_encoder x_encoder{kFixedCoordMagicOffset};
    delta_encoder y_encoder{kFixedCoordMagicOffset};

    utl::verify(!polyline.empty(), "empty polyline");
    sw.add_element(boost::numeric_cast<fixed_delta_t>(polyline.size()));

    for (auto const& line : polyline) {
      serialize_points(sw, x_encoder, y_encoder, line);
    }
  }
  return buffer;
}

std::string serialize(fixed_polygon const& multi_polygon) {
  std::string buffer;
  pz::pbf_builder<tags::FixedGeometry> pb(buffer);

  pb.add_enum(tags::FixedGeometry::required_FixedGeometryType_type,
              tags::FixedGeometryType::POLYGON);
  {
    pz::packed_field_sint64 sw{
        pb, static_cast<pz::pbf_tag_type>(
                tags::FixedGeometry::packed_sint64_geometry)};

    delta_encoder x_encoder{kFixedCoordMagicOffset};
    delta_encoder y_encoder{kFixedCoordMagicOffset};

    utl::verify(!multi_polygon.empty(), "empty polygon");
    sw.add_element(boost::numeric_cast<fixed_delta_t>(multi_polygon.size()));

    for (auto const& polygon : multi_polygon) {
      serialize_points(sw, x_encoder, y_encoder, polygon.outer());

      sw.add_element(
          boost::numeric_cast<fixed_delta_t>(polygon.inners().size()));
      for (auto const& inner : polygon.inners()) {
        serialize_points(sw, x_encoder, y_encoder, inner);
      }
    }
  }
  return buffer;
}

std::string serialize(fixed_geometry const& in) {
  return mpark::visit([&](auto const& arg) { return serialize(arg); }, in);
}

}  // namespace tiles
