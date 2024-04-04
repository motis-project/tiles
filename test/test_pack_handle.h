#pragma once

#include <set>

#include "tiles/db/pack_file.h"

namespace tiles {

struct test_pack_handle {
  [[nodiscard]] size_t size() const { return size_; }
  [[nodiscard]] size_t capacity() const { return capacity_; }

  void resize(size_t const new_size) {
    if (!records_.empty()) {
      auto it = records_.lower_bound(pack_record{new_size, 0ULL});
      if (it == end(records_)) {
        --it;
      }
      utl::verify(it->offset_ + it->size_ <= new_size,
                  "test_pack_handle.resize: new_size to small {} > {}",
                  (it->offset_ + it->size_), new_size);
    }

    size_ = new_size;
    capacity_ = std::max(capacity_, new_size);
  }

  std::string_view get(pack_record record) {
    utl::verify(
        record.offset_ < size_ && record.offset_ + record.size_ <= size_,
        "pack_file: record not file [size={},record=({},{})", size_,
        record.offset_, record.size_);

    utl::verify(record.size_ <= size_, "test_pack_handle: request to big");

    check_extract(record);
    return std::string_view{"nullptr", record.size_};
  }

  pack_record move(size_t offset, pack_record from_record) {
    pack_record to_record{offset, from_record.size_};
    resize(std::max(size_, to_record.offset_ + to_record.size_));

    check_extract(from_record);
    check_insert(to_record);

    return to_record;
  }

  pack_record insert(size_t offset, std::string_view dat) {
    pack_record record{offset, dat.size()};
    resize(std::max(size_, record.end_offset()));
    check_insert(record);
    return record;
  }

  pack_record append(std::string_view dat) { return insert(size_, dat); }

  pack_record append(size_t dat_size) {
    pack_record record{size_, dat_size};
    resize(std::max(size_, record.offset_ + record.size_));
    check_insert(record);
    return record;
  }

  void check_extract(pack_record const record) {
    auto it = records_.find(record);
    utl::verify(it != end(records_), "test_pack_handle.move: unknown record");
    records_.erase(it);
  }

  void check_insert(pack_record const record) {
    if (!records_.empty()) {
      auto it = records_.lower_bound(record);

      // check previous entry, if exists
      if (it != begin(records_)) {
        utl::verify(std::prev(it)->end_offset() <= record.offset_,
                    "test_pack_handle.insert : prev_end > offset : "
                    "({}, {}, {}) > ({}, {}, {})",
                    std::prev(it)->offset_, std::prev(it)->size_,
                    std::prev(it)->end_offset(), record.offset_, record.size_,
                    record.end_offset());
      }

      // check this (maybe succ), if exists
      if (it != end(records_)) {
        utl::verify(record.end_offset() <= it->offset_,
                    "test_pack_handle.insert : end_offset > offset : {} > {}",
                    record.end_offset(), it->offset_);
      }
    }

    records_.insert(record);
  }

  size_t size_{0};
  size_t capacity_{0};
  std::set<pack_record> records_;
};

}  // namespace tiles
