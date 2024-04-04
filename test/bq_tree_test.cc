#include "gtest/gtest.h"

#include <fstream>

#include "tiles/db/bq_tree.h"

TEST(bq_tree_contains, default_ctor) {
  tiles::bq_tree tree;
  EXPECT_TRUE(1 == tree.nodes_.size());
  EXPECT_TRUE(false == tree.contains({0, 0, 0}));
}

TEST(bq_tree_contains, root_tree) {
  auto empty_tree = tiles::make_bq_tree({});
  EXPECT_TRUE(1 == empty_tree.nodes_.size());
  EXPECT_TRUE(false == empty_tree.contains({0, 0, 0}));

  auto root_tree = tiles::make_bq_tree({{0, 0, 0}});
  EXPECT_TRUE(1 == root_tree.nodes_.size());
  EXPECT_TRUE(true == root_tree.contains({0, 0, 0}));
}

TEST(bq_tree_contains, l1_tree) {
  auto tree = tiles::make_bq_tree({{0, 0, 1}});
  EXPECT_TRUE(1 == tree.nodes_.size());

  // self
  EXPECT_TRUE(true == tree.contains({0, 0, 1}));

  // parent
  EXPECT_TRUE(false == tree.contains({0, 0, 0}));

  // siblings
  EXPECT_TRUE(false == tree.contains({0, 1, 1}));
  EXPECT_TRUE(false == tree.contains({1, 0, 1}));
  EXPECT_TRUE(false == tree.contains({1, 1, 1}));

  // child
  EXPECT_TRUE(true == tree.contains({0, 0, 2}));
}

TEST(bq_tree_contains, l2_tree) {
  auto tree = tiles::make_bq_tree({{0, 1, 2}, {3, 3, 2}});

  EXPECT_TRUE(3 == tree.nodes_.size());

  // self
  EXPECT_TRUE(true == tree.contains({0, 1, 2}));
  EXPECT_TRUE(true == tree.contains({3, 3, 2}));

  // root
  EXPECT_TRUE(false == tree.contains({0, 0, 0}));

  // parent
  EXPECT_TRUE(false == tree.contains({0, 0, 1}));
  EXPECT_TRUE(false == tree.contains({0, 1, 1}));
  EXPECT_TRUE(false == tree.contains({1, 0, 1}));
  EXPECT_TRUE(false == tree.contains({1, 1, 1}));

  // sibling
  EXPECT_TRUE(false == tree.contains({0, 0, 2}));
  EXPECT_TRUE(false == tree.contains({42, 48, 8}));
}

TEST(bq_tree_all_leafs, default_ctor) {
  tiles::bq_tree tree;
  EXPECT_TRUE(true == tree.all_leafs({0, 0, 0}).empty());
}

TEST(bq_tree_all_leafs, root_tree) {
  auto empty_tree = tiles::make_bq_tree({});
  EXPECT_TRUE(true == empty_tree.all_leafs({0, 0, 0}).empty());

  auto root_tree = tiles::make_bq_tree({{0, 0, 0}});
  auto root_result = root_tree.all_leafs({0, 0, 0});
  ASSERT_TRUE(1 == root_result.size());
  EXPECT_TRUE((geo::tile{0, 0, 0} == root_result.at(0)));
}

TEST(bq_tree_all_leafs, l1_tree) {
  auto tree = tiles::make_bq_tree({{1, 1, 1}});
  {  // parent
    auto result = tree.all_leafs({0, 0, 0});
    ASSERT_TRUE(1 == result.size());
    EXPECT_TRUE((geo::tile{1, 1, 1} == result.at(0)));
  }
  {  // self
    auto result = tree.all_leafs({1, 1, 1});
    ASSERT_TRUE(1 == result.size());
    EXPECT_TRUE((geo::tile{1, 1, 1} == result.at(0)));
  }
  {  // sibling
    ASSERT_TRUE(true == tree.all_leafs({0, 0, 1}).empty());
    ASSERT_TRUE(true == tree.all_leafs({0, 1, 1}).empty());
    ASSERT_TRUE(true == tree.all_leafs({1, 0, 1}).empty());
  }
  {  // self - child
    auto result = tree.all_leafs({2, 2, 2});
    ASSERT_TRUE(1 == result.size());
    EXPECT_TRUE((geo::tile{2, 2, 2} == result.at(0)));
  }
  {  // self - child
    ASSERT_TRUE(true == tree.all_leafs({0, 0, 2}).empty());
  }
}

TEST(bq_tree_all_leafs, l2_tree) {
  auto tree = tiles::make_bq_tree({{0, 1, 2}, {3, 3, 2}});
  {  // root
    auto result = tree.all_leafs({0, 0, 0});
    std::sort(begin(result), end(result));
    ASSERT_TRUE(2 == result.size());
    EXPECT_TRUE((geo::tile{0, 1, 2} == result.at(0)));
    EXPECT_TRUE((geo::tile{3, 3, 2} == result.at(1)));
  }
  {  // parent
    auto result = tree.all_leafs({0, 0, 1});
    std::sort(begin(result), end(result));
    ASSERT_TRUE(1 == result.size());
    EXPECT_TRUE((geo::tile{0, 1, 2} == result.at(0)));
  }
  {  // other
    ASSERT_TRUE(true == tree.all_leafs({0, 0, 8}).empty());
    ASSERT_TRUE(true == tree.all_leafs({42, 48, 8}).empty());
  }
}

TEST(bq_tree_all_leafs, l3_tree) {
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
  EXPECT_TRUE(tiles == result);
}

TEST(bq_tree_all_leafs, l1_fuzzy) {
  auto const tiles =
      std::vector<geo::tile>{{0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

  for (auto const& tut : tiles) {
    auto tree = tiles::make_bq_tree({tut});
    auto result = tree.all_leafs({0, 0, 0});
    ASSERT_TRUE(1 == result.size());
    EXPECT_TRUE(tut == result.at(0));
  }

  auto const in = [](auto const& e, auto const& v) {
    return std::find(begin(v), end(v), e) != end(v);
  };

  for (auto i = 0ULL; i < tiles.size(); ++i) {
    for (auto j = 0ULL; j < tiles.size(); ++j) {
      if (i == j) {
        continue;
      }
      auto const& tut1 = tiles.at(i);
      auto const& tut2 = tiles.at(j);

      auto tree = tiles::make_bq_tree({tut1, tut2});
      auto result = tree.all_leafs({0, 0, 0});
      ASSERT_TRUE(2 == result.size());
      EXPECT_TRUE(true == in(tut1, result));
      EXPECT_TRUE(true == in(tut2, result));
    }
  }

  for (auto i = 0ULL; i < tiles.size(); ++i) {
    for (auto j = 0ULL; j < tiles.size(); ++j) {
      for (auto k = 0ULL; k < tiles.size(); ++k) {
        if (i == j || i == k || j == k) {
          continue;
        }
        auto const& tut1 = tiles.at(i);
        auto const& tut2 = tiles.at(j);
        auto const& tut3 = tiles.at(k);

        auto tree = tiles::make_bq_tree({tut1, tut2, tut3});
        auto result = tree.all_leafs({0, 0, 0});
        ASSERT_TRUE(3 == result.size());
        EXPECT_TRUE(true == in(tut1, result));
        EXPECT_TRUE(true == in(tut2, result));
        EXPECT_TRUE(true == in(tut3, result));
      }
    }
  }
}

TEST(bq_tree_tsv_file, hide) {
  std::ifstream in("tiles.tsv");

  std::vector<geo::tile> tiles;
  geo::tile tmp{};
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

    EXPECT_TRUE(expected == actual);
  }
}
