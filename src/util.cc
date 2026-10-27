#include "tiles/util.h"

#include <cstdlib>
#include <regex>

#include "zlib.h"

#include "utl/to_vec.h"
#include "utl/verify.h"

namespace tiles {

std::string compress_deflate(std::string const& input) {
  auto out_size = compressBound(input.size());
  std::string buffer(out_size, '\0');

  auto error = compress2(reinterpret_cast<uint8_t*>(buffer.data()), &out_size,
                         reinterpret_cast<uint8_t const*>(input.data()),
                         input.size(), Z_BEST_COMPRESSION);
  utl::verify(error == 0, "compress_deflate failed");

  buffer.resize(out_size);
  return buffer;
}

std::string compress_gzip(std::string const& input) {
  /**
  Same as compress2, but with MAX_WBITS + 16 to indicate gzip header and trailer
  */
  static int const level = [] {
    auto const* e = std::getenv("TILES_GZIP_LEVEL");
    return e != nullptr ? std::atoi(e) : Z_BEST_COMPRESSION;
  }();

  // Reuse one deflate state per thread (deflateInit2 allocates ~256 KB of
  // window/hash tables; doing it per tile is pure churn). deflateReset keeps
  // the allocation and just rewinds the stream.
  static thread_local z_stream* zs = [] {
    auto* s = new z_stream{};
    utl::verify(deflateInit2(s, level, Z_DEFLATED, MAX_WBITS + 16, 8,
                             Z_DEFAULT_STRATEGY) == Z_OK,
                "deflateInit2 failed");
    return s;
  }();
  utl::verify(deflateReset(zs) == Z_OK, "deflateReset failed");

  auto out_size = deflateBound(zs, input.size());
  std::string buffer(out_size, '\0');

  zs->next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(input.data()));
  zs->avail_in = input.size();

  zs->next_out = reinterpret_cast<uint8_t*>(buffer.data());
  zs->avail_out = buffer.size();

  utl::verify(deflate(zs, Z_FINISH) == Z_STREAM_END, "deflate failed");

  buffer.resize(zs->total_out);
  return buffer;
}
struct regex_matcher::impl {
  explicit impl(std::string const& pattern) : regex_{pattern} {}

  match_result_t match(std::string_view target) const {
    std::cmatch match;
    if (std::regex_match<char const*>(&*begin(target),
                                      (&*begin(target)) + target.size(), match,
                                      regex_)) {
      return utl::to_vec(match, [](auto const& m) {
        return std::string_view{m.first, static_cast<size_t>(m.length())};
      });
    }
    return std::nullopt;
  }

  std::regex regex_;
};

regex_matcher::regex_matcher(std::string const& pattern)
    : impl_{std::make_unique<regex_matcher::impl>(pattern)} {}

regex_matcher::~regex_matcher() = default;

regex_matcher::match_result_t regex_matcher::match(
    std::string_view target) const {
  return impl_->match(target);
}

}  // namespace tiles
