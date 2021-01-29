#include "tiles/feature/aggregate_polygon_features.h"

#include "utl/equal_ranges_linear.h"
#include "utl/to_vec.h"

#include "tiles/feature/feature.h"
#include "tiles/fixed/algo/simplify.h"
#include "tiles/fixed/convert.h"
#include "tiles/util.h"

namespace tiles {

std::vector<feature> aggregate_polygon_features(std::vector<feature> features,
                                                uint32_t const z) {
  std::sort(
      begin(features), end(features), [](auto const& lhs, auto const& rhs) {
        return std::tie(lhs.meta_, lhs.id_) < std::tie(rhs.meta_, rhs.id_);
      });

  std::vector<feature> result;
  utl::equal_ranges_linear(
      features,
      [](auto const& lhs, auto const& rhs) { return lhs.meta_ == rhs.meta_; },
      [&](auto lb, auto ub) {
        fixed_polygon final_polygon;
        for (auto it = lb; it != ub; ++it) {
          for (auto& p : mpark::get<fixed_polygon>(it->geometry_)) {
            final_polygon.emplace_back(std::move(p));
          }
        }

        feature f;
        f.id_ = lb->id_;
        f.meta_ = std::move(lb->meta_);

        // TODO(root): this is a noop
        f.geometry_ =
            simplify(std::move(final_polygon), 1ULL << (kMaxZoomLevel - z));

        result.emplace_back(std::move(f));
      });

  return result;
}

}  // namespace tiles
