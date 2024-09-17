#include <gtest/gtest.h>
#include <iostream>
#ifndef USE_MODULE
#include "Support/Permutation.cxx"
#include "Math/ManagedArray.cxx"
#include "Graphs/IndexGraphs.cxx"
#include "Math/Array.cxx"
#else

import Array;
import IndexGraph;
import ManagedArray;
import Permutation;
#endif

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(IndexGraphLoopPerm, BasicAssertions) {
  // minimal examples of register-ordering
  utils::IndexRelationGraph matmul{3};
  EXPECT_EQ(matmul.data_.size(), 3);
  EXPECT_EQ(matmul.data_[0].size(), 0);
  EXPECT_EQ(matmul.data_[1].size(), 0);
  EXPECT_EQ(matmul.data_[2].size(), 0);
  // for (int m = 0; m < M; ++m)
  //   for (int n = 0; n < N; ++n)
  //     for (int k = 0; k < K; ++k)
  //       C[m,n] += A[m,k] * B[k,n];
  // A: 0,2; add edges from missing (`1`)
  matmul.add_edge(1, 0);
  matmul.add_edge(1, 2);
  // B: 2,1; add edges from missing (`0`)
  matmul.add_edge(0, 1);
  matmul.add_edge(0, 2);
  math::Vector<utils::LoopSet> cmpts;
  graph::stronglyConnectedComponents(cmpts, matmul);
  EXPECT_EQ(cmpts.size(), 2);
  std::cout << "cmpts[0] = " << cmpts[0] << "\ncmpts[1] = " << cmpts[1] << "\n";
  EXPECT_EQ(cmpts[0].size(), 1);
  EXPECT_EQ(*cmpts[0].begin(), 2);
  EXPECT_EQ(cmpts[1].size(), 2);
  EXPECT_EQ(*cmpts[1].begin(), 0);
  EXPECT_EQ(*(++cmpts[1].begin()), 1);
}