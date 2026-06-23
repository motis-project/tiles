#include "tiles/util.h"

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
  z_stream zs{};

  auto error = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16,
                            8, Z_DEFAULT_STRATEGY);

  utl::verify(error == Z_OK, "deflateInit2 failed");

  auto out_size = compressBound(input.size()) + 32;
  std::string buffer(out_size, '\0');

  zs.next_in = reinterpret_cast<uint8_t*>(const_cast<char*>(input.data()));
  zs.avail_in = input.size();

  zs.next_out = reinterpret_cast<uint8_t*>(buffer.data());
  zs.avail_out = buffer.size();

  error = deflate(&zs, Z_FINISH);
  utl::verify(error == Z_STREAM_END, "deflate failed");

  buffer.resize(zs.total_out);

  deflateEnd(&zs);
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
