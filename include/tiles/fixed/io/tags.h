#pragma once

#include "protozero/pbf_builder.hpp"
#include "protozero/pbf_writer.hpp"
#include "protozero/types.hpp"
#include "protozero/varint.hpp"

namespace tiles {
namespace tags {

enum FixedGeometryType : int {
  UNKNOWN = 0,
  POINT = 1,
  POLYLINE = 2,
  POLYGON = 3
};

enum class FixedGeometry : protozero::pbf_tag_type {
  required_FixedGeometryType_type = 1,
  packed_sint64_geometry = 2
};

}  // namespace tags
}  // namespace tiles
