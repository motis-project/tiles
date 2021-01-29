#pragma once

#include <map>
#include <string>
#include <vector>

#include "protozero/varint.hpp"

#include "tiles/bin_utils.h"
#include "tiles/db/quad_tree.h"

// FEATURE PACK "WIRE FORMAT" SPECIFICATION v2.1
//
// A feature pack is intended to hold serialized feature data for features in
// one "bucket" of the toplevel geo index.
//
// Each pack contains n renderable geometry features and m extra segments e.g.
// for index or meta data. Both, n and m may be zero. Each segment has a offset
// as entry point for the consumer (the offset does not necessarily have to
// point to the first byte!) and a type. Type ids < 128 are reserved, >= 128 can
// be used for application specific data.
//
// TYPE ID VALUES:
//    0x0: quad tree index
//
//  The pack starts with the header at offset 0x0.
//
// HEADER LAYOUT:
//  4b : uint32_t : feature count n
//  1b : uint8_t  : segment count m
//  1b : uint8_t  : segment 0 type
//  4b : uint32_t : segment 0 offset
//  1b : uint8_t  : segment i type
//  4b : uint32_t : segment i offset
//    ...
//
// Feature data starts directly afterwards at offset (m + 1) * 5. A
// renderable geography feature consists of a binary string of o > 0 bytes.
// It is prefixed by its size (encoded as varint, prefix does not add to
// size).
//
// There may also be null features which zero-length strings, in this case
// only the zero valued varint is present. Indexing schemes may use these
// null features to delimit associad features.
//
// There must be exactly n geography features and any number of null
// features after the header and before any other segment data (or end of
// file). This guarantees that all features in the pack can be accesses
// though a simple counting loop.
//
// The last four bytes of the feature pack are a crc32 checksum of the entire
// feature pack (obviously excluding the checksum itself).

namespace tiles {

constexpr auto const kQuadTreeFeatureIndexId = 0x0;

struct feature_packer {
  void register_segment(uint8_t const id) {
    utl::verify(buf_.empty(),
                "packer.add_segment: cannot register segment anymore.");
    utl::verify(segment_offsets_.emplace(id, 0U).second,
                "packer.add_segment: duplicate segment id");
    utl::verify(segment_offsets_.size() <= std::numeric_limits<uint8_t>::max(),
                "packer.add_segment: too many segments");
  }

  void finish_header(size_t const feature_count) {
    utl::verify(feature_count <= std::numeric_limits<uint32_t>::max(),
                "packer.finish_header: to many features to serialize");
    tiles::append<uint32_t>(buf_, feature_count);
    tiles::append<uint8_t>(buf_, static_cast<uint8_t>(segment_offsets_.size()));

    for (auto& [id, offset] : segment_offsets_) {
      tiles::append<uint8_t>(buf_, id);
      offset = static_cast<uint32_t>(buf_.size());
      tiles::append<uint32_t>(buf_, 0U);
    }
  }

  void update_segment_offset(uint8_t segment_id, uint32_t const offset) {
    tiles::write(buf_.data(), segment_offsets_.at(segment_id), offset);
  }

  template <typename It>
  uint32_t append_features(It begin, It end) {
    uint32_t offset = buf_.size();
    for (auto it = begin; it != end; ++it) {
      append_feature(*it);
    }
    append_span_end();
    return offset;
  }

  void append_feature(std::string const& feature) {
    utl::verify(feature.size() >= 32, "MINI FEATURE?!");
    protozero::write_varint(std::back_inserter(buf_), feature.size());
    buf_.append(feature.data(), feature.size());
  }

  void append_span_end() {
    protozero::write_varint(std::back_inserter(buf_),
                            0ULL);  // null terminated
  }

  uint32_t append_packed(std::vector<uint32_t> const& vec) {
    uint32_t offset = buf_.size();
    for (auto const& e : vec) {
      protozero::write_varint(std::back_inserter(buf_), e);
    }
    return offset;
  }

  template <typename String>
  uint32_t append(String const& string) {
    uint32_t offset = buf_.size();
    buf_.append(string);
    return offset;
  }

  void finish();

  std::string buf_;
  std::map<uint8_t, uint32_t> segment_offsets_;
};

bool feature_pack_valid(std::string_view);

inline std::optional<uint32_t> find_segment_offset(std::string_view const pack,
                                                   uint8_t const segment_id) {
  std::optional<uint32_t> offset;
  auto const segment_count = read_nth<uint8_t>(pack.data(), 4);
  for (auto i = 0ULL; i < segment_count; ++i) {
    auto const base = (1 + i) * 5;
    if (read<uint8_t>(pack.data(), base) == segment_id) {
      offset = {read<uint32_t>(pack.data(), base + 1)};
    }
  }
  return offset;
}

template <typename Fn>
size_t unpack_features(std::string_view const& string, Fn&& fn) {
  utl::verify(string.size() >= 5, "unpack_features: invalid feature_pack");
  auto const feature_count = read_nth<uint32_t>(string.data(), 0);
  auto const segment_count = read_nth<uint8_t>(string.data(), 4);

  auto const* ptr = string.data() + static_cast<size_t>(segment_count + 1) * 5;
  auto const* const end = string.data() + string.size();
  for (auto i = 0ULL; i < feature_count; ++i) {
    uint64_t size = 0;
    while (size == 0) {  // skip zero elements (= span terminators)
      size = protozero::decode_varint(&ptr, end);
    }
    fn(std::string_view{ptr, size});
    ptr += size;
  }
  return std::distance(string.data(), ptr);
}

template <typename Fn>
void unpack_features(geo::tile const& root, std::string_view const& string,
                     geo::tile const& tile, Fn&& fn) {
  utl::verify(string.size() >= 5, "unpack_features: invalid feature_pack");
  auto const idx_offset = find_segment_offset(string, kQuadTreeFeatureIndexId);
  if (!idx_offset) {
    unpack_features(string, fn);  // no quad tree available, fallback
    return;
  }

  utl::verify(string.size() >= *idx_offset, "invalid feature_pack idx_offset");
  auto const* idx_ptr = string.data() + *idx_offset;
  auto const* const end = string.data() + string.size();
  for (auto z = root.z_; z <= std::max(root.z_, tile.z_); ++z) {
    auto const tree_offset = protozero::decode_varint(&idx_ptr, end);
    if (tree_offset == 0) {
      continue;  // index empty
    }

    walk_quad_tree(
        string.data() + tree_offset, root, tile,
        [&](auto const span_offset, auto const span_count) {
          auto span_ptr = string.data() + span_offset;
          for (auto i = 0ULL; i < span_count; ++i) {
            uint64_t size = 0;
            while ((size = protozero::decode_varint(&span_ptr, end)) != 0) {
              fn(std::string_view{span_ptr, size});
              span_ptr += size;
            }
          }
        });
  }
}

struct tile_db_handle;
struct pack_handle;
struct shared_metadata_coder;

// quick packing (e.g. as part of a insert flush)
std::string pack_features(std::vector<std::string> const&);

// optimal packing (incl. index)
std::string pack_features(geo::tile const&, shared_metadata_coder const&,
                          std::vector<std::string> const&);

// full database packing (e.g. once and optimal)
void pack_features(tile_db_handle&, pack_handle&);

// full database packing (with custom packing function)
void pack_features(
    tile_db_handle&, pack_handle&,
    std::function<std::string(geo::tile, std::vector<std::string> const&)>);

}  // namespace tiles
