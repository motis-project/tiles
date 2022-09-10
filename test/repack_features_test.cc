#include "catch2/catch_all.hpp"

#include <random>

#include "utl/erase_if.h"

#include "tiles/db/repack_features.h"
#include "tiles/db/tile_index.h"

#include "test_pack_handle.h"

TEST_CASE("repack_features", "[.]") {
  tiles::test_pack_handle handle;

  std::vector<tiles::tile_record> tasks((1 << tiles::kTileDefaultIndexZoomLvl) *
                                        (1 << tiles::kTileDefaultIndexZoomLvl));
  auto it = geo::tile_iterator{tiles::kTileDefaultIndexZoomLvl};
  for (auto& task : tasks) {
    utl::verify(it->z_ == tiles::kTileDefaultIndexZoomLvl, "it broken");
    task.tile_ = *it;
    ++it;
  }
  utl::verify(it->z_ == tiles::kTileDefaultIndexZoomLvl + 1, "it broken");

  static std::mt19937_64 rand{123456};
  std::uniform_int_distribution<size_t> insert_dist(0, tasks.size() - 1);
  std::normal_distribution size_dist(10000., 10000.);

  size_t initial_packs = 0;
  while (handle.size() < 1024ULL * 1024 * 1024 * 40) {
    size_t size = std::fabs(size_dist(rand));
    if (size == 0) {
      continue;
    }
    tasks[insert_dist(rand)].records_.emplace_back(handle.append(size));
    ++initial_packs;
  }
  std::cout << "initial packs: " << initial_packs << std::endl;
  std::shuffle(begin(tasks), end(tasks), rand);

  size_t const initial_task_count =
      std::count_if(begin(tasks), end(tasks),
                    [](auto const& t) { return !t.records_.empty(); });

  size_t finished_task_count = 0;
  tiles::repack_features<std::string_view>(
      handle, tasks,
      [&](auto, auto const& packs) {
        static std::mt19937_64 rand{123456};
        static std::normal_distribution dist{1.1, 0.1};

        size_t size = std::accumulate(
            begin(packs), end(packs), 0ULL,
            [](auto const& acc, auto const& p) { return acc + p.size(); });
        size *= std::fabs(dist(rand));
        return std::string_view{nullptr, size};
      },
      [&](auto const& updates) { finished_task_count += updates.size(); });

  CHECK(initial_task_count == finished_task_count);
}
