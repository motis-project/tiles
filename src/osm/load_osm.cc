#include "tiles/osm/load_osm.h"

#include <filesystem>

#include "utl/verify.h"

#include "oneapi/tbb/parallel_pipeline.h"

#include "osmium/area/assembler.hpp"
#include "osmium/area/multipolygon_manager.hpp"
#include "osmium/io/pbf_input.hpp"
#include "osmium/io/reader_with_progress_bar.hpp"
#include "osmium/io/xml_input.hpp"
#include "osmium/memory/buffer.hpp"
#include "osmium/visitor.hpp"

#include "tiles/db/layer_names.h"
#include "tiles/db/shared_metadata.h"
#include "tiles/db/tile_database.h"
#include "tiles/osm/feature_handler.h"
#include "tiles/osm/hybrid_node_idx.h"
#include "tiles/osm/tmp_file.h"
#include "tiles/util.h"
#include "tiles/util_parallel.h"

namespace tiles {

namespace o = osmium;
namespace oa = osmium::area;
namespace oio = osmium::io;
namespace orel = osmium::relations;
namespace om = osmium::memory;
namespace oeb = osmium::osm_entity_bits;

void load_osm(tile_db_handle& db_handle, feature_inserter_mt& inserter,
              std::string const& osm_fname, std::string const& osm_profile,
              std::string const& tmp_dname, size_t flush_threshold) {
  oio::File input_file;
  size_t file_size{0};
  try {
    input_file = oio::File{osm_fname};
    file_size = oio::Reader{input_file}.file_size();
  } catch (...) {
    t_log("load_osm failed [file={}]", osm_fname);
    throw;
  }

  progress_tracker reader_progress;
  reader_progress->status("Load OSM").out_mod(3.F).in_high(2 * file_size);

  oa::MultipolygonManager<oa::Assembler> mp_manager{
      oa::Assembler::config_type{}};

  auto const node_idx_file =
      tmp_file{(std::filesystem::path{tmp_dname} / "idx.bin").generic_string()};
  auto const node_dat_file =
      tmp_file{(std::filesystem::path{tmp_dname} / "dat.bin").generic_string()};
  hybrid_node_idx node_idx{node_idx_file.fileno(), node_dat_file.fileno()};

  {
    reader_progress->status("Load OSM / Pass 1");
    hybrid_node_idx_builder node_idx_builder{node_idx};

    oio::Reader reader{input_file, oeb::node | oeb::relation};
    while (auto buffer = reader.read()) {
      reader_progress->update(reader.offset());
      o::apply(buffer, node_idx_builder, mp_manager);
    }
    reader.close();

    mp_manager.prepare_for_lookup();
    t_log("Multipolygon Manager Memory:");
    orel::print_used_memory(std::clog, mp_manager.used_memory());

    node_idx_builder.finish();
    t_log("Hybrid Node Index Statistics:");
    node_idx_builder.dump_stats();
  }

  layer_names_builder names_builder;
  shared_metadata_builder metadata_builder(flush_threshold);

  {
    reader_progress->status("Load OSM / Pass 2");
    auto const thread_count =
        std::max(2, static_cast<int>(std::thread::hardware_concurrency()));

    std::atomic_size_t next_handlers_slot{0};
    std::vector<std::pair<std::thread::id, feature_handler>> handlers;
    handlers.reserve(thread_count);
    for (auto i = 0; i < thread_count; ++i) {
      handlers.emplace_back(std::thread::id{},
                            feature_handler{osm_profile, inserter,
                                            names_builder, metadata_builder});
    }
    auto const get_handler = [&]() -> feature_handler& {
      auto const thread_id = std::this_thread::get_id();
      if (auto it = std::find_if(
              begin(handlers), end(handlers),
              [&](auto const& pair) { return pair.first == thread_id; });
          it != end(handlers)) {
        return it->second;
      }
      auto slot = next_handlers_slot.fetch_add(1);
      utl::verify(slot < handlers.size(), "more threads than expected");
      handlers[slot].first = thread_id;
      return handlers[slot].second;
    };

    auto reader = oio::Reader{input_file};
    oneapi::tbb::parallel_pipeline(
        thread_count * 4U,
        oneapi::tbb::make_filter<void, om::Buffer>(
            oneapi::tbb::filter_mode::serial_in_order,
            [&](oneapi::tbb::flow_control& fc) {
              auto buf = reader.read();
              reader_progress->update(reader.file_size() + reader.offset());
              if (!buf) {
                fc.stop();
              }
              return buf;
            }) &
            oneapi::tbb::make_filter<om::Buffer, om::Buffer>(
                oneapi::tbb::filter_mode::parallel,
                [&](om::Buffer&& buf) {
                  update_locations(node_idx, buf);
                  o::apply(buf, get_handler());
                  return std::move(buf);
                }) &
            oneapi::tbb::make_filter<om::Buffer, void>(
                oneapi::tbb::filter_mode::serial_in_order,
                [&](om::Buffer&& buf) {
                  o::apply(buf, mp_manager.handler([&](auto&& buf) {
                    o::apply(buf, get_handler());
                  }));
                }));

    reader.close();
    reader_progress->update(reader_progress->in_high_);

    t_log("Multipolygon Manager Memory:");
    orel::print_used_memory(std::clog, mp_manager.used_memory());
  }

  {
    auto txn = db_handle.make_txn();
    names_builder.store(db_handle, txn);
    metadata_builder.store(db_handle, txn);
    txn.commit();
  }
}

}  // namespace tiles
