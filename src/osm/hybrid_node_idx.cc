#include "tiles/osm/hybrid_node_idx.h"

#include <limits>
#include <tuple>

#include "osmium/index/detail/mmap_vector_file.hpp"
#include "osmium/index/detail/tmpfile.hpp"
#include "osmium/osm/way.hpp"
#include "osmium/visitor.hpp"

#include "protozero/varint.hpp"

#include "utl/verify.h"

#include "tiles/fixed/algo/delta.h"
#include "tiles/util.h"

namespace o = osmium;
namespace od = osmium::detail;
namespace pz = protozero;

using pz::decode_varint;
using pz::decode_zigzag64;
using pz::skip_varint;

using osm_id_t = o::object_id_type;

namespace tiles {

// dat contains spans which node sets with consecutive node ids
// there are two kinds of spans
// "coord_span" contains compressed coordianates of consecutive nodes
// "empty_span" contains a placeholder for n missing node ids

// a coord span is a stream of integers
// [fi] = uint32_t [vi] = var int normal [vi_zz] = var int zigzag encoded
// vi    | ((n << 1) | k) | this span contain n+1 nodes (k = 0x0 -> coord span)
// fi    |           x[0] | the absolute x coordinate of the 0th node
// fi    |           y[0] | the absolute y coordinate of the 0th node
// vi_zz |           x[1] | the delta encoded x coordinate of the 1st node
// vi_zz |           y[1] | the delta encoded y coordinate of the 1st node
//                     ...
// vi_zz |           x[n] | the delta encoded x coordinate of the last node
// vi_zz |           y[n] | the delta encoded y coordinate of the last node

// a empty span contains just the number of missing nodes
// vi    | ((n << 1) | k) | this represents n+1 missing nodes (k = 0x1 -> empty)

// a empty span with n = 0 and k = 0x1 marks EOF

// idx contains a sorted pairs
// of osm node id and offset of the corresponding span in dat

struct id_offset {
  id_offset(osm_id_t id, size_t offset) : id_{id}, offset_{offset} {}

  id_offset()
      : id_{std::numeric_limits<osm_id_t>::max()},
        offset_{std::numeric_limits<size_t>::max()} {}

  bool operator==(id_offset const& o) const {
    return std::tie(id_, offset_) == std::tie(o.id_, o.offset_);
  }

  osm_id_t id_;
  size_t offset_;
};

struct hybrid_node_idx::impl {
  impl(int idx_fd, int dat_fd) : idx_{idx_fd}, dat_{dat_fd} {}

