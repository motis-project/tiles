#include "catch2/catch_all.hpp"

#include "osmium/index/detail/tmpfile.hpp"
#include "osmium/io/pbf_input.hpp"
#include "osmium/io/reader_iterator.hpp"
#include "osmium/visitor.hpp"

#include "tiles/osm/hybrid_node_idx.h"
#include "tiles/util.h"

#define CHECK_EXISTS(nodes, id, pos_x, pos_y)  \
  {                                            \
    auto const result = get_coords(nodes, id); \
    REQUIRE(result);                           \
    CHECK((pos_x) == result->x());             \
    CHECK((pos_y) == result->y());             \
  }

#define CHECK_LOCATION(loc, pos_x, pos_y) \
  CHECK((pos_x) == (loc).x());            \
  CHECK((pos_y) == (loc).y());

void get_coords_helper(
    tiles::hybrid_node_idx const& nodes,
    std::vector<std::pair<osmium::object_id_type, osmium::Location*>> query) {
  tiles::get_coords(nodes, query);
}

TEST_CASE("hybrid_node_idx") {  // NOLINT
  SECTION("null") {
    tiles::hybrid_node_idx nodes;
    CHECK_FALSE(get_coords(nodes, 0));
  }

  SECTION("empty idx") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
  }

  SECTION("entry single") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {2, 3});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
    CHECK_FALSE(get_coords(nodes, 100));

    CHECK_EXISTS(nodes, 42, 2, 3);

    osmium::Location loc;
    get_coords_helper(nodes, {{42L, &loc}});
    CHECK_LOCATION(loc, 2, 3);
  }

  SECTION("entries consecutive") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {2, 3});
      builder.push(43, {5, 6});
      builder.push(44, {8, 9});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
    CHECK_FALSE(get_coords(nodes, 100));

    CHECK_EXISTS(nodes, 42, 2, 3);
    CHECK_EXISTS(nodes, 43, 5, 6);
    CHECK_EXISTS(nodes, 44, 8, 9);

    // some batch queries
    {
      osmium::Location l42;
      osmium::Location l43;
      osmium::Location l44;
      get_coords_helper(nodes, {{42L, &l42}, {43L, &l43}, {44L, &l44}});
      CHECK_LOCATION(l42, 2, 3);
      CHECK_LOCATION(l43, 5, 6);
      CHECK_LOCATION(l44, 8, 9);
    }
    {
      osmium::Location l42;
      osmium::Location l44;
      get_coords_helper(nodes, {{44L, &l44}, {42L, &l42}});
      CHECK_LOCATION(l42, 2, 3);
      CHECK_LOCATION(l44, 8, 9);
    }
    {
      osmium::Location l43;
      osmium::Location l44;
      get_coords_helper(nodes, {{43L, &l43}, {44L, &l44}});
      CHECK_LOCATION(l43, 5, 6);
      CHECK_LOCATION(l44, 8, 9);
    }
    {
      osmium::Location l43_a;
      osmium::Location l43_b;
      osmium::Location l44;
      get_coords_helper(nodes, {{43L, &l43_b}, {44L, &l44}, {43L, &l43_a}});
      CHECK_LOCATION(l43_a, 5, 6);
      CHECK_LOCATION(l43_b, 5, 6);
      CHECK_LOCATION(l44, 8, 9);
    }
    {
      osmium::Location l44;
      get_coords_helper(nodes, {{44L, &l44}});
      CHECK_LOCATION(l44, 8, 9);
    }
  }

  SECTION("entries gap") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {2, 3});
      builder.push(44, {8, 9});
      builder.push(45, {1, 2});
      builder.push(46, {4, 5});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
    CHECK_FALSE(get_coords(nodes, 100));

    CHECK_FALSE(get_coords(nodes, 41));
    CHECK_FALSE(get_coords(nodes, 43));
    CHECK_FALSE(get_coords(nodes, 47));

    CHECK_EXISTS(nodes, 42, 2, 3);
    CHECK_EXISTS(nodes, 44, 8, 9);
    CHECK_EXISTS(nodes, 45, 1, 2);
    CHECK_EXISTS(nodes, 46, 4, 5);

    {
      osmium::Location l42;
      osmium::Location l44;
      osmium::Location l45;
      get_coords_helper(nodes, {{42L, &l42}, {45L, &l45}, {44L, &l44}});
      CHECK_LOCATION(l42, 2, 3);
      CHECK_LOCATION(l44, 8, 9);
      CHECK_LOCATION(l45, 1, 2);
    }
    {
      osmium::Location l42;
      osmium::Location l45;
      get_coords_helper(nodes, {{42L, &l42}, {45L, &l45}});
      CHECK_LOCATION(l42, 2, 3);
      CHECK_LOCATION(l45, 1, 2);
    }
    {
      osmium::Location l45;
      get_coords_helper(nodes, {{45L, &l45}});
      CHECK_LOCATION(l45, 1, 2);
    }
    {
      osmium::Location l44;
      get_coords_helper(nodes, {{44L, &l44}});
      CHECK_LOCATION(l44, 8, 9);
    }
    {
      osmium::Location l44;
      osmium::Location l46;
      get_coords_helper(nodes, {{44L, &l44}, {46L, &l46}});
      CHECK_LOCATION(l44, 8, 9);
      CHECK_LOCATION(l46, 4, 5);
    }
    {
      osmium::Location l46;
      get_coords_helper(nodes, {{46L, &l46}});
      CHECK_LOCATION(l46, 4, 5);
    }
  }

  SECTION("artificial splits") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {2, 3});
      builder.push(43, {2, 7});
      builder.push(44, {(1 << 28) + 14, (1 << 28) + 15});
      builder.push(45, {(1 << 28) + 16, (1 << 28) + 17});
      builder.finish();

      CHECK(2 == builder.get_stat_spans());
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
    CHECK_FALSE(get_coords(nodes, 100));

    CHECK_FALSE(get_coords(nodes, 41));
    CHECK_FALSE(get_coords(nodes, 46));

    CHECK_EXISTS(nodes, 42, 2, 3);
    CHECK_EXISTS(nodes, 43, 2, 7);
    CHECK_EXISTS(nodes, 44, (1 << 28) + 14, (1 << 28) + 15);
    CHECK_EXISTS(nodes, 45, (1 << 28) + 16, (1 << 28) + 17);

    {
      osmium::Location l44;
      get_coords_helper(nodes, {{44L, &l44}});
      CHECK_LOCATION(l44, (1 << 28) + 14, (1 << 28) + 15);
    }
  }

  SECTION("large numbers") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {2251065056, 1454559573});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};
    CHECK_FALSE(get_coords(nodes, 0));
    CHECK_FALSE(get_coords(nodes, 100));

    CHECK_FALSE(get_coords(nodes, 41));
    CHECK_EXISTS(nodes, 42, 2251065056, 1454559573);
    CHECK_FALSE(get_coords(nodes, 43));
  }

  SECTION("limits") {
    tiles::hybrid_node_idx nodes;
    tiles::hybrid_node_idx_builder builder{nodes};

    CHECK_THROWS(builder.push(42, {-2, 3}));
    CHECK_THROWS(builder.push(42, {2, -3}));

    CHECK_NOTHROW(builder.push(42, {2, 3}));

    CHECK_THROWS(
        builder.push(43, {1ULL + std::numeric_limits<uint32_t>::max(), 3}));
    CHECK_THROWS(
        builder.push(43, {2, 1ULL + std::numeric_limits<uint32_t>::max()}));
  }

  SECTION("missing nodes") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(42, {1, 1});
      builder.push(43, {2, 2});
      builder.push(45, {4, 4});
      builder.push(46, {5, 5});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};

    std::vector<std::pair<osmium::object_id_type, osmium::Location>> mem;
    for (auto i = 41; i < 48; ++i) {
      mem.emplace_back(i, osmium::Location{});
    }
    std::vector<std::pair<osmium::object_id_type, osmium::Location*>> query;
    query.reserve(mem.size());
    for (auto& m : mem) {
      query.emplace_back(m.first, &m.second);
    }
    tiles::get_coords(nodes, query);

    CHECK(mem.at(0).second.x() == osmium::Location::undefined_coordinate);
    CHECK(mem.at(0).second.y() == osmium::Location::undefined_coordinate);

    CHECK(mem.at(1).second.x() == 1);
    CHECK(mem.at(1).second.y() == 1);

    CHECK(mem.at(2).second.x() == 2);
    CHECK(mem.at(2).second.y() == 2);

    CHECK(mem.at(3).second.x() == osmium::Location::undefined_coordinate);
    CHECK(mem.at(3).second.y() == osmium::Location::undefined_coordinate);

    CHECK(mem.at(4).second.x() == 4);
    CHECK(mem.at(4).second.y() == 4);

    CHECK(mem.at(5).second.x() == 5);
    CHECK(mem.at(5).second.y() == 5);

    CHECK(mem.at(6).second.x() == osmium::Location::undefined_coordinate);
    CHECK(mem.at(6).second.y() == osmium::Location::undefined_coordinate);
  }

  SECTION("negative nodes") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(-42, {1, 1});
      builder.push(-43, {2, 2});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};

    {
      osmium::Location l42;
      get_coords_helper(nodes, {{42L, &l42}});
      CHECK_LOCATION(l42, 1, 1);
    }
    {
      osmium::Location lm42;
      get_coords_helper(nodes, {{42L, &lm42}});
      CHECK_LOCATION(lm42, 1, 1);
    }
    {
      osmium::Location l43;
      get_coords_helper(nodes, {{43L, &l43}});
      CHECK_LOCATION(l43, 2, 2);
    }
    {
      osmium::Location lm43;
      get_coords_helper(nodes, {{43L, &lm43}});
      CHECK_LOCATION(lm43, 2, 2);
    }

    {
      std::vector<std::pair<osmium::object_id_type, osmium::Location>> mem;
      for (auto i = 42; i < 44; ++i) {
        mem.emplace_back(-i, osmium::Location{});
      }
      std::vector<std::pair<osmium::object_id_type, osmium::Location*>> query;
      query.reserve(mem.size());
      for (auto& m : mem) {
        query.emplace_back(m.first, &m.second);
      }
      tiles::get_coords(nodes, query);

      CHECK(mem.at(0).second.x() == 1);
      CHECK(mem.at(0).second.y() == 1);

      CHECK(mem.at(1).second.x() == 2);
      CHECK(mem.at(1).second.y() == 2);
    }
  }

  SECTION("duplicates") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(-42, {1, 1});
      builder.push(-42, {1, 1});
      builder.push(42, {1, 1});
      builder.push(42, {1, 1});
      builder.push(-42, {1, 1});
      builder.finish();
    }

    tiles::hybrid_node_idx nodes{idx_fd, dat_fd};

    {
      osmium::Location l42;
      get_coords_helper(nodes, {{42L, &l42}});
      CHECK_LOCATION(l42, 1, 1);
    }
    {
      osmium::Location lm42;
      get_coords_helper(nodes, {{42L, &lm42}});
      CHECK_LOCATION(lm42, 1, 1);
    }
  }

  SECTION("duplicates mismatch") {
    auto const idx_fd = osmium::detail::create_tmp_file();
    auto const dat_fd = osmium::detail::create_tmp_file();

    {
      tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};
      builder.push(-42, {1, 1});
      CHECK_THROWS(builder.push(-42, {2, 2}));
      CHECK_THROWS(builder.push(42, {2, 2}));
    }
  }
}

TEST_CASE("hybrid_node_idx_benchmark", "[.]") {
  tiles::t_log("start");

  auto const idx_fd = osmium::detail::create_tmp_file();
  auto const dat_fd = osmium::detail::create_tmp_file();
  tiles::hybrid_node_idx_builder builder{idx_fd, dat_fd};

  osmium::io::Reader reader("/data/osm/planet-latest.osm.pbf",
                            osmium::osm_entity_bits::node);
  osmium::apply(reader, builder);
  builder.finish();

  builder.dump_stats();
}
