#include "tiles/feature/aggregate_line_features.h"

#include <stack>

#include "utl/concat.h"
#include "utl/equal_ranges_linear.h"
#include "utl/erase_duplicates.h"
#include "utl/erase_if.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "tiles/feature/feature.h"
#include "tiles/fixed/algo/simplify.h"
#include "tiles/fixed/convert.h"
#include "tiles/util.h"

namespace tiles {

struct line;
using line_handle = std::unique_ptr<line>;

struct line {
  fixed_xy from_{}, to_{};

  line_handle left_, right_;
  feature* feature_{nullptr};
  uint32_t geo_idx_{std::numeric_limits<uint32_t>::max()};

  bool reversed_{false};
  bool oneway_{false};
};

template <typename FeatureIt>
std::vector<line_handle> make_line_handles(FeatureIt lb, FeatureIt ub) {
  std::vector<line_handle> lines;
  for (auto it = lb; it != ub; ++it) {
    auto const& l = mpark::get<fixed_polyline>(it->geometry_);
    utl::verify(l.size() < std::numeric_limits<uint32_t>::max(),
                "make_line_handles: too many features");

    for (auto i = 0ULL; i < l.size(); ++i) {
      lines.emplace_back(std::make_unique<line>());
      lines.back()->from_ = l[i].front();
      lines.back()->to_ = l[i].back();
      lines.back()->feature_ = &*it;
      lines.back()->geo_idx_ = i;
      // TODO(root): oneway support (needs special tag?!)
    }
  }
  return lines;
}

void join_lines(std::vector<line_handle>& handles) {
  std::vector<std::pair<fixed_xy, line_handle*>> idx;
  for (auto& lh : handles) {
    idx.emplace_back(lh->from_, &lh);
    idx.emplace_back(lh->to_, &lh);
  }
  utl::erase_duplicates(
      idx,
      [](auto const& a, auto const& b) {
        return std::tie(a.first.x(), a.first.y(), a.second) <
               std::tie(b.first.x(), b.first.y(), b.second);
      },
      [](auto const& a, auto const& b) {
        return std::tie(a.first.x(), a.first.y(), a.second) ==
               std::tie(b.first.x(), b.first.y(), b.second);
      });

  auto const find_incident_line = [&](line_handle const& lh,
                                      fixed_xy const& pos) -> line_handle* {
    if (pos == invalid_xy) {
      return nullptr;
    }

    auto it0 = std::lower_bound(
        begin(idx), end(idx), pos, [](auto const& a, auto const& b) {
          return std::tie(a.first.x(), a.first.y()) < std::tie(b.x(), b.y());
        });

    size_t count = 0;
    line_handle* other = nullptr;
    for (auto it = it0; it != end(idx) && it->first == pos; ++it) {
      ++count;
      // NOLINTNEXTLINE
      if (it->second->get() == lh.get() || it->second->get() == nullptr) {
        continue;  // found self or already gone
      }
      other = it->second;
    }

    if (count == 2) {
      return other;
    }

    // degree != 2 -> "burn" this coordinate for further processing
    for (auto it = it0; it != end(idx) && it->first == pos; ++it) {
      // NOLINTNEXTLINE
      if (it->second->get() == nullptr) {
        continue;  // self can already be gone in bwd pass
      }

      if ((**it->second).from_ == pos) {
        (**it->second).from_ = invalid_xy;
      }
      if ((**it->second).to_ == pos) {
        (**it->second).to_ = invalid_xy;
      }
    }

    return nullptr;
  };

  auto const mark_reversed = [](line* l) {
    std::stack<line*> stack{{l}};
    while (!stack.empty()) {
      auto* curr = stack.top();
      stack.pop();

      curr->reversed_ = !curr->reversed_;

      if (curr->left_) {
        stack.push(curr->left_.get());
      }
      if (curr->right_) {
        stack.push(curr->right_.get());
      }
    }
  };

  for (auto it = begin(handles); it != end(handles); ++it) {
    if (!*it || (**it).from_ == (**it).to_) {
      continue;
    }

    line_handle* other = nullptr;
    while ((other = find_incident_line(*it, (**it).from_)) != nullptr) {
      if (*other == nullptr) {
        break;  // other alreay moved if join already discarded
      }
      if ((**it).oneway_ != (**other).oneway_) {
        break;  // dont join oneway with twoway
      }
      if ((**other).from_ == (**other).to_) {
        break;  // other is a "blossom"
      }

      auto joined = std::make_unique<line>();
      joined->to_ = (**it).to_;
      joined->oneway_ = (**it).oneway_;

      if ((**it).from_ == (**other).to_) {  //  --(other)--> X --(this)-->
        joined->from_ = (**other).from_;
      } else {  //  <--(other)-- X --(this)-->
        if ((**it).oneway_) {
          break;  // dont join conflicting oneway directions
        }
        joined->from_ = (**other).to_;
        mark_reversed(other->get());
      }

      joined->left_ = std::move(*other);
      joined->right_ = std::move(*it);
      *it = std::move(joined);
    }

    if ((**it).from_ == (**it).to_) {
      continue;  // cycle detected
    }

    while ((other = find_incident_line(*it, (**it).to_)) != nullptr) {
      if (*other == nullptr) {
        break;  // other alreay moved if join already discarded
      }
      if ((**it).oneway_ != (**other).oneway_) {
        break;  // dont join oneway with twoway
      }
      if ((**other).from_ == (**other).to_) {
        break;  // other is a "blossom"
      }

      auto joined = std::make_unique<line>();
      joined->from_ = (**it).from_;
      joined->oneway_ = (**it).oneway_;

      if ((**it).to_ == (**other).from_) {  // --(this)--> X --(other)-->
        joined->to_ = (**other).to_;
      } else {  // --(this)--> X <--(other)--
        if ((**it).oneway_) {
          break;  // conflicting oneway directions
        }
        joined->to_ = (**other).from_;
        mark_reversed(other->get());
      }

      joined->left_ = std::move(*it);
      joined->right_ = std::move(*other);
      *it = std::move(joined);
    }
  }
}

fixed_polyline aggregate_geometry(std::vector<line_handle> lines) {
  fixed_polyline polyline;

  for (auto& line : lines) {
    if (!line) {  // joined away
      continue;
    } else if (line->feature_ != nullptr) {  // unjoined / single
      polyline.emplace_back(
          std::move(mpark::get<fixed_polyline>(line->feature_->geometry_)
                        .at(line->geo_idx_)));
    } else {  // join result;
      fixed_line joined_geo;

      std::stack<line_handle> stack;
      stack.emplace(std::move(line));
      while (!stack.empty()) {
        auto curr = std::move(stack.top());
        stack.pop();

        if (curr->feature_ != nullptr) {
          auto const skip = joined_geo.empty() ? 0 : 1;
          auto const& curr_geo =
              mpark::get<fixed_polyline>(curr->feature_->geometry_)
                  .at(curr->geo_idx_);

          if (curr->reversed_) {
            std::reverse_copy(begin(curr_geo), std::next(end(curr_geo), -skip),
                              std::back_inserter(joined_geo));
          } else {
            std::copy(std::next(begin(curr_geo), skip), end(curr_geo),
                      std::back_inserter(joined_geo));
          }
        } else {
          if (curr->reversed_) {
            stack.emplace(std::move(curr->left_));
            stack.emplace(std::move(curr->right_));
          } else {
            stack.emplace(std::move(curr->right_));
            stack.emplace(std::move(curr->left_));
          }
        }
      }

      polyline.emplace_back(std::move(joined_geo));
    }
  }
  return polyline;
}

std::vector<feature> aggregate_line_features(std::vector<feature> features,
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
        auto lines = make_line_handles(lb, ub);
        join_lines(lines);

        feature f;
        f.id_ = lb->id_;
        f.meta_ = std::move(lb->meta_);

        f.geometry_ = aggregate_geometry(std::move(lines));
        if (z <= kMaxZoomLevel) {
          f.geometry_ =
              simplify(std::move(f.geometry_), 1ULL << (kMaxZoomLevel - z));
        }

        result.emplace_back((f));
      });

  return result;
}

}  // namespace tiles
