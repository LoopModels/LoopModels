#include <cassert>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#ifndef USE_MODULE
#include "TestUtilities.cxx"
#include "Optimize/Legality.cxx"
#include "IR/IR.cxx"
#include "Math/Comparisons.cxx"
#include "Utilities/MatrixStringParse.cxx"
#include "Math/Array.cxx"
#else

import Array;
import ArrayParse;
import Comparisons;
import IR;
import Legality;
import TestUtilities;
#endif

using math::DenseMatrix, math::DenseDims, math::PtrMatrix, math::MutPtrMatrix,
  math::Vector, math::IntMatrix, math::last, math::_, utils::operator""_mat;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(RankDeficientLoad, BasicAssertions) {

  // for (i = 0:I-1){
  //   for (j = 0:i){
  //     A(i,j) = A(i,i);
  //   }
  // }
  // A*x <= b
  // [ 1   0     [i        [ I - 1
  //  -1   0   *  j ]        0
  //  -1   1           <=    0
  //   0  -1 ]               0     ]
  //
  TestLoopFunction tlf;
  poly::Loop *loop = tlf.addLoop("[-1 1 -1 0; "  // i <= I-1
                                 "0 0 1 0; "     // i >= 0
                                 "0 0 1 -1; "    // j <= i
                                 "0 0 0 1]"_mat, // j >= 0
                                 2);
  auto *ptrA = tlf.createArray();
  IR::Value *M = loop->getSyms()[0];
  IR::Cint *one = tlf.getConstInt(1);

  IR::Addr *msrc{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 1 0]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 0 0]"_mat, loop)};
  IR::Addr *mtgt{tlf.createStow(ptrA, msrc, "[1 0; 0 1]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 0 1]"_mat, loop)};

  poly::Dependencies deps{};
  deps.check(tlf.getAlloc(), mtgt, msrc);
  // mtgt <- msrc
  int32_t e = mtgt->getEdgeIn();
  EXPECT_EQ(e, msrc->getEdgeOut());
  EXPECT_NE(e, -1);
  EXPECT_EQ(mtgt->getEdgeOut(), -1);
  EXPECT_EQ(msrc->getEdgeIn(), -1);
  EXPECT_FALSE(deps[e].isForward());
  std::cout << "Blog post example:\n" << deps[e] << "\n";
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TimeHidingInRankDeficiency, BasicAssertions) {
  // for (i = 0; i < I; ++i)
  //   for (j = 0; j < J; ++j)
  //     for (k = 0; k < K; ++k)
  //       A(i+j, j+k, i-k) = foo(A(i+j, j+k, i-k));
  //
  // Indexed by three LIVs, and three dimensional
  // but memory access pattern is only rank 2, leaving
  // a time dimension of repeated memory accesses.
  // A*x <= b
  // [ 1   0  0     [i        [ I - 1
  //  -1   0  0   *  j          0
  //   0   1  0      k ]    <=  J - 1
  //   0  -1  0 ]               0
  //   0   0  1 ]               K - 1
  //   0   0 -1 ]               0     ]
  //
  TestLoopFunction tlf;
  auto *loop = tlf.addLoop("[-1 1 0 0 0 0 -1; "
                           "0 0 0 0 0 0 1; "
                           "-1 0 1 0 0 -1 0; "
                           "0 0 0 0 0 1 0; "
                           "-1 0 0 1 -1 0 0; "
                           "0 0 0 0 1 0 0]"_mat,
                           3);

  IR::Value *II = loop->getSyms()[0];
  IR::Value *J = loop->getSyms()[1];
  IR::Value *K = loop->getSyms()[2];

  auto *ptrA = tlf.createArray();
  IR::Value *one = tlf.getConstInt(1);
  IR::Cache &ir = tlf.getIRC();

  IR::Addr *mtgt{tlf.createLoad(
    ptrA, tlf.getDoubleTy(), "[1 1 0; 0 1 1; 1 0 -1]"_mat,
    std::array<IR::Value *, 3>{ir.createAdd(J, K), ir.createAdd(II, K), one},
    "[0 0 0 0]"_mat, loop)};
  IR::Addr *msrc{tlf.createStow(
    ptrA, mtgt, "[1 1 0; 0 1 1; 1 0 -1]"_mat,
    std::array<IR::Value *, 3>{ir.createAdd(J, K), ir.createAdd(II, K), one},
    "[0 0 0 1]"_mat, loop)};

  poly::Dependencies deps{};
  deps.check(tlf.getAlloc(), msrc, mtgt);
  int32_t e0 = msrc->getEdgeIn();
  int32_t e1 = msrc->getEdgeOut();
  EXPECT_NE(e0, -1);
  EXPECT_NE(e1, -1);
  EXPECT_EQ(e0, mtgt->getEdgeOut());
  EXPECT_EQ(e1, mtgt->getEdgeIn());
  std::cout << "Rank deficicient example:\nForward:\n"
            << deps[e0] << "\nReverse:\n"
            << deps[e1] << "\n";
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(DoubleDependenceTest, BasicAssertions) {

  TestLoopFunction tlf;
  poly::Loop *loop = tlf.addLoop("[-2 1 0 -1 0; "
                                 "0 0 0 1 0; "
                                 "-2 0 1 0 -1; "
                                 "0 0 0 0 1]"_mat,
                                 2);

  // create arrays
  IR::FunArg *ptrA = tlf.createArray();
  IR::Cache &ir = tlf.getIRC();

  IR::Value *II = loop->getSyms()[1];
  IR::Cint *one = tlf.getConstInt(1);

  IR::Addr *mtgt01{
    tlf.createLoad(ptrA, tlf.getDoubleTy(), "[0 1; 1 0]"_mat, "[0 1]"_mat,
                   std::array<IR::Value *, 2>{II, one}, "[0 0 0]"_mat, loop)};
  IR::Addr *mtgt10{
    tlf.createLoad(ptrA, tlf.getDoubleTy(), "[0 1; 1 0]"_mat, "[1 0]"_mat,
                   std::array<IR::Value *, 2>{II, one}, "[0 0 1]"_mat, loop)};

  IR::Addr *msrc{tlf.createStow(
    ptrA, ir.createFAdd(mtgt01, mtgt10), "[0 1; 1 0]"_mat, "[1 1]"_mat,
    std::array<IR::Value *, 2>{II, one}, "[0 0 2]"_mat, loop)};

  // for (i = 0:I-2)   // satisfies A[j+1,i+1] -> A[j+1,i]
  //   for (j = 0:J-2) // satisfies A[j+1,i+1] -> A[j,i+1]
  //     A[j+1,i+1] = A[j,i+1] + A[j+1,i];
  // A*x >= 0;
  // [ -2  1  0 -1  0    [ 1
  //    0  0  0  1  0  *   I   >= 0
  //   -2  0  1  0 -1      J
  //    0  0  0  0  1 ]    i
  //                       j ]
  // we have three array refs
  // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
  //
  // Current bug:
  // Rotation swapping `i` and `j` applied, so `j` is outer loop
  // msrc->mtgt10 [1, 0], i.e. A[j+1,i] satisfied according to simplex
  // msrc->mtgt01 [0, 1], i.e. A[j,i+1] satisfied according to `checkEmptySat`
  // With `j` as the outer loop, only `msrc->mtgt01` should be satisfied
  // so the simplex appears to be wrong?
  poly::DepPoly *dep0{poly::DepPoly::dependence(tlf.getAlloc(), msrc, mtgt01)};
  EXPECT_FALSE(dep0->isEmpty());
  dep0->pruneBounds();
  std::cout << "Dep0 = \n" << dep0 << "\n";

  ASSERT_EQ(dep0->getNumInequalityConstraints(), 4);
  ASSERT_EQ(dep0->getNumEqualityConstraints(), 2);
  Vector<int64_t, 4> schLoad1(math::length(2 + 1), 0);
  schLoad1[2] = 1;
  poly::DepPoly *dep1{poly::DepPoly::dependence(tlf.getAlloc(), msrc, mtgt10)};
  EXPECT_FALSE(dep1->isEmpty());
  dep1->pruneBounds();
  std::cout << "Dep1 = \n" << dep1 << "\n";
  EXPECT_EQ(dep1->getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep1->getNumEqualityConstraints(), 2);
  if (dep1->getNumInequalityConstraints() != 4) __builtin_trap();
  if (dep1->getNumEqualityConstraints() != 2) __builtin_trap();
  poly::Dependencies deps{};
  {
    deps.check(tlf.getAlloc(), msrc, mtgt01);
    EXPECT_EQ(deps.size(), 1);
    poly::Dependence d0{deps[0]};
    EXPECT_TRUE(d0.isForward());
    std::cout << d0 << "\n";
    if (!d0.isForward()) __builtin_trap();
    if (allZero(d0.getSatConstraints()[last, _])) __builtin_trap();
    deps.check(tlf.getAlloc(), msrc, mtgt10);
    EXPECT_EQ(deps.size(), 2);
    poly::Dependence d1{deps[1]};
    EXPECT_TRUE(d1.isForward());
    std::cout << d1 << "\n";
    if (!d1.isForward()) __builtin_trap();
    if (allZero(d1.getSatConstraints()[last, _])) __builtin_trap();
  }
  for (IR::Addr *A : tlf.getTreeResult().getAddr()) {
    A->setEdgeIn(-1);
    A->setEdgeOut(-1);
  }
  deps.clear();

  alloc::OwningArena salloc;
  lp::LoopBlock loopBlock{deps, salloc};
  lp::LoopBlock::OptimizationResult optRes =
    loopBlock.optimize(ir, tlf.getTreeResult());
  EXPECT_EQ(deps.size(), 2);

  EXPECT_NE(optRes.nodes, nullptr);
  DenseMatrix<int64_t> optPhi(math::DenseDims<>{math::row(2), math::col(2)}, 1);
  optPhi[1, 1] = 0;
  size_t numEdges = 0;
  for (auto *node : optRes.nodes->getAllVertices()) {
    for (auto e : node->outputEdges(deps)) {
      ++numEdges;
      auto [i, o] = e.getInOutPair();
      std::cout << "\nEdge for array " << *e.getArrayPointer() << ", &in: " << i
                << "; &out: " << o << "\nSat: " << int(e.satLevel()) << "\n";
    }
    std::cout << "\nmem =";
    for (IR::Addr *a : node->localAddr()) std::cout << *a << "\n";
    std::cout << *node;
    poly::AffineSchedule s = node->getSchedule();
    EXPECT_EQ(s.getPhi(), optPhi);
    EXPECT_TRUE(allZero(s.getOffsetOmega()));
    EXPECT_TRUE(allZero(s.getFusionOmega()));
  }
  EXPECT_EQ(numEdges, 2);

  // Graphs::print(iOuterLoopNest.fullGraph());
}

inline auto addrChainLen(const TestLoopFunction &tlf) -> int {
  int len = 0;
  for (auto *_ : tlf.getTreeResult().getAddr()) ++len;
  return len;
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(ConvReversePass, BasicAssertions) {
  // for (n = 0; n < N; ++n){
  //   for (m = 0; n < M; ++m){
  //     for (j = 0; n < J; ++j){
  //       for (i = 0; n < I; ++i){
  //         C[j+n,m+i] += A[n,m] * B[j,i];
  //       }
  //     }
  //   }
  // }
  TestLoopFunction tlf;
  IR::Cache &ir = tlf.getIRC();
  // syms: N, M, J, I
  poly::Loop *loop = tlf.addLoop("[-1 0 1 0 0 0 -1 0 0; "
                                 "0 0 0 0  0 0 1 0 0; "
                                 "-1 1 0 0 0 -1 0 0 0; "
                                 "0 0 0 0  0 1 0 0 0; "
                                 "-1 0 0 0 1 0 0 0 -1; "
                                 "0 0 0 0 0 0 0 0 1; "
                                 "-1 0 0 1 0 0 0 -1 0; "
                                 "0 0 0 0 0 0 0 1 0]"_mat,
                                 4);

  IR::Value *II = loop->getSyms()[3];
  IR::Value *M = loop->getSyms()[1];

  // create arrays
  llvm::Type *f64 = tlf.getDoubleTy();
  IR::FunArg *ptrB = tlf.createArray();
  IR::FunArg *ptrA = tlf.createArray();
  IR::FunArg *ptrC = tlf.createArray();
  IR::Cint *one = tlf.getConstInt(1);
  EXPECT_EQ(addrChainLen(tlf), 0);
  IR::Addr *loadA{tlf.createLoad(ptrA, f64, "[1 0 0 0; 0 1 0 0]"_mat,
                                 std::array<IR::Value *, 2>{M, one},
                                 "[0 0 0 0 0]"_mat, loop)};
  EXPECT_EQ(addrChainLen(tlf), 1);
  IR::Addr *loadB{tlf.createLoad(ptrB, f64, "[0 0 1 0; 0 0 0 1]"_mat,
                                 std::array<IR::Value *, 2>{II, one},
                                 "[0 0 0 0 1]"_mat, loop)};
  EXPECT_EQ(addrChainLen(tlf), 2);

  IR::Addr *loadC{tlf.createLoad(
    ptrC, f64, "[1 0 1 0; 0 1 0 1]"_mat,
    std::array<IR::Value *, 2>{ir.createSub(ir.createAdd(M, II), one), one},
    "[0 0 0 0 2]"_mat, loop)};
  EXPECT_EQ(addrChainLen(tlf), 3);
  IR::Addr *stowC{tlf.createStow(
    ptrC, ir.createFAdd(loadC, ir.createFMul(loadA, loadB)),
    "[1 0 1 0; 0 1 0 1]"_mat,
    std::array<IR::Value *, 2>{ir.createSub(ir.createAdd(M, II), one), one},
    "[0 0 0 0 3]"_mat, loop)};
  EXPECT_EQ(addrChainLen(tlf), 4);

  // for (n = 0; n < N; ++n){
  //   for (m = 0; n < M; ++m){
  //     for (j = 0; n < J; ++j){
  //       for (i = 0; n < I; ++i){
  //         C[n+j,m+i] = C[n+j,m+i] + A[n,m] * B[j,i];
  //       }
  //     }
  //   }
  // }
  alloc::OwningArena salloc;
  poly::Dependencies deps{};
  lp::LoopBlock loopBlock{deps, salloc};
  lp::LoopBlock::OptimizationResult optRes =
    loopBlock.optimize(ir, tlf.getTreeResult());
  EXPECT_NE(optRes.nodes, nullptr);
  for (auto *node : optRes.nodes->getAllVertices()) {
    for (auto e : node->outputEdges(deps)) {
      auto [i, o] = e.getInOutPair();
      std::cout << "\nEdge for array " << *e.getArrayPointer()
                << ", in ID: " << i << "; out ID: " << o << "\n";
    }
    std::cout << "\nmem =";
    for (IR::Addr *a : node->localAddr()) std::cout << *a << "\n";
    std::cout << *node;
    poly::AffineSchedule s = node->getSchedule();
    std::cout << "s.getPhi(): " << s.getPhi() << "\n";
    EXPECT_TRUE(allZero(s.getOffsetOmega()));
    EXPECT_TRUE(allZero(s.getFusionOmega()));
  }
  EXPECT_TRUE(allZero(loadA->getFusionOmega())); // shouldn't have changed!
  EXPECT_TRUE(loadB->getFusionOmega() ==
              "[0 0 0 0 1]"_mat); // shouldn't have changed!
  EXPECT_TRUE(loadC->getFusionOmega() ==
              "[0 0 0 0 2]"_mat); // shouldn't have changed!
  EXPECT_TRUE(stowC->getFusionOmega() ==
              "[0 0 0 0 3]"_mat); // shouldn't have changed!

  auto tr = tlf.getTreeResult();
  auto addrIter = tr.getAddr();

  std::cout << "==================================" << '\n';
  for (auto *addr : addrIter) {
    auto omega = addr->getFusionOmega();
    auto loopi = addr->getAffineLoop();
    auto A = loopi->getA();
    std::cout << "omega = " << omega << '\n';
    std::cout << "A = " << A << '\n';
    std::cout << "==================================" << '\n';
  }
}