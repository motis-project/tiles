#pragma once

#include "geo/tile.h"

#include "utl/verify.h"

#include "tiles/constants.h"
#include "tiles/fixed/algo/shift.h"
#include "tiles/fixed/fixed_geometry.h"

namespace tiles {

// tile_key_t : 64bit unsigned key
//  5 bit | z | zoom level          [0 - 32)
// 21 bit | y | y index of tile     [0 - 2097152)
// 21 bit | x | x index of tile     [0 - 2097152)
// 17 bit | n | n-th entry of tile  [0 - 131072)
using tile_key_t = uint64_t;

constexpr tile_key_t kTileKeyZShift{59ULL};
constexpr tile_key_t kTileKeyZBits{5ULL};
constexpr tile_key_t kTileKeyZMask{((1ULL << kTileKeyZBits) - 1ULL)};
constexpr tile_key_t kTileKeyYShift{38};
constexpr tile_key_t kTileKeyYBits{21ULL};
constexpr tile_key_t kTileKeyYMask{((1ULL << kTileKeyYBits) - 1ULL)};
constexpr tile_key_t kTileKeyXShift{17ULL};
constexpr tile_key_t kTileKeyXBits{21ULL};
constexpr tile_key_t kTileKeyXMask{((1ULL << kTileKeyXBits) - 1ULL)};
constexpr tile_key_t kTileKeyNShift{0ULL};
constexpr tile_key_t kTileKeyNBits{17ULL};
constexpr tile_key_t kTileKeyNMask{((1ULL << kTileKeyNBits) - 1ULL)};

inline tile_key_t tile_to_key(tile_key_t const x, tile_key_t const y,
                              tile_key_t const z, tile_key_t const n = 0) {
  utl::verify((x & kTileKeyXMask) == x && (y & kTileKeyYMask) == y &&
                  (z & kTileKeyZMask) == z && (n & kTileKeyNMask) == n,
              "tile_to_key: value(s) in invalid range(s)");

  tile_key_t key{0};
  key |= (z & kTileKeyZMask) << kTileKeyZShift;
  key |= (y & kTileKeyYMask) << kTileKeyYShift;
  key |= (x & kTileKeyXMask) << kTileKeyXShift;
  key |= (n & kTileKeyNMask) << kTileKeyNShift;
  return key;
}

inline tile_key_t tile_to_key(geo::tile const t, tile_key_t const n = 0) {
  return tile_to_key(t.x_, t.y_, t.z_, n);
}

inline geo::tile key_to_tile(tile_key_t const key) {
  return geo::tile{
      static_cast<uint32_t>((key >> kTileKeyXShift) & kTileKeyXMask),
      static_cast<uint32_t>((key >> kTileKeyYShift) & kTileKeyYMask),
      static_cast<uint32_t>((key >> kTileKeyZShift) & kTileKeyZMask)};
}

inline tile_key_t key_to_n(tile_key_t const key) {
  return (key >> kTileKeyNShift) & kTileKeyNMask;
}

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

}  // namespace tiles
