#pragma once

#include <memory>
#include <string>

#include "osmium/handler.hpp"

namespace tiles {

struct feature_shard;
struct layer_names_builder;
struct shared_metadata_builder;
struct script_runner;

void check_profile(std::string const& osm_profile);

struct feature_handler : public osmium::handler::Handler {
  feature_handler(std::string const& osm_profile, feature_shard&,
                  layer_names_builder&, shared_metadata_builder&);

  feature_handler(feature_handler&&) noexcept;
  feature_handler(feature_handler const&) = delete;
  feature_handler& operator=(feature_handler&&) = delete;
  feature_handler& operator=(feature_handler const&) = delete;

  ~feature_handler();

  void node(osmium::Node const&);
  void way(osmium::Way const&);
  void area(osmium::Area const&);

private:
  std::unique_ptr<script_runner> runner_;

  feature_shard& shard_;
  layer_names_builder& layer_names_builder_;
  shared_metadata_builder& shared_metadata_builder_;
};

}  // namespace tiles
