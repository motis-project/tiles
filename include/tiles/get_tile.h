#pragma once

#include "geo/tile.h"
#include "lmdb/lmdb.hpp"

#include "tiles/db/bq_tree.h"
#include "tiles/db/feature_pack.h"
#include "tiles/db/layer_names.h"
#include "tiles/db/pack_file.h"
#include "tiles/db/shared_metadata.h"
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
  shared_metadata_decoder metadata_decoder_;

  bool compress_result_ = true;
  bool ignore_prepared_ = false;
  bool ignore_fully_seaside_ = false;

  bool tb_render_debug_info_ = false;
  bool tb_aggregate_lines_ = false;
  bool tb_aggregate_polygons_ = false;
  bool tb_drop_subpixel_polygons_ = true;
  bool tb_print_stats_ = false;
};

inline render_ctx make_render_ctx(tile_db_handle& db_handle) {
  auto txn = db_handle.make_txn();
  auto meta_dbi = db_handle.meta_dbi(txn);

  auto opt_max_prep = txn.get(meta_dbi, kMetaKeyMaxPreparedZoomLevel);
  auto opt_seaside = txn.get(meta_dbi, kMetaKeyFullySeasideTree);

  return {opt_max_prep ? std::stoi(std::string{*opt_max_prep}) : -1,
          opt_seaside ? bq_tree{*opt_seaside} : bq_tree{},
          get_layer_names(db_handle, txn),
          make_shared_metadata_decoder(db_handle, txn)};
}

template <typename PerfCounter>
void render_seaside(tile_builder& builder, render_ctx const& ctx,
                    geo::tile const& tile, PerfCounter& pc) {
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
    builder.add_feature({tile_to_key(seaside_tile),
                         kLayerCoastlineIdx,
                         std::pair<uint32_t, uint32_t>{0, kMaxZoomLevel + 1},
                         {{"layer", "coastline"}},
                         fixed_polygon{std::move(polygon)}});
    stop<perf_task::RENDER_TILE_ADD_SEASIDE>(pc);
  }
}

template <typename Fn>
void pack_records_foreach(lmdb::cursor& c, geo::tile const& query_tile,
                          Fn&& fn) {
  // XXX not working on zoom level zero "whole database" ?!
  auto const bounds = query_tile.bounds_on_z(kTileDefaultIndexZoomLvl);
  for (auto y = bounds.miny_; y < bounds.maxy_; ++y) {
    auto const key_begin =
        tile_to_key(bounds.minx_, y, kTileDefaultIndexZoomLvl);
    auto const key_end = tile_to_key(bounds.maxx_, y, kTileDefaultIndexZoomLvl);

    for (auto el = c.get(lmdb::cursor_op::SET_RANGE, key_begin);
         el && el->first < key_end;
         el = c.get<decltype(key_begin)>(lmdb::cursor_op::NEXT)) {

      auto const result_tile = key_to_tile(el->first);
      pack_records_foreach(el->second, [&](auto const& pack_record) {
        fn(result_tile, pack_record);
      });
    }
  }
}

template <typename ForeachPack, typename PerfCounter>
size_t render_features(tile_builder& builder, render_ctx const& ctx,
                       geo::tile const& tile, ForeachPack&& foreach_pack,
                       PerfCounter& pc) {
  size_t added_features = 0;
  auto const box = tile_spec{tile}.draw_bounds_;  // XXX really with overdraw?

  start<perf_task::RENDER_TILE_QUERY_FEATURE>(pc);
  foreach_pack([&](auto const& db_tile, auto const& pack_str) {
    stop<perf_task::RENDER_TILE_QUERY_FEATURE>(pc);
    stop<perf_task::RENDER_TILE_ITER_FEATURE>(pc);

    unpack_features(db_tile, pack_str, tile, [&](auto const& feature_str) {
      start<perf_task::RENDER_TILE_DESER_FEATURE_OKAY>(pc);
      start<perf_task::RENDER_TILE_DESER_FEATURE_SKIP>(pc);
      auto const feature =
          deserialize_feature(feature_str, ctx.metadata_decoder_, box, tile.z_);
      if (!feature) {
        stop<perf_task::RENDER_TILE_DESER_FEATURE_SKIP>(pc);
        start<perf_task::RENDER_TILE_ITER_FEATURE>(pc);
        return;
      }
      stop<perf_task::RENDER_TILE_DESER_FEATURE_OKAY>(pc);

      start<perf_task::RENDER_TILE_ADD_FEATURE>(pc);
      builder.add_feature(std::move(*feature));
      ++added_features;
      stop<perf_task::RENDER_TILE_ADD_FEATURE>(pc);
    });

    start<perf_task::RENDER_TILE_ITER_FEATURE>(pc);
  });
  return added_features;
}

