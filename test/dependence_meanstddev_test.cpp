#include <array>
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
#include "Math/Comparisons.cxx"
#include "Utilities/MatrixStringParse.cxx"
#else

import ArrayParse;
import Comparisons;
import TestUtilities;
#endif

using math::DenseMatrix, math::DenseDims, math::PtrMatrix, math::MutPtrMatrix,
  math::Vector, math::IntMatrix, math::last, math::_, utils::operator""_mat;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(MeanStDevTest0jOuter, BasicAssertions) {
  // jOuter variant:
  // for (i = 0; i < I; ++i){
  //    x[i] = 0;
  //    s[i] = 0;
  // }
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //      x[i] += A[j,i];
  // for (i = 0; i < I; ++i){
  //   x[i] /= J;
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //     d = (A[j,i] - x[i]);
  //     s[i] += d*d;
  //   }
  // }
  // for (i = 0; i < I; ++i)
  //   s[i] = sqrt(s[i] / (J-1));
  TestLoopFunction tlf;

  // FIXME: we don't have good tracking/of loop lengths for fusion
  poly::Loop *loopI = tlf.addLoop("[-1 1 -1; "
                                  "0 0 1]"_mat,
                                  1);
  poly::Loop *loopJI = tlf.addLoop("[-1 0 1 -1 0; "
                                   "0 0 0 1 0; "
                                   "-1 1 0 0 -1; "
                                   "0 0 0 0 1]"_mat,
                                   2);

  // create arrays
  IR::FunArg *ptrX = tlf.createArray();
  IR::FunArg *ptrA = tlf.createArray();
  IR::FunArg *ptrS = tlf.createArray();

  IR::Cache &ir = tlf.getIRC();

  IR::Value *one = tlf.getConstInt(1);
  IR::Value *zero = ir.createConstant(tlf.getDoubleTy(), 0.0);

  IR::Value *II = loopJI->getSyms()[0];
  IR::Value *J = loopJI->getSyms()[1];

  // for (i = 0; i < I; ++i){
  //    x[i] = 0;
  tlf.createStow(ptrX, zero, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 0]"_mat, loopI);
  //    s[i] = 0;
  tlf.createStow(ptrS, zero, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 1]"_mat, loopI);
  // }
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //      x[i] = x[i] + A[j,i];
  //      x[i] = x[i] + ->A[j,i]<-;
  IR::Addr *aloadacc{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat,
                                    std::array<IR::Value *, 2>{II, one},
                                    "[1 0 0]"_mat, loopJI)};
  //      x[i] = ->x[i]<- + A[j,i];
  IR::Addr *xloadacc{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1 0]"_mat,
                                    std::array<IR::Value *, 1>{one},
                                    "[1 0 1]"_mat, loopJI)};
  //   ->x[i]<- = x[i] + A[j,i];
  tlf.createStow(ptrX, ir.createFAdd(aloadacc, xloadacc), "[1 0]"_mat,
                 std::array<IR::Value *, 1>{one}, "[1 0 2]"_mat, loopJI);

  // for (i = 0; i < I; ++i){
  //   x[i] = x[i] / J;
  //    x[i] = ->x[i]<- / J;
  IR::Addr *xloadscale{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1]"_mat,
                                      std::array<IR::Value *, 1>{one},
                                      "[2 0]"_mat, loopI)};
  // ->x[i]<- = x[i] / J;
  tlf.createStow(ptrX, ir.createFDiv(xloadscale, ir.createSItoFP(J)), "[1]"_mat,
                 std::array<IR::Value *, 1>{one}, "[2 1]"_mat, loopI);
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //     d = (A[j,i] - x[i]);
  //     d = (->A[j,i]<- - x[i]);
  IR::Addr *aloadss{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat,
                                   std::array<IR::Value *, 2>{II, one},
                                   "[3 0 0]"_mat, loopJI)};
  //     d = (A[j,i] - ->x[i]<- );
  IR::Addr *xloadss{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1 0]"_mat,
                                   std::array<IR::Value *, 1>{one},
                                   "[3 0 1]"_mat, loopJI)};
  //     s[i] = ->s[i]<- + d*d;
  IR::Addr *sloadss{tlf.createLoad(ptrS, tlf.getDoubleTy(), "[1 0]"_mat,
                                   std::array<IR::Value *, 1>{one},
                                   "[3 0 2]"_mat, loopJI)};
  // s[i] + d*d;
  auto *ss =
    ir.createFAdd(sloadss, ir.createFMul(ir.createFSub(aloadss, xloadss),
                                         ir.createFSub(aloadss, xloadss)));
  //   ->s[i]<- = s[i] + d*d;
  tlf.createStow(ptrS, ss, "[1 0]"_mat, std::array<IR::Value *, 1>{one},
                 "[3 0 3]"_mat, loopJI);

  // for (i = 0; i < I; ++i)
  //   s[i] = sqrt(s[i] / (J-1));
  //   s[i] = sqrt(->s[i]<- / (J-1));
  IR::Addr *sloadsqrt{tlf.createLoad(ptrS, tlf.getDoubleTy(), "[1]"_mat,
                                     std::array<IR::Value *, 1>{one},
                                     "[4 0]"_mat, loopI)};
  auto *sqrt = ir.createSqrt(ir.createFDiv(
    sloadsqrt,
    ir.createSItoFP(ir.createSub(J, ir.createConstant(tlf.getInt64Ty(), 1)))));
  //   ->s[i]<- = sqrt(s[i] / (J-1));
  tlf.createStow(ptrS, sqrt, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[4 1]"_mat, loopI);

  alloc::OwningArena salloc;
  poly::Dependencies deps{};
  lp::LoopBlock jblock{deps, salloc};
  lp::LoopBlock::OptimizationResult optRes =
    jblock.optimize(ir, tlf.getTreeResult());
  EXPECT_NE(optRes.nodes, nullptr);

  DenseMatrix<int64_t> optS{math::SquareDims<>{math::row(2)}, 0};
  // we want antiDiag, as that represents swapping loops
  optS.antiDiag() << 1;
  size_t jj = 0;
  for (auto *node : optRes.nodes->getVertices()) {
    std::cout << "v_" << jj++ << ":\nInput edges:";
    for (auto edge : node->inputEdges(deps))
      std::cout << "\nedge = " << edge << "\n";
    std::cout << "\nmem =";
    for (IR::Addr *a : node->localAddr()) std::cout << *a << "\n";
    std::cout << *node;
    poly::AffineSchedule s = node->getSchedule();
    if (s.getNumLoops() == 1) EXPECT_EQ((s.getPhi()[0, 0]), 1);
    else EXPECT_EQ(s.getPhi(), optS);
  }
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(MeanStDevTest0iOuter, BasicAssertions) {
  // iOuter variant:
  // for (i = 0; i < I; ++i){
  //   x[i] = 0; // [0]
  //   for (j = 0; j < J; ++j)
  //     x[i] += A[j,i]; // [1,0:2]
  //   x[i] /= J;
  //   s[i] = 0;
  //   for (j = 0; j < J; ++j){
  //     d = (A[j,i] - x[i]);
  //     s[i] += d*d;
  //   }
  //   s[i] = sqrt(s[i] / (J-1));
  // }
  TestLoopFunction tlf;

  poly::Loop *loopIJ = tlf.addLoop("[-1 1 0 -1 0; "
                                   "0 0 0 1 0; "
                                   "-1 0 1 0 -1; "
                                   "0 0 0 0 1]"_mat,
                                   2);

  // create arrays
  IR::FunArg *ptrX = tlf.createArray();
  IR::FunArg *ptrA = tlf.createArray();
  IR::FunArg *ptrS = tlf.createArray();

  IR::Cache &ir = tlf.getIRC();

  IR::Value *one = tlf.getConstInt(1);
  IR::Value *zero = ir.createConstant(tlf.getDoubleTy(), 0.0);

  IR::Value *II = loopIJ->getSyms()[0];
  IR::Value *J = loopIJ->getSyms()[1];

  // for (i = 0; i < I; ++i){
  //   x[i] = 0; // [0]
  tlf.createStow(ptrX, zero, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 0]"_mat, loopIJ);
  //   for (j = 0; j < J; ++j)
  //     x[i] = x[i] + A[j,i]; // [1,0:2]
  IR::Addr *aloadacc{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat,
                                    std::array<IR::Value *, 2>{II, one},
                                    "[0 1 0]"_mat, loopIJ)};
  IR::Addr *xloadacc{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1 0]"_mat,
                                    std::array<IR::Value *, 1>{one},
                                    "[0 1 1]"_mat, loopIJ)};
  tlf.createStow(ptrX, ir.createFAdd(aloadacc, xloadacc), "[1 0]"_mat,
                 std::array<IR::Value *, 1>{one}, "[0 1 2]"_mat, loopIJ);
  //   x[i] = x[i] / J;
  IR::Addr *xloadscale{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1]"_mat,
                                      std::array<IR::Value *, 1>{one},
                                      "[0 2]"_mat, loopIJ)};
  tlf.createStow(ptrX, ir.createFDiv(xloadscale, ir.createSItoFP(J)), "[1]"_mat,
                 std::array<IR::Value *, 1>{one}, "[0 3]"_mat, loopIJ);
  //   s[i] = 0;
  tlf.createStow(ptrS, zero, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 4]"_mat, loopIJ);
  //   for (j = 0; j < J; ++j){
  //     d = (A[j,i] - x[i]);
  IR::Addr *aloadss{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat,
                                   std::array<IR::Value *, 2>{II, one},
                                   "[0 5 0]"_mat, loopIJ)};
  IR::Addr *xloadss{tlf.createLoad(ptrX, tlf.getDoubleTy(), "[1 0]"_mat,
                                   std::array<IR::Value *, 1>{one},
                                   "[0 5 1]"_mat, loopIJ)};
  //     s[i] = s[i] + d*d;
  IR::Addr *sloadss{tlf.createLoad(ptrS, tlf.getDoubleTy(), "[1 0]"_mat,
                                   std::array<IR::Value *, 1>{one},
                                   "[0 5 2]"_mat, loopIJ)};
  auto *ss =
    ir.createFAdd(sloadss, ir.createFMul(ir.createFSub(aloadss, xloadss),
                                         ir.createFSub(aloadss, xloadss)));
  tlf.createStow(ptrS, ss, "[1 0]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 5 3]"_mat, loopIJ);
  //   }
  //   s[i] = sqrt(s[i] / (J-1));
  IR::Addr *sloadsqrt{tlf.createLoad(ptrS, tlf.getDoubleTy(), "[1]"_mat,
                                     std::array<IR::Value *, 1>{one},
                                     "[0 6]"_mat, loopIJ)};
  auto *sqrt = ir.createSqrt(ir.createFDiv(
    sloadsqrt,
    ir.createSItoFP(ir.createSub(J, ir.createConstant(tlf.getInt64Ty(), 1)))));
  //   ->s[i]<- = sqrt(s[i] / (J-1));
  tlf.createStow(ptrS, sqrt, "[1]"_mat, std::array<IR::Value *, 1>{one},
                 "[0 7]"_mat, loopIJ);

  alloc::OwningArena salloc;
  poly::Dependencies deps{};
  lp::LoopBlock iblock{deps, salloc};
  lp::LoopBlock::OptimizationResult opt_res =
    iblock.optimize(ir, tlf.getTreeResult());
  EXPECT_NE(opt_res.nodes, nullptr);

  DenseMatrix<int64_t> opt_s{math::SquareDims<>{math::row(2)}, 0};
  // We want `diag`, as it shouldn't be swapping loops when `i` is already outer
  opt_s.diag() << 1;
  size_t jj = 0;
  for (auto *node : opt_res.nodes->getVertices()) {
    std::cout << "v_" << jj++ << ":\nInput edges:";
    for (auto edge : node->inputEdges(deps))
      std::cout << "\nedge = " << edge << "\n";
    std::cout << "\nmem =";
    for (IR::Addr *a : node->localAddr()) std::cout << *a << "\n";
    std::cout << *node;
    poly::AffineSchedule s = node->getSchedule();
    if (s.getNumLoops() == 1) EXPECT_EQ((s.getPhi()[0, 0]), 1);
    else EXPECT_EQ(s.getPhi(), opt_s);
  }
}