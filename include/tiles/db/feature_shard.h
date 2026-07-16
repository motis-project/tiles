#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "cista/mmap.h"

#include "geo/tile.h"

#include "tiles/db/pack_file.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/feature.h"

namespace tiles {

// Per-thread / per-fiber feature inserter shard.
//
// Single-threaded by construction: no mutex on buckets, no atomic on
// `cache_size_`, no LMDB at all during insertion. The shard buffers features
// per output tile (at `kTileDefaultIndexZoomLvl`) in a hash map; when the
// in-memory cache crosses a threshold, the largest buckets are drained,
// concatenated into a "feature pack" blob, and appended to two private
// memory-mapped files:
//
//   shard.pck : append-only blob bytes (one blob per drained bucket)
//   shard.idx : append-only fixed-size records `(tile_key, off, size)`,
//                where (off, size) point into shard.pck
//
// The destructor performs a final flush so all buckets are written before
// the merge phase reads `entries()` / `blob(...)`.
struct feature_shard {
  struct entry {
    tile_key_t tile_;
    std::uint64_t offset_;
    std::uint32_t size_;
  };

  static constexpr std::size_t kCacheThresholdUpper = 256ULL * 1024 * 1024;
  static constexpr std::size_t kCacheThresholdLower =
      kCacheThresholdUpper / 4 * 3;

  feature_shard(std::filesystem::path const& tmp_dir, std::uint32_t shard_id);
  ~feature_shard();

  feature_shard(feature_shard const&) = delete;
  feature_shard(feature_shard&&) noexcept = default;
  feature_shard& operator=(feature_shard const&) = delete;
  feature_shard& operator=(feature_shard&&) noexcept = default;

  // Insert a fully-built feature. Computes its bbox/tile range, serializes
  // it once, then bucketizes into each tile in the range.
  geo::tile insert(feature const& f);

  // Insert a single (already serialized) feature into one specific tile.
  // Used by callers that route by tile manually.
  void insert(geo::tile const& tile, std::string const& value);

  // Drain large buckets until `cache_size_` is below `threshold_lower`.
  // No-op when `cache_size_ <= threshold_upper`. The dtor calls `flush(0,0)`.
  void flush(std::size_t threshold_upper = kCacheThresholdUpper,
             std::size_t threshold_lower = kCacheThresholdLower);

  // Read access for the merge phase.
  std::span<entry const> entries() const noexcept;
  std::string_view blob(entry const& e) const noexcept;

private:
  void persist_bucket(geo::tile tile, std::vector<std::string> const& mem,
                      std::size_t mem_size);
  void grow_pack_to(std::size_t new_size);
  void grow_idx_to(std::size_t new_size);

  std::uint32_t shard_id_;

  struct bucket_data {
    std::size_t mem_size_{0};
    std::vector<std::string> mem_;
  };
  std::unordered_map<tile_key_t, bucket_data> cache_;
  std::size_t cache_size_{0};

  cista::mmap pack_;  // bytes
  cista::mmap idx_;  // packed `entry` records; size_/sizeof(entry) = count
};

// Lazy shard allocator. Each worker fiber calls `acquire()` once on its
// first insert and keeps the returned reference for its entire lifetime —
// so the mutex is contended at most N times total (one per worker), not per
// feature. Backed by `std::deque` because references to elements must
// remain stable across `push_back`s (workers concurrently hold references
// to their shards while another worker is acquiring a new one).
struct shard_pool {
  explicit shard_pool(std::filesystem::path tmp_dir)
      : tmp_dir_{std::move(tmp_dir)} {}

  shard_pool(shard_pool const&) = delete;
  shard_pool(shard_pool&&) = delete;
  shard_pool& operator=(shard_pool const&) = delete;
  shard_pool& operator=(shard_pool&&) = delete;

  feature_shard& acquire() {
    auto lock = std::lock_guard{mtx_};
    return shards_.emplace_back(tmp_dir_,
                                static_cast<std::uint32_t>(shards_.size()));
  }

  std::deque<feature_shard>& shards() noexcept { return shards_; }
  bool empty() const noexcept { return shards_.empty(); }

private:
  std::filesystem::path tmp_dir_;
  std::mutex mtx_;
  std::deque<feature_shard> shards_;
};

}  // namespace tiles
