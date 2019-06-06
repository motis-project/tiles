#include "tiles/fixed/io/dump.h"

namespace tiles {

void dump(fixed_null const&) { std::cout << "null geometry" << std::endl; }

void dump(fixed_point const&) {
  std::cout << "point geometry\n";
  // std::cout << "  " << point.x_ << ", " << point.y_ << "\n";
}

void dump(fixed_polyline const&) {
  std::cout << "polyline geometry\n";  // << polyline.geometry_.size() << "\n";
  // for (auto i = 0u; i < polyline.geometry_.size(); ++i) {
  //   for (auto& point : polyline.geometry_[i]) {
  //     std::cout << "  " << i << "\t" << point.x_ << ", " << point.y_ << "\n";
  //   }
  // }
}

void dump(fixed_polygon const&) {
  std::cout << "polygon geometry\n";
  std::cout << "  not supported\n";
}

void dump(fixed_geometry const& geometry) {
  mpark::visit([](auto& arg) { dump(arg); }, geometry);
}

}  // namespace tiles
