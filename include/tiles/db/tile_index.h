#pragma once

#include "geo/tile.h"

#include "tiles/constants.h"
#include "tiles/fixed/algo/shift.h"
#include "tiles/fixed/fixed_geometry.h"

namespace tiles {

using tile_index_t = uint64_t;
constexpr auto const kTileDefaultIndexZoomLvl = 10;

inline geo::tile_range make_tile_range(fixed_box /*copy*/ box,
                                       uint32_t z = kTileDefaultIndexZoomLvl) {
  shift(box, z);

  auto const x_1 = static_cast<uint32_t>(box.min_corner().x() / kTileSize);
  auto const y_1 = static_cast<uint32_t>(box.min_corner().y() / kTileSize);
  auto const x_2 = static_cast<uint32_t>(box.max_corner().x() / kTileSize);
  auto const y_2 = static_cast<uint32_t>(box.max_corner().y() / kTileSize);

  return geo::make_tile_range(x_1, y_1, x_2, y_2, z);
}

inline tile_index_t make_feature_key(tile_index_t const x,  //
                                     tile_index_t const y,  //
                                     tile_index_t const z,  //
                                     tile_index_t const idx = 0) {
  auto const coord_bits = z + 1;
  auto const idx_bits = sizeof(tile_index_t) * 8 - 2 * coord_bits;

  auto const coord_mask = (static_cast<tile_index_t>(1) << coord_bits) - 1;
  auto const idx_mask = (static_cast<tile_index_t>(1) << idx_bits) - 1;

  assert((x & coord_mask) == x);
  assert((y & coord_mask) == y);
  assert((idx & idx_mask) == idx);

  tile_index_t key = 0;
  key |= (y & coord_mask) << (coord_bits + idx_bits);
  key |= (x & coord_mask) << idx_bits;
  key |= idx & idx_mask;
  return key;
}

inline geo::tile feature_key_to_tile(tile_index_t key,
                                     uint32_t z = kTileDefaultIndexZoomLvl) {
  auto const coord_bits = z + 1;
  auto const idx_bits = sizeof(tile_index_t) * 8 - 2 * coord_bits;

  auto const coord_mask = (static_cast<tile_index_t>(1) << coord_bits) - 1;

  return geo::tile{
      static_cast<uint32_t>((key >> idx_bits) & coord_mask),  //
      static_cast<uint32_t>((key >> (coord_bits + idx_bits)) & coord_mask),  //
      z};
}

inline tile_index_t feature_key_to_idx(tile_index_t key,
                                       uint32_t z = kTileDefaultIndexZoomLvl) {
  auto const coord_bits = z + 1;
  auto const idx_bits = sizeof(tile_index_t) * 8 - 2 * coord_bits;
  auto const idx_mask = (static_cast<tile_index_t>(1) << idx_bits) - 1;

  return key & idx_mask;
}

inline tile_index_t make_feature_key(geo::tile const& t, size_t const idx = 0) {
  return make_feature_key(t.x_, t.y_, t.z_, idx);
}

inline tile_index_t make_tile_key(geo::tile const& t) {
  constexpr tile_index_t kCoordBits = 24;
  constexpr tile_index_t kZLvlBits =
      std::numeric_limits<tile_index_t>::digits - 2 * kCoordBits;

  auto const coord_mask = (static_cast<tile_index_t>(1) << kCoordBits) - 1;
  auto const z_lvl_mask = (static_cast<tile_index_t>(1) << kZLvlBits) - 1;

  assert((t.x_ & coord_mask) == t.x_);
  assert((t.y_ & coord_mask) == t.y_);
  assert((t.z_ & z_lvl_mask) == t.z_);

  tile_index_t key = 0;
  key |= (t.z_ & z_lvl_mask) << (2 * kCoordBits);
  key |= (t.x_ & coord_mask) << kCoordBits;
  key |= (t.y_ & coord_mask);

  return key;
}

inline geo::tile tile_key_to_tile(tile_index_t const& key) {
  constexpr tile_index_t kCoordBits = 24;
  constexpr tile_index_t kZLvlBits =
      std::numeric_limits<tile_index_t>::digits - 2 * kCoordBits;

  auto const coord_mask = (static_cast<tile_index_t>(1) << kCoordBits) - 1;
  auto const z_lvl_mask = (static_cast<tile_index_t>(1) << kZLvlBits) - 1;

  uint32_t z = (key >> 2 * kCoordBits) & z_lvl_mask;
  uint32_t x = (key >> kCoordBits) & coord_mask;
  uint32_t y = key & coord_mask;

  return geo::tile{x, y, z};
}

}  // namespace tiles
