#include "catch2/catch_all.hpp"

#include "tiles/db/pack_file.h"

TEST_CASE("pack_record") {
  SECTION("empty") {
    auto ser = tiles::pack_records_serialize(std::vector<tiles::pack_record>{});
    CHECK(ser.empty());

    auto deser0 = tiles::pack_records_deserialize(ser);
    CHECK(deser0.empty());

    tiles::pack_records_update(ser, tiles::pack_record{1, 2});
    auto deser1 = tiles::pack_records_deserialize(ser);
    CHECK((deser1 == std::vector<tiles::pack_record>{{1, 2}}));
  }

  SECTION("buildup deserialize") {
    auto ser = tiles::pack_records_serialize(tiles::pack_record{8, 9});
    auto deser0 = tiles::pack_records_deserialize(ser);
    CHECK((deser0 == std::vector<tiles::pack_record>{{8, 9}}));

    tiles::pack_records_update(ser, tiles::pack_record{42, 43});
    auto deser1 = tiles::pack_records_deserialize(ser);
    CHECK((deser1 == std::vector<tiles::pack_record>{{8, 9}, {42, 43}}));

    tiles::pack_records_update(ser, tiles::pack_record{88, 99});
    auto deser2 = tiles::pack_records_deserialize(ser);
    CHECK((deser2 ==
           std::vector<tiles::pack_record>{{8, 9}, {42, 43}, {88, 99}}));
  }

  SECTION("buildup foreach") {
    std::vector<tiles::pack_record> deser;

    auto ser = tiles::pack_records_serialize(tiles::pack_record{8, 9});
    deser.clear();
    tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
    CHECK((deser == std::vector<tiles::pack_record>{{8, 9}}));

    tiles::pack_records_update(ser, tiles::pack_record{42, 43});
    deser.clear();
    tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
    CHECK((deser == std::vector<tiles::pack_record>{{8, 9}, {42, 43}}));

    tiles::pack_records_update(ser, tiles::pack_record{88, 99});
    deser.clear();
    tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
    CHECK(
        (deser == std::vector<tiles::pack_record>{{8, 9}, {42, 43}, {88, 99}}));
  }
}
