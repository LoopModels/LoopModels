#include "../include/BitSets.hpp"
#include <gtest/gtest.h>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BitSetTest, BasicAssertions) {
  BitSet bs(1000);
  bs[4] = true;
  bs[10] = true;
  bs[200] = true;
  bs[117] = true;
  bs[87] = true;
  bs[991] = true;
  bs[0] = true;
  llvm::errs() << bs << "\n";
  EXPECT_EQ(std::ranges::begin(bs), bs.begin());
  EXPECT_EQ(std::ranges::end(bs), bs.end());
  llvm::SmallVector<size_t> bsc{0, 4, 10, 87, 117, 200, 991};
  size_t j = 0;
  for (auto I = bs.begin(); I != bs.end(); ++I) {
    EXPECT_EQ(*I, bsc[j]);
    EXPECT_TRUE(bs[*I]);
    printf("We get: %zu\n", *I);
    ++j;
  }
  j = 0;
  for (auto i : bs) {
    EXPECT_EQ(i, bsc[j]);
    EXPECT_TRUE(bs[i]);
    printf("We get: %zu\n", i);
    ++j;
  }
  EXPECT_EQ(j, bsc.size());
  EXPECT_EQ(j, bs.size());
  BitSet empty;
  size_t c = 0, d = 0;
  for (auto b : empty) {
    ++c;
    d += b;
  }
  EXPECT_FALSE(c);
  EXPECT_FALSE(d);
}
