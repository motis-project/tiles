#pragma once

#include <string>
#include <vector>

#include "protozero/pbf_builder.hpp"
#include "protozero/pbf_writer.hpp"
#include "protozero/types.hpp"
#include "protozero/varint.hpp"

namespace tiles {

// following directly:
// https://github.com/mapbox/vector-tile-spec/blob/master/2.1/vector_tile.proto
namespace tags {
namespace mvt {

enum GeomType : int { UNKNOWN = 0, POINT = 1, LINESTRING = 2, POLYGON = 3 };

enum class Value : protozero::pbf_tag_type {
  optional_string_string_value = 1,
  optional_float_float_value = 2,
  optional_double_double_value = 3,
  optional_int64_int_value = 4,
  optional_uint64_uint_value = 5,
  optional_sint64_sint_value = 6,
  optional_bool_bool_value = 7
};

enum class Feature : protozero::pbf_tag_type {
  optional_uint64_id = 1,
  packed_uint32_tags = 2,
  optional_GeomType_type = 3,
  packed_uint32_geometry = 4
};

enum class Layer : protozero::pbf_tag_type {
  required_uint32_version = 15,
  required_string_name = 1,
  repeated_Feature_features = 2,
  repeated_string_keys = 3,
  repeated_Value_values = 4,
  optional_uint32_extent = 5
};

enum class Tile : protozero::pbf_tag_type { repeated_Layer_layers = 3 };

}  // namespace mvt
}  // namespace tags
}  // namespace tiles
