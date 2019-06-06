#pragma once

#include <algorithm>
#include <iterator>
#include <vector>

#include "geo/tile.h"

#include "tiles/bin_utils.h"
#include "tiles/util.h"

namespace tiles {

using quad_entry_t = uint32_t;
constexpr auto const kQuadEntryBits = sizeof(quad_entry_t) * 8;
constexpr auto const kQuadChildOffset = kQuadEntryBits - 4;
constexpr auto const kQuadOffsetMask =
    (static_cast<quad_entry_t>(1) << (kQuadEntryBits - 4)) - 1;

// expected for quad_tree_input:
// - parent elements first; then child elements in quad_pos order
// - full range with all elments is consecutive in memory:
//      [p.offset_, p.offset_+p.size_+c0.size_+c1.size_+c2.size_+c3.size_)
// - see: geohash / z-order curve [https://en.wikipedia.org/wiki/Z-order_curve]
struct quad_tree_input {
  geo::tile tile_;
  uint32_t offset_, size_;
};

std::string make_quad_tree(geo::tile const& root,
                           std::vector<quad_tree_input> const& input);

template <typename Fn>
void walk_quad_tree(char const* tree, geo::tile const& root,
                    geo::tile const& query, Fn&& fn) {
  if (read_nth<quad_entry_t>(tree, 3) == 0) {
    return;  // whole tree empty
  }

  {
    auto parent = root;
    while (true) {
      if (parent == query) {
        return fn(read_nth<quad_entry_t>(tree, 1),
                  read_nth<quad_entry_t>(tree, 3));  // whole range
      }
      if (parent.z_ == 0) {
        break;
      }
      parent = parent.parent();
    }
  }

  std::vector<geo::tile> trace{query};
  while (!(trace.back().parent() == root)) {
    if (trace.back().z_ == 0 || trace.back().z_ < root.z_) {
      return;
    }
    trace.push_back(trace.back().parent());
  }
  trace.push_back(root);
  std::reverse(begin(trace), end(trace));

  auto offset = 0;
  for (auto it = begin(trace); it != end(trace); ++it) {
    if (*it == query) {
      return fn(read_nth<quad_entry_t>(tree, offset + 1),
                read_nth<quad_entry_t>(tree, offset + 3));  // emit full subtree
    } else if (read_nth<quad_entry_t>(tree, offset + 2) != 0) {
      fn(read_nth<quad_entry_t>(tree, offset + 1),
         read_nth<quad_entry_t>(tree, offset + 2));  // emit self
    }

    if (read_nth<quad_entry_t>(tree, offset) == 0) {
      return;  // no more children
    }

    // descend to next level
    utl::verify(std::next(it) != end(trace), "should not happen");
    auto const& next_tile = *std::next(it);

    auto const curr = read_nth<quad_entry_t>(tree, offset);
    offset = curr & kQuadOffsetMask;
    for (auto i = 0u; i < next_tile.quad_pos(); ++i) {
      if (bit_set(curr, i + kQuadChildOffset)) {
        offset += 4;  // four entries per node
      }
    }
  }
}

}  // namespace tiles
