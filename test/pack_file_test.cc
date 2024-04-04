#include "gtest/gtest.h"

#include "tiles/db/pack_file.h"

TEST(pack_record, empty) {
  auto ser = tiles::pack_records_serialize(std::vector<tiles::pack_record>{});
  EXPECT_TRUE(ser.empty());

  auto deser0 = tiles::pack_records_deserialize(ser);
  EXPECT_TRUE(deser0.empty());

  tiles::pack_records_update(ser, tiles::pack_record{1, 2});
  auto deser1 = tiles::pack_records_deserialize(ser);
  EXPECT_TRUE((deser1 == std::vector<tiles::pack_record>{{1, 2}}));
}

TEST(pack_record, buildup_deserialize) {
  auto ser = tiles::pack_records_serialize(tiles::pack_record{8, 9});
  auto deser0 = tiles::pack_records_deserialize(ser);
  EXPECT_TRUE((deser0 == std::vector<tiles::pack_record>{{8, 9}}));

  tiles::pack_records_update(ser, tiles::pack_record{42, 43});
  auto deser1 = tiles::pack_records_deserialize(ser);
  EXPECT_TRUE((deser1 == std::vector<tiles::pack_record>{{8, 9}, {42, 43}}));

  tiles::pack_records_update(ser, tiles::pack_record{88, 99});
  auto deser2 = tiles::pack_records_deserialize(ser);
  EXPECT_TRUE(
      (deser2 == std::vector<tiles::pack_record>{{8, 9}, {42, 43}, {88, 99}}));
}

TEST(pack_record, buildup_foreach) {
  std::vector<tiles::pack_record> deser;

  auto ser = tiles::pack_records_serialize(tiles::pack_record{8, 9});
  deser.clear();
  tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
  EXPECT_TRUE((deser == std::vector<tiles::pack_record>{{8, 9}}));

  tiles::pack_records_update(ser, tiles::pack_record{42, 43});
  deser.clear();
  tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
  EXPECT_TRUE((deser == std::vector<tiles::pack_record>{{8, 9}, {42, 43}}));

  tiles::pack_records_update(ser, tiles::pack_record{88, 99});
  deser.clear();
  tiles::pack_records_foreach(ser, [&](auto r) { deser.push_back(r); });
  EXPECT_TRUE(
      (deser == std::vector<tiles::pack_record>{{8, 9}, {42, 43}, {88, 99}}));
}
