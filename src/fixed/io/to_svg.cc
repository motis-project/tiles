#include "tiles/fixed/io/to_svg.h"

#include "boost/geometry.hpp"

#include "utl/erase_if.h"

#include "tiles/util.h"

namespace tiles {

std::string to_svg(fixed_null const&) { throw utl::fail("no impl"); }
std::string to_svg(fixed_point const&) { throw utl::fail("no impl"); }
std::string to_svg(fixed_polyline const&) { throw utl::fail("no impl"); }

std::string to_svg(fixed_polygon const& in) {
  fixed_box box;
  boost::geometry::envelope(in, box);

  std::stringstream ss;
  ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
     << box.min_corner().x() << " " << box.min_corner().y() << " "
     << (box.max_corner().x() - box.min_corner().x()) << " "
     << (box.max_corner().y() - box.min_corner().y()) << "\">";

  for (auto const& poly : in) {
    ss << "<path stroke=\"blue\" d=\"";

    bool first = true;
    for (auto const& pt : poly.outer()) {
      ss << (first ? "M " : "L ");
      first = false;

      ss << pt.x() << " " << pt.y() << " ";
    }

    ss << "\" />";
  }
  ss << "</svg>";
  return ss.str();
}

std::string to_svg(fixed_geometry const& in) {
  return mpark::visit([&](auto const& arg) { return to_svg(arg); }, in);
}

}  // namespace tiles
