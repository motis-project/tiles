#include "tiles/db/feature_shard.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <cstring>
#include <algorithm>
#include <utility>

#include "utl/verify.h"

#include "tiles/db/feature_pack.h"
#include "tiles/db/tile_index.h"
#include "tiles/feature/serialize.h"
#include "tiles/fixed/algo/bounding_box.h"

namespace tiles {

namespace {

cista::mmap make_tmp_mmap(std::filesystem::path const& tmp_dir) {
  return cista::mmap{tmp_dir.generic_string().c_str(),
                     cista::mmap::protection::TMPFILE};
}

}  // namespace

feature_shard::feature_shard(std::filesystem::path const& tmp_dir)
    : pack_{make_tmp_mmap(tmp_dir)}, idx_{make_tmp_mmap(tmp_dir)} {}

feature_shard::~feature_shard() {
  // Final drain. Mirrors `feature_inserter_mt` semantics: any throw here
  // would call std::terminate.
  try {
    flush(0U, 0U);
  } catch (...) {
  }
}

geo::tile feature_shard::insert(feature const& f) {
  auto const box = bounding_box(f.geometry_);
  auto const range = make_tile_range(box);
  utl::verify(range.begin() != range.end(), "shard: no tile for feature");

  auto const value = serialize_feature(f);
  for (auto const& tile : range) {
    insert(tile, value);
  }
  if (cache_size_ > kCacheThresholdUpper) {
    flush();
  }
  return *range.begin();
}

void feature_shard::insert(geo::tile const& tile, std::string const& value) {
  cache_size_ += value.size();
  auto& bucket = cache_[tile_to_key(tile)];
  bucket.mem_size_ += value.size();
  bucket.mem_.push_back(value);
}

void feature_shard::flush(std::size_t const threshold_upper,
                          std::size_t const threshold_lower) {
  if (cache_size_ <= threshold_upper) {
    return;
  }

  // Order buckets by size descending so we drain the biggest first.
  auto buckets = std::vector<std::pair<std::size_t, tile_key_t>>{};
  buckets.reserve(cache_.size());
  for (auto const& [key, b] : cache_) {
    if (b.mem_size_ > 0U) {
      buckets.emplace_back(b.mem_size_, key);
    }
  }
  std::sort(begin(buckets), end(buckets),
            [](auto const& a, auto const& b) { return a.first > b.first; });

  for (auto const& [bucket_size, key] : buckets) {
    if (cache_size_ <= threshold_lower) {
      break;
    }
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      continue;
    }
    auto& bucket = it->second;
    persist_bucket(key_to_tile(key), bucket.mem_, bucket.mem_size_);
    cache_size_ -= bucket.mem_size_;
    bucket.mem_size_ = 0;
    bucket.mem_.clear();
  }
}

void feature_shard::persist_bucket(geo::tile const tile,
                                   std::vector<std::string> const& mem,
                                   std::size_t const /*mem_size*/) {
  auto const blob = pack_features(mem);
  auto const offset = pack_.size();
  grow_pack_to(offset + blob.size());
  std::memcpy(pack_.data() + offset, blob.data(), blob.size());

  auto const idx_off = idx_.size();
  grow_idx_to(idx_off + sizeof(entry));
  auto const e =
      entry{tile_to_key(tile), offset, static_cast<std::uint32_t>(blob.size())};
  std::memcpy(idx_.data() + idx_off, &e, sizeof(entry));
}

void feature_shard::grow_pack_to(std::size_t const new_size) {
  if (new_size <= pack_.size()) {
    return;
  }
  [[maybe_unused]] auto const old_size = pack_.size();
  pack_.resize(new_size);
#ifdef MADV_POPULATE_WRITE
  ::madvise(pack_.data() + old_size, new_size - old_size, MADV_POPULATE_WRITE);
#endif
}

void feature_shard::grow_idx_to(std::size_t const new_size) {
  if (new_size <= idx_.size()) {
    return;
  }
  [[maybe_unused]] auto const old_size = idx_.size();
  idx_.resize(new_size);
#ifdef MADV_POPULATE_WRITE
  ::madvise(idx_.data() + old_size, new_size - old_size, MADV_POPULATE_WRITE);
#endif
}

std::span<feature_shard::entry const> feature_shard::entries() const noexcept {
  return std::span<entry const>{reinterpret_cast<entry const*>(idx_.data()),
                                idx_.size() / sizeof(entry)};
}

std::string_view feature_shard::blob(entry const& e) const noexcept {
  return std::string_view{
      reinterpret_cast<char const*>(pack_.data()) + e.offset_, e.size_};
}

}  // namespace tiles
