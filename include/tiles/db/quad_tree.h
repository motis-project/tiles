#pragma once

#include <algorithm>
#include <iterator>
#include <vector>

#include "geo/tile.h"

#include "utl/verify.h"

#include "tiles/bin_utils.h"
#include "tiles/util.h"

namespace tiles {

// QUAD TREE "WIRE FORMAT" SPECIFICATION v1
//
// A quad tree is a spatial index for geometrical features. Nearby features are
// stored as compact "spans" in the fashion of a space filling curve following
// the the tile numbering scheme. This allows efficient inclusion queries.
//
// Multiple indices are stored as in an extra segment of a feature pack. (See
// feature_pack.h and feature_pack_quadtree.h for details.)
//
// The quad tree is stored as a compact string of nodes starting at tree base.
// Each parent node n consists of four uint32_t values:
//  - n[0] : child node offset and bitmap
//  - n[1] : feature data offset
//  - n[2] : number of features in this node (without children)
//  - n[3] : number of features in this subtree (this node and children)
//
// SIBLINGS
// All of the up to four sibling nodes (children 0-3 of a parent) are stored
// consecutively in "pre-order" fashion. Their offset from the tree base and
// their individual existence is stored in n[0].
//  - bit    31 : true -> child 3 exists (max-y, max-x)
//  - bit    30 : true -> child 2 exists (max-y, min-x)
//  - bit    29 : true -> child 1 exists (min-y, max-x)
//  - bit    28 : true -> child 0 exists (min-y, min-x)
//  - bits 27-0 : distance from tree base to the first existing child
//
// DATA
// Feature spans are stored in the same order as nodes. Since they are stored
// outside the tree, the feature data offset at n[1] is related to that buffer
// (and NOT tree base). The storage format at that location should be in a way,
// that n[2] or n[3] features can be read from n[1] sequentially, without
// additional information.

using quad_entry_t = uint32_t;
constexpr auto const kQuadEntryBits = sizeof(quad_entry_t) * 8U;
constexpr auto const kQuadChildOffset =
    static_cast<unsigned>(kQuadEntryBits - 4U);
constexpr auto const kQuadOffsetMask =
    (static_cast<quad_entry_t>(1) << (kQuadEntryBits - 4)) - 1;

constexpr size_t const kQuadNodeChildOffset = 0;
constexpr size_t const kQuadNodeDataOffset = 1;
constexpr size_t const kQuadNodeNFeatures = 2;
constexpr size_t const kQuadNodeNFeaturesSubtree = 3;

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
void walk_quad_tree(char const* base, geo::tile const& root,
                    geo::tile const& query, Fn&& fn) {
  if (read_nth<quad_entry_t>(base, kQuadNodeNFeaturesSubtree) == 0) {
    return;  // whole tree empty
  }

  {  // search z-downward -> entire quad tree is smaller than query
    auto parent = root;
    while (true) {
      if (parent == query) {  // whole range
        return fn(read_nth<quad_entry_t>(base, kQuadNodeDataOffset),
                  read_nth<quad_entry_t>(base, kQuadNodeNFeaturesSubtree));
      }
      if (parent.z_ == 0) {
        break;
      }
      parent = parent.parent();
    }
  }

  // search z-upward -> query smaller / inside this quad tree
  std::vector<geo::tile> trace{query};
  while (!(trace.back().parent() == root)) {
    if (trace.back().z_ == 0 || trace.back().z_ < root.z_) {
      return;
    }
    trace.push_back(trace.back().parent());
  }
  trace.push_back(root);
  std::reverse(begin(trace), end(trace));

  auto offset = 0U;
  for (auto it = begin(trace); it != end(trace); ++it) {
    if (*it == query) {
      // query tile found -> emit fill subtree and stop
      return fn(
          read_nth<quad_entry_t>(base, offset + kQuadNodeDataOffset),
          read_nth<quad_entry_t>(base, offset + kQuadNodeNFeaturesSubtree));
    } else if (read_nth<quad_entry_t>(base, offset + kQuadNodeNFeatures) != 0) {
      // emit *only* this (and continue to descent)
      fn(read_nth<quad_entry_t>(base, offset + kQuadNodeDataOffset),
         read_nth<quad_entry_t>(base, offset + kQuadNodeNFeatures));
    }

    if (read_nth<quad_entry_t>(base, offset + kQuadNodeChildOffset) == 0) {
      return;  // no more children
    }

    // try to descend to next level
    utl::verify(std::next(it) != end(trace), "should not happen");
    auto const& next_tile = *std::next(it);

    auto const curr =
        read_nth<quad_entry_t>(base, offset + kQuadNodeChildOffset);

    if (!bit_set(curr, kQuadChildOffset + next_tile.quad_pos())) {
      return;  // next child tile does not exist, just stop
    }

    offset = curr & kQuadOffsetMask;
    for (auto i = 0U; i < next_tile.quad_pos(); ++i) {
      if (bit_set(curr, kQuadChildOffset + i)) {
        offset += 4;  // four entries/children per node
      }
    }
  }
}

}  // namespace tiles
