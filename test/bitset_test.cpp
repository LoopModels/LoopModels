#include "../include/Containers/BitSets.hpp"
#include <array>
#include <cstdint>
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
  // EXPECT_EQ(std::ranges::begin(bs), bs.begin());
  // EXPECT_EQ(std::ranges::end(bs), bs.end());
  llvm::SmallVector<size_t> bsc{0, 4, 10, 87, 117, 200, 991};
  size_t j = 0;
  for (auto J = bs.begin(); J != decltype(bs)::end(); ++J) {
    EXPECT_EQ(*J, bsc[j++]);
    EXPECT_TRUE(bs[*J]);
    printf("We get: %zu\n", *J);
  }
  j = 0;
  for (auto i : bs) {
    EXPECT_EQ(i, bsc[j++]);
    EXPECT_TRUE(bs[i]);
    printf("We get: %zu\n", i);
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
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(DynSizeBitSetTest, BasicAssertions) {
  BitSet bs;
  EXPECT_EQ(bs.data.size(), 0);
  bs[4] = true;
  bs[10] = true;
  EXPECT_EQ(bs.data.size(), 1);
  EXPECT_EQ(bs.data.front(), 1040);
  llvm::SmallVector<size_t> sv;
  for (auto i : bs) sv.push_back(i);
  EXPECT_EQ(sv.size(), 2);
  EXPECT_EQ(sv[0], 4);
  EXPECT_EQ(sv[1], 10);
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(FixedSizeBitSetTest, BasicAssertions) {
  BitSet<std::array<uint64_t, 2>> bs;
  bs[4] = true;
  bs[10] = true;
  EXPECT_EQ(bs.data[0], 1040);
  EXPECT_EQ(bs.data[1], 0);
  llvm::SmallVector<size_t> sv;
  for (auto i : bs) sv.push_back(i);
  EXPECT_EQ(sv.size(), 2);
  EXPECT_EQ(sv[0], 4);
  EXPECT_EQ(sv[1], 10);
}
