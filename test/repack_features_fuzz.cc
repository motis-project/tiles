#define TILES_REPACK_FEATURES_SILENT

#include <iostream>

#include "utl/to_vec.h"

#include "tiles/bin_utils.h"
#include "tiles/db/repack_features.h"
#include "tiles/db/tile_index.h"

#include "test_pack_handle.h"

struct delta_record {
  geo::tile tile_;
  long delta_{0};
};

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size) {
  tiles::test_pack_handle handle;

  std::vector<tiles::tile_record> tasks(2048);
  auto it = geo::tile_iterator{tiles::kTileDefaultIndexZoomLvl};
  for (auto i = 0ULL; i < tasks.size(); ++i) {
    utl::verify(it->z_ == tiles::kTileDefaultIndexZoomLvl, "it broken");
    tasks[i].tile_ = *it;
    ++it;
  }

  auto deltas = utl::to_vec(
      tasks, [](auto const& tr) { return delta_record{tr.tile_, 0LL}; });

  constexpr auto const kIncrement = 4 + 2 + 2;
  for (auto i = 0ULL; i < size; i += kIncrement) {
    if (size < i + kIncrement) {
      break;
    }

    auto const task =
        tiles::read<uint32_t>(reinterpret_cast<char const*>(data), i);
    auto const pack =
        tiles::read<uint16_t>(reinterpret_cast<char const*>(data), i + 4);
    auto const delta =
        tiles::read<int16_t>(reinterpret_cast<char const*>(data), i + 4 + 2);

    if (task >= tasks.size() || pack == 0 || pack + delta < 1) {
      continue;
    }
    tasks[task].records_.emplace_back(handle.append(pack));
    deltas[task].delta_ += delta;
  }

  size_t const initial_task_count =
      std::count_if(begin(tasks), end(tasks),
                    [](auto const& t) { return !t.records_.empty(); });

  if (initial_task_count == 0) {
    return 0;
  }

  std::sort(begin(deltas), end(deltas),
            [](auto const& a, auto const& b) { return a.tile_ < b.tile_; });

  size_t finished_task_count = 0;
  tiles::repack_features<std::string_view>(
      handle, tasks,
      [&](auto const tile, auto const& packs) {
        auto const it = std::lower_bound(
            begin(deltas), end(deltas), tile,
            [](auto const& lhs, auto const& rhs) { return lhs.tile_ < rhs; });
        utl::verify(it != end(deltas) && it->tile_ == tile, "unknown tile");
        size_t size = std::accumulate(begin(packs), end(packs), 0ULL,
                                      [](auto const& acc, auto const& p) {
                                        return acc + p.size();
                                      }) +
                      it->delta_;
        return std::string(size, '\0');
      },
      [&](auto const& updates) { finished_task_count += updates.size(); });

  utl::verify(initial_task_count == finished_task_count,
              "something is missing");

  return 0;
}