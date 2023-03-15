#include "Math/Math.hpp"
#include "Math/Unimodularization.hpp"
#include "MatrixStringParse.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <random>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(UnimodularizationTest, BasicAssertions) {
  IntMatrix VE{"[0 1; 1 0; 0 1; 1 0]"_mat};
  llvm::errs() << "VE=\n" << VE << "\n";
  auto VB = unimodularize(VE);
  EXPECT_TRUE(VB.has_value());
  assert(VB.has_value());
  llvm::errs() << "VB:\n" << *VB << "\n";

  IntMatrix A23{"[9 5; -5 -2; 1 0]"_mat};
  auto B = unimodularize(A23);
  EXPECT_TRUE(B.has_value());
  assert(B.has_value());
  llvm::errs() << "B:\n" << *B << "\n";
  // EXPECT_EQ(j, length(bsc));
  // EXPECT_EQ(j, length(bs));

  IntMatrix A13{"[6; -5; 15]"_mat};
  auto test6_10_15 = unimodularize(A13); //, 1, 93, 1001);
  EXPECT_TRUE(test6_10_15.has_value());
  // if (test6_10_15.has_value()) {
  //     auto [r1, r2] = test6_10_15.get_value();
  //     auto [A10, A11, A12] = r1;
  //     auto [A20, A21, A22] = r2;
  //     llvm::errs() << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 <<
  //     ", "
  //               << A12 << " ]\n";
  //     llvm::errs() << "A(2,:): [ " << A20 << ", " << A21 << ", "
  //               << A22 << " ]\n";
  // }
  A13(0, 0) = 102;
  A13(1, 0) = 190;
  A13(2, 0) = 345;
  auto test102_190_345 = unimodularize(A13); //, 1, 93, 1001);
  EXPECT_TRUE(test102_190_345.has_value());
  // auto test102_190_345 = unimodularize2x3(102, 190, 345, 1, 0, 1);
  //  if (test102_190_345.has_value()) {
  //      auto [r1, r2] = test102_190_345.get_value();
  //      auto [A10, A11, A12] = r1;
  //      auto [A20, A21, A22] = r2;
  //      llvm::errs() << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 <<
  //      ",
  //      "
  //                << A12 << " ]\n";
  //      llvm::errs() << "A(2,:): [ " << A20 << ", " << A21 << ", "
  //                << A22 << " ]\n";
  //  }
}
