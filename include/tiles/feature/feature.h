#pragma once

#include <map>
#include <string>
#include <utility>

#include "protozero/types.hpp"

#include "tiles/fixed/fixed_geometry.h"

namespace tiles {

constexpr uint32_t kInvalidZoomLevel = 0x3F;  // 63; max for one byte in svarint
constexpr fixed_coord_t kInvalidBoxHint =
    std::numeric_limits<fixed_coord_t>::max();

struct metadata {
  metadata() = default;
  metadata(std::string key, std::string value)
      : key_{std::move(key)}, value_{std::move(value)} {}

  friend bool operator==(metadata const& lhs, metadata const& rhs) {
    return std::tie(lhs.key_, lhs.value_) == std::tie(rhs.key_, rhs.value_);
  }

  friend bool operator!=(metadata const& lhs, metadata const& rhs) {
    return std::tie(lhs.key_, lhs.value_) != std::tie(rhs.key_, rhs.value_);
  }

  friend bool operator<(metadata const& lhs, metadata const& rhs) {
    return std::tie(lhs.key_, lhs.value_) < std::tie(rhs.key_, rhs.value_);
  }

  std::string key_, value_;
};

struct feature {
  uint64_t id_;
  size_t layer_;
  std::pair<uint32_t, uint32_t> zoom_levels_;
  std::vector<metadata> meta_;
  fixed_geometry geometry_;
};

namespace tags {

enum class Feature : protozero::pbf_tag_type {
  packed_sint64_header = 1,

  required_uint64_id = 2,

  packed_uint64_meta_pairs = 3,

  repeated_string_keys = 4,
  repeated_string_values = 5,

  repeated_string_simplify_masks = 6,
  required_FixedGeometry_geometry = 7
};

}  // namespace tags
}  // namespace tiles
