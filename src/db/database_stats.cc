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
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/util.h"

namespace tiles {

void database_stats(tile_db_handle& handle) {
  auto const format_num = [](auto& os, char const* label, double const n) {
    auto const k = n / 1e3;
    auto const m = n / 1e6;
    auto const g = n / 1e9;
    if (n < 1e3) {
      fmt::print(os, "{}: {:>6}  ", label, n);
    } else if (k < 1e3) {
      fmt::print(os, "{}: {:>6.1f}K ", label, k);
    } else if (m < 1e3) {
      fmt::print(os, "{}: {:>6.1f}M ", label, m);
    } else {
      fmt::print(os, "{}: {:>6.1f}G ", label, g);
    }
  };

  auto const format_bytes = [](auto& os, char const* label, double const n) {
    auto const k = n / 1024;
    auto const m = n / (1024 * 1024);
    auto const g = n / (1024 * 1024 * 1024);
    if (n < 1024) {
      fmt::print(os, "{}: {:>7.2f}B  ", label, n);
    } else if (k < 1024) {
      fmt::print(os, "{}: {:>7.2f}KB ", label, k);
    } else if (m < 1024) {
      fmt::print(os, "{}: {:>7.2f}MB ", label, m);
    } else {
      fmt::print(os, "{}: {:>7.2f}GB ", label, g);
    }
  };

  auto const print_stat = [&](auto& os, char const* label, auto const& stat) {
    fmt::print(std::cout, "{:<14} > ", label);
    format_bytes(os, "page", stat.ms_psize);
    format_num(os, "depth", stat.ms_depth);
    format_num(os, "branch", stat.ms_branch_pages);
    format_num(os, "leafs", stat.ms_leaf_pages);
    format_num(os, "oflow", stat.ms_overflow_pages);
    format_num(os, "numdat", stat.ms_entries);
    std::cout << "\n";
  };

  auto const print_sizes = [&](auto const& label, auto& m) {
    auto const sum = std::accumulate(begin(m), end(m), 0.);
    std::sort(begin(m), end(m));

    fmt::print(std::cout, "{:<16} > ", label);
    format_num(std::cout, "cnt", m.size());
    format_bytes(std::cout, "sum", sum);

    if (m.empty()) {
      std::cout << "\n";
      return;
    }

    format_bytes(std::cout, "mean", sum / m.size());
    format_bytes(std::cout, "q95", m[m.size() * .95]);
    format_bytes(std::cout, "max", m.back());
    std::cout << "\n";
  };

  auto txn = handle.make_txn();

  auto features_dbi = handle.features_dbi(txn);
  auto tiles_dbi = handle.tiles_dbi(txn);
  auto meta_dbi = handle.meta_dbi(txn);

  std::cout << ">> lmdb stat:\n";
  print_stat(std::cout, "lmdb:env", handle.env_.stat());
  print_stat(std::cout, " dbi:features", features_dbi.stat());
  print_stat(std::cout, " dbi:tiles", tiles_dbi.stat());
  print_stat(std::cout, " dbi:meta", meta_dbi.stat());
  std::cout << "\n";

  std::vector<size_t> pack_sizes;
  std::vector<size_t> index_sizes;
  std::vector<size_t> header_sizes;
  std::vector<size_t> simplify_mask_sizes;
  std::vector<size_t> geometry_sizes;

  auto fc = lmdb::cursor{txn, features_dbi};
  for (auto el = fc.get<tile_index_t>(lmdb::cursor_op::FIRST); el;
       el = fc.get<tile_index_t>(lmdb::cursor_op::NEXT)) {
    pack_sizes.emplace_back(el->second.size());

    auto const index_offset = read_nth<uint32_t>(el->second.data(), 1);
    if (index_offset != 0) {
      auto idx_ptr = el->second.data() + index_offset;
      auto const end_ptr = el->second.data() + el->second.size();
      auto tree_offset = 0ull;
      while (idx_ptr < end_ptr && tree_offset == 0) {
        tree_offset = protozero::decode_varint(&idx_ptr, end_ptr);
      }

      if (tree_offset == 0) {
        index_sizes.push_back(el->second.size() - index_offset);
      } else {
        index_sizes.push_back(el->second.size() - tree_offset);
      }
    }

    unpack_features(el->second, [&](auto const& str) {
      protozero::pbf_message<tags::Feature> msg{str};
      while (msg.next()) {
        switch (msg.tag()) {
          case tags::Feature::packed_sint64_header: {
            auto const* pre = msg.m_data;
            msg.skip();
            auto const* post = msg.m_data;
            header_sizes.push_back(std::distance(pre, post));
          } break;
          case tags::Feature::repeated_string_simplify_masks:
            simplify_mask_sizes.push_back(msg.get_view().size());
            break;
          case tags::Feature::required_FixedGeometry_geometry:
            geometry_sizes.push_back(msg.get_view().size());
            break;
          default: msg.skip();
        }
      }
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

  uint32_t max_prep = std::stoi(std::string{*opt_max_prep});
  std::vector<std::vector<size_t>> tile_sizes(max_prep + 1);

  auto tc = lmdb::cursor{txn, tiles_dbi};
  for (auto el = tc.get<tile_index_t>(lmdb::cursor_op::FIRST); el;
       el = tc.get<tile_index_t>(lmdb::cursor_op::NEXT)) {
    auto const& tile = tile_key_to_tile(el->first);
    utl::verify(tile.z_ <= max_prep, "tile outside prepared range found!");
    tile_sizes.at(tile.z_).emplace_back(el->second.size());
  }

  auto total = std::accumulate(begin(pack_sizes), end(pack_sizes), 0ull);
  for (auto z = 0u; z < tile_sizes.size(); ++z) {
    print_sizes(fmt::format("tiles[z={:0>2}]", z), tile_sizes[z]);
    total += std::accumulate(begin(tile_sizes[z]), end(tile_sizes[z]), 0ull);
  }

  std::cout << "====\n";
  format_bytes(std::cout, "total", total);
  std::cout << "\n\n";
}

}  // namespace tiles
