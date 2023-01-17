#include "Containers/BumpMap.hpp"
#include <gtest/gtest.h>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BumpMapTest, BasicAssertions) {
  BumpAlloc<> alloc;
  for (int i = 0; i < 100; ++i) {
    BumpMap<uint64_t, uint64_t> map(alloc);
    for (int j = 0; j < 100; ++j) map.insert({j, j});
    for (int j = 0; j < 100; ++j) EXPECT_EQ(map.find(j)->second, j);
    alloc.reset();
  }
}
