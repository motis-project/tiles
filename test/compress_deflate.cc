#include "catch2/catch_all.hpp"

#include <random>

#include "tiles/bin_utils.h"
#include "tiles/util.h"

TEST_CASE("compress_deflate") {
  std::string test(1024ULL * 1024, '\0');

  std::mt19937 gen{42};
  std::uniform_int_distribution<uint64_t> dist;
  for (auto i = 0ULL; i < test.size(); i += sizeof(uint64_t)) {
    tiles::write(test.data(), i, dist(gen));
  }

  auto out = tiles::compress_deflate(test);
  CHECK_FALSE(out.empty());
}
