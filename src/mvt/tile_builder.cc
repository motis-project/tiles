#include "tiles/mvt/tile_builder.h"

#include <iostream>
#include <limits>
#include <unordered_set>

#include "boost/algorithm/string/predicate.hpp"

#include "utl/get_or_create.h"
#include "utl/get_or_create_index.h"

#include "tiles/bin_utils.h"
#include "tiles/feature/aggregate_line_features.h"
#include "tiles/feature/aggregate_polygon_features.h"
#include "tiles/fixed/algo/area.h"
#include "tiles/fixed/algo/clip.h"
#include "tiles/fixed/algo/shift.h"
#include "tiles/fixed/io/deserialize.h"
#include "tiles/fixed/io/dump.h"
#include "tiles/get_tile.h"
#include "tiles/mvt/encode_geometry.h"
#include "tiles/mvt/tags.h"
#include "tiles/util.h"

using namespace protozero;
namespace ttm = tiles::tags::mvt;

namespace tiles {

constexpr auto const kVectorTileExtend = 4096;
constexpr auto const kRasterTileExtend = 256;
constexpr auto const kScreenPixelArea =
    (kVectorTileExtend / kRasterTileExtend) *
    (kVectorTileExtend / kRasterTileExtend);

struct layer_builder {
  layer_builder(render_ctx const& ctx, std::string layer_name,
                tile_spec const& spec)
      : ctx_{ctx},
        layer_name_{std::move(layer_name)},
        spec_{spec},
        has_geometry_{false},
        pb_{buf_} {
    pb_.add_uint32(ttm::Layer::required_uint32_version, 2);
    pb_.add_string(ttm::Layer::required_string_name, layer_name_);
    pb_.add_uint32(ttm::Layer::optional_uint32_extent, kVectorTileExtend);
  }

  void add_feature(feature f) {
    if ((mpark::holds_alternative<fixed_point>(f.geometry_) &&
         !node_ids_.insert(f.id_).second) ||
        (mpark::holds_alternative<fixed_polyline>(f.geometry_) &&
         !line_ids_.insert(f.id_).second) ||
        (mpark::holds_alternative<fixed_polygon>(f.geometry_) &&
         !poly_ids_.insert(f.id_).second)) {
      return;
    }

    ++features_added_;
    if (mpark::holds_alternative<fixed_null>(f.geometry_)) {
      return;
    }

    if (ctx_.tb_aggregate_lines_ &&
        mpark::holds_alternative<fixed_polyline>(f.geometry_)) {
      line_buffer_.emplace_back(std::move(f));
    } else if (ctx_.tb_aggregate_polygons_ &&
               mpark::holds_alternative<fixed_polygon>(f.geometry_)) {
      polygon_buffer_.emplace_back(std::move(f));
    } else {
      f.geometry_ = clip(f.geometry_, spec_.draw_bounds_);
      f.geometry_ = shift(f.geometry_, spec_.tile_.z_);
      write_feature(f);
    }
  }

  void write_feature(feature const& f) {
    if (mpark::holds_alternative<fixed_null>(f.geometry_)) {
      return;
    }

    has_geometry_ = true;
    ++features_written_;

    std::string feature_buf;
    pbf_builder<ttm::Feature> feature_pb(feature_buf);

    encode_geometry(feature_pb, f.geometry_, spec_);

    feature_pb.add_uint64(ttm::Feature::optional_uint64_id, f.id_);
    write_metadata(feature_pb, f.meta_);
    pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
  }

  void write_metadata(pbf_builder<ttm::Feature>& pb,
                      std::vector<metadata> const& meta) {
    std::vector<uint32_t> t;

    for (auto const& m : meta) {
      if (m.key_ == "layer" || boost::starts_with(m.key_, "__")) {
        continue;
      }

      t.emplace_back(utl::get_or_create_index(meta_key_cache_, m.key_));
      t.emplace_back(utl::get_or_create_index(meta_value_cache_, m.value_));
    }

    pb.add_packed_uint32(ttm::Feature::packed_uint32_tags, begin(t), end(t));
  }

  void aggregate_geometry() {
    if (ctx_.tb_aggregate_polygons_ && !polygon_buffer_.empty()) {
      //   for (auto&& f :
      //   aggregate_polygon_features(std::move(polygon_buffer_),
      //                                              spec_.tile_.z_)) {

      for (auto& f : polygon_buffer_) {
        f.geometry_ = clip(f.geometry_, spec_.draw_bounds_);
        f.geometry_ = shift(f.geometry_, spec_.tile_.z_);

        if (f.layer_ != kLayerCoastlineIdx && ctx_.tb_drop_subpixel_polygons_ &&
            area(f.geometry_) < kScreenPixelArea) {
          continue;
        }

        write_feature(f);
      }
    }

    if (ctx_.tb_aggregate_lines_ && !line_buffer_.empty()) {
      for (auto& f :
           aggregate_line_features(std::move(line_buffer_), spec_.tile_.z_)) {
        f.geometry_ = clip(f.geometry_, spec_.draw_bounds_);
        f.geometry_ = shift(f.geometry_, spec_.tile_.z_);
        write_feature(f);
      }
    }
  }

