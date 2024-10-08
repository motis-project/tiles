#pragma once

#include <charconv>
#include <chrono>
#include <ctime>
#include <atomic>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "fmt/ostream.h"

#include "utl/progress_tracker.h"
#include "utl/verify.h"

namespace tiles {
template <typename... Args>
inline void t_log(fmt::format_string<Args...> fmt_str, Args&&... args) {
  using clock = std::chrono::system_clock;
  auto const now = clock::to_time_t(clock::now());
  struct tm tmp{};
#if _MSC_VER >= 1400
  gmtime_s(&tmp, &now);
#else
  gmtime_r(&now, &tmp);
#endif
  std::clog << std::put_time(&tmp, "%FT%TZ") << " | ";
  fmt::print(std::clog, fmt_str, std::forward<Args>(args)...);
  std::clog << std::endl;
}

std::string compress_deflate(std::string const&);

struct progress_tracker {
#ifdef TILES_GLOBAL_PROGRESS_TRACKER
  progress_tracker() : ptr_{utl::get_active_progress_tracker()} {}
#else
  progress_tracker()
      : ptr_{std::make_shared<utl::progress_tracker>([](auto const& t) {
          t_log("{} : {:>3}%", t.status_, t.out_.load());
        })} {}
#endif

  utl::progress_tracker* operator->() const { return ptr_.get(); }

  utl::progress_tracker_ptr ptr_;
};

struct scoped_timer final {
  explicit scoped_timer(std::string label)
      : label_{std::move(label)}, start_{std::chrono::steady_clock::now()} {
    std::clog << "|> start: " << label_ << "\n";
  }

  ~scoped_timer() {
    using namespace std::chrono;

    auto const now = steady_clock::now();
    double dur =
        static_cast<double>(duration_cast<microseconds>(now - start_).count()) /
        1000.0;

    std::clog << "|> done: " << label_ << " (";
    if (dur < 1000) {
      std::clog << std::setw(6) << std::setprecision(4) << dur << "ms";
    } else {
      dur /= 1000;
      std::clog << std::setw(6) << std::setprecision(4) << dur << "s";
    }
    std::clog << ")" << std::endl;
  }

  scoped_timer(scoped_timer const&) = delete;
  scoped_timer(scoped_timer&&) = delete;
  scoped_timer& operator=(scoped_timer const&) = delete;
  scoped_timer& operator=(scoped_timer&&) = delete;

  std::string label_;
  std::chrono::time_point<std::chrono::steady_clock> start_;
};

template <typename Container, typename Fn>
void transform_erase(Container& c, Fn&& fn) {
  if (c.empty()) {
    return;
  }

  auto it = std::begin(c);
  fn(*it);

  for (auto it2 = std::next(it); it2 != std::end(c); ++it2) {
    fn(*it2);
    if (!(*it == *it2)) {
      *++it = std::move(*it2);
    }
  }

  c.erase(++it, std::end(c));
}

struct printable_num {
  template <typename T>
  // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
  explicit printable_num(T&& t) : n_{static_cast<double>(t)} {}
  double n_;
};

struct printable_ns {
  template <typename T>
  // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
  explicit printable_ns(T&& t) : n_{static_cast<double>(t)} {}
  double n_;
};

struct printable_bytes {
  template <typename T>
  // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
  explicit printable_bytes(T&& t) : n_{static_cast<double>(t)} {}
  double n_;
};

}  // namespace tiles

namespace fmt {

template <>
struct formatter<tiles::printable_num> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(tiles::printable_num const& num, FormatContext& ctx) const {
    auto const n = num.n_;
    auto const k = n / 1e3;
    auto const m = n / 1e6;
    auto const g = n / 1e9;
    if (n < 1e3) {
      return format_to(ctx.out(), "{:>6} ", n);
    } else if (k < 1e3) {
      return format_to(ctx.out(), "{:>6.1f}K", k);
    } else if (m < 1e3) {
      return format_to(ctx.out(), "{:>6.1f}M", m);
    } else {
      return format_to(ctx.out(), "{:>6.1f}G", g);
    }
  }
};

template <>
struct formatter<tiles::printable_ns> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(tiles::printable_ns const& num, FormatContext& ctx) const {
    auto const ns = num.n_;
    auto const mys = ns / 1e3;
    auto const ms = ns / 1e6;
    auto const s = ns / 1e9;
    if (ns < 1e3) {
      return format_to(ctx.out(), "{:>7.3f}ns", ns);
    } else if (mys < 1e3) {
      return format_to(ctx.out(), "{:>7.3f}µs", mys);
    } else if (ms < 1e3) {
      return format_to(ctx.out(), "{:>7.3f}ms", ms);
    } else {
      return format_to(ctx.out(), "{:>7.3f}s ", s);
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
  auto format(tiles::printable_bytes const& bytes, FormatContext& ctx) const {
    auto const n = bytes.n_;
    auto const k = n / 1024;
    auto const m = n / (1024 * 1024);
    auto const g = n / (1024 * 1024 * 1024);
    if (n < 1024) {
      return format_to(ctx.out(), "{:>7.2f}B ", n);
    } else if (k < 1024) {
      return format_to(ctx.out(), "{:>7.2f}KB", k);
    } else if (m < 1024) {
      return format_to(ctx.out(), "{:>7.2f}MB", m);
    } else {
      return format_to(ctx.out(), "{:>7.2f}GB", g);
    }
  }
};

}  // namespace fmt

namespace tiles {

inline uint32_t stou(std::string_view sv) {
  uint32_t var = 0;
  auto result = std::from_chars(sv.data(), sv.data() + sv.size(), var);
  utl::verify(result.ec == std::errc(), "cannot convert to uint32_t: {}", sv);
  return var;
}

struct regex_matcher {
  using match_result_t = std::optional<std::vector<std::string_view>>;

  explicit regex_matcher(std::string const& pattern);
  ~regex_matcher();

  regex_matcher(regex_matcher const&) = delete;
  regex_matcher(regex_matcher&&) noexcept = default;
  regex_matcher& operator=(regex_matcher const&) = delete;
  regex_matcher& operator=(regex_matcher&&) noexcept = default;

  match_result_t match(std::string_view) const;

  struct impl;
  std::unique_ptr<impl> impl_;
};

}  // namespace tiles
