#include "tiles/db/quad_tree.h"

#include <array>
#include <map>
#include <stack>

namespace tiles {

struct tmp_quad_node_t {
  std::array<tmp_quad_node_t const*, 4> children_;
  uint32_t offset_, size_self_, size_subtree_;  // payload range
};

using tmp_quad_tree_t = std::vector<std::map<geo::tile, tmp_quad_node_t>>;

tmp_quad_tree_t make_tmp_quad_tree(geo::tile const& root,
                                   std::vector<quad_tree_input> const& input) {
  auto root_z = root.z_;
  std::vector<std::map<geo::tile, tmp_quad_node_t>> nodes{{}};
  for (auto const& entry : input) {
    auto const& tile = entry.tile_;
    utl::verify_silent(static_cast<int>(tile.z_) >= static_cast<int>(root_z),
                       "entry z less than root z");
    utl::verify_silent(root_z != tile.z_ || root == tile,
                       "another tile at root z");

    auto const rel_z = tile.z_ - root_z;
    if (rel_z + 1 > nodes.size()) {
      nodes.resize(rel_z + 1);
    }
    nodes[rel_z][tile] = {{nullptr, nullptr, nullptr, nullptr},
                          entry.offset_,
                          entry.size_,
                          entry.size_};
  }

  for (auto z = nodes.size() - 1; z > 0; --z) {
    for (auto const& pair : nodes[z]) {
      auto const& tile = pair.first;

      auto& parent = nodes[z - 1]
                         .insert({tile.parent(),
                                  {{nullptr, nullptr, nullptr, nullptr},
                                   std::numeric_limits<uint32_t>::max(),
                                   0u,
                                   0u}})
                         .first->second;

      parent.children_[tile.quad_pos()] = &pair.second;
      parent.offset_ = std::min(parent.offset_, pair.second.offset_);
      parent.size_subtree_ += pair.second.size_subtree_;
    }
  }

  utl::verify_silent(nodes.at(0).size() == 1, "root node missing / not unique");
  utl::verify_silent(begin(nodes.at(0))->first == root, "root node mismatch");
  return nodes;
}

std::vector<quad_entry_t> serialize_quad_tree(tmp_quad_node_t const root) {
  std::stack<std::pair<size_t, tmp_quad_node_t const*>> stack;
  std::vector<quad_entry_t> vec;

  auto const allocate_and_enqueue = [&](auto const* node) {
    stack.emplace(vec.size(), node);

    vec.emplace_back(0);  // allocate: child ptr
    vec.emplace_back(0);  // allocate: range/offset
    vec.emplace_back(0);  // allocate: range/size_self
    vec.emplace_back(0);  // allocate: range/size_subtree
  };

  allocate_and_enqueue(&root);
  while (!stack.empty()) {
    auto const [offset, node] = stack.top();  // copy required!
    stack.pop();

    vec.at(offset + 1) = node->offset_;  // set: range/offset
    vec.at(offset + 2) = node->size_self_;  // set: range/size_self
    vec.at(offset + 3) = node->size_subtree_;  // set: range/size_subtree

    for (auto i = 0u; i < 4u; ++i) {
      auto& storage = vec.at(offset);  // emplace_back invalidates maybe!
      auto const* child = node->children_[i];

      if (child != nullptr) {
        if (storage == 0) {
          storage = vec.size();  // set: child_ptr
        }

        storage |= 1 << (i + kQuadChildOffset);
        allocate_and_enqueue(child);
      }
    }
  }
  return vec;
}

std::string make_quad_tree(geo::tile const& root,
                           std::vector<quad_tree_input> const& input) {
  std::string buf;
  if (input.empty()) {
    append<quad_entry_t>(buf, 0u);  // child_ptr
    append<quad_entry_t>(buf, 0u);  // range/offset
    append<quad_entry_t>(buf, 0u);  // range/size_self
    append<quad_entry_t>(buf, 0u);  // range/size_subtree
    return buf;
  }

  auto const tmp_tree = make_tmp_quad_tree(root, input);
  auto const tree_vec = serialize_quad_tree(begin(tmp_tree.at(0))->second);

  buf.reserve(tree_vec.size() * sizeof(quad_entry_t));
  for (auto const& entry : tree_vec) {
    append<quad_entry_t>(buf, entry);
  }

  return buf;
}

}  // namespace tiles
