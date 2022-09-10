#include "catch2/catch_all.hpp"

#include "fmt/core.h"
#include "geo/tile.h"

#include "tiles/db/quad_tree.h"
#include "tiles/util.h"

namespace tiles {

void dump_tree(std::string const& tree) {
  utl::verify_silent(tree.size() % (4 * 4) == 0, "invalid tree size");
  for (auto i = 0ULL; i < tree.size() / 4; i += 4) {
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

TEST_CASE("quad_tree_real_world_problem_1") {
  geo::tile root{534, 362, 10};

  std::vector<tiles::quad_tree_input> input;
  input.push_back({{534, 362, 10}, 1703394, 1});
  input.push_back({{17099, 11600, 15}, 1704003, 1});
  input.push_back({{8546, 5807, 14}, 1704193, 1});
  input.push_back({{34185, 23231, 16}, 1704499, 1});
  input.push_back({{136744, 92925, 18}, 1704727, 1});
  input.push_back({{2137, 1451, 12}, 1704785, 1});
  input.push_back({{4275, 2903, 13}, 1705392, 1});
  input.push_back({{547239, 371607, 20}, 1706102, 1});
  input.push_back({{17101, 11613, 15}, 1706160, 1});
  input.push_back({{547241, 371633, 20}, 1706233, 1});
  input.push_back({{547241, 371634, 20}, 1706291, 1});
  input.push_back({{547216, 371700, 20}, 1706349, 1});
  input.push_back({{17101, 11615, 15}, 1706464, 1});
  input.push_back({{1069, 725, 11}, 1706666, 1});
  input.push_back({{8552, 5807, 14}, 1706732, 1});
  input.push_back({{2139, 1451, 12}, 1706992, 1});
  input.push_back({{34229, 23221, 16}, 1707667, 1});
  input.push_back({{547671, 371550, 20}, 1707754, 1});
  input.push_back({{4278, 2903, 13}, 1707812, 1});
  input.push_back({{8556, 5806, 14}, 1707938, 1});
  input.push_back({{17112, 11612, 15}, 1708308, 1});
  input.push_back({{34224, 23224, 16}, 1708378, 1});
  input.push_back({{68448, 46449, 17}, 1708513, 1});
  input.push_back({{273794, 185797, 19}, 1708571, 1});
  input.push_back({{17113, 11612, 15}, 1708638, 1});
  input.push_back({{68452, 46450, 17}, 1708914, 1});
  input.push_back({{34226, 23228, 16}, 1708974, 1});

  auto tree = tiles::make_quad_tree(root, input);

  // tiles::dump_tree(tree);

  geo::tile query{17097, 11585, 15};

  std::optional<std::pair<uint32_t, uint32_t>> result;
  tiles::walk_quad_tree(tree.data(), root, query,
                        [&](auto const offset, auto const count) {
                          REQUIRE(!result.has_value());
                          result = {offset, count};
                        });
  REQUIRE(result.has_value());
  CHECK(result->first == 1703394);
  CHECK(result->second == 1);
}