template <typename ForeachPack, typename PerfCounter>
std::optional<std::string> get_tile(render_ctx const& ctx,
                                    geo::tile const& tile,
                                    ForeachPack&& foreach_pack,
                                    PerfCounter& pc) {
  start<perf_task::GET_TILE_RENDER>(pc);

  tile_builder builder{ctx, tile};
  render_seaside(builder, ctx, tile, pc);
  auto const rendered_features = render_features(
      builder, ctx, tile, std::forward<ForeachPack>(foreach_pack), pc);

  if (ctx.ignore_fully_seaside_ && ctx.seaside_tiles_.contains(tile) &&
      rendered_features == 0) {
    return std::nullopt;
  }

  start<perf_task::RENDER_TILE_FINISH>(pc);
  auto rendered_tile = builder.finish();
  stop<perf_task::RENDER_TILE_FINISH>(pc);

  stop<perf_task::GET_TILE_RENDER>(pc);

  if (rendered_tile.empty()) {
    return std::nullopt;
  }

  if (ctx.compress_result_) {
    start<perf_task::GET_TILE_COMPRESS>(pc);
    auto compressed = compress_deflate(rendered_tile);
    stop<perf_task::GET_TILE_COMPRESS>(pc);
    pc.template append<perf_task::RESULT_SIZE>(compressed.size());
    return {std::move(compressed)};
  } else {
    pc.template append<perf_task::RESULT_SIZE>(rendered_tile.size());
    return {std::move(rendered_tile)};
  }
}

template <typename PerfCounter>
std::optional<std::string> get_tile(tile_db_handle& handle, lmdb::txn& txn,
                                    lmdb::cursor& features_cursor,
                                    pack_handle const& pack_handle,
                                    render_ctx const& ctx,
                                    geo::tile const& tile, PerfCounter& pc) {
  utl::verify(tile.z_ <= kMaxZoomLevel, "invalid zoom level {}", tile.z_);

  auto total = scoped_perf_counter<perf_task::GET_TILE_TOTAL>(pc);

  if (!ctx.ignore_prepared_ &&
      static_cast<int>(tile.z_) <= ctx.max_prepared_zoom_level_) {
    auto tiles_dbi = handle.tiles_dbi(txn);

    start<perf_task::GET_TILE_FETCH>(pc);
    auto db_tile = txn.get(tiles_dbi, tile_to_key(tile));
    stop<perf_task::GET_TILE_FETCH>(pc);

    if (db_tile) {
      return std::string{*db_tile};
    }

    if (ctx.seaside_tiles_.contains(tile)) {
      return get_tile(ctx, tile, [](auto&&) {}, pc);
    }

    return std::nullopt;
  }

  return get_tile(
      ctx, tile,
      [&](auto&& fn) {
        pack_records_foreach(features_cursor, tile, [&](auto t, auto r) {
          fn(t, pack_handle.get(r));
        });
      },
      pc);
}

template <typename PerfCounter>
std::optional<std::string> get_tile(tile_db_handle& db_handle,
                                    pack_handle const& pack_handle,
                                    render_ctx const& ctx,
                                    geo::tile const& tile, PerfCounter& pc) {
  auto txn = db_handle.make_txn();
  auto features_dbi = db_handle.features_dbi(txn);
  auto features_cursor = lmdb::cursor{txn, features_dbi};

  return get_tile(db_handle, txn, features_cursor, pack_handle, ctx, tile, pc);
}

}  // namespace tiles
