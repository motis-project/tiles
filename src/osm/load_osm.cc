#include "tiles/osm/load_osm.h"

#include <sys/mman.h>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "boost/fiber/all.hpp"

#include "cista/mmap.h"

#include "osmium/builder/osm_object_builder.hpp"
#include "osmium/memory/buffer.hpp"
#include "osmium/osm/area.hpp"
#include "osmium/osm/location.hpp"
#include "osmium/osm/node.hpp"
#include "osmium/osm/node_ref.hpp"
#include "osmium/osm/way.hpp"

#include "utl/parallel_for.h"

#include "osm/decoder.h"
#include "osm/hnidx/hybrid_node_index.h"
#include "osm/inflate.h"
#include "osm/mp_manager.h"
#include "osm/parse.h"
#include "osm/raw_reader.h"
#include "osm/types.h"
#include "osm/way_handler.h"

#include "tiles/db/feature_inserter_mt.h"
#include "tiles/db/feature_shard.h"
#include "tiles/db/layer_names.h"
#include "tiles/db/shared_metadata.h"
#include "tiles/db/tile_database.h"
#include "tiles/osm/feature_handler.h"
#include "tiles/util.h"

namespace tiles {

namespace {

namespace bf = boost::fibers;

// `osmium::Location` is constructed `(x = lng, y = lat)` in the same
// fixed-precision units as `geo::fixed_latlng`'s `lng_`/`lat_` members.
inline osmium::Location to_osmium(osm::location const& l) noexcept {
  return osmium::Location{l.lng_, l.lat_};
}

template <typename Tags>
void add_tags(osmium::builder::Builder& parent, Tags&& tags) {
  osmium::builder::TagListBuilder tl{parent};
  for (auto const& [k, v] : tags) {
    tl.add_tag(k.data(), k.size(), v.data(), v.size());
  }
}

template <typename Tags>
osmium::Node const& build_node(osmium::memory::Buffer& buf,
                               std::int64_t const id, geo::latlng const& pos,
                               Tags&& tags) {
  buf.clear();
  {
    osmium::builder::NodeBuilder nb{buf};
    nb.set_id(id);
    nb.set_location(osmium::Location{pos.lng_, pos.lat_});
    add_tags(nb, std::forward<Tags>(tags));
  }
  buf.commit();
  return buf.get<osmium::Node>(0);
}

template <typename Tags>
osmium::Way const& build_way(osmium::memory::Buffer& buf, osm::way const& w,
                             Tags&& tags) {
  buf.clear();
  {
    osmium::builder::WayBuilder wb{buf};
    wb.set_id(w.id);
    {
      osmium::builder::WayNodeListBuilder wnl{wb};
      for (auto const& nr : w.nodes()) {
        wnl.add_node_ref(osmium::NodeRef{nr.ref(), to_osmium(nr.location())});
      }
    }
    add_tags(wb, std::forward<Tags>(tags));
  }
  buf.commit();
  return buf.get<osmium::Way>(0);
}

template <typename RingBuilder>
void emit_ring(osmium::builder::AreaBuilder& ab,
               std::span<osm::node_ref const> const ring) {
  RingBuilder rb{ab};
  for (auto const& nr : ring) {
    rb.add_node_ref(osmium::NodeRef{nr.ref(), to_osmium(nr.location())});
  }
}

template <typename Tags>
osmium::Area const& build_area(osmium::memory::Buffer& buf,
                               osm::polygon_area const& pa, Tags&& tags) {
  buf.clear();
  {
    osmium::builder::AreaBuilder ab{buf};
    // Use the original (way/relation) id directly; downstream feature_handler
    // does not depend on osmium's area-id scheme.
    ab.set_id(pa.origin_id);
    add_tags(ab, std::forward<Tags>(tags));

    for (auto const& ap : pa.area) {
      if (ap.offsets.empty()) {
        continue;
      }
      auto const part_size = static_cast<std::int64_t>(ap.area_part.size());
      // outer ring
      auto const outer_start = ap.offsets[0];
      auto const outer_end = ap.offsets.size() > 1 ? ap.offsets[1] : part_size;
      if (outer_end > outer_start) {
        emit_ring<osmium::builder::OuterRingBuilder>(
            ab, std::span<osm::node_ref const>{
                    ap.area_part.data() + outer_start,
                    static_cast<std::size_t>(outer_end - outer_start)});
      }
      // inner rings
      for (std::size_t i = 1; i < ap.offsets.size(); ++i) {
        auto const inner_start = ap.offsets[i];
        auto const inner_end =
            (i + 1 < ap.offsets.size()) ? ap.offsets[i + 1] : part_size;
        if (inner_end > inner_start) {
          emit_ring<osmium::builder::InnerRingBuilder>(
              ab, std::span<osm::node_ref const>{
                      ap.area_part.data() + inner_start,
                      static_cast<std::size_t>(inner_end - inner_start)});
        }
      }
    }
  }
  buf.commit();
  return buf.get<osmium::Area>(0);
}

struct fiber_state {
  feature_handler fh;
  osmium::memory::Buffer buf{4096U, osmium::memory::Buffer::auto_grow::yes};
};

}  // namespace

void load_osm(tile_db_handle& db_handle, shard_pool& pool,
              std::string const& osm_fname, std::string const& osm_profile,
              std::string const& tmp_dname, size_t flush_threshold) {
  auto r = osm::raw_reader{
      .file_ = cista::mmap{osm_fname.c_str(), cista::mmap::protection::READ}};

  // Both passes read the PBF strictly forward: hint the kernel to ramp up
  // readahead and evict pages behind the cursor. On large inputs (planet
  // ≈ 90 GB) this also frees RAM for the `dat_` index.
  ::madvise(const_cast<std::uint8_t*>(r.file_.data()), r.file_.size(),
            MADV_SEQUENTIAL);

  progress_tracker reader_progress;

  // Hybrid (delta-compressed) node index in two unnamed (O_TMPFILE) files
  // under `tmp_dname`. The kernel reclaims them on close — no path on
  // disk, no zero-fill on grow.
  auto node_idx = osm::hybrid_node_idx{
      cista::mmap{tmp_dname.c_str(), cista::mmap::protection::TMPFILE},
      cista::mmap{tmp_dname.c_str(), cista::mmap::protection::TMPFILE}};
  auto node_idx_merger = osm::hybrid_block_merger{node_idx};

  auto mp_manager = osm::polygon_manager{/*assemble_way_polygons=*/true};

  auto layer_names = layer_names_builder{};
  auto metadata = shared_metadata_builder{flush_threshold};

  // -------- Pass 1 --------
  // Workers run a per-block `hybrid_block_encoder` (varint encoding +
  // delta compression of node positions) entirely in parallel. The collect
  // step calls `merger.merge` on resulting blocks in input order; that
  // step is just a per-block memcpy + a few `idx_` pushes + one inter-block
  // empty-span varint, so the brief mutex around it isn't a bottleneck.
  auto blocks = std::vector<osm::buf>{};
  while (auto b = r.read()) {
    blocks.emplace_back(*b);
  }

  struct pass1_local {
    osm::inflate decompressor;
    std::string out;
    std::vector<std::string_view> strings;
  };

  auto n_ways = std::atomic_uint64_t{0U};

  reader_progress->status("Load OSM / Pass 1")
      .out_bounds(20.F, 45.F)
      .in_high(blocks.size());

  utl::parallel_ordered_collect_threadlocal<pass1_local>(
      blocks.size(),
      [&](pass1_local& local, std::size_t const i) -> osm::hybrid_block {
        auto const& b = blocks[i];
        local.out.resize(b.raw_size_);
        local.decompressor.decompress(b.compressed_, local.out);
        auto enc = osm::hybrid_block_encoder{};
        osm::decode_primitive(
            local.out, local.strings,
            /*read_nodes=*/true, /*read_ways=*/true, /*read_relations=*/true,
            [&](std::int64_t const id, geo::latlng const& pos, auto&&) {
              enc.node(osm::node{id, geo::fixed_latlng::from_latlng(pos)});
            },
            [&](std::int64_t, auto&&, auto&&) { ++n_ways; },
            [&](std::int64_t const id, auto&& members, auto&& tags) {
              mp_manager.save_ways_of_relation(id, members, tags);
            });
        return std::move(enc).finish();
      },
      [&](std::size_t /*idx*/, osm::hybrid_block&& block) {
        node_idx_merger.merge(std::move(block));
      },
      reader_progress->update_fn());

  node_idx_merger.finish();
  mp_manager.reserve_way_map(n_ways.load());
  r.reset();

  // -------- Pass 2 --------
  // Reset `in_` so the monotonic update tracks pass 2's 0..file_size bytes
  // (otherwise it stays pinned at file_size from pass 1).
  reader_progress->update(0);
  reader_progress->status("Load OSM / Pass 2")
      .out_bounds(45.F, 70.F)
      .in_high(r.rest_.size());
  auto ways_processed = std::atomic_uint64_t{0U};
  auto ways_done = false;
  auto rel_mtx = bf::mutex{};
  auto rel_cv = bf::condition_variable{};
  auto const total_ways = n_ways.load();

  // Each worker fiber lazily acquires its own shard from the pool on first
  // call (mutex held briefly during push_back). After that, all inserts go
  // to that shard with no synchronization.
  osm::parse_osm(
      r,
      [&] {
        return fiber_state{.fh = feature_handler{osm_profile, pool.acquire(),
                                                 layer_names, metadata}};
      },
      [&](fiber_state& fs, std::int64_t const id, geo::latlng const& pos,
          auto&& tags) { fs.fh.node(build_node(fs.buf, id, pos, tags)); },
      osm::way_handler{[&](fiber_state& fs, osm::way&& w, auto&& tags) {
        osm::update_locations_of_way(node_idx, w);
        fs.fh.way(build_way(fs.buf, w, tags));
        if (auto a = mp_manager.save_ways(std::move(w), tags); a && a->valid) {
          fs.fh.area(build_area(fs.buf, *a, tags));
        }
        if (++ways_processed == total_ways) {
          auto lock = std::lock_guard{rel_mtx};
          ways_done = true;
          rel_cv.notify_all();
        }
      }},
      [&](fiber_state& fs, std::int64_t const id, auto&& members, auto&& tags) {
        {
          auto lock = std::unique_lock{rel_mtx};
          rel_cv.wait(lock, [&] { return ways_done; });
        }
        auto const a = mp_manager.assemble_area(id, members, tags);
        if (a.valid) {
          fs.fh.area(build_area(fs.buf, a, tags));
        }
      },
      reader_progress->update_fn(),
      /*n_threads=*/std::thread::hardware_concurrency(),
      /*n_fibers=*/std::thread::hardware_concurrency());

  // -------- Persist --------
  auto txn = db_handle.make_txn();
  layer_names.store(db_handle, txn);
  metadata.store(db_handle, txn);
  txn.commit();
}

}  // namespace tiles
