#pragma once

#include <optional>

#include "utl/helpers/algorithm.h"
#include "utl/parser/split.h"

#include "geo/tile.h"

#include "tiles/util.h"

namespace tiles {

inline std::optional<geo::tile> parse_tile_url(std::string url) {
  std::reverse(begin(url), end(url));
  if (!url.starts_with("tvm.")) {
    return std::nullopt;
  }

  auto const [z_rev, y_rev, x_rev] =
      utl::split<'/', utl::cstr, utl::cstr, utl::cstr>(url);

  for (auto const rev : {z_rev, y_rev, x_rev}) {
    utl::verify(
        rev.valid() &&
            utl::all_of(rev, [](char const c) { return std::isdigit(c) != 0; }),
        "invalid url");
  }

  std::reverse(const_cast<char*>(x_rev.str),
               const_cast<char*>(x_rev.str + x_rev.len));
  std::reverse(const_cast<char*>(y_rev.str),
               const_cast<char*>(y_rev.str + y_rev.len));
  std::reverse(const_cast<char*>(z_rev.str),
               const_cast<char*>(z_rev.str + z_rev.len));

  auto const x = utl::parse<std::uint32_t>(x_rev);
  auto const y = utl::parse<std::uint32_t>(y_rev);
  auto const z = utl::parse<std::uint32_t>(z_rev);

  return geo::tile{y, z, x};
}

}  // namespace tiles
