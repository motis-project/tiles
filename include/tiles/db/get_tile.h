#pragma once

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

#include "tiles/db/bq_tree.h"
#include "tiles/db/feature_pack.h"
#include "tiles/db/query_features.h"
#include "tiles/db/shared_strings.h"
#include "tiles/db/tile_database.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/deserialize.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/mvt/tile_builder.h"
#include "tiles/mvt/tile_spec.h"
#include "tiles/perf_counter.h"

#include "boost/geometry.hpp"

namespace tiles {

struct render_ctx {
  int max_prepared_zoom_level_ = -1;
  bq_tree seaside_tiles_;

  std::vector<std::string> layer_names_;
  meta_coding_vec_t meta_coding_;

  bool ignore_prepared_ = false;
};

render_ctx make_render_ctx(tile_db_handle& handle) {
  auto txn = handle.make_txn();

  auto meta_dbi = handle.meta_dbi(txn);

  auto opt_max_prep = txn.get(meta_dbi, kMetaKeyMaxPreparedZoomLevel);
  auto opt_seaside = txn.get(meta_dbi, kMetaKeyFullySeasideTree);

  return {opt_max_prep ? std::stoi(std::string{*opt_max_prep}) : -1,
          opt_seaside ? bq_tree{*opt_seaside} : bq_tree{},
          get_layer_names(handle, txn), load_meta_coding_vec(handle, txn)};
}

template <typename PerfCounter>
std::string render_tile(lmdb::cursor& c, render_ctx const& ctx,
                        geo::tile const& tile, PerfCounter& pc) {
  tile_builder builder{tile, ctx.layer_names_};

  start<perf_task::RENDER_TILE_FIND_SEASIDE>(pc);
  auto const& seaside_tiles = ctx.seaside_tiles_.all_leafs(tile);
  stop<perf_task::RENDER_TILE_FIND_SEASIDE>(pc);

  for (auto const& seaside_tile : seaside_tiles) {
    auto const bounds = tile_spec{seaside_tile}.draw_bounds_;

    fixed_simple_polygon polygon{
        {{bounds.min_corner().x(), bounds.min_corner().y()},
         {bounds.min_corner().x(), bounds.max_corner().y()},
         {bounds.max_corner().x(), bounds.max_corner().y()},
         {bounds.max_corner().x(), bounds.min_corner().y()},
         {bounds.min_corner().x(), bounds.min_corner().y()}}};
    boost::geometry::correct(polygon);

    start<perf_task::RENDER_TILE_ADD_SEASIDE>(pc);
    builder.add_feature({0ul,
                         kLayerCoastlineIdx,
                         std::pair<uint32_t, uint32_t>{0, kMaxZoomLevel + 1},
                         {{"layer", "coastline"}},
                         fixed_polygon{std::move(polygon)}});
    stop<perf_task::RENDER_TILE_ADD_SEASIDE>(pc);
  }

  auto const box = tile_spec{tile}.draw_bounds_;  // XXX really with overdraw?

  start<perf_task::RENDER_TILE_QUERY_FEATURE>(pc);
  query_features(c, tile, [&](auto const& db_tile, auto const& pack_str) {
    stop<perf_task::RENDER_TILE_QUERY_FEATURE>(pc);
    stop<perf_task::RENDER_TILE_ITER_FEATURE>(pc);

    unpack_features(db_tile, pack_str, tile, [&](auto const& feature_str) {
      start<perf_task::RENDER_TILE_DESER_FEATURE_OKAY>(pc);
      start<perf_task::RENDER_TILE_DESER_FEATURE_SKIP>(pc);
      auto const feature =
          deserialize_feature(feature_str, ctx.meta_coding_, box, tile.z_);
      if (!feature) {
        stop<perf_task::RENDER_TILE_DESER_FEATURE_SKIP>(pc);
        start<perf_task::RENDER_TILE_ITER_FEATURE>(pc);
        return;
      }
      stop<perf_task::RENDER_TILE_DESER_FEATURE_OKAY>(pc);

      start<perf_task::RENDER_TILE_ADD_FEATURE>(pc);
      builder.add_feature(*feature);
      stop<perf_task::RENDER_TILE_ADD_FEATURE>(pc);
    });

    start<perf_task::RENDER_TILE_ITER_FEATURE>(pc);
  });

  auto finish = scoped_perf_counter<perf_task::RENDER_TILE_FINISH>(pc);
  return builder.finish();
}

template <typename PerfCounter>
std::optional<std::string> get_tile(tile_db_handle& handle, lmdb::txn& txn,
                                    lmdb::cursor& features_cursor,
                                    render_ctx const& ctx,
                                    geo::tile const& tile, PerfCounter& pc) {
  utl::verify(tile.z_ <= kMaxZoomLevel, "invalid zoom level");

  auto total = scoped_perf_counter<perf_task::GET_TILE_TOTAL>(pc);

  if (!ctx.ignore_prepared_ &&
      static_cast<int>(tile.z_) <= ctx.max_prepared_zoom_level_) {
    auto tiles_dbi = handle.tiles_dbi(txn);

    start<perf_task::GET_TILE_FETCH>(pc);
    auto db_tile = txn.get(tiles_dbi, make_tile_key(tile));
    stop<perf_task::GET_TILE_FETCH>(pc);

    if (!db_tile) {
      return std::nullopt;
    }
    return std::string{*db_tile};
  }

  start<perf_task::GET_TILE_RENDER>(pc);
  auto const rendered_tile = render_tile(features_cursor, ctx, tile, pc);
  stop<perf_task::GET_TILE_RENDER>(pc);

  if (rendered_tile.empty()) {
    return std::nullopt;
  }
  start<perf_task::GET_TILE_COMPRESS>(pc);
  auto compressed = compress_deflate(rendered_tile);
  stop<perf_task::GET_TILE_COMPRESS>(pc);

  return {std::move(compressed)};
}

template <typename PerfCounter>
std::optional<std::string> get_tile(tile_db_handle& handle,
                                    render_ctx const& ctx,
                                    geo::tile const& tile, PerfCounter& pc) {
  auto txn = handle.make_txn();
  auto features_dbi = handle.features_dbi(txn);
  auto features_cursor = lmdb::cursor{txn, features_dbi};

  return get_tile(handle, txn, features_cursor, ctx, tile, pc);
}

}  // namespace tiles
