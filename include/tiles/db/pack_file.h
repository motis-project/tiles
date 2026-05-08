#pragma once

#include <sys/mman.h>
#include <cstring>
#include <filesystem>
#include <vector>

#include "cista/mmap.h"

#include "tiles/bin_utils.h"

#include "utl/verify.h"

namespace tiles {

struct pack_record {
  pack_record() = default;
  pack_record(size_t offset, size_t size) : offset_{offset}, size_{size} {}

  friend bool operator==(pack_record const& lhs, pack_record const& rhs) {
    return std::tie(lhs.offset_, lhs.size_) == std::tie(rhs.offset_, rhs.size_);
  }

  friend bool operator<(pack_record const& lhs, pack_record const& rhs) {
    return std::tie(lhs.offset_, lhs.size_) < std::tie(rhs.offset_, rhs.size_);
  }

  [[nodiscard]] size_t end_offset() const { return offset_ + size_; }

  size_t offset_, size_;
};

inline std::string pack_records_serialize(pack_record record) {
  std::string buf;
  append(buf, record);
  return buf;
}

inline std::string pack_records_serialize(
    std::vector<pack_record> const& records) {
  std::string buf;
  buf.reserve(records.size() * sizeof(pack_record));
  for (auto const& record : records) {
    append(buf, record);
  }
  return buf;
}

inline void pack_records_update(std::string& dat, pack_record record) {
  utl::verify(dat.size() % sizeof(pack_record) == 0,
              "pack_records_update: invalid pack_record count");
  append(dat, record);
}

inline std::vector<pack_record> pack_records_deserialize(std::string_view dat) {
  utl::verify(dat.size() % sizeof(pack_record) == 0,
              "pack_records_deserialize: invalid pack_record count");
  auto const size = dat.size() / sizeof(pack_record);

  std::vector<pack_record> vec;
  vec.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    vec.push_back(read_nth<pack_record>(dat.data(), i));
  }
  return vec;
}

template <typename Fn>
inline void pack_records_foreach(std::string_view dat, Fn&& fn) {
  utl::verify(dat.size() % sizeof(pack_record) == 0,
              "pack_records_foreach: invalid pack_record count");
  for (size_t i = 0; i < dat.size() / sizeof(pack_record); ++i) {
    fn(read_nth<pack_record>(dat.data(), i));
  }
}

inline std::string pack_file_name(char const* db_fname) {
  std::string fname{db_fname};
  if (fname.size() >= 4 && fname.substr(fname.size() - 4) == ".mdb") {
    fname.replace(fname.size() - 4, 4, ".pck");
  } else {
    fname.append(".pck");
  }
  return fname;
}

inline void clear_pack_file(char const* db_fname) {
  auto const fname = pack_file_name(db_fname);
  // WRITE protection (= "w+") truncates to empty; the dtor closes the file.
  cista::mmap{fname.c_str(), cista::mmap::protection::WRITE};
}

struct pack_handle {
  explicit pack_handle(char const* db_fname) {
    auto const fname = pack_file_name(db_fname);
    auto const prot = std::filesystem::exists(fname)
                          ? cista::mmap::protection::MODIFY
                          : cista::mmap::protection::WRITE;
    dat_ = cista::mmap{fname.c_str(), prot};
  }

  ~pack_handle() {
    // suffix must not be '\0' (= default value for char)
    if (dat_.size() == 0U ||
        static_cast<char>(dat_[dat_.size() - 1U]) == '\0') {
      grow_to(dat_.size() + 1U);
      dat_[dat_.size() - 1U] = static_cast<std::uint8_t>('a');
    }
  }

  pack_handle(pack_handle const&) = delete;
  pack_handle(pack_handle&&) noexcept = default;
  pack_handle& operator=(pack_handle const&) = delete;
  pack_handle& operator=(pack_handle&&) noexcept = default;

  [[nodiscard]] size_t size() const noexcept { return dat_.size(); }
  // cista::mmap doesn't expose its power-of-two-rounded internal allocation;
  // diagnostic logging in `repack_features` just wants something to compare
  // against.
  [[nodiscard]] size_t capacity() const noexcept { return dat_.size(); }

  void resize(size_t new_size) { grow_to(new_size); }

  std::string_view get(pack_record record) const {
    utl::verify(record.offset_ < dat_.size() &&
                    record.offset_ + record.size_ <= dat_.size(),
                "pack_file: record not file [size={},record=({},{})",
                dat_.size(), record.offset_, record.size_);
    return std::string_view{
        reinterpret_cast<char const*>(dat_.data()) + record.offset_,
        record.size_};
  }

  pack_record move(size_t offset, pack_record from_record) {
    pack_record to_record{offset, from_record.size_};
    grow_to(to_record.offset_ + to_record.size_);
    std::memmove(dat_.data() + to_record.offset_,
                 dat_.data() + from_record.offset_, to_record.size_);
    return to_record;
  }

  pack_record insert(size_t offset, std::string_view dat) {
    pack_record record{offset, dat.size()};
    grow_to(record.offset_ + record.size_);
    std::memcpy(dat_.data() + offset, dat.data(), dat.size());
    return record;
  }

  pack_record append(std::string_view dat) { return insert(dat_.size(), dat); }

  // Grow to at least `new_size` bytes and prefault the new range. Without
  // the prefault, each subsequent `memcpy` in `insert()` faults pages in
  // one at a time — a hot-path stall on every flush. `MADV_POPULATE_WRITE`
  // (Linux ≥ 5.14) does the same allocation work in one batched syscall.
  // Best-effort: older kernels silently fall back to per-page faults.
  void grow_to(size_t new_size) {
    if (new_size <= dat_.size()) {
      return;
    }
    auto const old_size = dat_.size();
    dat_.resize(new_size);
#ifdef MADV_POPULATE_WRITE
    ::madvise(dat_.data() + old_size, new_size - old_size, MADV_POPULATE_WRITE);
#endif
  }

  cista::mmap dat_;
};

}  // namespace tiles