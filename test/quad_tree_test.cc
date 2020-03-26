#include "catch2/catch.hpp"

#include "fmt/core.h"
#include "geo/tile.h"

#include "tiles/db/quad_tree.h"
#include "tiles/util.h"

namespace tiles {

void dump_tree(std::string const& tree) {
  utl::verify_silent(tree.size() % (4 * 4) == 0, "invalid tree size");
  for (auto i = 0u; i < tree.size() / 4; i += 4) {
    std::cout << fmt::format(
                     "{:4} {:032b} : {:10x} : {:5} : {:10} {:10} {:10}", i,
                     read_nth<quad_entry_t>(tree.data(), i),
                     read_nth<quad_entry_t>(tree.data(), i),
                     read_nth<quad_entry_t>(tree.data(), i) & kQuadOffsetMask,
                     read_nth<quad_entry_t>(tree.data(), i + 1),
                     read_nth<quad_entry_t>(tree.data(), i + 2),
                     read_nth<quad_entry_t>(tree.data(), i + 3))
              << std::endl;
  }
}

}  // namespace tiles

std::vector<std::pair<uint32_t, uint32_t>> collect(char const* tree,
                                                   geo::tile const& root,
                                                   geo::tile const& tile) {
  std::vector<std::pair<uint32_t, uint32_t>> result;
  tiles::walk_quad_tree(tree, root, tile,
                        [&](auto const offset, auto const size) {
                          result.emplace_back(offset, size);
                        });
  std::sort(begin(result), end(result));
  return result;
}

TEST_CASE("quad_tree") {
  SECTION("empty tree") {
    geo::tile root = {0, 0, 0};
    auto tree = tiles::make_quad_tree(root, {});
    REQUIRE(16 == tree.size());

    CHECK(true == collect(tree.data(), root, {0, 0, 0}).empty());
    CHECK(true == collect(tree.data(), root, {1, 1, 2}).empty());
  }

  SECTION("broken tree input") {
    geo::tile root{0, 0, 1};
    CHECK_THROWS(tiles::make_quad_tree(root, {{{0, 1, 1}, 0, 0}}));
    CHECK_THROWS(tiles::make_quad_tree(root, {{{0, 0, 0}, 0, 0}}));
    CHECK_THROWS(tiles::make_quad_tree(root, {{{2, 2, 2}, 0, 0}}));
  }

  SECTION("root tree") {
    geo::tile root{4, 5, 6};

    auto tree = tiles::make_quad_tree(root, {{root, 42, 23}});
    REQUIRE(16 == tree.size());

    // query outside
    CHECK(true == collect(tree.data(), root, {8, 8, 6}).empty());
    CHECK(true == collect(tree.data(), root, {8, 8, 5}).empty());

    // query above
    {
      auto content = collect(tree.data(), root, {0, 0, 2});
      REQUIRE(1 == content.size());
      CHECK(42 == content.at(0).first);
      CHECK(23 == content.at(0).second);
    }

    // query root
    {
      auto content = collect(tree.data(), root, root);
      REQUIRE(1 == content.size());
      CHECK(42 == content.at(0).first);
      CHECK(23 == content.at(0).second);
    }

    // query child
    {
      auto content = collect(tree.data(), root, {8, 10, 7});
      REQUIRE(1 == content.size());
      CHECK(42 == content.at(0).first);
      CHECK(23 == content.at(0).second);
    }
  }

  SECTION("child tree") {
    geo::tile root{0, 0, 1};

    auto tree = tiles::make_quad_tree(
        root, {{{0, 0, 2}, 1, 3}, {{0, 2, 4}, 5, 1}, {{0, 0, 4}, 4, 1}});

    // tiles::dump_tree(tree);

    {
      auto content = collect(tree.data(), root, {0, 0, 0});
      REQUIRE(1 == content.size());
      CHECK(1 == content.at(0).first);
      CHECK(5 == content.at(0).second);
    }
    {
      auto content = collect(tree.data(), root, {0, 0, 1});
      REQUIRE(1 == content.size());
      CHECK(1 == content.at(0).first);
      CHECK(5 == content.at(0).second);
    }
    {
      auto content = collect(tree.data(), root, {0, 0, 2});
      REQUIRE(1 == content.size());
      CHECK(1 == content.at(0).first);
      CHECK(5 == content.at(0).second);
    }
    {
      auto content = collect(tree.data(), root, {0, 0, 3});
      REQUIRE(2 == content.size());

      CHECK(1 == content.at(0).first);
      CHECK(3 == content.at(0).second);

      CHECK(4 == content.at(1).first);
      CHECK(1 == content.at(1).second);
    }
    {
      auto content = collect(tree.data(), root, {0, 2, 4});
      REQUIRE(2 == content.size());

      CHECK(1 == content.at(0).first);
      CHECK(3 == content.at(0).second);

      CHECK(5 == content.at(1).first);
      CHECK(1 == content.at(1).second);
    }
    {
      auto content = collect(tree.data(), root, {0, 4, 5});
      REQUIRE(2 == content.size());

      CHECK(1 == content.at(0).first);
      CHECK(3 == content.at(0).second);

      CHECK(5 == content.at(1).first);
      CHECK(1 == content.at(1).second);
    }
  }
}
