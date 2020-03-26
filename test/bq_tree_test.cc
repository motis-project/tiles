#include "catch2/catch.hpp"

#include <fstream>

#include "tiles/db/bq_tree.h"

TEST_CASE("bq_tree_contains") {
  SECTION("default ctor") {
    tiles::bq_tree tree;
    CHECK(1 == tree.nodes_.size());
    CHECK(false == tree.contains({0, 0, 0}));
  }

  SECTION("root tree") {
    auto empty_tree = tiles::make_bq_tree({});
    CHECK(1 == empty_tree.nodes_.size());
    CHECK(false == empty_tree.contains({0, 0, 0}));

    auto root_tree = tiles::make_bq_tree({{0, 0, 0}});
    CHECK(1 == root_tree.nodes_.size());
    CHECK(true == root_tree.contains({0, 0, 0}));
  }

  SECTION("l1 tree") {
    auto tree = tiles::make_bq_tree({{0, 0, 1}});
    CHECK(1 == tree.nodes_.size());

    // self
    CHECK(true == tree.contains({0, 0, 1}));

    // parent
    CHECK(false == tree.contains({0, 0, 0}));

    // siblings
    CHECK(false == tree.contains({0, 1, 1}));
    CHECK(false == tree.contains({1, 0, 1}));
    CHECK(false == tree.contains({1, 1, 1}));

    // child
    CHECK(true == tree.contains({0, 0, 2}));
  }

  SECTION("l2 tree") {
    auto tree = tiles::make_bq_tree({{0, 1, 2}, {3, 3, 2}});

    CHECK(3 == tree.nodes_.size());

    // self
    CHECK(true == tree.contains({0, 1, 2}));
    CHECK(true == tree.contains({3, 3, 2}));

    // root
    CHECK(false == tree.contains({0, 0, 0}));

    // parent
    CHECK(false == tree.contains({0, 0, 1}));
    CHECK(false == tree.contains({0, 1, 1}));
    CHECK(false == tree.contains({1, 0, 1}));
    CHECK(false == tree.contains({1, 1, 1}));

    // sibling
    CHECK(false == tree.contains({0, 0, 2}));
    CHECK(false == tree.contains({42, 48, 8}));
  }
}

