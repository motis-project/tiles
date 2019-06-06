#pragma once

#include "geo/tile.h"
#include "geo/webmercator.h"

#include "tiles/constants.h"
#include "tiles/util.h"

namespace tiles {

constexpr auto kOverdraw = 4096;
// constexpr auto kOverdraw = 128;

struct tile_spec {
  tile_spec(geo::tile tile) : tile_(std::move(tile)) {
    utl::verify(kMaxZoomLevel >= tile.z_, "invalid z");
    auto delta_z = kMaxZoomLevel - tile.z_;

    auto px_bounds = proj::tile_bounds_pixel(tile.x_, tile.y_);  // lvl z

    auto insert_bounds = px_bounds;  // lvl 20
    insert_bounds.minx_ = insert_bounds.minx_ << delta_z;
    insert_bounds.miny_ = insert_bounds.miny_ << delta_z;
    insert_bounds.maxx_ = insert_bounds.maxx_ << delta_z;
    insert_bounds.maxy_ = insert_bounds.maxy_ << delta_z;

    // XXX overdraw should be determined at lvl z
    auto draw_bounds = px_bounds;  // lvl 20
    draw_bounds.minx_ = (draw_bounds.minx_ << delta_z) - kOverdraw;
    draw_bounds.miny_ = (draw_bounds.miny_ << delta_z) - kOverdraw;
    draw_bounds.maxx_ = (draw_bounds.maxx_ << delta_z) + kOverdraw;
    draw_bounds.maxy_ = (draw_bounds.maxy_ << delta_z) + kOverdraw;

    px_bounds_ = fixed_box{{px_bounds.minx_, px_bounds.miny_},
                           {px_bounds.maxx_, px_bounds.maxy_}};
    insert_bounds_ = fixed_box{{insert_bounds.minx_, insert_bounds.miny_},
                               {insert_bounds.maxx_, insert_bounds.maxy_}};
    draw_bounds_ = fixed_box{{draw_bounds.minx_, draw_bounds.miny_},
                             {draw_bounds.maxx_, draw_bounds.maxy_}};
  }

  geo::tile tile_;
  fixed_box px_bounds_;  // on tile z
  fixed_box insert_bounds_, draw_bounds_;  // z lvl 20
};

}  // namespace tiles
