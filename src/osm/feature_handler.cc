#include "tiles/osm/feature_handler.h"

#include "sol.hpp"

#include "tiles/db/insert_feature.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/fixed/fixed_geometry.h"
#include "tiles/fixed/io/serialize.h"
#include "tiles/osm/pending_feature.h"
#include "tiles/osm/read_osm_geometry.h"

namespace tiles {

struct feature_handler::script_runner {
  script_runner() {
    lua_.script_file("../profile/profile.lua");
    lua_.open_libraries(sol::lib::base, sol::lib::package);

    lua_.new_usertype<pending_feature>(  //
        "pending_feature",  //
        "get_id", &pending_feature::get_id,  //
        "has_tag", &pending_feature::has_tag,  //
        "has_any_tag", &pending_feature::has_any_tag,  //
        "has_any_tag2", &pending_feature::has_any_tag2,  //
        "set_approved_min", &pending_feature::set_approved_min,  //
        "set_approved_full", &pending_feature::set_approved_full,  //
        "set_approved", &pending_feature::set_approved,  //
        "set_target_layer", &pending_feature::set_target_layer,  //
        "add_tag_as_metadata", &pending_feature::add_tag_as_metadata);

    process_node_ = lua_["process_node"];
    process_way_ = lua_["process_way"];
    process_area_ = lua_["process_area"];
  }

  sol::state lua_;

  sol::function process_node_;
  sol::function process_way_;
  sol::function process_area_;
};

feature_handler::feature_handler(feature_inserter& inserter)
    : runner_(std::make_unique<feature_handler::script_runner>()),
      inserter_(inserter) {}
feature_handler::~feature_handler() = default;

template <typename OSMObject>
std::map<std::string, std::string> make_meta(pending_feature const& f,
                                             OSMObject const& o) {
  std::map<std::string, std::string> meta;

  meta["layer"] = f.target_layer_;

  for (auto const& tag : f.tag_as_metadata_) {
    meta[tag] = std::string{o.get_value_by_key(tag.c_str(), "")};
  }

  return meta;
}

template <typename OSMObject>
void handle_feature(feature_inserter& inserter, sol::function const& process,
                    OSMObject const& obj) {
  auto pf = pending_feature{obj};
  process(pf);

  if (!pf.is_approved_) {
    return;
  }

  insert_feature(inserter, feature{pf.zoom_levels_, make_meta(pf, obj),
                                   read_osm_geometry(obj)});
}

void feature_handler::node(osmium::Node const& n) {
  handle_feature(inserter_, runner_->process_node_, n);
}
void feature_handler::way(osmium::Way const& w) {
  handle_feature(inserter_, runner_->process_way_, w);
}
void feature_handler::area(osmium::Area const& a) {
  handle_feature(inserter_, runner_->process_area_, a);
}

}  // namespace tiles
