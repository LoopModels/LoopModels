#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#ifndef USE_MODULE
#include "IR/IR.cxx"
#include "Math/Array.cxx"
#include "Math/Constraints.cxx"
#include "Support/OStream.cxx"
#include "TestUtilities.cxx"
#include "Utilities/MatrixStringParse.cxx"
#include "Utilities/Valid.cxx"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#else

import Array;
import ArrayParse;
import Constraints;
import IR;
import OStream;
import STL;
import TestUtilities;
import Valid;
#endif

using math::IntMatrix, math::DenseMatrix, utils::operator""_mat;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TrivialPruneBounds0, BasicAssertions) {
  // A(5, 3) [1, M, m] constants, symbolic vars, loop vars
  //[0 1 0;
  //  -1 1 -1;
  //  0 0 1;
  //  -2 1 -1;
  // 1 0 1;]
  //  query = [1 0 0];
  // Constraints: {
  //  0 <= M; (0)
  //  -1 + M - m >= 0; (1)
  //  m >= 0; (2)
  //-2 + M - m >= 0 (3)
  // 1 + m >= 0;(4)
  //  diff = (3) - （4）
  // }
  // Our test: whether we could erase (1) or (3). query = (1) - (3)
  // swap and eliminate
  //
  // M >= 0
  // -1 + M - m >= 0
  // m >= 0
  // -2 + M - m >= 0
  // 1 + m >= 0
  IntMatrix<> A{"[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 1);
  poly::Loop *aff = tlf.getLoopNest(0);
  aff->pruneBounds();
  std::cout << *aff << "\naff.A = " << aff->getA() << "\n";
  // M >= 0 is redundant
  // because M - 1 >= m >= 0
  // hence, we should be left with 1 bound (-2 + M - m >= 0)
  EXPECT_EQ(ptrdiff_t(aff->getA().numRow()), 1);
  EXPECT_EQ(aff->getA(), "[-2 1 -1]"_mat);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TrivialPruneBounds1, BasicAssertions) {
  // i >= 1
  // I >= 1
  // i <= J - 1
  // J >= 1
  IntMatrix<> A{
    "[-1 0 0 0 1 0; -1 1 0 0 0 0; -1 0 1 0 -1 0; -1 0 1 0 0 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 2);
  poly::Loop *aff = tlf.getLoopNest(0);
  aff->pruneBounds(*tlf.getAlloc());
#ifndef NDEBUG
  aff->dump();
#endif
  std::cout << "aff.A = " << aff->getA() << "\n";
  // we expect J >= 1 to be dropped
  // because J >= i + 1 >= 2
  // because i >= 1
  EXPECT_EQ(ptrdiff_t(aff->getA().numRow()), 3);
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(LessTrivialPruneBounds, BasicAssertions) {

  // Ax * b >= 0
  IntMatrix<> A{"[-3 1 1 1 -1 -1 -1; "
                "0 0 0 0 1 1 1;"
                " -2 1 0 1 -1 0 -1;"
                " 0 0 0 0 1 0 1;"
                "0 0 0 0 0 1 0;"
                "-1 0 1 0 0 -1 0;"
                "-1 1 0 0 -1 0 0;"
                "0 0 0 0 1 0 0;"
                "0 0 0 0 0 0 1;"
                "-1 0 0 1 0 0 -1]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 3);
  poly::Loop &aff = *tlf.getLoopNest(0);

  aff.pruneBounds();
  std::cout << "LessTrivial test Bounds pruned:\n";
#ifndef NDEBUG
  aff.dump();
#endif
  std::cout << "aff.A = " << aff.getA() << "\n";
  EXPECT_EQ(aff.getNumCon(), 3);
  auto loop2Count = countSigns(aff.getA(), 2 + aff.getNumSymbols());
  EXPECT_EQ(loop2Count[0], 1);
  EXPECT_EQ(loop2Count[1], 0);
  auto *aff2 = aff.removeLoop(tlf.getAlloc(), 2);
  auto loop1Count = countSigns(aff2->getA(), 1 + aff2->getNumSymbols());
  EXPECT_EQ(loop1Count[0], 1);
  EXPECT_EQ(loop1Count[1], 0);
  auto *aff3 = aff2->removeLoop(tlf.getAlloc(), 1);
  auto loop0Count = countSigns(aff3->getA(), 0 + aff3->getNumSymbols());
  EXPECT_EQ(loop0Count[0], 1);
  EXPECT_EQ(loop0Count[1], 0);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(AffineTest0, BasicAssertions) {
  std::cout << "Starting affine test 0\n";
  // the loop is
  // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
  //
  IntMatrix<> A{"[-1 1 0 -1 0 0; "   // m <= M - 1
                "0 0 0 1 0 0; "      // m >= 0
                "-1 0 1 0 -1 0; "    // n <= N - 1
                "0 0 0 0 1 0; "      // n >= 0
                "-1 0 1 0 0 -1; "    // k <= N - 1
                "-1 0 0 0 -1 1; "    // k >= n + 1
                "0 1 0 0 0 0; "      // M >= 0
                "0 0 1 0 0 0]"_mat}; // N >= 0

  TestLoopFunction tlf;
  std::cout << "About to construct affine obj\n";
  tlf.addLoop(std::move(A), 3);
  poly::Loop &aff = *tlf.getLoopNest(0);
  aff.pruneBounds();
  EXPECT_EQ(aff.getA().numRow(), 3);

  std::cout << "Constructed affine obj\n";
  std::cout << "About to run first compat test\n";
  std::cout << "aff.getA() = " << aff.getA();
  EXPECT_FALSE(aff.zeroExtraItersUponExtending(*tlf.getAlloc(), 0, false));
  EXPECT_FALSE(aff.zeroExtraItersUponExtending(*tlf.getAlloc(), 0, true));
  EXPECT_TRUE(aff.zeroExtraItersUponExtending(*tlf.getAlloc(), 1, false));
  std::cout << "About to run second compat test\n";
  EXPECT_FALSE(aff.zeroExtraItersUponExtending(*tlf.getAlloc(), 1, true));
#ifndef NDEBUG
  aff.dump();
#endif
  std::cout << "About to run first set of bounds tests\n";
  std::cout << "\nPermuting loops 1 and 2\n";
  alloc::OwningArena<> allocator;
  utils::Valid<poly::Loop> affp021ptr{
    aff.rotate(&allocator, "[1 0 0; 0 0 1; 0 1 0]"_mat, nullptr)};
  poly::Loop &affp021 = *affp021ptr;
  // Now that we've swapped loops 1 and 2, we should have
  // for m in 0:M-1, k in 1:N-1, n in 0:k-1
#ifndef NDEBUG
  affp021.dump();
#endif
  // For reference, the permuted loop bounds are:
  // for m in 0:M-1, k in 1:N-1, n in 0:k-1
  std::cout << "Checking if the inner most loop iterates when adjusting "
               "outer loops:\n";
  std::cout << "Constructed affine obj\n";
  std::cout << "About to run first compat test\n";
  EXPECT_FALSE(affp021.zeroExtraItersUponExtending(*tlf.getAlloc(), 1, false));
  std::cout << "About to run second compat test\n";
  EXPECT_TRUE(affp021.zeroExtraItersUponExtending(*tlf.getAlloc(), 1, true));

  // affp021.zeroExtraIterationsUponExtending(poset, 1, )
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(NonUnimodularExperiment, BasicAssertions) {
  std::cout << "Starting affine test 1\n";
  IntMatrix<> A{"[0 2 1 -1; "
                "-2 0 -1 1; "
                "0 2 1 1; "
                "-2 0 -1 -1; "
                " 0 1 0 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 2);
  poly::Loop &aff = *tlf.getLoopNest(tlf.getNumLoopNests() - 1);
  std::cout << "Original order:\n";
#ifndef NDEBUG
  aff.dump();
#endif
  // -2 - i - j >= 0 -> i + j <= -2
  // but i >= 0 and j >= 0 -> isEmpty()
  aff.pruneBounds();
  EXPECT_TRUE(aff.isEmpty());

  DenseMatrix<int64_t> B = "[0 2 1 -1; "
                           "-2 0 -1 1; "
                           "0 2 1 1; "
                           "8 0 -1 -1; "
                           " 0 1 0 0]"_mat;
  tlf.addLoop(std::move(B), 2);
  poly::Loop &aff2 = *tlf.getLoopNest(tlf.getNumLoopNests() - 1);
  EXPECT_FALSE(aff2.isEmpty());
  alloc::OwningArena<> allocator;
  utils::Valid<poly::Loop> affp10{
    aff2.rotate(&allocator, "[0 1; 1 0]"_mat, nullptr)};

  std::cout << "Swapped order:\n";
#ifndef NDEBUG
  affp10->dump();
#endif
  EXPECT_FALSE(affp10->isEmpty());
}
