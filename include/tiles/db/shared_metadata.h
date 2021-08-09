#pragma once

#include <string>
#include <vector>

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

struct shared_metadata_builder {
  static constexpr auto const kDefaultFlushThreshold =
      sizeof(void*) >= 8 ? 1e7 : 1e5;

  explicit shared_metadata_builder(
      size_t const flush_threshold = kDefaultFlushThreshold)
      : flush_threshold_{flush_threshold} {}

  void update(std::vector<metadata> const& data) {
    queue_.enqueue_bulk(data);

    // TODO(root): flush in other thread
    if (should_flush()) {
      flush();
    }
  }

  bool should_flush() { return queue_.pending_ > flush_threshold_; }

  bool flush(bool force = false) {
    if (!should_flush() && !force) {
      return false;
    }

    std::vector<metadata> buf(flush_threshold_);
    queue_.dequeue_bulk(buf);
    queue_.finish_bulk(buf.size());  // directly (shutdown determined globally)
    if (buf.empty()) {
      return false;
    }

    std::vector<std::pair<metadata, uint64_t>> buf_counts;
    std::sort(begin(buf), end(buf));
    utl::equal_ranges_linear(buf, [&](auto lb, auto ub) {
      buf_counts.emplace_back(*lb, std::distance(lb, ub));
    });

    std::lock_guard<std::mutex> lock{mutex_};
    auto old_size = counts_.size();
    utl::concat(counts_, buf_counts);
    std::inplace_merge(
        begin(counts_), begin(counts_) + old_size, end(counts_),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    utl::equal_ranges_linear(
        counts_,
        [](auto const& a, auto const& b) { return a.first == b.first; },
        [&](auto lb, auto ub) {
          if (std::distance(lb, ub) == 1) {
            return;
          }
          utl::verify(std::distance(lb, ub) == 2,
                      "shared_metadata_builder: unexpected element count");
          lb->second += std::next(lb)->second;
          std::next(lb)->second = 0;
        });
    utl::erase_if(counts_, [](auto const& pair) { return pair.second == 0; });

    return true;
  }

  void store(tile_db_handle& db_handle, lmdb::txn& txn) {
    while (flush(true)) {
    }

    utl::erase_if(counts_, [](auto const& pair) { return pair.second == 1; });
    std::sort(begin(counts_), end(counts_),
              [](auto const& a, auto const& b) { return a.second > b.second; });

    t_log("have {} key/value pairs in shared metadata",
          printable_num(counts_.size()));

    std::string buf;
    protozero::pbf_writer writer{buf};
    for (auto const& meta : counts_) {
      writer.add_string(1, meta.first.key_);
      writer.add_string(1, meta.first.value_);
    }

    auto meta_dbi = db_handle.meta_dbi(txn);
    txn.put(meta_dbi, kMetaKeyFeatureMetaCoding, buf);
  }

  size_t flush_threshold_;

  queue_wrapper<metadata> queue_;

  std::mutex mutex_;
  std::vector<std::pair<metadata, uint64_t>> counts_;
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
      : shared_metadata_decoder(std::move(data)),
        enc_data_(
            utl::to_vec(dec_data_, [i = uint64_t{0}](auto const& sm) mutable {
              return std::make_pair(sm, i++);
            })) {
    std::sort(begin(enc_data_), end(enc_data_));
  }

  std::optional<uint64_t> encode(metadata const& q) const {
    auto const it = std::lower_bound(
        begin(enc_data_), end(enc_data_), q,
        [](auto const& a, auto const& b) { return a.first < b; });
    if (it == end(enc_data_) || it->first != q) {
      return std::nullopt;
    }
    return {it->second};
  }

  std::vector<std::pair<metadata, uint64_t>> enc_data_;
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
