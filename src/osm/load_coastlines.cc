#include "tiles/osm/load_coastlines.h"

#include <memory>
#include <mutex>
#include <type_traits>
#include <variant>

#include "clipper/clipper.hpp"

#include "utl/to_vec.h"

#include "tiles/db/bq_tree.h"
#include "tiles/db/feature_inserter_mt.h"
#include "tiles/db/layer_names.h"
#include "tiles/db/tile_database.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/algo/area.h"
#include "tiles/fixed/algo/bounding_box.h"
#include "tiles/fixed/algo/clip.h"
#include "tiles/fixed/convert.h"
#include "tiles/fixed/fixed_geometry.h"
#include "tiles/mvt/tile_spec.h"
#include "tiles/osm/load_shapefile.h"
#include "tiles/util_parallel.h"

namespace cl = ClipperLib;

namespace tiles {

static_assert(sizeof(cl::cInt) == sizeof(fixed_coord_t), "coord type problem");

struct coastline {
  coastline(fixed_box box, cl::Paths geo) : box_{box}, geo_{std::move(geo)} {}

  fixed_box box_;
  cl::Paths geo_;
};
using coastline_ptr = std::shared_ptr<coastline>;

struct geo_task {
  geo::tile tile_{};
  std::vector<coastline_ptr> coastlines_;
};

using geo_queue_t = queue_wrapper<geo_task>;
using db_queue_t = queue_wrapper<std::pair<geo::tile, std::string>>;

struct coastline_stats {
  static constexpr uint64_t kTotal = (1 << 10) * (1 << 10);

  coastline_stats() : fully_dirtside_{0}, fully_seaside_{0} {

    progress_->status("Process Coastline").out_mod(5.F).in_high(kTotal);
  }

  void report_progess(uint32_t z) const {
    progress_->increment(1ULL << (10 - z) * 1ULL << (10 - z));
  }

  void summary() {
    t_log("fully:  seaside {}, dirtside {}", fully_seaside_, fully_dirtside_);
  }

