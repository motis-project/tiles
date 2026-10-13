#pragma once

#include <string>
#include <vector>

#include "ankerl/cista_adapter.h"

#include "cista/hash.h"

#include "protozero/pbf_reader.hpp"
#include "protozero/pbf_writer.hpp"

#include "utl/concat.h"
#include "utl/equal_ranges_linear.h"
#include "utl/erase_if.h"
#include "utl/to_vec.h"
#include "utl/verify.h"

#include "tiles/db/tile_database.h"
#include "tiles/feature/feature.h"
#include "tiles/util.h"
#include "tiles/util_parallel.h"

namespace tiles {

struct metadata_hash {
  std::size_t operator()(metadata const& m) const noexcept {
    return cista::hash(std::string_view{m.value_},
                       cista::hash(std::string_view{m.key_}));
  }
};

using metadata_counts_t =
    cista::raw::ankerl_map<metadata, uint64_t, metadata_hash>;

struct shared_metadata_builder {
  static constexpr auto const kDefaultFlushThreshold =
      sizeof(void*) >= 8 ? 1e7 : 1e5;

  explicit shared_metadata_builder(size_t = kDefaultFlushThreshold) {}

  void merge(metadata_counts_t const& counts) {
    std::lock_guard<std::mutex> lock{mutex_};
    for (auto const& [m, c] : counts) {
      counts_[m] += c;
    }
  }

  void store(tile_db_handle& db_handle, lmdb::txn& txn) {
    // Drop pairs seen only once, then order by frequency (descending). The old
    // builder held `counts_` sorted by metadata and applied an unstable
    // count-descending sort on top; sorting the extracted entries by metadata
    // first reproduces that exact input, so the coding stays byte-identical.
    std::vector<std::pair<metadata, uint64_t>> counts;
    counts.reserve(counts_.size());
    for (auto const& [m, c] : counts_) {
      if (c != 1) {
        counts.emplace_back(m, c);
      }
    }
    std::sort(begin(counts), end(counts),
              [](auto const& a, auto const& b) { return a.first < b.first; });
    std::sort(begin(counts), end(counts),
              [](auto const& a, auto const& b) { return a.second > b.second; });

    t_log("have {} key/value pairs in shared metadata",
          printable_num(counts.size()));

    std::string buf;
    protozero::pbf_writer writer{buf};
    for (auto const& meta : counts) {
      writer.add_string(1, meta.first.key_);
      writer.add_string(1, meta.first.value_);
    }

    auto meta_dbi = db_handle.meta_dbi(txn);
    txn.put(meta_dbi, kMetaKeyFeatureMetaCoding, buf);
  }

  std::mutex mutex_;
  metadata_counts_t counts_;
};

// The counts of one feature handler (thread or fiber): no lock and no copy of
// the metadata per feature. Added to the shared counts when destroyed.
struct shared_metadata_counts {
  explicit shared_metadata_counts(shared_metadata_builder& b) : builder_{b} {}
  shared_metadata_counts(shared_metadata_counts const&) = delete;
  shared_metadata_counts(shared_metadata_counts&&) = delete;
  shared_metadata_counts& operator=(shared_metadata_counts const&) = delete;
  shared_metadata_counts& operator=(shared_metadata_counts&&) = delete;
  ~shared_metadata_counts() { builder_.merge(counts_); }

  void update(std::vector<metadata> const& data) {
    for (auto const& m : data) {
      ++counts_[m];  // copies m only for its first occurrence
    }
  }

  shared_metadata_builder& builder_;
  metadata_counts_t counts_;
};

struct shared_metadata_decoder {
  shared_metadata_decoder() = default;

  explicit shared_metadata_decoder(std::vector<metadata> data)
      : dec_data_{std::move(data)} {}

  metadata const& decode(uint64_t id) const { return dec_data_.at(id); }

  std::vector<metadata> dec_data_;
};

struct shared_metadata_coder : public shared_metadata_decoder {
  shared_metadata_coder() = default;

  explicit shared_metadata_coder(std::vector<metadata> data)
      : shared_metadata_decoder(std::move(data)) {
    enc_map_.reserve(dec_data_.size());
    for (auto i = uint64_t{0}; i != dec_data_.size(); ++i) {
      enc_map_.emplace(dec_data_[i], i);
    }
  }

  std::optional<uint64_t> encode(metadata const& q) const {
    auto const it = enc_map_.find(q);
    if (it == enc_map_.end()) {
      return std::nullopt;
    }
    return {it->second};
  }

  cista::raw::ankerl_map<metadata, uint64_t, metadata_hash> enc_map_;
};

inline std::vector<metadata> load_shared_metadata(tile_db_handle& db_handle,
                                                  lmdb::txn& txn) {
  auto meta_dbi = db_handle.meta_dbi(txn);
  auto const opt = txn.get(meta_dbi, kMetaKeyFeatureMetaCoding);
  if (!opt) {
    return {};
  }

  std::vector<metadata> vec;
  protozero::pbf_reader reader{*opt};
  while (reader.next()) {
    auto key = reader.get_string();
    utl::verify(reader.next(), "invalid meta coding");
    auto value = reader.get_string();

    vec.emplace_back(std::move(key), std::move(value));
  }
  return vec;
}

inline shared_metadata_decoder make_shared_metadata_decoder(
    tile_db_handle& db_handle, lmdb::txn& txn) {
  return shared_metadata_decoder{load_shared_metadata(db_handle, txn)};
}

inline shared_metadata_coder make_shared_metadata_coder(
    tile_db_handle& db_handle) {
  auto txn = db_handle.make_txn();
  return shared_metadata_coder{load_shared_metadata(db_handle, txn)};
}

}  // namespace tiles
