#pragma once

#include "geo/webmercator.h"

namespace tiles {

constexpr auto kTileSize = 4096;
using proj = geo::webmercator<kTileSize, 20>;
constexpr auto kMaxZoomLevel = proj::kMaxZoomLevel;

}  // namespace tiles
