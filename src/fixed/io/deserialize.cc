#include "tiles/fixed/io/deserialize.h"

#include "protozero/pbf_message.hpp"

#include "geo/simplify_mask.h"

#include "utl/erase_if.h"

#include "tiles/fixed/algo/delta.h"
#include "tiles/fixed/io/tags.h"
#include "tiles/util.h"

namespace pz = protozero;

namespace tiles {

struct default_decoder {
  using range_t = pz::iterator_range<pz::pbf_reader::const_sint64_iterator>;

  explicit default_decoder(range_t range) : range_{std::move(range)} {}

  template <typename Container>
  void deserialize_points(Container& out) {
    auto const size = get_next();
    out.reserve(size);

    for (auto i = 0u; i < size; ++i) {
      // do not inline -> undefined execution order
      auto const x_val = x_decoder_.decode(get_next());
      auto const y_val = y_decoder_.decode(get_next());
      out.emplace_back(x_val, y_val);
    }
  }

  fixed_delta_t get_next() {
    utl::verify(range_.first != range_.second, "iterator problem");
    auto val = *range_.first;
    ++range_.first;
    return val;
  }

  range_t range_;

  delta_decoder x_decoder_{kFixedCoordMagicOffset};
  delta_decoder y_decoder_{kFixedCoordMagicOffset};
};

default_decoder make_default_decoder(pz::pbf_message<tags::FixedGeometry>& m) {
  utl::verify(m.next(), "invalid message");
  utl::verify(m.tag() == tags::FixedGeometry::packed_sint64_geometry,
              "invalid tag");
  return default_decoder{m.get_packed_sint64()};
}

struct simplifying_decoder : public default_decoder {
  simplifying_decoder(default_decoder::range_t range,
                      std::vector<std::string_view> simplify_masks, uint32_t z)
      : default_decoder{std::move(range)},
        simplify_masks_{std::move(simplify_masks)},
        z_{z} {}

  template <typename Container>
  void deserialize_points(Container& out) {
    utl::verify(curr_mask_ < simplify_masks_.size(), "mask part missing");
    geo::simplify_mask_reader reader{simplify_masks_[curr_mask_].data(), z_};

    auto const size = get_next();
    utl::verify(size == reader.size_, "simplify mask size mismatch");

    out.reserve(size);
    for (auto i = 0u; i < size; ++i) {
      if (reader.get_bit(i)) {
        // do not inline -> undefined execution order
        auto const x_val = x_decoder_.decode(get_next());
        auto const y_val = y_decoder_.decode(get_next());
        out.emplace_back(x_val, y_val);
      } else {
        x_decoder_.decode(get_next());
        y_decoder_.decode(get_next());
      }
    }

    ++curr_mask_;
  }

  std::vector<std::string_view> simplify_masks_;
  uint32_t z_;
  size_t curr_mask_{0};
};

simplifying_decoder make_simplifying_decoder(
    pz::pbf_message<tags::FixedGeometry>& m,
    std::vector<std::string_view> simplify_masks, uint32_t z) {
  utl::verify(m.next(), "invalid message");
  utl::verify(m.tag() == tags::FixedGeometry::packed_sint64_geometry,
              "invalid tag");
  return {m.get_packed_sint64(), std::move(simplify_masks), z};
}

template <typename Decoder>
fixed_point deserialize_point(Decoder&& decoder) {
  fixed_point point;
  decoder.deserialize_points(point);
  return point;
}

template <typename Decoder>
fixed_polyline deserialize_polyline(Decoder&& decoder) {
  auto const count = decoder.get_next();

  fixed_polyline polyline;
  polyline.resize(count);
  for (auto i = 0u; i < count; ++i) {
    decoder.deserialize_points(polyline[i]);
  }
  return polyline;
}

template <typename Decoder>
fixed_geometry deserialize_polygon(Decoder&& decoder) {
  auto const count = decoder.get_next();

  fixed_polygon polygon;
  polygon.resize(count);
  for (auto i = 0u; i < count; ++i) {
    decoder.deserialize_points(polygon[i].outer());

    auto const inner_count = decoder.get_next();
    polygon[i].inners().resize(inner_count);
    for (auto j = 0u; j < inner_count; ++j) {
      decoder.deserialize_points(polygon[i].inners()[j]);
    }
  }

  utl::erase_if(polygon, [](auto& p) {
    utl::erase_if(p.inners(), [](auto const& i) { return i.size() < 4; });
    return p.outer().size() < 4;
  });

  if (polygon.empty()) {
    return fixed_null{};
  } else {
    return polygon;
  }
}

fixed_geometry deserialize(std::string_view geo) {
  pz::pbf_message<tags::FixedGeometry> m{geo};
  utl::verify(m.next(), "invalid msg");
  utl::verify(m.tag() == tags::FixedGeometry::required_FixedGeometryType_type,
              "invalid tag");

  switch (static_cast<tags::FixedGeometryType>(m.get_enum())) {
    case tags::FixedGeometryType::POINT:
      return deserialize_point(make_default_decoder(m));
    case tags::FixedGeometryType::POLYLINE:
      return deserialize_polyline(make_default_decoder(m));
    case tags::FixedGeometryType::POLYGON:
      return deserialize_polygon(make_default_decoder(m));
    default: throw utl::fail("unknown geometry");
  }
}

fixed_geometry deserialize(std::string_view geo,
                           std::vector<std::string_view> simplify_masks,
                           uint32_t const z) {
  pz::pbf_message<tags::FixedGeometry> m{geo};
  utl::verify(m.next(), "invalid msg");
  utl::verify(m.tag() == tags::FixedGeometry::required_FixedGeometryType_type,
              "invalid tag");

  switch (static_cast<tags::FixedGeometryType>(m.get_enum())) {
    case tags::FixedGeometryType::POINT:
      return deserialize_point(make_default_decoder(m));
    case tags::FixedGeometryType::POLYLINE:
      return deserialize_polyline(
          make_simplifying_decoder(m, std::move(simplify_masks), z));
    case tags::FixedGeometryType::POLYGON:
      return deserialize_polygon(
          make_simplifying_decoder(m, std::move(simplify_masks), z));
    default: throw utl::fail("unknown geometry");
  }
}

}  // namespace tiles