TEST_CASE("bq_tree_all_leafs") {
  SECTION("default ctor") {
    tiles::bq_tree tree;
    CHECK(true == tree.all_leafs({0, 0, 0}).empty());
  }

  SECTION("root tree") {
    auto empty_tree = tiles::make_bq_tree({});
    CHECK(true == empty_tree.all_leafs({0, 0, 0}).empty());

    auto root_tree = tiles::make_bq_tree({{0, 0, 0}});
    auto root_result = root_tree.all_leafs({0, 0, 0});
    REQUIRE(1 == root_result.size());
    CHECK((geo::tile{0, 0, 0} == root_result.at(0)));
  }

  SECTION("l1 tree") {
    auto tree = tiles::make_bq_tree({{1, 1, 1}});
    {  // parent
      auto result = tree.all_leafs({0, 0, 0});
      REQUIRE(1 == result.size());
      CHECK((geo::tile{1, 1, 1} == result.at(0)));
    }
    {  // self
      auto result = tree.all_leafs({1, 1, 1});
      REQUIRE(1 == result.size());
      CHECK((geo::tile{1, 1, 1} == result.at(0)));
    }
    {  // sibling
      REQUIRE(true == tree.all_leafs({0, 0, 1}).empty());
      REQUIRE(true == tree.all_leafs({0, 1, 1}).empty());
      REQUIRE(true == tree.all_leafs({1, 0, 1}).empty());
    }
    {  // self - child
      auto result = tree.all_leafs({2, 2, 2});
      REQUIRE(1 == result.size());
      CHECK((geo::tile{2, 2, 2} == result.at(0)));
    }
    {  // self - child
      REQUIRE(true == tree.all_leafs({0, 0, 2}).empty());
    }
  }

  SECTION("l2 tree") {
    auto tree = tiles::make_bq_tree({{0, 1, 2}, {3, 3, 2}});
    {  // root
      auto result = tree.all_leafs({0, 0, 0});
      std::sort(begin(result), end(result));
      REQUIRE(2 == result.size());
      CHECK((geo::tile{0, 1, 2} == result.at(0)));
      CHECK((geo::tile{3, 3, 2} == result.at(1)));
    }
    {  // parent
      auto result = tree.all_leafs({0, 0, 1});
      std::sort(begin(result), end(result));
      REQUIRE(1 == result.size());
      CHECK((geo::tile{0, 1, 2} == result.at(0)));
    }
    {  // other
      REQUIRE(true == tree.all_leafs({0, 0, 8}).empty());
      REQUIRE(true == tree.all_leafs({42, 48, 8}).empty());
    }
  }

  SECTION("l3 tree") {
    std::vector<geo::tile> tiles;
    for (auto x = 0; x < 3; ++x) {
      for (auto y = 0; y < 3; ++y) {
        tiles.emplace_back(x, y, 3);
      }
    }
    std::sort(begin(tiles), end(tiles));

    auto tree = tiles::make_bq_tree(tiles);
    auto result = tree.all_leafs({0, 0, 0});
    std::sort(begin(result), end(result));
    CHECK(tiles == result);
  }

  SECTION("l1 fuzzy") {
    auto const tiles =
        std::vector<geo::tile>{{0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

    for (auto i = 0u; i < tiles.size(); ++i) {
      auto const& tut = tiles.at(i);

      auto tree = tiles::make_bq_tree({tut});
      auto result = tree.all_leafs({0, 0, 0});
      REQUIRE(1 == result.size());
      CHECK(tut == result.at(0));
    }

    auto const in = [](auto const& e, auto const& v) {
      return std::find(begin(v), end(v), e) != end(v);
    };

    for (auto i = 0u; i < tiles.size(); ++i) {
      for (auto j = 0u; j < tiles.size(); ++j) {
        if (i == j) {
          continue;
        }
        auto const& tut1 = tiles.at(i);
        auto const& tut2 = tiles.at(j);

        auto tree = tiles::make_bq_tree({tut1, tut2});
        auto result = tree.all_leafs({0, 0, 0});
        REQUIRE(2 == result.size());
        CHECK(true == in(tut1, result));
        CHECK(true == in(tut2, result));
      }
    }

    for (auto i = 0u; i < tiles.size(); ++i) {
      for (auto j = 0u; j < tiles.size(); ++j) {
        for (auto k = 0u; k < tiles.size(); ++k) {
          if (i == j || i == k || j == k) {
            continue;
          }
          auto const& tut1 = tiles.at(i);
          auto const& tut2 = tiles.at(j);
          auto const& tut3 = tiles.at(k);

          auto tree = tiles::make_bq_tree({tut1, tut2, tut3});
          auto result = tree.all_leafs({0, 0, 0});
          REQUIRE(3 == result.size());
          CHECK(true == in(tut1, result));
          CHECK(true == in(tut2, result));
          CHECK(true == in(tut3, result));
        }
      }
    }
  }
}

TEST_CASE("bq_tree_tsv_file", "[!hide]") {
  std::ifstream in("tiles.tsv");

  std::vector<geo::tile> tiles;
  geo::tile tmp;
  while (in >> tmp.x_ >> tmp.y_ >> tmp.z_) {
    tiles.push_back(tmp);
  }

  auto const is_parent = [](auto tile, auto const& parent) {
    if (parent == geo::tile{0, 0, 0}) {
      return true;
    }

    while (!(tile == geo::tile{0, 0, 0})) {
      if (tile == parent) {
        return true;
      }
      tile = tile.parent();
    }
    return false;
  };

  auto tree = tiles::make_bq_tree(tiles);
  for (auto const& query : geo::make_tile_pyramid()) {
    if (query.z_ == 4) {
      break;
    }

    std::vector<geo::tile> expected;
    for (auto const& tile : tiles) {
      if (is_parent(tile, query)) {
        expected.push_back(tile);
      }
    }

    std::sort(begin(expected), end(expected));

    auto actual = tree.all_leafs(query);
    std::sort(begin(actual), end(actual));

    CHECK(expected == actual);
  }
}