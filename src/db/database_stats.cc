#include "tiles/db/database_stats.h"

#include <iostream>
#include <numeric>

#include "fmt/core.h"
#include "fmt/ostream.h"

#include "lmdb/lmdb.hpp"

#include "protozero/pbf_message.hpp"
#include "protozero/varint.hpp"

#include "tiles/bin_utils.h"
#include "tiles/db/feature_pack.h"
#include "tiles/db/pack_file.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/util.h"

namespace tiles {

void database_stats(tile_db_handle& db_handle, pack_handle& pack_handle) {
  auto const print_stat = [&](char const* label, auto const& stat) {
    fmt::print(
        std::cout,
        "{:<14} > page: {} depth: {} branch: {} leaf: {} oflow: {} ndat: {}\n",
        label, printable_bytes{stat.ms_psize}, printable_num{stat.ms_depth},
        printable_num{stat.ms_branch_pages}, printable_num{stat.ms_leaf_pages},
        printable_num{stat.ms_overflow_pages}, printable_num{stat.ms_entries});
  };

  auto const print_sizes = [&](auto const& label, auto& m) {
    auto const sum = std::accumulate(begin(m), end(m), 0.);
    std::sort(begin(m), end(m));

    fmt::print(std::cout, "{:<16} > cnt: {} sum: {}", label,
               printable_num(m.size()), printable_bytes(sum));

    if (m.empty()) {
      std::cout << "\n";
      return;
    }

    fmt::print(std::cout, "mean: {} q95: {} max: {}\n",
               printable_bytes{sum / m.size()},
               printable_bytes{m[m.size() * .95]}, printable_bytes{m.back()});
  };

  auto txn = db_handle.make_txn();

  auto features_dbi = db_handle.features_dbi(txn);
  auto tiles_dbi = db_handle.tiles_dbi(txn);
  auto meta_dbi = db_handle.meta_dbi(txn);

  std::cout << ">> lmdb stat:\n";
  print_stat("lmdb:env", db_handle.env_.stat());
  print_stat(" dbi:features", features_dbi.stat());
  print_stat(" dbi:tiles", tiles_dbi.stat());
  print_stat(" dbi:meta", meta_dbi.stat());
  std::cout << "\n";

  std::vector<size_t> pack_sizes;
  std::vector<size_t> index_sizes;
  std::vector<size_t> header_sizes;
  std::vector<size_t> simplify_mask_sizes;
  std::vector<size_t> geometry_sizes;

  auto fc = lmdb::cursor{txn, features_dbi};
  for (auto el = fc.get<tile_key_t>(lmdb::cursor_op::FIRST); el;
       el = fc.get<tile_key_t>(lmdb::cursor_op::NEXT)) {
    pack_records_foreach(el->second, [&](auto record) {
      auto const pack = pack_handle.get(record);
      utl::verify(feature_pack_valid(pack),  //
                  "have invalid feature pack {}", el->first);

      auto const feature_end_offset =
          unpack_features(pack, [&](auto const& str) {
            protozero::pbf_message<tags::feature> msg{str};
            while (msg.next()) {
              switch (msg.tag()) {
                case tags::feature::packed_sint64_header: {
                  auto const* pre = msg.m_data;
                  msg.skip();
                  auto const* post = msg.m_data;
                  header_sizes.push_back(std::distance(pre, post));
                } break;
                case tags::feature::repeated_string_simplify_masks:
                  simplify_mask_sizes.push_back(msg.get_view().size());
                  break;
                case tags::feature::required_fixed_geometry_geometry:
                  geometry_sizes.push_back(msg.get_view().size());
                  break;
                default: msg.skip();
              }
            }
          });

      pack_sizes.push_back(pack.size());
      index_sizes.push_back(pack.size() - feature_end_offset);
    });
  }

  std::cout << ">> payload stats:\n";
  print_sizes("pack", pack_sizes);
  print_sizes("pack: index", index_sizes);
  print_sizes("feature: header", header_sizes);
  print_sizes("feature: masks", simplify_mask_sizes);
  print_sizes("feature: geo", geometry_sizes);

  auto opt_max_prep = txn.get(meta_dbi, kMetaKeyMaxPreparedZoomLevel);
  if (!opt_max_prep) {
    std::cout << "no tiles prepared!" << std::endl;
    return;
  }

  auto const max_prep =
      static_cast<std::uint32_t>(std::stoi(std::string{*opt_max_prep}));
  std::vector<std::vector<size_t>> tile_sizes(max_prep + 1);

  auto tc = lmdb::cursor{txn, tiles_dbi};
  for (auto el = tc.get<tile_key_t>(lmdb::cursor_op::FIRST); el;
       el = tc.get<tile_key_t>(lmdb::cursor_op::NEXT)) {
    auto const tile = key_to_tile(el->first);
    utl::verify(tile.z_ <= max_prep, "tile outside prepared range found!");
    tile_sizes.at(tile.z_).emplace_back(el->second.size());
  }

  auto total = std::accumulate(begin(pack_sizes), end(pack_sizes), 0ULL);
  for (auto z = 0ULL; z < tile_sizes.size(); ++z) {
    print_sizes(fmt::format("tiles[z={:0>2}]", z), tile_sizes[z]);
    total += std::accumulate(begin(tile_sizes[z]), end(tile_sizes[z]), 0ULL);
  }

  std::cout << "====\n";
  fmt::print(std::cout, "total: {}", printable_bytes{total});
  std::cout << "\n\n";
}

}  // namespace tiles