  progress_tracker progress_;
  std::atomic_uint64_t fully_dirtside_, fully_seaside_;
};

std::ostream& operator<<(std::ostream& os, fixed_box const& box) {
  return os << "(" << box.min_corner().x() << ", " << box.min_corner().y()
            << ")(" << box.max_corner().x() << ", " << box.max_corner().y()
            << ")";
}

fixed_box bounding_box(cl::Paths const& geo) {
  auto min_x = std::numeric_limits<fixed_coord_t>::max();
  auto min_y = std::numeric_limits<fixed_coord_t>::max();
  auto max_x = std::numeric_limits<fixed_coord_t>::min();
  auto max_y = std::numeric_limits<fixed_coord_t>::min();

  for (auto const& path : geo) {
    for (auto const& pt : path) {
      min_x = std::min(min_x, static_cast<fixed_coord_t>(pt.X));
      min_y = std::min(min_y, static_cast<fixed_coord_t>(pt.Y));
      max_x = std::max(max_x, static_cast<fixed_coord_t>(pt.X));
      max_y = std::max(max_y, static_cast<fixed_coord_t>(pt.Y));
    }
  }

  return fixed_box{{min_x, min_y}, {max_x, max_y}};
}

bool touches(fixed_box const& a, fixed_box const& b) {
  return !(a.min_corner().x() > b.max_corner().x() ||
           a.max_corner().x() < b.min_corner().x()) &&
         !(a.min_corner().y() > b.max_corner().y() ||
           a.max_corner().y() < b.min_corner().y());
}

bool within(fixed_box const& outer, fixed_box const& inner) {
  return outer.min_corner().x() <= inner.min_corner().x() &&
         outer.max_corner().x() >= inner.max_corner().x() &&
         outer.min_corner().y() <= inner.min_corner().y() &&
         outer.max_corner().y() >= inner.max_corner().y();
}

cl::Path box_to_path(fixed_box const& box) {
  return {{box.min_corner().x(), box.min_corner().y()},
          {box.max_corner().x(), box.min_corner().y()},
          {box.max_corner().x(), box.max_corner().y()},
          {box.min_corner().x(), box.max_corner().y()}};
}

cl::Paths intersection(cl::Paths const& subject, cl::Path const& clip) {
  cl::Clipper clpr;
  utl::verify(clpr.AddPaths(subject, cl::ptSubject, true), "AddPath failed");
  utl::verify(clpr.AddPath(clip, cl::ptClip, true), "AddPaths failed");

  cl::Paths solution;
  utl::verify(clpr.Execute(cl::ctIntersection, solution, cl::pftEvenOdd,
                           cl::pftEvenOdd),
              "Execute failed");
  return solution;
}

void to_fixed_polygon(fixed_polygon& polygon, cl::PolyNodes const& nodes) {
  auto const path_to_ring = [](auto const& path) {
    utl::verify(!path.empty(), "path empty");
    fixed_ring ring;
    ring.reserve(path.size() + 1);
    for (auto const& pt : path) {
      ring.emplace_back(pt.X, pt.Y);
    }
    ring.emplace_back(path[0].X, path[0].Y);
    return ring;
  };

  for (auto const* outer : nodes) {
    utl::verify(!outer->IsHole(), "outer ring is hole");
    fixed_simple_polygon simple;
    simple.outer() = path_to_ring(outer->Contour);

    for (auto const* inner : outer->Childs) {
      utl::verify(inner->IsHole(), "inner ring is no hole");
      simple.inners().emplace_back(path_to_ring(inner->Contour));

      to_fixed_polygon(polygon, inner->Childs);
    }

    polygon.emplace_back(std::move(simple));
  }
}

std::optional<std::string> finalize_tile(
    uint64_t const id, cl::Path const& draw_clip, cl::Path const& insert_clip,
    std::vector<coastline_ptr> const& coastlines) {
  cl::Clipper clpr;
  clpr.AddPath(draw_clip, cl::ptSubject, true);
  for (auto const& coastline : coastlines) {
    clpr.AddPaths(coastline->geo_, cl::ptClip, true);
  }

  cl::PolyTree solution;
  clpr.Execute(cl::ctDifference, solution, cl::pftEvenOdd, cl::pftEvenOdd);
  utl::verify(!solution.Childs.empty(), "difference empty!");

  cl::Paths solution_paths;
  cl::ClosedPathsFromPolyTree(solution, solution_paths);
  if (intersection(solution_paths, insert_clip).empty()) {
    return std::nullopt;
  }

  fixed_polygon polygon;
  to_fixed_polygon(polygon, solution.Childs);

  boost::geometry::correct(polygon);
  return serialize_feature({id,
                            kLayerCoastlineIdx,
                            std::pair<uint32_t, uint32_t>{0, kMaxZoomLevel + 1},
                            {},
                            std::move(polygon)});
}

void process_coastline(
    geo_task& task, geo_queue_t& geo_q, db_queue_t& db_q,
    coastline_stats& stats,
    std::function<void(geo::tile const&)>&& seaside_appender) {
  for (auto const& child : task.tile_.direct_children()) {
    auto const insert_bounds = tile_spec{child}.insert_bounds_;
    auto const insert_clip = box_to_path(insert_bounds);

    auto const draw_bounds = tile_spec{child}.draw_bounds_;
    auto const draw_clip = box_to_path(draw_bounds);

    bool fully_dirtside = false;
    std::vector<coastline_ptr> matching;
    for (auto const& coastline : task.coastlines_) {
      if (!touches(insert_bounds, coastline->box_)) {
        continue;
      }

      if (within(insert_bounds, coastline->box_)) {
        matching.push_back(coastline);
        continue;
      }

      auto geo = intersection(coastline->geo_, draw_clip);
      if (geo.empty()) {
        continue;
      }

      if (geo.size() == 1 && geo[0].size() == 4 &&
          std::all_of(begin(geo[0]), end(geo[0]), [&draw_clip](auto const& pt) {
            return end(draw_clip) !=
                   std::find(begin(draw_clip), end(draw_clip), pt);
          })) {
        fully_dirtside = true;
        break;
      }

      matching.push_back(std::make_shared<struct coastline>(bounding_box(geo),
                                                            std::move(geo)));
    }

    if (fully_dirtside) {
      ++stats.fully_dirtside_;
      stats.report_progess(child.z_);
    } else if (matching.empty()) {
      seaside_appender(child);
      ++stats.fully_seaside_;
      stats.report_progess(child.z_);
    } else if (child.z_ < 10) {
      geo_q.enqueue(geo_task{child, std::move(matching)});
    } else {
      if (auto str = finalize_tile(tile_to_key(child),  //
                                   draw_clip, insert_clip, matching);
          str) {
        db_q.enqueue({child, std::move(*str)});
      } else {
        ++stats.fully_dirtside_;
      }
      stats.report_progess(child.z_);
    }
  }
}

void load_coastlines(tile_db_handle& db_handle, feature_inserter_mt& inserter,
                     std::string const& fname) {
  geo_queue_t geo_queue;
  db_queue_t db_queue;
  coastline_stats stats;

  auto convert_path = [](auto const& in) {
    return utl::to_vec(in, [](auto const& pt) {
      return cl::IntPoint{pt.x(), pt.y()};
    });
  };

  {
    std::vector<coastline_ptr> coastlines;
    auto coastline_handler = [&](fixed_simple_polygon geo) {
      cl::Paths coastline;
      coastline.emplace_back(convert_path(geo.outer()));
      for (auto const& inner : geo.inners()) {
        coastline.emplace_back(convert_path(inner));
      }
      coastlines.emplace_back(std::make_shared<struct coastline>(
          bounding_box(geo), std::move(coastline)));
    };

    try {
      load_shapefile(fname, coastline_handler);
    } catch (...) {
      t_log("load_shapefile failed [file={}]", fname);
      throw;
    }

    constexpr auto const kInitialZoomlevel = 4ULL;
    auto it = geo::tile_iterator(kInitialZoomlevel);
    while (it->z_ == kInitialZoomlevel) {
      geo_queue.enqueue(geo_task{*it, coastlines});
      ++it;
    }
  }

  std::mutex fully_seaside_mutex;
  std::vector<geo::tile> fully_seaside;

  // auto const num_workers = 1;
  auto const num_workers = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  threads.reserve(num_workers);
  for (auto i = 0ULL; i < num_workers; ++i) {
    threads.emplace_back([&] {
      while (!geo_queue.finished()) {
        geo_task task;
        if (!geo_queue.dequeue(task)) {
          continue;
        }

        process_coastline(
            task, geo_queue, db_queue, stats, [&](auto const& tile) {
              std::lock_guard<std::mutex> lock(fully_seaside_mutex);
              fully_seaside.push_back(tile);
            });
        geo_queue.finish();
      }
    });
  }

  {
    while (!geo_queue.finished() || !db_queue.finished()) {
      std::pair<geo::tile, std::string> data;
      if (!db_queue.dequeue(data)) {
        continue;
      }

      inserter.insert(data.first, data.second);
      inserter.flush();
      db_queue.finish();
    }
  }

  std::for_each(begin(threads), end(threads), [](auto& t) { t.join(); });

  utl::verify(geo_queue.queue_.size_approx() == 0, "geo_queue not empty");
  utl::verify(db_queue.queue_.size_approx() == 0, "db_queue not empty");
  stats.summary();

  bq_tree seaside_tree;
  {
    scoped_timer t{"seaside_tree"};
    seaside_tree = make_bq_tree(fully_seaside);
  }
  t_log("seaside_tree with {} nodes", seaside_tree.nodes_.size());

  {
    auto txn = db_handle.make_txn();
    auto meta_dbi = db_handle.meta_dbi(txn);
    txn.put(meta_dbi, kMetaKeyFullySeasideTree, seaside_tree.string_view());
    txn.commit();
  }
}

}  // namespace tiles
