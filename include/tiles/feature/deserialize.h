#pragma once

#include <optional>

#include "protozero/pbf_message.hpp"

#include "tiles/db/shared_metadata.h"
#include "tiles/feature/feature.h"
#include "tiles/fixed/algo/delta.h"
#include "tiles/fixed/io/deserialize.h"
#include "tiles/util.h"

namespace tiles {

inline std::optional<feature> deserialize_feature(
    std::string_view const& str,  //
    shared_metadata_decoder const& metadata_decoder,
    fixed_box const& box_hint = {{kInvalidBoxHint, kInvalidBoxHint},
                                 {kInvalidBoxHint, kInvalidBoxHint}},
    uint32_t const zoom_level_hint = kInvalidZoomLevel) {

  uint64_t id = 0;
  std::pair<uint32_t, uint32_t> zoom_levels{kInvalidZoomLevel,
                                            kInvalidZoomLevel};

  constexpr auto const kInvalidLayer = std::numeric_limits<size_t>::max();
  size_t layer = kInvalidLayer;

  size_t meta_fill = 0;
  std::vector<metadata> meta;

  std::vector<std::string_view> simplify_masks;
  fixed_geometry geometry;

  namespace pz = protozero;
  pz::pbf_message<tags::feature> msg{str.data(), str.size()};
  while (msg.next()) {
    switch (msg.tag()) {
      case tags::feature::packed_sint64_header: {
        auto range = msg.get_packed_sint64();
        auto next = [&range] {
          utl::verify(!range.empty(), "read_header: range empty");
          return *(range.first++);
        };

        zoom_levels.first = static_cast<uint32_t>(next());
        if (zoom_level_hint != kInvalidZoomLevel &&
            zoom_levels.first > zoom_level_hint) {
          return std::nullopt;
        }
        zoom_levels.second = static_cast<uint32_t>(next());
        if (zoom_level_hint != kInvalidZoomLevel &&
            zoom_levels.second < zoom_level_hint) {
          return std::nullopt;
        }

        delta_decoder x_dec{kFixedCoordMagicOffset};
        auto const min_x = x_dec.decode(static_cast<fixed_coord_t>(next()));
        auto const max_x = x_dec.decode(static_cast<fixed_coord_t>(next()));
        if (box_hint.min_corner().x() != kInvalidBoxHint &&
            box_hint.max_corner().x() != kInvalidBoxHint &&
            (max_x < box_hint.min_corner().x() ||
             min_x > box_hint.max_corner().x())) {
          return std::nullopt;
        }

        delta_decoder y_dec{kFixedCoordMagicOffset};
        auto const min_y = y_dec.decode(static_cast<fixed_coord_t>(next()));
        auto const max_y = y_dec.decode(static_cast<fixed_coord_t>(next()));
        if (box_hint.min_corner().y() != kInvalidBoxHint &&
            box_hint.max_corner().y() != kInvalidBoxHint &&
            (max_y < box_hint.min_corner().y() ||
             min_y > box_hint.max_corner().y())) {
          return std::nullopt;
        }

        layer = static_cast<size_t>(next());  // layer key
        utl::verify(range.empty(), "read_header: superfluous elements");
      } break;

      case tags::feature::required_uint64_id: id = msg.get_uint64(); break;

      case tags::feature::packed_uint64_meta_pairs:
        utl::verify(meta.empty(),
                    "meta_pairs must come before, meta keys/values!");
        for (auto const id : msg.get_packed_uint64()) {
          meta.push_back(metadata_decoder.decode(id));
        }
        meta_fill = meta.size();
        break;
      case tags::feature::repeated_string_keys:
        meta.emplace_back(msg.get_string(), std::string{});
        break;
      case tags::feature::repeated_string_values:
        utl::verify(meta_fill < meta.size(), "meta data imbalance! (a)");
        meta[meta_fill++].value_ = msg.get_string();
        break;

      case tags::feature::repeated_string_simplify_masks:
        simplify_masks.emplace_back(msg.get_view());
        break;
      case tags::feature::required_fixed_geometry_geometry: {

        std::vector<std::string_view> simplify_masks_tmp;
        std::swap(simplify_masks, simplify_masks_tmp);
        if (zoom_level_hint != kInvalidZoomLevel &&
            !simplify_masks_tmp.empty()) {
          geometry = deserialize(msg.get_view(), std::move(simplify_masks_tmp),
                                 zoom_level_hint);
          if (mpark::holds_alternative<fixed_null>(geometry)) {
            return std::nullopt;  // killed by mask
          }
        } else {
          geometry = deserialize(msg.get_view());
        }
      } break;
      default: msg.skip();
    }
  }

  utl::verify(meta_fill == meta.size(), "meta data imbalance! (b)");
  utl::verify(layer != kInvalidLayer, "invalid layer found!");

  return feature{id, layer, zoom_levels, std::move(meta), std::move(geometry)};
}

}  // namespace tiles
