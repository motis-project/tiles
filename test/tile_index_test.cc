#include "catch2/catch_all.hpp"

#include "tiles/db/tile_index.h"
#include "utl/erase_duplicates.h"

TEST_CASE("tile_index") {
  std::vector<tiles::tile_key_t> keys;
  for (geo::tile_iterator it{0}; it->z_ != 6; ++it) {
    for (auto n : {0UL, 1UL, 131071UL}) {
      CAPTURE(*it);
      CAPTURE(n);

      auto const key = tiles::tile_to_key(*it, n);

      CHECK(*it == tiles::key_to_tile(key));
      CHECK(n == tiles::key_to_n(key));

      keys.push_back(key);
    }
  }

  auto const keys_size = keys.size();
  utl::erase_duplicates(keys);
  CHECK(keys_size == keys.size());

  CHECK(geo::tile{0, 0, 31} ==
        tiles::key_to_tile(tiles::tile_to_key(geo::tile{0, 0, 31})));
  CHECK(geo::tile{2097151, 0, 0} ==
        tiles::key_to_tile(tiles::tile_to_key(geo::tile{2097151, 0, 0})));
  CHECK(geo::tile{0, 2097151, 0} ==
        tiles::key_to_tile(tiles::tile_to_key(geo::tile{0, 2097151, 0})));
}
