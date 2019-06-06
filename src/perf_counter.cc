#include "tiles/perf_counter.h"

#include <algorithm>
#include <iostream>

#include "fmt/core.h"
#include "fmt/ostream.h"

namespace tiles {

void perf_report_get_tile(perf_counter& pc) {
  auto const format_count = [](auto& os, char const* label, double const n) {
    auto const k = n / 1e3;
    auto const m = n / 1e6;
    auto const g = n / 1e9;
    if (n < 1e3) {
      fmt::print(os, "{:>4}: {:>7}  ", label, n);
    } else if (k < 1e3) {
      fmt::print(os, "{:>4}: {:>7.1f}K ", label, k);
    } else if (m < 1e3) {
      fmt::print(os, "{:>4}: {:>7.1f}M ", label, m);
    } else {
      fmt::print(os, "{:>4}: {:>7.1f}G ", label, g);
    }
  };

  auto const format_dur = [](auto& os, char const* label, double const ns) {
    auto const mys = ns / 1e3;
    auto const ms = ns / 1e6;
    auto const s = ns / 1e9;
    if (ns < 1e3) {
      fmt::print(os, "{:>4}: {:>7.3f}ns ", label, ns);
    } else if (mys < 1e3) {
      fmt::print(os, "{:>4}: {:>7.3f}µs ", label, mys);
    } else if (ms < 1e3) {
      fmt::print(os, "{:>4}: {:>7.3f}ms ", label, ms);
    } else {
      fmt::print(os, "{:>4}: {:>7.3f}s  ", label, s);
    }
  };

  auto const print_counter = [&](auto const& label, auto& m) {
    auto const sum = std::accumulate(begin(m), end(m), 0.);
    std::sort(begin(m), end(m));

    fmt::print(std::cout, "{:<24} > ", label);
    format_count(std::cout, "cnt", m.size());
    format_dur(std::cout, "sum", sum);

    if (m.empty()) {
      std::cout << "\n";
      return;
    }

    format_dur(std::cout, "mean", sum / m.size());
    format_dur(std::cout, "q95", m[m.size() * .95]);
    format_dur(std::cout, "max", m.back());
    std::cout << "\n";
  };

  print_counter("GET: TOTAL", pc.finished_[perf_task::GET_TILE_TOTAL]);
  print_counter("GET: FETCH", pc.finished_[perf_task::GET_TILE_FETCH]);
  print_counter("GET: RENDER", pc.finished_[perf_task::GET_TILE_RENDER]);
  print_counter("GET: COMPRESS", pc.finished_[perf_task::GET_TILE_COMPRESS]);

  print_counter("RNDR: FIND SEASIDE",
                pc.finished_[perf_task::RENDER_TILE_FIND_SEASIDE]);
  print_counter("RNDR: ADD SEASIDE",
                pc.finished_[perf_task::RENDER_TILE_ADD_SEASIDE]);

  print_counter("RNDR: QUERY FEAT",
                pc.finished_[perf_task::RENDER_TILE_QUERY_FEATURE]);
  print_counter("RNDR: ITER FEAT",
                pc.finished_[perf_task::RENDER_TILE_ITER_FEATURE]);
  print_counter("RNDR: DESER FEAT OKAY",
                pc.finished_[perf_task::RENDER_TILE_DESER_FEATURE_OKAY]);
  print_counter("RNDR: DESER FEAT SKIP",
                pc.finished_[perf_task::RENDER_TILE_DESER_FEATURE_SKIP]);
  print_counter("RNDR: ADD FEAT",
                pc.finished_[perf_task::RENDER_TILE_ADD_FEATURE]);
  print_counter("RNDR: FINISH", pc.finished_[perf_task::RENDER_TILE_FINISH]);
}

}  // namespace tiles
