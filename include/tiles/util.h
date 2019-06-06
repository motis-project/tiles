#pragma once

#include <time.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

#include "fmt/core.h"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "utl/verify.h"

namespace tiles {
template <typename... Args>
inline void t_log(Args&&... args) {
  using clock = std::chrono::system_clock;
  auto const now = clock::to_time_t(clock::now());
  struct tm tmp;
#if _MSC_VER >= 1400
  gmtime_s(&tmp, &now);
#else
  gmtime_r(&now, &tmp);
#endif
  std::cout << std::put_time(&tmp, "%FT%TZ") << " | ";
  fmt::print(std::cout, std::forward<Args>(args)...);
  std::cout << std::endl;
}

std::string compress_deflate(std::string const&);

template <typename Fun>
struct raii_helper {
  raii_helper(Fun&& fun) : fun_{std::move(fun)} {}
  ~raii_helper() { fun_(); }

  Fun fun_;
};

struct scoped_timer final {
  explicit scoped_timer(std::string label)
      : label_{std::move(label)}, start_{std::chrono::steady_clock::now()} {
    std::cout << "|> start: " << label_ << "\n";
  }

  ~scoped_timer() {
    using namespace std::chrono;

    auto const now = steady_clock::now();
    double dur = duration_cast<microseconds>(now - start_).count() / 1000.0;

    std::cout << "|> done: " << label_ << " (";
    if (dur < 1000) {
      std::cout << std::setw(6) << std::setprecision(4) << dur << "ms";
    } else {
      dur /= 1000;
      std::cout << std::setw(6) << std::setprecision(4) << dur << "s";
    }
    std::cout << ")" << std::endl;
  }

  std::string label_;
  std::chrono::time_point<std::chrono::steady_clock> start_;
};

struct printable_num {
  explicit printable_num(double n) : n_{n} {}
  explicit printable_num(size_t n) : n_{static_cast<double>(n)} {}
  double n_;
};

struct printable_bytes {
  explicit printable_bytes(double n) : n_{n} {}
  explicit printable_bytes(size_t n) : n_{static_cast<double>(n)} {}
  double n_;
};

}  // namespace tiles

namespace fmt {

template <>
struct formatter<tiles::printable_num> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(tiles::printable_num const& num, FormatContext& ctx) {
    auto const n = num.n_;
    auto const k = n / 1e3;
    auto const m = n / 1e6;
    auto const g = n / 1e9;
    if (n < 1e3) {
      return format_to(ctx.begin(), "{:>6}  ", n);
    } else if (k < 1e3) {
      return format_to(ctx.begin(), "{:>6.1f}K ", k);
    } else if (m < 1e3) {
      return format_to(ctx.begin(), "{:>6.1f}M ", m);
    } else {
      return format_to(ctx.begin(), "{:>6.1f}G ", g);
    }
  }
};

template <>
struct formatter<tiles::printable_bytes> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(tiles::printable_bytes const& bytes, FormatContext& ctx) {
    auto const n = bytes.n_;
    auto const k = n / 1024;
    auto const m = n / (1024 * 1024);
    auto const g = n / (1024 * 1024 * 1024);
    if (n < 1024) {
      return format_to(ctx.begin(), "{:>7.2f}B  ", n);
    } else if (k < 1024) {
      return format_to(ctx.begin(), "{:>7.2f}KB ", k);
    } else if (m < 1024) {
      return format_to(ctx.begin(), "{:>7.2f}MB ", m);
    } else {
      return format_to(ctx.begin(), "{:>7.2f}GB ", g);
    }
  }
};

}  // namespace fmt
