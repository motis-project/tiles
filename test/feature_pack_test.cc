#include "catch2/catch_all.hpp"

#include "tiles/bin_utils.h"
#include "tiles/db/feature_pack.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/convert.h"
#include "tiles/fixed/fixed_geometry.h"

TEST_CASE("feature_pack") {
  SECTION("empty") {
    auto const pack = tiles::pack_features({});
    CHECK(tiles::feature_pack_valid(pack));

    REQUIRE(pack.size() == 10ULL);
    CHECK(tiles::read_nth<uint32_t>(pack.data(), 0) == 0U);  // feature count
    CHECK(tiles::read_nth<uint8_t>(pack.data(), 4) == 0U);  // segment count
    CHECK(tiles::read_nth<uint8_t>(pack.data(), 5) == 0U);  // null terminator

    auto count = 0;
    tiles::unpack_features(pack, [&](auto const&) { ++count; });
    CHECK(count == 0);

    tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                           [&](auto const&) { ++count; });
    CHECK(count == 0);
  }

  SECTION("one") {
    tiles::fixed_polyline tuda{
        {tiles::latlng_to_fixed({49.87805785566374, 8.654533624649048}),
         tiles::latlng_to_fixed({49.87574857815668, 8.657859563827515})}};

    tiles::feature f{42ULL, 1, {0U, 20U}, {}, tuda};
    std::string ser = tiles::serialize_feature(f);

    {
      auto const pack = tiles::pack_features({ser});
      CHECK(tiles::feature_pack_valid(pack));

      REQUIRE(pack.size() > 5ULL);
      CHECK(tiles::read_nth<uint32_t>(pack.data(), 0) == 1U);  // feature count
      CHECK(tiles::read_nth<uint8_t>(pack.data(), 4) == 0U);  // segment count

      auto count = 0;
      tiles::unpack_features(pack, [&](auto const&) { ++count; });
      CHECK(count == 1);

      tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                             [&](auto const&) { ++count; });
      CHECK(count == 2);
    }

    {
      auto const pack = tiles::pack_features({536, 347, 10}, {},
                                             {tiles::pack_features({ser})});
      CHECK(tiles::feature_pack_valid(pack));

      REQUIRE(pack.size() > 5ULL);
      CHECK(tiles::read_nth<uint32_t>(pack.data(), 0) == 1U);  // feature count
      CHECK(tiles::read_nth<uint8_t>(pack.data(), 4) == 1U);  // segment count

      auto count = 0;
      tiles::unpack_features(pack, [&](auto const&) { ++count; });
      CHECK(count == 1);

      tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                             [&](auto const&) { ++count; });
      CHECK(count == 2);
    }
  }
}
