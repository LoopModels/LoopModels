#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#ifndef USE_MODULE
#include "Containers/TinyVector.cxx"
#include "Support/Permutation.cxx"
#else

import Permutation;
import TinyVector;
#endif

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(PermutationTest, BasicAssertions) {
  int count = 0;
  for (auto p : utils::Permutations(3)) {
    std::cout << "Perm: " << p << "\n";
    ++count;
  }

  EXPECT_EQ(count, 6);
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(LoopPermutationTest, BasicAssertions) {
  using utils::LoopSet;
  containers::TinyVector<LoopSet, 15, int16_t> cmpts;
  // cmpts are [1], [0,3], [2, 4]
  cmpts.push_back(LoopSet::fromMask(0x02)); // 0x00000010
  cmpts.push_back(LoopSet::fromMask(0x09)); // 0x00001001
  cmpts.push_back(LoopSet::fromMask(0x14)); // 0x00010100
  int count = 0;
  for (auto p : utils::LoopPermutations(cmpts)) {
    std::cout << "Perm: " << p << "\n";
    ++count;
    ASSERT_LE(count, 4);
  }

  EXPECT_EQ(count, 4);
}