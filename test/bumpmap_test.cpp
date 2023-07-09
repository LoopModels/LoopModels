#include "Dicts/BumpMapSet.hpp"
#include <gtest/gtest.h>

// // NOLINTNEXTLINE(modernize-use-trailing-return-type)
// TEST(BumpUpMapTest, BasicAssertions) {
//   OwningArena<16384, true> alloc;
//   for (int i = 0; i < 100; ++i) {
//     amap<uint64_t, uint64_t, decltype(alloc)> map(alloc);
//     for (int j = 0; j < 100; ++j) map.insert({j, j});
//     for (int j = 0; j < 100; ++j) EXPECT_EQ(map.find(j)->second, j);
//     alloc.reset();
//   }
// }
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BumpDownMapTest, BasicAssertions) {
  OwningArena<> alloc;
  for (int i = 0; i < 100; ++i) {
    poly::dict::amap<uint64_t, uint64_t> map{&alloc};
    for (int j = 0; j < 100; ++j) map.insert({j, j});
    for (int j = 0; j < 100; ++j) EXPECT_EQ(map.find(j)->second, j);
    alloc.reset();
  }
}
