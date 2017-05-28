#pragma once

#include "rocksdb/utilities/spatial_db.h"

#include "geo/webmercator.h"

#include "tiles/util.h"

namespace tiles {

constexpr auto kTileSize = 4096;
using proj = geo::webmercator<kTileSize, 20>;
constexpr auto kMaxZoomLevel = proj::kMaxZoomLevel;

constexpr auto kOverdraw = 128;

struct tile_spec {

  tile_spec(uint32_t const x, uint32_t const y, uint32_t const z)
      : x_(x), y_(y), z_(z), delta_z_(kMaxZoomLevel - z) {
    verify(delta_z_ >= 0 && delta_z_ <= kMaxZoomLevel, "invalid z");

    pixel_bounds_ = proj::tile_bounds_pixel(x_, y_);  // lvl z

    bounds_ = pixel_bounds_;  // lvl 20
    bounds_.minx_ = bounds_.minx_ << delta_z_;
    bounds_.miny_ = bounds_.miny_ << delta_z_;
    bounds_.maxx_ = bounds_.maxx_ << delta_z_;
    bounds_.maxy_ = bounds_.maxy_ << delta_z_;

    overdraw_bounds_ = bounds_;
    overdraw_bounds_.minx_ -= kOverdraw;
    overdraw_bounds_.miny_ -= kOverdraw;
    overdraw_bounds_.maxx_ += kOverdraw;
    overdraw_bounds_.maxy_ += kOverdraw;
  }

  static std::vector<std::string> const& zoom_level_names() {
    static std::vector<std::string> names{"high", "mid"};
    return names;
  }

  static std::vector<uint32_t> const& zoom_level_bases() {
    static std::vector<uint32_t> bases{14, 10};
    return bases;
  }

  std::string z_str() const {
    auto const& bases = zoom_level_bases();
    auto const it =
        std::find_if(begin(bases), end(bases),
                     [this](auto const& base) { return z_ >= base; });

    if (it == end(bases)) {
      return zoom_level_names().back();
    }

    return zoom_level_names().at(std::distance(begin(bases), it));
  }

  rocksdb::spatial::BoundingBox<double> bbox() const {
    return {
        static_cast<double>(bounds_.minx_), static_cast<double>(bounds_.miny_),
        static_cast<double>(bounds_.maxx_), static_cast<double>(bounds_.maxy_)};
  }

  uint32_t x_, y_, z_, delta_z_;
  geo::pixel_bounds pixel_bounds_, bounds_, overdraw_bounds_;
};

struct tile_iterator {
  tile_iterator() : tile_iterator(0, 0, 0) {}
  explicit tile_iterator(uint32_t const z) : tile_iterator(0, 0, z) {}
  tile_iterator(uint32_t const x, uint32_t const y, uint32_t const z)
      : x_(x), y_(y), z_(z) {}

  tile_spec operator*() const { return tile_spec{x_, y_, z_}; }

  tile_iterator& operator++() {
    if (z_ > kMaxZoomLevel) {
      return *this;
    }

    uint32_t const tile_count = 1 << z_;

    ++x_;
    if (x_ == tile_count) {
      x_ = 0;
      ++y_;
      if (y_ == tile_count) {
        y_ = 0;
        ++z_;
      }
    }

    return *this;
  }

  bool operator==(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) == std::tie(o.z_, o.x_, o.y_);
  }
  bool operator!=(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) != std::tie(o.z_, o.x_, o.y_);
  }
  bool operator>(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) > std::tie(o.z_, o.x_, o.y_);
  }
  bool operator<(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) < std::tie(o.z_, o.x_, o.y_);
  }
  bool operator>=(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) >= std::tie(o.z_, o.x_, o.y_);
  }
  bool operator<=(tile_iterator const& o) const {
    return std::tie(z_, x_, y_) <= std::tie(o.z_, o.x_, o.y_);
  }

private:
  uint32_t x_, y_, z_;
};

struct tile_pyramid {
  using iterator = tile_iterator;

  iterator begin() const { return iterator{}; }
  iterator end() const { return iterator{kMaxZoomLevel + 1}; }
};

}  // namespace tiles