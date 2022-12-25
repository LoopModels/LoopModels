#include "../include/Comparators.hpp"
#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <memory>

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BasicCompare, BasicAssertions) {

  // TEST full column rank case of A
  // This is an example from ordering blog https://spmd.org/posts/ordering/
  // Move all the variables to one side of the inequality and make it larger
  // than zero and represent them in a matrix A, such that we could have
  // assembled Ax >= 0
  IntMatrix A = "[-1 0 1 0 0; 0 -1 1 0 0; 0 0 -1 1 0; 0 0 -1 0 1]"_mat;
  auto comp = LinearSymbolicComparator::construct(std::move(A), false);
  Vector<int64_t> query{-1, 0, 0, 1, 0};

  // llvm::SmallVector<int64_t, 16> query{1, 0, 0, -1, 0};
  EXPECT_TRUE(comp.greaterEqual(query));

  // TEST column deficient rank case of A
  //  We add two more constraints to the last example
  //  we add x >= a; b >= a
  IntMatrix A2 = "[-1 0 1 0 0; 0 -1 1 0 0; 0 0 -1 1 0; 0 0 "
                 "-1 0 1; -1 1 0 0 0; -1 0 0 1 0]"_mat;
  auto comp2 = LinearSymbolicComparator::construct(std::move(A2), false);
  Vector<int64_t> query2{-1, 0, 0, 0, 1};
  Vector<int64_t> query3{0, 0, 0, -1, 1};
  EXPECT_TRUE(comp2.greaterEqual(query2));
  EXPECT_TRUE(!comp2.greaterEqual(query3));

  // TEST on non identity diagonal case
  // We change the final constraint to x >= 2a + b
  // Vector representation of the diagonal matrix will become [1, ... , 1, 2]
  IntMatrix A3 = "[-1 0 1 0 0; 0 -1 1 0 0; 0 0 -1 1 0; 0 0 "
                 "-1 0 1; -1 1 0 0 0; -2 -1 0 1 0]"_mat;
  auto comp3 = LinearSymbolicComparator::construct(std::move(A3), false);
  // Vector<int64_t> query2{-1, 0, 0, 1, 0};
  //  Vector<int64_t> query3{0, 0, 0, -1, 1};
  Vector<int64_t> query4{-3, 0, 0, 1, 0}; // x >= 3a is expected to be true
  Vector<int64_t> query5{
    0, 0, 0, 1, -1}; // we could not identity the relation between x and y
  Vector<int64_t> query6{
    0, -2, 0, 1, 0}; // we could not know whether x is larger than 2b or not
  EXPECT_TRUE(comp3.greaterEqual(query2));
  // llvm::errs() <<  "comp3 wrong test " << comp3.greaterEqual(query3)
  // <<"\n";
  EXPECT_TRUE(!comp3.greaterEqual(query3));
  EXPECT_TRUE(!comp3.greaterEqual(query5));
  EXPECT_TRUE(comp3.greaterEqual(query4));
  EXPECT_TRUE(!comp3.greaterEqual(query6));
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(V2Matrix, BasicAssertions) {
  IntMatrix A = "[0 -1 0 1 0 0; 0 0 -1 1 0 0; 0 0 0 1 -1 0; 0 0 0 1 0 -1]"_mat;
  // IntMatrix A = " [1 0 0 0 0 0 0 0 0 0 1 1; 0 1 0 0 0 0 0
  // 0 0 0 -1 0; 0 0 1 0 0 0 0 0 0 0 0 1; 0 0 0 1 0 0 0 0 0 0 0 0; 0 0 0 0 1 0
  // 0 0 0 0 -1 0; 0 0 0 0 0 1 0 0 0 0 0 -1; 0 0 0 0 0 0 1 0 0 0 1 1; 0 0 0 0
  // 0 0 0 1 0 0 -1 0; 0 0 0 0 0 0 0 0 1 0 0 1; 0 0 0 0 0 0 0 0 0 1 0 0]"_mat;
  auto comp = LinearSymbolicComparator::construct(A, false);
  auto [H, U] = NormalForm::hermite(std::move(A));
  IntMatrix Ht = H.transpose();
  // llvm::errs() << "Ht matrix:" << Ht << "\n";
  auto Vt = IntMatrix::identity(Ht.numRow());
  auto NS = NormalForm::nullSpace(Ht);
  NormalForm::solveSystem(Ht, Vt);
  // NormalForm::solveSystem(NormalForm::solvePair(Ht, Vt));

  // llvm::errs() << "Null space matrix:" << NS << "\n";
  // llvm::errs() << "Diagonal matrix:" << Ht << "\n";
  // llvm::errs() << "Transposed V matrix:" << Vt << "\n";
  auto NSrow = NS.numRow();
  auto NScol = NS.numCol();
  auto offset = Vt.numRow() - NS.numRow();
  for (size_t i = 0; i < NSrow; ++i)
    for (size_t j = 0; j < NScol; ++j)
      EXPECT_EQ(NS(i, j), Vt(offset + i, j));
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(ConstantTest, BasicAssertions) {
  auto A{"[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]"_mat};
  auto comp = LinearSymbolicComparator::construct(A);
  Vector<int64_t> query0{-1, 0, 0};
  Vector<int64_t> query1{1, 0, 0};
  EXPECT_FALSE(comp.greaterEqual(query0));
  EXPECT_TRUE(comp.greaterEqual(query1));
  EXPECT_FALSE(comp.isEmpty());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(ConstantTest2, BasicAssertions) {
  auto A{"[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]"_mat};
  auto comp = LinearSymbolicComparator::construct(A, false);
  Vector<int64_t> query0{-1, 0, 0};
  Vector<int64_t> query1{1, 0, 0};
  EXPECT_FALSE(comp.greaterEqual(query0));
  EXPECT_FALSE(comp.greaterEqual(query1));
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(EqTest, BasicAssertions) {
  IntMatrix A{
    "[-2 1 0 -1 0 0 0; 0 0 0 1 0 0 0; -2 0 1 0 -1 0 0; 0 0 0 0 1 0 0; -2 1 "
    "0 0 0 -1 0; 0 0 0 0 0 1 0; -2 0 1 0 0 0 -1; 0 0 0 0 0 0 1]"_mat};
  IntMatrix E{"[1 0 0 1 0 -1 0; 1 0 0 0 1 0 -1]"_mat};
  auto comp = LinearSymbolicComparator::construct(A, E);
  Vector<int64_t> diff = A(7, _) - A(3, _);
  EXPECT_TRUE(comp.greaterEqual(diff));
  EXPECT_TRUE(comp.greater(diff));
  diff *= -1;
  EXPECT_FALSE(comp.greaterEqual(diff));
  EXPECT_FALSE(comp.isEmpty());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TestEmpty, BasicAssertions) {
  IntMatrix A{
    "[0 0 1 0 0 0; -1 1 -1 0 0 0; 0 0 0 1 0 0; -1 0 1 -1 0 0; 0 0 0 0 1 0; "
    "-1 1 0 0 -1 0; 0 0 0 0 0 1; -1 0 0 0 1 -1]"_mat};
  // Empty
  IntMatrix E0{"[0 0 1 0 0 -1; 0 0 0 1 -1 0]"_mat};
  // not Empty
  IntMatrix E1{"[0 0 1 0 -1 0; 0 0 0 1 0 -1]"_mat};
  Vector<int64_t> zeros{0, 0, 0, 0, 0, 0};
  auto compEmpty = LinearSymbolicComparator::construct(A, E0);
  // contradiction, 0 can't be less than 0
  EXPECT_TRUE(compEmpty.greater(zeros));
  // contradiction, 0 can't be greater than 0
  EXPECT_TRUE(compEmpty.less(zeros));
  EXPECT_TRUE(compEmpty.greaterEqual(zeros));
  EXPECT_TRUE(compEmpty.lessEqual(zeros));
  EXPECT_TRUE(compEmpty.isEmpty());
  auto compNonEmpty = LinearSymbolicComparator::construct(A, E1);
  // contradiction, 0 can't be less than 0
  EXPECT_FALSE(compNonEmpty.greater(zeros));
  // contradiction, 0 can't be greater than 0
  EXPECT_FALSE(compNonEmpty.less(zeros));
  EXPECT_TRUE(compNonEmpty.greaterEqual(zeros));
  EXPECT_TRUE(compNonEmpty.lessEqual(zeros));
  EXPECT_FALSE(compNonEmpty.isEmpty());
}
