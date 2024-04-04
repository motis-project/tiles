#include "gtest/gtest.h"

#include "tiles/db/tile_index.h"
#include "utl/erase_duplicates.h"

TEST(tile_index, tile_index) {
  std::vector<tiles::tile_key_t> keys;
  for (geo::tile_iterator it{0}; it->z_ != 6; ++it) {
    for (auto n : {0UL, 1UL, 131071UL}) {
      SCOPED_TRACE(*it);
      SCOPED_TRACE(n);

      auto const key = tiles::tile_to_key(*it, n);

      EXPECT_EQ(*it, tiles::key_to_tile(key));
      EXPECT_EQ(n, tiles::key_to_n(key));

      keys.push_back(key);
    }
  }

  auto const keys_size = keys.size();
  utl::erase_duplicates(keys);
  EXPECT_EQ(keys_size, keys.size());

  EXPECT_EQ((geo::tile{0, 0, 31}),
            (tiles::key_to_tile(tiles::tile_to_key(geo::tile{0, 0, 31}))));
  EXPECT_EQ((geo::tile{2097151, 0, 0}),
            (tiles::key_to_tile(tiles::tile_to_key(geo::tile{2097151, 0, 0}))));
  EXPECT_EQ((geo::tile{0, 2097151, 0}),
            (tiles::key_to_tile(tiles::tile_to_key(geo::tile{0, 2097151, 0}))));
}
