#pragma once

#include <vector>

#include "osmium/osm.hpp"

#include "sol.hpp"

#include "tiles/fixed/algo/area.h"
#include "tiles/osm/read_osm_geometry.h"
#include "tiles/util.h"

namespace tiles {

struct pending_feature {
  pending_feature(osmium::OSMObject const& obj,
                  std::function<fixed_geometry()> read_geometry)
      : obj_{obj},
        is_approved_{false},
        read_geometry_{std::move(read_geometry)} {}

  int64_t get_id() const { return obj_.id(); }

  bool has_tag(std::string const& key, std::string const& value) {
    // TODO call .tags() and cache the result
    return value == obj_.get_value_by_key(key.c_str(), "");
  }

  bool has_any_tag(std::string const& key, sol::variadic_args va) {
    if (std::distance(va.begin(), va.end()) == 0) {
      return obj_.get_value_by_key(key.c_str()) != nullptr;
    } else {
      auto const actual_value = obj_.get_value_by_key(key.c_str(), "");
      return std::any_of(std::begin(va), std::end(va),
                         [&actual_value](std::string const& value) {
                           return actual_value == value;
                         });
    }
  }

  void set_approved_min(uint32_t min) {
    set_approved(min, (kMaxZoomLevel + 1));
  }

  void set_approved_full() { set_approved(0, (kMaxZoomLevel + 1)); }

  // earth radius = 6371000 meters
  // map size @ zoom 20 = 4096 << 20 = 4294967296 pixel
  // 1 m = 674.14 px -> 1 m^2 = 454000 px^2
  // pairwise growing area, lower zoom_level:
  // >> max_area_1, zoom_level_1, max_area_2, zoom_level_2, ...
  void set_approved_min_by_area(sol::variadic_args va) {
    utl::verify(std::distance(va.begin(), va.end()) % 2 == 0,
                "set_approved_by_area input size not even!");

    if (!geometry_) {
      geometry_ = read_geometry_();
    }
    auto const area = tiles::area(*geometry_);

    for (auto it = va.begin(); it != va.end(); it += 2) {
      auto const limit = static_cast<fixed_coord_t>(*(it + 1));
      if (limit == -1 || area < limit) {
        set_approved_min(*it);
        break;
      }
    }
  }

  void set_approved(uint32_t min = 0, uint32_t max = (kMaxZoomLevel + 1)) {
    is_approved_ = true;
    zoom_levels_ = {min, max};
  }

  void set_target_layer(std::string target_layer) {
    target_layer_ = std::move(target_layer);
  }

  void add_tag_as_metadata(std::string tag) {
    tag_as_metadata_.emplace_back(tag);
  }

  void add_metadata(std::string key, std::string value) {
    metadata_.emplace_back(std::move(key), std::move(value));
  }

  osmium::OSMObject const& obj_;

  bool is_approved_;
  std::pair<uint32_t, uint32_t> zoom_levels_;

  std::string target_layer_;
  std::vector<std::string> tag_as_metadata_;
  std::vector<std::pair<std::string, std::string>> metadata_;

  std::function<fixed_geometry()> read_geometry_;
  std::optional<fixed_geometry> geometry_;
};

}  // namespace tiles