  std::string finish() {
    std::vector<std::string const*> keys(meta_key_cache_.size());
    for (auto const& pair : meta_key_cache_) {
      keys[pair.second] = &pair.first;
    }
    for (auto const& key : keys) {
      pb_.add_string(ttm::Layer::repeated_string_keys, *key);
    }

    std::vector<std::string const*> values(meta_value_cache_.size());
    for (auto const& pair : meta_value_cache_) {
      values[pair.second] = &pair.first;
    }
    for (auto const& value : values) {
      pbf_builder<ttm::Value> val_pb(pb_, ttm::Layer::repeated_Value_values);

      static_assert(sizeof(metadata_value_t) == 1);
      utl::verify(!value->empty(), "tile_builder: have empty value");
      switch (read<metadata_value_t>(value->data())) {
        case metadata_value_t::bool_false:
          val_pb.add_bool(ttm::Value::optional_bool_bool_value, false);
          break;
        case metadata_value_t::bool_true:
          val_pb.add_bool(ttm::Value::optional_bool_bool_value, true);
          break;
        case metadata_value_t::string:
          val_pb.add_string(ttm::Value::optional_string_string_value,
                            value->data() + 1, value->size() - 1);
          break;
        case metadata_value_t::numeric:
          utl::verify(value->size() == 1 + sizeof(double),
                      "tile_builder: invalid numeric feature");
          val_pb.add_double(ttm::Value::optional_double_double_value,
                            read<double>(value->data(), 1));
          break;
        case metadata_value_t::integer:
          utl::verify(value->size() == 1 + sizeof(int64_t),
                      "tile_builder: invalid integer feature");
          val_pb.add_sint64(ttm::Value::optional_sint64_sint_value,
                            read<int64_t>(value->data(), 1));
          break;
        default: throw utl::fail("tile_builder: unknown metadata_value_t");
      }
    }

    if (ctx_.tb_print_stats_) {
      fmt::print("tile layer: {:<10} added:{} written:{} ({})\n", layer_name_,
                 printable_num{features_added_},
                 printable_num{features_written_},
                 printable_bytes{buf_.size()});
    }

    return buf_;
  }

  render_ctx const& ctx_;
  std::string layer_name_;
  tile_spec const& spec_;

  bool has_geometry_;

  std::vector<feature> line_buffer_, polygon_buffer_;

  std::string buf_;
  pbf_builder<ttm::Layer> pb_;

  std::map<std::string, size_t> meta_key_cache_;
  std::map<std::string, size_t> meta_value_cache_;

  std::unordered_set<uint64_t> node_ids_, line_ids_, poly_ids_;

  size_t features_added_{0};
  size_t features_written_{0};
};

struct tile_builder::impl {
  impl(render_ctx const& ctx, geo::tile const& tile) : ctx_{ctx}, spec_{tile} {}

  void add_feature(feature f) {
    utl::verify(f.layer_ < ctx_.layer_names_.size(), "invalid layer in db");
    auto& builder = utl::get_or_create(builders_, f.layer_, [&] {
      return std::make_unique<layer_builder>(
          ctx_, ctx_.layer_names_.at(f.layer_), spec_);
    });
    builder->add_feature(std::move(f));
  }

  std::string finish() {
    std::string buf;
    pbf_builder<ttm::Tile> pb(buf);

    for (auto const& pair : builders_) {
      pair.second->aggregate_geometry();

      if (pair.second->has_geometry_) {
        pb.add_message(ttm::Tile::repeated_Layer_layers, pair.second->finish());
      }
    }

    if (ctx_.tb_render_debug_info_) {
      layer_builder lb{ctx_, "tiles_debug_info", spec_};
      auto const& min = spec_.px_bounds_.min_corner();
      auto const& max = spec_.px_bounds_.max_corner();

      {
        std::string feature_buf;
        pbf_builder<ttm::Feature> feature_pb(feature_buf);

        std::string buf;
        append(buf, metadata_value_t::string);
        buf.append(fmt::format("[x={}, y={}, z={}]", spec_.tile_.x_,
                               spec_.tile_.y_, spec_.tile_.z_));

        std::vector<uint32_t> t{static_cast<uint32_t>(utl::get_or_create_index(
                                    lb.meta_key_cache_, "tile_id")),
                                static_cast<uint32_t>(utl::get_or_create_index(
                                    lb.meta_value_cache_, buf))};
        feature_pb.add_packed_uint32(ttm::Feature::packed_uint32_tags, begin(t),
                                     end(t));

        fixed_geometry pt = fixed_point{{min.x() + (max.x() - min.x()) / 2,
                                         min.y() + (max.y() - min.y()) / 2}};
        encode_geometry(feature_pb, pt, spec_);
        lb.pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
      }
      {
        std::string feature_buf;
        pbf_builder<ttm::Feature> feature_pb(feature_buf);

        fixed_geometry line = fixed_polyline{{{min.x(), min.y()},
                                              {min.x(), max.y()},
                                              {max.x(), max.y()},
                                              {max.x(), min.y()},
                                              {min.x(), min.y()}}};
        encode_geometry(feature_pb, line, spec_);
        lb.pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
      }

      pb.add_message(ttm::Tile::repeated_Layer_layers, lb.finish());
    }

    return buf;
  }

  render_ctx const& ctx_;
  tile_spec spec_;
  std::map<size_t, std::unique_ptr<layer_builder>> builders_;
};

tile_builder::tile_builder(render_ctx const& ctx, geo::tile const& tile)
    : impl_(std::make_unique<impl>(ctx, tile)) {}

tile_builder::~tile_builder() = default;

void tile_builder::add_feature(feature f) const {
  impl_->add_feature(std::move(f));
}

std::string tile_builder::finish() const { return impl_->finish(); }

}  // namespace tiles
