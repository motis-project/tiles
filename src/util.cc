#include "tiles/util.h"

#include "utl/verify.h"

#include "zlib.h"

namespace tiles {

std::string compress_deflate(std::string const& input) {
  auto out_size = compressBound(input.size());
  std::string buffer(out_size, '\0');

  auto error = compress2(reinterpret_cast<uint8_t*>(&buffer[0]), &out_size,
                         reinterpret_cast<uint8_t const*>(&input[0]),
                         input.size(), Z_BEST_COMPRESSION);
  utl::verify(!error, "compress_deflate failed");

  buffer.resize(out_size);
  return buffer;
}

}  // namespace tiles
