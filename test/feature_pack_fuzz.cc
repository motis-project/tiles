#include <exception>
#include <iostream>
#include <limits>

#include "geo/tile.h"

#include "protozero/pbf_message.hpp"

#include "utl/repeat_n.h"
#include "utl/verify.h"

#include "tiles/db/feature_pack.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"
#include "tiles/feature/serialize.h"
#include "tiles/get_tile.h"
#include "tiles/mvt/tags.h"

struct invalid_input_exception : public std::exception {};

template <typename T>
T read_value(uint8_t const* data, size_t& size) {
  if (size < sizeof(T)) {
    throw invalid_input_exception{};
  }
  size -= sizeof(T);
  return tiles::read<T>(reinterpret_cast<char const*>(data));
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size) {
  auto const read = [&](uint64_t max = std::numeric_limits<uint64_t>::max(),
                        bool only_positive = true) {
    uint64_t v = 0;
    if (max > std::numeric_limits<uint32_t>::max()) {
      v = read_value<uint64_t>(data, size);
    } else if (max > std::numeric_limits<uint16_t>::max()) {
      v = read_value<uint32_t>(data, size);
    } else if (max > std::numeric_limits<uint8_t>::max()) {
      v = read_value<uint16_t>(data, size);
    } else {
      v = read_value<uint8_t>(data, size);
    }

    auto mult =
        (!only_positive && read_value<uint8_t>(data, size) % 2 == 1) ? -1. : 1.;

    return static_cast<int64_t>(mult *
                                std::abs(static_cast<int64_t>(v % max + 1)));
  };

  geo::tile root{};
  geo::tile query{};

  std::vector<std::string> features;
  try {
    constexpr auto kTileCount = 1ULL << tiles::kTileDefaultIndexZoomLvl;

    root.x_ = read(kTileCount);
    root.y_ = read(kTileCount);
    root.z_ = tiles::kTileDefaultIndexZoomLvl;

    query = root;
    auto const query_z = read(20);
    if (query_z <= root.z_) {
      while (query.z_ != query_z) {
        query = query.parent();
      }
    } else {
      while (query.z_ != query_z) {
        auto nth_child = read(3);
        auto it = query.direct_children().begin();
        for (auto i = 0; i < nth_child; ++i) {
          ++it;
        }
        query = *it;
      }
    }

    features.resize(read(2048));
    for (auto fi = 0U; fi < features.size(); ++fi) {
      tiles::feature f;
      f.id_ = features.size();
      f.layer_ = read(128);
      f.zoom_levels_ =
          std::minmax({read(tiles::kMaxZoomLevel), read(tiles::kMaxZoomLevel)});

      auto px =
          tiles::proj::map_size(tiles::kMaxZoomLevel) * root.x_ / kTileCount +
          tiles::kTileSize / 2;
      auto py =
          tiles::proj::map_size(tiles::kMaxZoomLevel) * root.y_ / kTileCount +
          tiles::kTileSize / 2;

      tiles::fixed_line l;
      l.resize(2 + read(2048));
      for (auto li = 0U; li < l.size(); ++li) {
        px += read(li == 0U ? tiles::kTileSize / 2
                            : std::numeric_limits<uint16_t>::max(),
                   false);
        py += read(li == 0U ? tiles::kTileSize / 2
                            : std::numeric_limits<uint16_t>::max(),
                   false);
        l[li] = {px, py};
      }

      f.geometry_ = tiles::fixed_polyline{std::move(l)};

      features[fi] = tiles::serialize_feature(f);
    }
  } catch (invalid_input_exception const&) {
    return 0;  // input data is garbage
  }

  auto quick_pack = tiles::pack_features(features);

  tiles::shared_metadata_coder coder{};
  auto optimal_pack = tiles::pack_features(root, coder, {quick_pack});

  tiles::render_ctx ctx;
  ctx.layer_names_ = utl::repeat_n(std::string{}, 129);
  ctx.compress_result_ = false;

  tiles::null_perf_counter npc;
  auto const opt_result =
      get_tile(ctx, query, [&](auto cb) { cb(root, optimal_pack); }, npc);

  auto const check_feature = [&](auto const& reader) {
    namespace pz = protozero;
    namespace mvt = tiles::tags::mvt;
    pz::pbf_message<mvt::Feature> msg = reader;
    while (msg.next()) {
      switch (msg.tag()) {
        case mvt::Feature::optional_uint64_id: break;
        case mvt::Feature::packed_uint32_tags: break;
        case mvt::Feature::optional_GeomType_type: break;
        case mvt::Feature::packed_uint32_geometry: break;
        default: throw utl::fail("unknown feature tag {}", msg.tag());
      }
      msg.skip();
    }
  };

  auto const check_layer = [&](auto const& reader) {
    namespace pz = protozero;
    namespace mvt = tiles::tags::mvt;
    pz::pbf_message<mvt::Layer> msg = reader;
    bool have_version = false;
    bool have_name = false;
    while (msg.next()) {
      switch (msg.tag()) {
        case mvt::Layer::required_uint32_version:
          have_version = true;
          msg.skip();
          break;
        case mvt::Layer::required_string_name:
          have_name = true;
          msg.skip();
          break;
        case mvt::Layer::repeated_Feature_features:
          check_feature(msg.get_message());
          break;
        case mvt::Layer::repeated_string_keys: msg.skip(); break;
        case mvt::Layer::repeated_Value_values: msg.skip(); break;
        case mvt::Layer::optional_uint32_extent: msg.skip(); break;
        default: throw utl::fail("unknown layer tag {}", msg.tag());
      }
    }
    utl::verify(have_version && have_name, "layer version/name missing");
  };

  auto const check_tile = [&](auto const& view) {
    namespace pz = protozero;
    namespace mvt = tiles::tags::mvt;
    pz::pbf_message<mvt::Tile> msg{view.data(), view.size()};
    while (msg.next()) {
      switch (msg.tag()) {
        case mvt::Tile::repeated_Layer_layers:
          check_layer(msg.get_message());
          break;
        default: throw utl::fail("unknown tile tag {}", msg.tag());
      }
    }
  };

  if (opt_result) {
    check_tile(*opt_result);
  }

  return 0;
}
