#include "gtest/gtest.h"

#include "tiles/bin_utils.h"
#include "tiles/db/feature_pack.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/convert.h"
#include "tiles/fixed/fixed_geometry.h"

TEST(feature_pack, empty) {
  auto const pack = tiles::pack_features({});
  EXPECT_TRUE(tiles::feature_pack_valid(pack));

  ASSERT_TRUE(pack.size() == 10ULL);
  EXPECT_TRUE(tiles::read_nth<uint32_t>(pack.data(), 0) ==
              0U);  // feature count
  EXPECT_TRUE(tiles::read_nth<uint8_t>(pack.data(), 4) == 0U);  // segment count
  EXPECT_TRUE(tiles::read_nth<uint8_t>(pack.data(), 5) ==
              0U);  // null terminator

  auto count = 0;
  tiles::unpack_features(pack, [&](auto const&) { ++count; });
  EXPECT_TRUE(count == 0);

  tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                         [&](auto const&) { ++count; });
  EXPECT_TRUE(count == 0);
}

TEST(feature_pack, one) {
  tiles::fixed_polyline tuda{
      {tiles::latlng_to_fixed({49.87805785566374, 8.654533624649048}),
       tiles::latlng_to_fixed({49.87574857815668, 8.657859563827515})}};

  auto const f = tiles::feature{42ULL, 1, {0U, 20U}, {}, tuda};
  auto const ser = tiles::serialize_feature(f);

  {
    auto const pack = tiles::pack_features({ser});
    EXPECT_TRUE(tiles::feature_pack_valid(pack));

    ASSERT_TRUE(pack.size() > 5ULL);
    EXPECT_TRUE(tiles::read_nth<uint32_t>(pack.data(), 0) ==
                1U);  // feature count
    EXPECT_TRUE(tiles::read_nth<uint8_t>(pack.data(), 4) ==
                0U);  // segment count

    auto count = 0;
    tiles::unpack_features(pack, [&](auto const&) { ++count; });
    EXPECT_TRUE(count == 1);

    tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                           [&](auto const&) { ++count; });
    EXPECT_TRUE(count == 2);
  }

  {
    auto const pack =
        tiles::pack_features({536, 347, 10}, {}, {tiles::pack_features({ser})});
    EXPECT_TRUE(tiles::feature_pack_valid(pack));

    ASSERT_TRUE(pack.size() > 5ULL);
    EXPECT_TRUE(tiles::read_nth<uint32_t>(pack.data(), 0) ==
                1U);  // feature count
    EXPECT_TRUE(tiles::read_nth<uint8_t>(pack.data(), 4) ==
                1U);  // segment count

    auto count = 0;
    tiles::unpack_features(pack, [&](auto const&) { ++count; });
    EXPECT_TRUE(count == 1);

    tiles::unpack_features(geo::tile{}, pack, geo::tile{},
                           [&](auto const&) { ++count; });
    EXPECT_TRUE(count == 2);
  }
}
