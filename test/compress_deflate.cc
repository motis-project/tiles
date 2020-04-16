#include "catch2/catch.hpp"

#include <random>

#include "tiles/util.h"

TEST_CASE("compress_deflate") {
  std::string test(1024ULL * 1024, '\0');

  std::mt19937 gen{42};
  std::uniform_int_distribution<char> dist;
  for (auto i = 0ULL; i < test.size(); ++i) {
    test[i] = dist(gen);
  }

  auto out = tiles::compress_deflate(test);
  CHECK_FALSE(out.empty());
}
