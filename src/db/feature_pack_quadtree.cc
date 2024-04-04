#include "tiles/db/feature_pack_quadtree.h"

#include "utl/equal_ranges.h"
#include "utl/to_vec.h"

#include "tiles/feature/deserialize.h"
#include "tiles/feature/serialize.h"
#include "tiles/mvt/tile_spec.h"

namespace tiles {

void quadtree_feature_packer::pack_features(
    std::vector<std::string> const& packs) {
  std::vector<std::vector<quadtree_feature>> features_by_min_z(kMaxZoomLevel +
                                                               1 - root_.z_);

  uint32_t feature_count = 0;
  for (auto const& pack : packs) {
    unpack_features(pack, [&](auto const& str) {
      auto const feature = deserialize_feature(str, metadata_coder_);
      utl::verify(feature.has_value(), "feature must be valid (!?)");

      auto const best_tile = find_best_tile(*feature);
      auto const z = std::max(root_.z_, feature->zoom_levels_.first) - root_.z_;
      features_by_min_z.at(z).emplace_back(make_quad_key(best_tile), best_tile,
                                           std::move(*feature));
      ++feature_count;
    });
  }

  packer_.finish_header(feature_count);

  std::vector<std::string> quad_trees;
  for (auto& features : features_by_min_z) {
    if (features.empty()) {
      quad_trees.emplace_back();
      continue;
    }

    std::vector<quad_tree_input> quad_tree_input;
    utl::equal_ranges(
        features,
        [](auto const& a, auto const& b) { return a.quad_key_ < b.quad_key_; },
        [&](auto const& lb, auto const& ub) {
          quad_tree_input.push_back(
              {lb->best_tile_, serialize_and_append_span(lb, ub), 1ULL});
        });

    quad_trees.emplace_back(make_quad_tree(root_, quad_tree_input));
  }

  // var : one quad tree per level
  // var : index array of offsets of the quad trees
  // the segment offset points to the begin of the index array
  packer_.update_segment_offset(
      kQuadTreeFeatureIndexId,
      packer_.append_packed(utl::to_vec(quad_trees, [&](auto const& quad_tree) {
        return quad_tree.empty() ? 0U : packer_.append(quad_tree);
      })));
}

geo::tile quadtree_feature_packer::find_best_tile(
    feature const& feature) const {
  auto const& feature_box = bounding_box(feature.geometry_);

  geo::tile best = root_;
  while (best.z_ < kMaxZoomLevel) {
    std::optional<geo::tile> next_best;
    for (auto const& child : best.direct_children()) {
      auto const tile_box = tile_spec{child}.insert_bounds_;
      if ((feature_box.max_corner().x() < tile_box.min_corner().x() ||
           feature_box.min_corner().x() > tile_box.max_corner().x()) ||
          (feature_box.max_corner().y() < tile_box.min_corner().y() ||
           feature_box.min_corner().y() > tile_box.max_corner().y())) {
        continue;
      }
      if (next_best) {
        return best;  // two matches -> take prev best
      }
      next_best = child;
    }

    utl::verify(next_best.has_value(), "at least one child must match");
    best = *next_best;
  }

  return best;
}

std::vector<uint8_t> quadtree_feature_packer::make_quad_key(
    geo::tile const& tile) const {
  if (tile == root_) {
    return {};
  }

  std::vector<geo::tile> trace{tile};
  while (!(trace.back().parent() == root_)) {
    utl::verify(trace.back().z_ > root_.z_, "tile outside root");
    trace.push_back(trace.back().parent());
  }
  trace.push_back(root_);
  std::reverse(begin(trace), end(trace));

  return utl::to_vec(trace,
                     [](auto const& t) -> uint8_t { return t.quad_pos(); });
}

uint32_t quadtree_feature_packer::serialize_and_append_span(
    quadtree_feature_it begin, quadtree_feature_it end) {
  auto const offset = packer_.buf_.size();
  for (auto it = begin; it != end; ++it) {
    packer_.append_feature(
        serialize_feature(it->feature_, metadata_coder_, false));
  }
  packer_.append_span_end();
  return static_cast<std::uint32_t>(offset);
}

void quadtree_feature_packer::finish() { packer_.finish(); }

}  // namespace tiles
