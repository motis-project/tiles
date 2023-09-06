#pragma once

#include <cstdio>
#include <string>

#include "boost/filesystem.hpp"

#include "utl/verify.h"

namespace tiles {

struct tmp_file {
  explicit tmp_file(std::string path)
      : path_{std::move(path)},
#ifdef _MSC_VER
        file_(std::fopen(path_.c_str(), "wb+"))
#else
        file_(std::fopen(path_.c_str(), "wb+e"))
#endif
  {
    utl::verify(file_ != nullptr, "tmp_file: unable to open file {}", path_);
  }

  ~tmp_file() {
    if (file_ != nullptr) {
      std::fclose(file_);
      boost::filesystem::remove(path_);
    }
    file_ = nullptr;
  }

  tmp_file(tmp_file const&) = default;
  tmp_file(tmp_file&&) = default;
  tmp_file& operator=(tmp_file const&) = default;
  tmp_file& operator=(tmp_file&&) = default;

  int fileno() const { return ::fileno(file_); }

  std::string path_;
  FILE* file_;
};

}  // namespace tiles