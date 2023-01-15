#include "Loops.hpp"
#include "Math/Constraints.hpp"
#include "Math/Math.hpp"
#include "MatrixStringParse.hpp"
#include "TestUtilities.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <memory>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(FourierMotzkin, BasicAssertions) {
  auto A{"[-2 1 0 -1 -1 0; -1 0 1 0 0 -1]"_mat};
  fourierMotzkinNonNegative(A, 3);
  auto B{"[-2 1 0 0 -1 0; -1 0 1 0 0 -1]"_mat};
  llvm::errs() << "A = " << A << "\nB = " << B << "\n";
  EXPECT_EQ(A, B);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TrivialPruneBounds, BasicAssertions) {
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
  auto A{"[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 1);
  AffineLoopNest<true> &aff = tlf.alns[0];
  aff.pruneBounds();
  llvm::errs() << aff << "\naff.A = " << aff.A << "\n";
  // M >= 0 is redundant
  // because M - 1 >= m >= 0
  // hence, we should be left with 1 bound (-2 + M - m >= 0)
  EXPECT_EQ(aff.A.numRow(), 1);
  EXPECT_EQ(aff.A, "[-2 1 -1]"_mat);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TrivialPruneBounds2, BasicAssertions) {
  // i >= 1
  // I >= 1
  // i <= J - 1
  // J >= 1
  auto A{"[-1 0 0 0 1 0; -1 1 0 0 0 0; -1 0 1 0 -1 0; -1 0 1 0 0 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 2);
  AffineLoopNest<true> &aff = tlf.alns[0];
  aff.pruneBounds();
  aff.dump();
  llvm::errs() << "aff.A = " << aff.A << "\n";
  // we expect J >= 1 to be dropped
  // because J >= i + 1 >= 2
  // because i >= 1
  EXPECT_EQ(aff.A.numRow(), 3);
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(LessTrivialPruneBounds, BasicAssertions) {

  // Ax * b >= 0
  IntMatrix A{"[-3 1 1 1 -1 -1 -1; "
              "0 0 0 0 1 1 1; "
              "-2 1 0 1 -1 0 -1; "
              "0 0 0 0 1 0 1; "
              "0 0 0 0 0 1 0; "
              "-1 0 1 0 0 -1 0; "
              "-1 1 0 0 -1 0 0; "
              "0 0 0 0 1 0 0; "
              "0 0 0 0 0 0 1; "
              "-1 0 0 1 0 0 -1]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 3);
  AffineLoopNest<true> &aff = tlf.alns[0];

  aff.pruneBounds();
  llvm::errs() << "LessTrival test Bounds pruned:\n";
  aff.dump();
  llvm::errs() << "aff.A = " << aff.A << "\n";
  EXPECT_EQ(aff.A.numRow(), 3);
  auto loop2Count = countSigns(aff.A, 2 + aff.getNumSymbols());
  EXPECT_EQ(loop2Count.first, 1);
  EXPECT_EQ(loop2Count.second, 0);
  aff.removeLoopBang(2);
  auto loop1Count = countSigns(aff.A, 1 + aff.getNumSymbols());
  EXPECT_EQ(loop1Count.first, 1);
  EXPECT_EQ(loop1Count.second, 0);
  aff.removeLoopBang(1);
  auto loop0Count = countSigns(aff.A, 0 + aff.getNumSymbols());
  EXPECT_EQ(loop0Count.first, 1);
  EXPECT_EQ(loop0Count.second, 0);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(AffineTest0, BasicAssertions) {
  llvm::errs() << "Starting affine test 0\n";
  // the loop is
  // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
  //
  IntMatrix A{"[-1 1 0 -1 0 0; "
              "0 0 0 1 0 0; "
              "-1 0 1 0 -1 0; "
              "0 0 0 0 1 0; "
              "-1 0 1 0 0 -1; "
              "-1 0 0 0 -1 1; "
              "0 1 0 0 0 0; "
              "0 0 1 0 0 0]"_mat};

  TestLoopFunction tlf;
  llvm::errs() << "About to construct affine obj\n";
  tlf.addLoop(std::move(A), 3);
  AffineLoopNest<true> &aff = tlf.alns[0];
  aff.pruneBounds();
  EXPECT_EQ(aff.A.numRow(), 3);

  llvm::errs() << "Constructed affine obj\n";
  llvm::errs() << "About to run first compat test\n";
  llvm::errs() << "aff.A.size() = (" << size_t(aff.A.numRow()) << ", "
               << size_t(aff.A.numCol()) << ")\n";
  EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(0, false));
  EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(0, true));
  EXPECT_TRUE(aff.zeroExtraIterationsUponExtending(1, false));
  llvm::errs() << "About to run second compat test\n";
  EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(1, true));
  aff.dump();
  llvm::errs() << "About to run first set of bounds tests\n";
  llvm::errs() << "\nPermuting loops 1 and 2\n";
  llvm::BumpPtrAllocator allocator;
  NotNull<AffineLoopNest<false>> affp021ptr{
    aff.rotate(allocator, "[1 0 0; 0 0 1; 0 1 0]"_mat)};
  AffineLoopNest<false> &affp021 = *affp021ptr;
  // Now that we've swapped loops 1 and 2, we should have
  // for m in 0:M-1, k in 1:N-1, n in 0:k-1
  affp021.dump();
  // For reference, the permuted loop bounds are:
  // for m in 0:M-1, k in 1:N-1, n in 0:k-1
  llvm::errs() << "Checking if the inner most loop iterates when adjusting "
                  "outer loops:"
               << "\n";
  llvm::errs() << "Constructed affine obj\n";
  llvm::errs() << "About to run first compat test\n";
  EXPECT_FALSE(affp021.zeroExtraIterationsUponExtending(1, false));
  llvm::errs() << "About to run second compat test\n";
  EXPECT_TRUE(affp021.zeroExtraIterationsUponExtending(1, true));

  // affp021.zeroExtraIterationsUponExtending(poset, 1, )
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(NonUnimodularExperiment, BasicAssertions) {
  llvm::errs() << "Starting affine test 1\n";
  IntMatrix A{"[0 2 1 -1; "
              "-2 0 -1 1; "
              "0 2 1 1; "
              "-2 0 -1 -1; "
              " 0 1 0 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 2);
  AffineLoopNest<true> &aff = tlf.alns.back();
  llvm::errs() << "Original order:\n";
  aff.dump();
  // -2 - i - j >= 0 -> i + j <= -2
  // but i >= 0 and j >= 0 -> isEmpty()
  aff.initializeComparator();
  aff.pruneBounds();
  EXPECT_TRUE(aff.isEmpty());

  A = "[0 2 1 -1; "
      "-2 0 -1 1; "
      "0 2 1 1; "
      "8 0 -1 -1; "
      " 0 1 0 0]"_mat;
  tlf.addLoop(std::move(A), 2);
  AffineLoopNest<true> &aff2 = tlf.alns.back();
  EXPECT_FALSE(aff2.isEmpty());
  llvm::BumpPtrAllocator allocator;
  NotNull<AffineLoopNest<false>> affp10{
    aff2.rotate(allocator, "[0 1; 1 0]"_mat)};

  llvm::errs() << "Swapped order:\n";
  affp10->dump();

  EXPECT_FALSE(affp10->isEmpty());
}