  od::mmap_vector_file<id_offset> idx_;
  od::mmap_vector_file<char> dat_;
};

uint32_t read_fixed(char const** data) {
  auto const* p = reinterpret_cast<uint8_t const*>(*data);
  uint32_t val = 0;
  val |= *p++;
  val |= (*p++) << 8;
  val |= (*p++) << 16;
  val |= (*p++) << 24;
  *data = reinterpret_cast<const char*>(p);
  return val;
}

std::optional<fixed_xy> get_coords(hybrid_node_idx const& nodes,
                                   osm_id_t const& id) {
  auto const& idx = nodes.impl_->idx_;
  auto const& dat = nodes.impl_->dat_;

  if (idx.empty()) {  // very unlikely in prod
    return std::nullopt;
  }
  auto const abs_id = std::abs(id);
  auto const* it =
      std::lower_bound(std::begin(idx), std::end(idx), abs_id,
                       [](auto const& o, auto const& i) { return o.id_ < i; });

  if (it == std::begin(idx) &&
      it->id_ != abs_id) {  // not empty -> begin != end
    return std::nullopt;
  }
  if (it == std::end(idx) || it->id_ != abs_id) {
    --it;
  }

  osm_id_t curr_id = it->id_;
  auto const* dat_it = dat.data() + it->offset_;

  while (curr_id <= abs_id && dat_it != std::end(dat)) {
    auto const header = pz::decode_varint(&dat_it, std::end(dat));
    auto const span_size = header >> 1;
    auto const root_idx = header & 0x1;

    if (root_idx == 0x1) {
      if (span_size == 0) {
        break;  // eof
      } else {
        curr_id += span_size;  // empty span
        continue;
      }
    }

    delta_decoder x_dec{read_fixed(&dat_it)};
    delta_decoder y_dec{read_fixed(&dat_it)};
    if (curr_id == abs_id) {
      return fixed_xy{x_dec.curr_, y_dec.curr_};
    }
    ++curr_id;

    for (auto i = 1ULL; i < (span_size + 1); ++i) {
      auto x = x_dec.decode(
          pz::decode_zigzag64(pz::decode_varint(&dat_it, std::end(dat))));
      auto y = y_dec.decode(
          pz::decode_zigzag64(pz::decode_varint(&dat_it, std::end(dat))));

      if (curr_id == abs_id) {
        return fixed_xy{x, y};
      }
      ++curr_id;
    }
  }
  return std::nullopt;
}

void get_coords(
    hybrid_node_idx const& nodes,
    std::vector<std::pair<o::object_id_type, o::Location*>>& queries) {
  auto const& idx = nodes.impl_->idx_;
  auto const& dat = nodes.impl_->dat_;

  if (idx.empty()) {  // unlikely in prod
    return;
  }

  osm_id_t curr_id = std::numeric_limits<osm_id_t>::min();
  char const* dat_it = nullptr;
  int64_t span_size = 0;  // length of current span
  int64_t span_pos = 0;  // position in current span

  delta_decoder x_dec{0};
  delta_decoder y_dec{0};

  enum class fsm_state {
    from_index,
    at_span_start,
    in_span,
  };

  fsm_state state = fsm_state::from_index;

  std::sort(begin(queries), end(queries), [](auto const& lhs, auto const& rhs) {
    return std::abs(lhs.first) < std::abs(rhs.first);
  });
  auto q_it = begin(queries);
  for (; q_it != end(queries) && std::abs(q_it->first) < idx.at(0).id_;
       ++q_it) {
    // skip missing pre
  }

  constexpr auto kReInitDistance = 1024;

  while (q_it != end(queries)) {
    auto const& query_id = std::abs(q_it->first);

    switch (state) {
      // pre cond: have any query_id
      case fsm_state::from_index: {
        auto const* it_idx = std::lower_bound(
            std::begin(idx), std::end(idx), query_id,
            [](auto const& o, auto const& i) { return o.id_ < i; });

        utl::verify(!(it_idx == std::begin(idx) && it_idx->id_ != query_id),
                    "missing (cannnot happen)");
        if (it_idx == std::end(idx) || it_idx->id_ != query_id) {
          --it_idx;
        }

        curr_id = it_idx->id_;
        dat_it = dat.data() + it_idx->offset_;
        state = fsm_state::at_span_start;
      } break;

      // pre cond: curr_id <= query_id, dat_it points to header for curr_id
      case fsm_state::at_span_start: {
        auto const header = decode_varint(&dat_it, std::end(dat));
        span_size = header >> 1;

        if ((header & 0x1) == 0x1) {
          if (span_size == 0) {
            return;  // eof
          } else {
            curr_id += span_size;  // skip missing node ids
            state = fsm_state::at_span_start;
            break;
          }
        }
        span_size += 1;
        span_pos = 0;

        // not found : skip missing queries
        if (query_id < curr_id) {
          for (; q_it != end(queries) && std::abs(q_it->first) < curr_id;
               ++q_it) {
          }
          x_dec.reset(read_fixed(&dat_it));
          y_dec.reset(read_fixed(&dat_it));
          state = fsm_state::in_span;
          break;
        }

        // is in this span : go into
        if (query_id < curr_id + span_size) {
          x_dec.reset(read_fixed(&dat_it));
          y_dec.reset(read_fixed(&dat_it));
          state = fsm_state::in_span;
          break;
        }

        // not in this span : skip
        dat_it += 2 * sizeof(uint32_t);  // skip fixed x[0]/y[0];
        for (auto i = 1; i < span_size; ++i) {
          skip_varint(&dat_it, std::end(dat));  // x[i]
          skip_varint(&dat_it, std::end(dat));  // y[i]
        }
        curr_id += span_size;
        state = fsm_state::at_span_start;
      } break;

      // pre cond: x_dec/y_dec initialized for span_pos
      case fsm_state::in_span: {
        if (query_id < curr_id + (span_size - span_pos)) {
          while (curr_id != query_id) {
            utl::verify(dat_it != std::end(dat), "hit end(dat)");
            utl::verify(span_pos < span_size, "hit end of span");

            x_dec.decode(
                decode_zigzag64(decode_varint(&dat_it, std::end(dat))));
            y_dec.decode(
                decode_zigzag64(decode_varint(&dat_it, std::end(dat))));
            ++curr_id;
            ++span_pos;
          }

          utl::verify(query_id == curr_id, "missed node");
          for (; q_it != end(queries) && std::abs(q_it->first) == query_id;
               ++q_it) {
            q_it->second->set_x(x_dec.curr_);
            q_it->second->set_y(y_dec.curr_);
          }
          state = fsm_state::in_span;
          break;
        }

        // distance big enough : use index from scratch
        if (query_id > curr_id + kReInitDistance) {
          state = fsm_state::from_index;
          break;
        }

        // next distance nearby, but not in this span : unwind span first
        ++span_pos;  // don't unwind current pos
        for (; span_pos < span_size; ++span_pos) {
          skip_varint(&dat_it, std::end(dat));  // x
          skip_varint(&dat_it, std::end(dat));  // y
          ++curr_id;
        }

        ++curr_id;  // first id of next span
        state = fsm_state::at_span_start;
      } break;
    }
  }
}

void update_locations(hybrid_node_idx const& nodes, o::memory::Buffer& buffer) {
  struct query_builder : public o::handler::Handler {
    void way(o::Way& way) {
      for (auto& node_ref : way.nodes()) {
        query_.emplace_back(node_ref.ref(), &node_ref.location());
      }
    }
    std::vector<std::pair<o::object_id_type, o::Location*>> query_;
  };

  query_builder builder;
  o::apply(buffer, builder);

  if (!builder.query_.empty()) {
    get_coords(nodes, builder.query_);

    for (auto const& pair : builder.query_) {
      pair.second->set_x(pair.second->x() - hybrid_node_idx::x_offset);
      pair.second->set_y(pair.second->y() - hybrid_node_idx::y_offset);
    }
  }
}

hybrid_node_idx::hybrid_node_idx()
    : impl_{std::make_unique<impl>(od::create_tmp_file(),
                                   od::create_tmp_file())} {}
hybrid_node_idx::hybrid_node_idx(int idx_fd, int dat_fd)
    : impl_{std::make_unique<impl>(idx_fd, dat_fd)} {}
hybrid_node_idx::~hybrid_node_idx() = default;

void hybrid_node_idx::way(o::Way& way) const {
  for (auto& node_ref : way.nodes()) {
    auto const& coords = get_coords(*this, node_ref.ref());
    utl::verify(coords.has_value(), "coords missing!");
    node_ref.set_location(
        o::Location{static_cast<int32_t>(coords->x() - x_offset),
                    static_cast<int32_t>(coords->y() - y_offset)});
  }
}

struct hybrid_node_idx_builder::impl {
  impl(od::mmap_vector_file<id_offset>& idx, od::mmap_vector_file<char>& dat)
      : nodes_{nullptr}, idx_{idx}, dat_{dat} {}

