#include "gtest/gtest.h"

#include <random>

#include "tiles/bin_utils.h"
#include "tiles/util.h"

std::string test_data() {
  std::string test(1024ULL * 1024, '\0');

  std::mt19937 gen{42};
  std::uniform_int_distribution<uint64_t> dist;
  for (auto i = 0ULL; i < test.size(); i += sizeof(uint64_t)) {
    tiles::write(test.data(), i, dist(gen));
  }
  return test;
}

TEST(compress, deflate) {
  auto test=test_data();

  auto out = tiles::compress_deflate(test);
  EXPECT_FALSE(out.empty());
}

TEST(compress, gzip) {
  auto test=test_data();

  auto out = tiles::compress_gzip(test);
  EXPECT_FALSE(out.empty());
}
