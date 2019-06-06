#include "tiles/mvt/tile_builder.h"

#include <iostream>
#include <limits>

#include "boost/algorithm/string/predicate.hpp"

#include "utl/get_or_create.h"
#include "utl/get_or_create_index.h"

#include "tiles/fixed/algo/clip.h"
#include "tiles/fixed/algo/shift.h"
#include "tiles/fixed/io/deserialize.h"
#include "tiles/fixed/io/dump.h"

#include "tiles/mvt/encode_geometry.h"
#include "tiles/mvt/tags.h"
#include "tiles/util.h"

using namespace protozero;
namespace ttm = tiles::tags::mvt;

namespace tiles {

// TODO reimplement this
// struct variant_less {
//   bool operator()(Variant const& a, Variant const& b) const {
//     if (a.type() != b.type()) {
//       return a.type() < b.type();
//     }

//     switch (a.type()) {
//       case Variant::Type::kBool: return a.get_bool() < b.get_bool();
//       case Variant::Type::kInt: return a.get_int() < b.get_int();
//       case Variant::Type::kDouble: return a.get_double() < b.get_double();
//       case Variant::Type::kString: return a.get_string() < b.get_string();
//       default: return false;
//     }
//   }
// };

// struct layer_builder {
//   layer_builder(std::string const& name, tile_spec const& spec,
//                 tile_builder::config const& cfg)
//       : spec_(spec), config_(cfg), has_geometry_(false), buf_(), pb_(buf_) {
//     pb_.add_uint32(ttm::Layer::required_uint32_version, 2);
//     pb_.add_string(ttm::Layer::required_string_name, name);
//     pb_.add_uint32(ttm::Layer::optional_uint32_extent, 4096);
//   }

//   void add_feature(FeatureSet const& meta, Slice const& geo) {
//     std::string feature_buf;
//     pbf_builder<ttm::Feature> feature_pb(feature_buf);

//     if (in_z_range(meta) && write_geometry(feature_pb, geo)) {
//       has_geometry_ = true;

//       write_metadata(feature_pb, meta);
//       pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
//     }
//   }

//   bool in_z_range(FeatureSet const& meta) {
//     auto min_it = meta.Find("__min_z");
//     if (min_it != meta.end() && (*min_it).second.get_int() > spec_.z_) {
//       return false;
//     }

//     auto max_it = meta.Find("__max_z");
//     if (max_it != meta.end() && (*max_it).second.get_int() < spec_.z_) {
//       return false;
//     }

//     return true;
//   }

//   bool write_geometry(pbf_builder<ttm::Feature>& pb, Slice const& geo) {
//     auto geometry = deserialize(geo.ToString());
//     geometry = simplify(geometry, spec_.z_);

//     // if (config_.verbose_) {
//     //   dump(geometry);
//     // }

//     geometry = clip(geometry, spec_);

//     // if (config_.verbose_) {
//     //   std::cout << "--" << std::endl;
//     //   dump(geometry);
//     // }

//     if (std::holds_alternative<fixed_null>(geometry)) {
//       return false;
//     }

//     shift(geometry, spec_.z_);
//     encode_geometry(pb, geometry, spec_);
//     return true;

//     // auto const encoded = encode_geometry(geo, spec_);
//     // pb.add_enum(ttm::Feature::optional_GeomType_type, encoded.first);
//     // pb.add_packed_uint32(ttm::Feature::packed_uint32_geometry,
//     //                      begin(encoded.second), end(encoded.second));
//   }

//   void write_metadata(pbf_builder<ttm::Feature>& pb, FeatureSet const& meta)
//   {
//     std::vector<uint32_t> t;

//     for (auto const& pair : meta) {
//       if (pair.first == "layer" || boost::starts_with(pair.first, "__")) {
//         continue;
//       }

//       t.emplace_back(utl::get_or_create_index(meta_key_cache_, pair.first));
//       t.emplace_back(utl::get_or_create_index(meta_value_cache_,
//       pair.second));
//     }

//     pb.add_packed_uint32(ttm::Feature::packed_uint32_tags, begin(t),
//     end(t));
//   }

//   std::string finish() {
//     std::vector<std::string const*> keys(meta_key_cache_.size());
//     for (auto const& pair : meta_key_cache_) {
//       keys[pair.second] = &pair.first;
//     }
//     for (auto const& key : keys) {
//       pb_.add_string(ttm::Layer::repeated_string_keys, *key);
//     }

//     std::vector<Variant const*> values(meta_value_cache_.size());
//     for (auto const& pair : meta_value_cache_) {
//       values[pair.second] = &pair.first;
//     }
//     for (auto const& value : values) {
//       pbf_builder<ttm::Value> val_pb(pb_,
//       ttm::Layer::repeated_Value_values);

//       switch (value->type()) {
//         case Variant::Type::kBool:
//           val_pb.add_bool(ttm::Value::optional_bool_bool_value,
//                           value->get_bool());
//           break;
//         case Variant::Type::kInt:
//           val_pb.add_uint64(ttm::Value::optional_uint64_uint_value,
//                             value->get_int());
//           break;
//         case Variant::Type::kDouble:
//           val_pb.add_double(ttm::Value::optional_double_double_value,
//                             value->get_double());
//           break;
//         case Variant::Type::kString:
//           val_pb.add_string(ttm::Value::optional_string_string_value,
//                             value->get_string());
//           break;
//         // should not happen
//         case Variant::Type::kNull:
//         default:
//           val_pb.add_double(ttm::Value::optional_double_double_value,
//                             std::numeric_limits<double>::quiet_NaN());
//           break;
//       }
//     }

//     return buf_;
//   }

//   tile_spec const& spec_;
//   tile_builder::config const& config_;

//   bool has_geometry_;

//   std::string buf_;
//   pbf_builder<ttm::Layer> pb_;

//   std::map<std::string, size_t> meta_key_cache_;
//   std::map<Variant, size_t, variant_less> meta_value_cache_;
// };

struct layer_builder {
  layer_builder(std::string const& name, tile_spec const& spec,
                tile_builder::config cfg)
      : spec_(spec), config_(cfg), has_geometry_(false), buf_(), pb_(buf_) {
    pb_.add_uint32(ttm::Layer::required_uint32_version, 2);
    pb_.add_string(ttm::Layer::required_string_name, name);
    pb_.add_uint32(ttm::Layer::optional_uint32_extent, 4096);
  }