  explicit impl(std::unique_ptr<hybrid_node_idx::impl> nodes)
      : nodes_{std::move(nodes)}, idx_{nodes_->idx_}, dat_{nodes_->dat_} {}

  void push(osm_id_t const id, fixed_xy const& pos) {
    constexpr auto coord_min = std::numeric_limits<uint32_t>::min();
    constexpr auto coord_max = std::numeric_limits<uint32_t>::max();

    utl::verify(pos.x() >= coord_min && pos.y() >= coord_min &&
                    pos.x() <= coord_max && pos.y() <= coord_max,
                "pos ({}, {}) not within bounds ({} / {})",  //
                pos.x(), pos.y(), coord_min, coord_max);

    auto const abs_id = std::abs(id);
    if (abs_id == last_id_) {
      utl::verify(
          pos == last_pos_,
          "input: duplicate absolute node id with mismatching coordinates {}",
          abs_id);
      return;
    }

    utl::verify(abs_id > last_id_, "input: node ids are not sorted! {} <= {}",
                abs_id, last_id_);
    ++stat_nodes_;

    if (last_id_ + 1 != abs_id && !span_.empty()) {
      push_coord_span();
      push_empty_span(abs_id);
    }

    last_id_ = abs_id;
    last_pos_ = pos;
    span_.emplace_back(pos);
  }

  void finish() {
    push_coord_span();
    push_empty_span(last_id_ + 1);  // 0 length empty span --> "EOF"
  }

