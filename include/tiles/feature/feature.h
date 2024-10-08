#pragma once

#include <map>
#include <string>
#include <utility>

#include "protozero/types.hpp"

#include "tiles/feature/metadata.h"
#include "tiles/fixed/fixed_geometry.h"

namespace tiles {

constexpr auto const kInvalidFeatureId = std::numeric_limits<uint64_t>::max();
constexpr auto const kInvalidLayerId = std::numeric_limits<size_t>::max();

constexpr uint32_t kInvalidZoomLevel = 0x3F;  // 63; max for one byte in svarint
constexpr fixed_coord_t kInvalidBoxHint =
    std::numeric_limits<fixed_coord_t>::max();

struct feature {
  uint64_t id_{kInvalidFeatureId};
  size_t layer_{kInvalidLayerId};
  std::pair<uint32_t, uint32_t> zoom_levels_;
  std::vector<metadata> meta_;
  fixed_geometry geometry_;
};

namespace tags {

enum class feature : protozero::pbf_tag_type {
  packed_sint64_header = 1,

  required_uint64_id = 2,

  packed_uint64_meta_pairs = 3,

  repeated_string_keys = 4,
  repeated_string_values = 5,

  repeated_string_simplify_masks = 6,
  required_fixed_geometry_geometry = 7,

  other = 999
};

}  // namespace tags
}  // namespace tiles
