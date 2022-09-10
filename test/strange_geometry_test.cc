#include "catch2/catch_all.hpp"

#include <iomanip>
#include <iostream>

#include "geo/tile.h"

#include "tiles/db/feature_pack.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/convert.h"
#include "tiles/fixed/fixed_geometry.h"

TEST_CASE("at_antimeridian") {
  geo::tile tile{1023, 560, 10};

  tiles::fixed_polyline west_coast_road{
      {tiles::latlng_to_fixed({-16.7935583, 180.0000000}),
       tiles::latlng_to_fixed({-16.7936245, 179.9997797})}};

  tiles::feature f{42ULL, 1, {0U, 20U}, {}, west_coast_road};

  std::string ser = tiles::serialize_feature(f);

  auto quick_pack = tiles::pack_features({ser});
  auto optimal_pack = tiles::pack_features(tile, {}, {quick_pack});

  CHECK(!optimal_pack.empty());
}