  void push_coord_span() {
    if (span_.empty()) {
      return;
    }

    if (idx_.empty() || coords_written_ > kCoordsPerIndex) {
      osm_id_t const start_id = last_id_ - span_.size() + 1;
      idx_.push_back(id_offset{start_id, dat_.size()});
      coords_written_ = 0;
    }

    delta_encoder x_enc{0};
    delta_encoder y_enc{0};
    for (auto i = 0ULL; i < span_.size();) {
      x_enc.reset(span_[i].x());
      y_enc.reset(span_[i].y());

      auto j = i + 1;
      for (; j < span_.size(); ++j) {
        auto const sx =
            get_varint_size(pz::encode_zigzag64(x_enc.encode(span_[j].x())));
        auto const sy =
            get_varint_size(pz::encode_zigzag64(y_enc.encode(span_[j].y())));

        if (sx + sy > 2 * sizeof(uint32_t) + 1) {
          break;
        }

        ++stat_coord_chars_[sx < 10 ? sx : 0];
        ++stat_coord_chars_[sy < 10 ? sy : 0];
      }

      auto const span_size = j - i;
      push_varint((span_size - 1) << 1 | 0x0);
      push_fixed(span_[i].x());
      push_fixed(span_[i].y());

      x_enc.reset(span_[i].x());
      y_enc.reset(span_[i].y());

      ++i;
      for (; i < j; ++i) {
        push_varint_zz(x_enc.encode(span_[i].x()));
        push_varint_zz(y_enc.encode(span_[i].y()));
      }

      ++stat_spans_;
      for (auto k = 0ULL; k < kStatSpanCumSizeLimits.size(); ++k) {
        if (span_size <= kStatSpanCumSizeLimits[k]) {
          ++stat_span_cum_sizes_[k];
        }
      }
    }

    coords_written_ += span_.size();
    span_.clear();
  }

  void push_empty_span(osm_id_t const next_id) {
    push_varint((next_id - last_id_ - 1) << 1 | 0x1);
  }

  template <typename Integer64>
  int push_varint(Integer64 v) {
    return pz::write_varint(std::back_inserter(dat_), v);
  }

  template <typename Integer64>
  int push_varint_zz(Integer64 v) {
    return push_varint(pz::encode_zigzag64(v));
  }

  void push_fixed(uint32_t v) {
    for (auto i = 0ULL; i < sizeof(v); ++i) {
      dat_.push_back(static_cast<char>(v & 0xffU));
      v >>= 8ULL;
    }
  }

  static inline uint32_t get_varint_size(uint64_t value) {
    uint32_t n = 1;
    while (value >= 0x80ULL) {
      value >>= 7ULL;
      ++n;
    }
    return n;
  }

  void dump_stats() const {
    tiles::t_log("index size: {} entries", idx_.size());
    tiles::t_log("data size: {} bytes", dat_.size());

    tiles::t_log("builder: nodes {}", stat_nodes_);
    tiles::t_log("builder: spans {}", stat_spans_);

    for (auto i = 0ULL; i < stat_coord_chars_.size(); ++i) {
      tiles::t_log("builder: coord chars {} {}", i, stat_coord_chars_[i]);
    }

    for (auto i = 0ULL; i < kStatSpanCumSizeLimits.size(); ++i) {
      tiles::t_log("builder: cum spans <= {:>5} {:>12}",
                   kStatSpanCumSizeLimits[i], stat_span_cum_sizes_[i]);
    }
  }

  std::unique_ptr<hybrid_node_idx::impl> nodes_;
  od::mmap_vector_file<id_offset>& idx_;
  od::mmap_vector_file<char>& dat_;

  osm_id_t last_id_ = std::numeric_limits<osm_id_t>::min();
  fixed_xy last_pos_{0, 0};

  std::vector<fixed_xy> span_;

  static constexpr auto const kCoordsPerIndex = 1024;  // one idx entry each n
  size_t coords_written_ = 0;

  size_t stat_nodes_ = 0;
  size_t stat_spans_ = 0;
  std::array<size_t, 7> stat_coord_chars_ = {};

  static constexpr std::array<size_t, 5> kStatSpanCumSizeLimits{1, 64, 100,
                                                                1000, 10000};
  std::array<size_t, kStatSpanCumSizeLimits.size()> stat_span_cum_sizes_ = {};
};

hybrid_node_idx_builder::hybrid_node_idx_builder(hybrid_node_idx& nodes)
    : impl_{std::make_unique<impl>(nodes.impl_->idx_, nodes.impl_->dat_)} {}

hybrid_node_idx_builder::hybrid_node_idx_builder(int idx_fd, int dat_fd)
    : impl_{std::make_unique<impl>(
          std::make_unique<hybrid_node_idx::impl>(idx_fd, dat_fd))} {}

hybrid_node_idx_builder::~hybrid_node_idx_builder() = default;

void hybrid_node_idx_builder::push(o::object_id_type const id,
                                   fixed_xy const& coords) const {
  impl_->push(id, coords);
}

void hybrid_node_idx_builder::finish() const { impl_->finish(); }

void hybrid_node_idx_builder::dump_stats() const { impl_->dump_stats(); }
size_t hybrid_node_idx_builder::get_stat_spans() const {
  return impl_->stat_spans_;
}

}  // namespace tiles
