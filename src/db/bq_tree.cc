#include "tiles/db/bq_tree.h"

#include <array>
#include <map>
#include <stack>

#include "geo/tile.h"

#include "utl/verify.h"

#include "tiles/util.h"

/**
bq_tree is a binary quad tree following the tile pyramid
 - the root of the tree is tile (0, 0, 0)
 - each leaf is associated with one one bit of data
 - each node stores the values of its children (enumerated 0-3)
 - enumeration of nodes is consistent with geo::tile_iterator
 - each node is represented as 32 bit integer containing three parts
   - first 4 bits: 1 if child is a TRUE leaf
   - next 4 bits: 1 if child is a FALSE leaf
   - other bits: base offset (from start of vector) to the first child node
 - only those child nodes exist which are not maked as TRUE or FALSE in parent
*/

namespace tiles {

constexpr auto const kBQBits = sizeof(bq_node_t) * 8;

constexpr auto const kTrueOffset = kBQBits - 4;
constexpr auto const kFalseOffset = kBQBits - 8;

constexpr auto const kOffsetMask =
    (static_cast<bq_node_t>(1) << (kBQBits - 8)) - 1;

constexpr auto const kEmptyRoot = std::numeric_limits<bq_node_t>::min();
constexpr auto const kFullRoot = std::numeric_limits<bq_node_t>::max();
constexpr auto const kInvalidNode = std::numeric_limits<bq_node_t>::max() - 1;

inline bool bit_set(uint32_t val, uint32_t idx) {
  return (val & (1 << idx)) != 0;
}

bq_tree::bq_tree() : nodes_{kEmptyRoot} {}
bq_tree::bq_tree(std::string_view str) {
  utl::verify(str.size() % sizeof(bq_node_t) == 0,
              "bq_tree invalid string_view");

  nodes_.resize(str.size() / sizeof(bq_node_t));
  std::memcpy(nodes_.data(), str.data(), str.size());
}

std::pair<std::optional<bool>, bq_node_t> bq_tree::find_parent_leaf(
    geo::tile const& q) const {
  if (nodes_.at(0) == kFullRoot) {
    return {{true}, kInvalidNode};
  } else if (nodes_.at(0) == kEmptyRoot) {
    return {{false}, kInvalidNode};
  } else if (q == geo::tile{0, 0, 0}) {
    return {std::nullopt, nodes_.at(0)};
  }

  std::vector<geo::tile> trace{q};
  while (!(trace.back().parent() == geo::tile{0, 0, 0})) {
    utl::verify(trace.size() <= 22, "invalid tile z={}, y={}, y={}", q.z_, q.y_,
                q.x_);
    trace.push_back(trace.back().parent());
  }
  std::reverse(begin(trace), end(trace));

  // curr is at lvl z, tile is at lvl z+1
  auto curr = nodes_.at(0);
  for (auto const& tile : trace) {
    // std::cout << "tile" << tile << std::endl;
    // printf("%i %08x\n\n", tile.quad_pos(), curr);

    if (bit_set(curr, tile.quad_pos() + kFalseOffset)) {
      return {{false}, kInvalidNode};
    }
    if (bit_set(curr, tile.quad_pos() + kTrueOffset)) {
      return {{true}, kInvalidNode};
    }

    auto offset = curr & kOffsetMask;
    for (auto i = 0ULL; i < tile.quad_pos(); ++i) {
      if (!bit_set(curr, i + kFalseOffset) && !bit_set(curr, i + kTrueOffset)) {
        ++offset;
      }
    }

    curr = nodes_.at(offset);
  }

  return {std::nullopt, curr};
}

bool bq_tree::contains(geo::tile const& q) const {
  auto const decision = find_parent_leaf(q).first;
  return decision.has_value() ? *decision : false;
}

std::vector<geo::tile> bq_tree::all_leafs(geo::tile const& q) const {
  auto const parent = find_parent_leaf(q);
  auto const& decision = parent.first;
  if (decision.has_value()) {
    return *decision ? std::vector<geo::tile>{q} : std::vector<geo::tile>{};
  }

  std::stack<std::pair<geo::tile, bq_node_t>> stack;
  stack.emplace(q, parent.second);

  std::vector<geo::tile> result;
  while (!stack.empty()) {
    auto const [tile, node] = stack.top();  // copy required!
    stack.pop();

    auto child_tile_it = tile.as_tile_range().begin();
    auto child_count = 0;
    for (auto i = 0ULL; i < 4ULL; ++i) {
      auto const& child_tile = *(++child_tile_it);

      if (bit_set(node, child_tile.quad_pos() + kTrueOffset)) {
        result.push_back(child_tile);
        continue;
      }
      if (bit_set(node, child_tile.quad_pos() + kFalseOffset)) {
        continue;
      }

      stack.emplace(child_tile, nodes_.at((node & kOffsetMask) + child_count));
      ++child_count;
    }
  }
  return result;
}

std::string_view bq_tree::string_view() const {
  return std::string_view{reinterpret_cast<char const*>(nodes_.data()),
                          nodes_.size() * sizeof(bq_node_t)};
}

void bq_tree::dump() const {
  std::cout << "bq_tree with " << nodes_.size() << " nodes:\n";
  for (auto i = 0ULL; i < nodes_.size(); ++i) {
    printf("%llu | %08x\n", i, nodes_[i]);
  }
  std::cout << "---" << std::endl;
}

struct bq_tmp_node_t {
  bool leaf_;
  std::array<bq_tmp_node_t const*, 4> children_;
};

bq_tree serialize_bq_tree(bq_tmp_node_t const& root) {
  std::stack<std::pair<size_t, bq_tmp_node_t const*>> stack;
  stack.emplace(0, &root);

  std::vector<bq_node_t> vec;
  vec.emplace_back(0);

  while (!stack.empty()) {
    auto const [offset, node] = stack.top();  // copy required!
    stack.pop();

    for (auto i = 0ULL; i < 4ULL; ++i) {
      auto& storage = vec.at(offset);  // emplace_back invalidates maybe!
      auto const* child = node->children_[i];

      if (child == nullptr) {
        storage |= 1 << (i + kFalseOffset);
      } else if (child->leaf_) {
        storage |= 1 << (i + kTrueOffset);
      } else {
        if ((storage & kOffsetMask) == 0) {
          storage |= vec.size();
        }

        stack.emplace(vec.size(), child);
        vec.emplace_back(0);
      }
    }
  }
  return bq_tree{std::move(vec)};
}

bq_tree make_bq_tree(std::vector<geo::tile> const& tiles) {
  if (tiles.empty()) {
    return bq_tree{std::vector<bq_node_t>{kEmptyRoot}};
  }

  std::vector<std::map<geo::tile, bq_tmp_node_t>> nodes{{}};
  for (auto const& tile : tiles) {
    if (tile.z_ + 1 > nodes.size()) {
      nodes.resize(tile.z_ + 1);
    }
    nodes[tile.z_][tile] = {true, {nullptr, nullptr, nullptr, nullptr}};
  }

  for (auto z = nodes.size() - 1; z > 0; --z) {
    for (auto const& pair : nodes[z]) {
      auto const& tile = pair.first;
      auto& parent = nodes[z - 1][tile.parent()];
      if (!parent.leaf_) {
        parent.children_[tile.quad_pos()] = &pair.second;
      }
    }
  }

  // aggregate full nodes to leafs
  utl::verify(nodes.at(0).size() == 1, "root node missing");
  auto const& root = begin(nodes.at(0))->second;
  if (root.leaf_) {
    return bq_tree{std::vector<bq_node_t>{kFullRoot}};
  }
  return serialize_bq_tree(root);
}

}  // namespace tiles