  void add_feature(feature const& f) {
    std::string feature_buf;
    pbf_builder<ttm::Feature> feature_pb(feature_buf);

    if (write_geometry(feature_pb, f)) {
      has_geometry_ = true;

      feature_pb.add_uint64(ttm::Feature::optional_uint64_id, f.id_);
      write_metadata(feature_pb, f.meta_);
      pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
    }
  }

  bool write_geometry(pbf_builder<ttm::Feature>& pb, feature const& f) {
    auto geometry = clip(f.geometry_, spec_.draw_bounds_);

    if (mpark::holds_alternative<fixed_null>(geometry)) {
      return false;
    }

    shift(geometry, spec_.tile_.z_);
    encode_geometry(pb, geometry, spec_);
    return true;
  }

  void write_metadata(pbf_builder<ttm::Feature>& pb,
                      std::map<std::string, std::string> const& meta) {
    std::vector<uint32_t> t;

    for (auto const& pair : meta) {
      if (pair.first == "layer" || boost::starts_with(pair.first, "__")) {
        continue;
      }

      t.emplace_back(utl::get_or_create_index(meta_key_cache_, pair.first));
      t.emplace_back(utl::get_or_create_index(meta_value_cache_, pair.second));
    }

    pb.add_packed_uint32(ttm::Feature::packed_uint32_tags, begin(t), end(t));
  }

  void render_debug_info() {
    auto const& min = spec_.px_bounds_.min_corner();
    auto const& max = spec_.px_bounds_.max_corner();
    fixed_geometry line = fixed_polyline{{{min.x(), min.y()},
                                          {min.x(), max.y()},
                                          {max.x(), max.y()},
                                          {max.x(), min.y()},
                                          {min.x(), min.y()}}};
    {
      std::string feature_buf;
      pbf_builder<ttm::Feature> feature_pb(feature_buf);

      encode_geometry(feature_pb, line, spec_);
      pb_.add_message(ttm::Layer::repeated_Feature_features, feature_buf);
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
      val_pb.add_string(ttm::Value::optional_string_string_value, *value);
    }

    return buf_;
  }

  tile_spec const& spec_;
  tile_builder::config config_;

  bool has_geometry_;

  std::string buf_;
  pbf_builder<ttm::Layer> pb_;

  std::map<std::string, size_t> meta_key_cache_;
  std::map<std::string, size_t> meta_value_cache_;
};

struct tile_builder::impl {
  impl(geo::tile const& tile, std::vector<std::string> const& layer_names,
       tile_builder::config const& cfg)
      : spec_{tile}, layer_names_{layer_names}, config_{cfg} {}

  void add_feature(feature const& f) {
    utl::verify(f.layer_ < layer_names_.size(), "invalid layer in db");
    utl::get_or_create(builders_, f.layer_, [&] {
      return std::make_unique<layer_builder>(layer_names_.at(f.layer_), spec_,
                                             config_);
    })->add_feature(f);
  }

  std::string finish() {
    std::string buf;
    pbf_builder<ttm::Tile> pb(buf);

    for (auto const& pair : builders_) {
      if (!pair.second->has_geometry_) {
        continue;
      }

      if (config_.render_debug_info_) {
        pair.second->render_debug_info();
      }

      pb.add_message(ttm::Tile::repeated_Layer_layers, pair.second->finish());
    }

    return buf;
  }

  tile_spec spec_;
  std::vector<std::string> const& layer_names_;
  tile_builder::config config_;

  std::map<size_t, std::unique_ptr<layer_builder>> builders_;
};

tile_builder::tile_builder(geo::tile const& tile,
                           std::vector<std::string> const& layer_names,
                           config cfg)
    : impl_(std::make_unique<impl>(tile, layer_names, cfg)) {}

tile_builder::~tile_builder() = default;

void tile_builder::add_feature(feature const& f) { impl_->add_feature(f); }

std::string tile_builder::finish() { return impl_->finish(); }

}  // namespace tiles
