
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
TEST(DependenceTest, BasicAssertions) {

  // for (i = 0:I-2){
  //   for (j = 0:J-2){
  //     A[i+1,j+1] = A[i+1,j] + A[i,j+1];
  //   }
  // }
  // A*x >= 0;
  // [ -2  1  0 -1  0    [ 1
  //    0  0  0  1  0  *   I   >= 0
  //   -2  0  1  0 -1      J
  //    0  0  0  0  1 ]    i
  //                       j ]
  TestLoopFunction tlf;

  poly::Loop *loop = tlf.addLoop("[-2 1 0 0 -1; "  // j <= I - 2
                                 "0 0 0 0 1; "     // j >= 0
                                 "-2 0 1 -1 0; "   // i <= J - 2
                                 "0 0 0 1 0]"_mat, // i >= 0
                                 2);
  IR::FunArg *ptrA = tlf.createArray();
  IR::Cache &ir = tlf.getIRC();

  IR::Value *M = loop->getSyms()[0];
  IR::Cint *one = tlf.getConstInt(1);
  IR::Addr *mtgt01{
    tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat, "[1 0]"_mat,
                   std::array<IR::Value *, 2>{M, one}, "[0 0 0]"_mat, loop)};
  IR::Addr *mtgt10{
    tlf.createLoad(ptrA, tlf.getDoubleTy(), "[1 0; 0 1]"_mat, "[0 1]"_mat,
                   std::array<IR::Value *, 2>{M, one}, "[0 0 1]"_mat, loop)};

  IR::Addr *msrc{tlf.createStow(
    ptrA, ir.createFAdd(mtgt01, mtgt10), "[1 0; 0 1]"_mat, "[1 1]"_mat,
    std::array<IR::Value *, 2>{M, one}, "[0 0 2]"_mat, loop)};

  //
  poly::DepPoly *dep0{poly::DepPoly::dependence(tlf.getAlloc(), msrc, mtgt01)};
  ASSERT_FALSE(dep0->isEmpty());
  dep0->pruneBounds();
  std::cout << "Dep0 = \n" << *dep0 << "\n";

  // FIXME: v_3 >= -1 && v_3 >= 0?
  // Why isn't the former dropped?
  ASSERT_EQ(dep0->getNumInequalityConstraints(), 4);
  ASSERT_EQ(dep0->getNumEqualityConstraints(), 2);

  poly::DepPoly *dep1{poly::DepPoly::dependence(tlf.getAlloc(), msrc, mtgt10)};
  ASSERT_FALSE(dep1->isEmpty());
  dep1->pruneBounds();
  std::cout << "Dep1 = \n" << dep1 << "\n";
  ASSERT_EQ(dep1->getNumInequalityConstraints(), 4);
  ASSERT_EQ(dep1->getNumEqualityConstraints(), 2);
  // MemoryAccess mtgt1{Atgt1,nullptr,schLoad,true};
  poly::Dependencies deps{};
  deps.check(tlf.getAlloc(), msrc, mtgt01);
  // msrc -> mtgt01
  // NextEdgeOut: [-1]
  // PrevEdgeOut: [-1]
  // NextEdgeIn: [-1]
  // PrevEdgeIn: [-1]
  ASSERT_EQ(deps.outEdges().size(), 1);
  ASSERT_EQ(deps.outEdges()[0], -1);
  ASSERT_EQ(deps[0].prevOut(), -1);
  ASSERT_EQ(deps.inEdges().size(), 1);
  ASSERT_EQ(deps.inEdges()[0], -1);
  ASSERT_EQ(deps[0].prevIn(), -1);

  ASSERT_EQ(msrc->getEdgeIn(), -1);
  ASSERT_EQ(msrc->getEdgeOut(), 0);
  ASSERT_EQ(mtgt01->getEdgeIn(), 0);
  ASSERT_EQ(mtgt01->getEdgeOut(), -1);
  ASSERT_EQ(mtgt01->getEdgeIn(), msrc->getEdgeOut());
  int32_t e01id = mtgt01->getEdgeIn();
  poly::Dependence e01 = deps[e01id];
  std::cout << e01 << "\n";
  ASSERT_FALSE(allZero(deps[msrc->getEdgeOut()].getSatConstraints()[last, _]));
  ASSERT_TRUE(e01.isForward()); // msrc -> mtgt01
  ASSERT_EQ(deps.outEdges()[mtgt01->getEdgeIn()], -1);

  deps.check(tlf.getAlloc(), mtgt10, msrc);
  // mtgt10 <- msrc
  ASSERT_EQ(deps.outEdges().size(), 2);
  // msrc has two out edges, outEdges should let us iterate over them
  // hence, this must equal `0`, the first edge
  ASSERT_EQ(deps.outEdges()[1], 0);
  ASSERT_EQ(deps[1].prevOut(), -1);
  ASSERT_EQ(deps.inEdges().size(), 2);
  ASSERT_EQ(deps.inEdges()[1], -1);
  ASSERT_EQ(deps[1].prevIn(), -1);
  ASSERT_EQ(deps[0].prevOut(), 1);
  ASSERT_EQ(deps[0].prevIn(), -1);

  poly::Dependence e10 = deps[mtgt10->getEdgeIn()];
  ASSERT_FALSE(e10.isForward());
  ASSERT_EQ(mtgt10->getEdgeIn(), msrc->getEdgeOut());
  // it should've been pushed to the font of `msrc`'s outputs
  ASSERT_EQ(deps.outEdges()[mtgt10->getEdgeIn()], mtgt01->getEdgeIn());
  ASSERT_EQ(deps.outEdges()[mtgt01->getEdgeIn()], -1);
  ASSERT_EQ(mtgt10->getEdgeIn(), msrc->getEdgeOut());

  ASSERT_EQ(mtgt10->getEdgeIn(), deps[mtgt01->getEdgeIn()].prevOut());
  ASSERT_EQ(mtgt01->getEdgeIn(), deps.outEdges()[msrc->getEdgeOut()]);

  ASSERT_EQ(mtgt10->getEdgeOut(), -1);
  ASSERT_EQ(mtgt10->getEdgeIn(), 1);
  ASSERT_EQ(msrc->getEdgeOut(), 1);
  ASSERT_EQ(msrc->getEdgeIn(), -1);

  std::cout << e10 << "\n";
  ASSERT_FALSE(allZero(e10.getSatConstraints()[last, _]));
  deps.check(tlf.getAlloc(), mtgt10, msrc);
  auto e10rev = msrc->getEdgeIn();
  ASSERT_EQ(e10rev, mtgt10->getEdgeOut());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(SymmetricIndependentTest, BasicAssertions) {
  // symmetric copy
  // for(i = 0:I-1)
  //   for(j = 0:i-1)
  //     A(j,i) = A(i,j)
  //

  TestLoopFunction tlf;
  poly::Loop *loop = tlf.addLoop("[-1 1 0 -1; "
                                 "0 0 0 1; "
                                 "-1 0 -1 1; "
                                 "0 0 1 0]"_mat,
                                 2);
  // loop.pruneBounds();

  // llvm::ScalarEvolution &SE{tlf.getSE()};
  // llvm::Type *i64 = tlf.getInt64Ty();
  IR::FunArg *ptrA = tlf.createArray();
  IR::Value *M = loop->getSyms()[0];
  // IR::Value *zero = tlf.getConstInt(0);
  IR::Value *one = tlf.getConstInt(1);

  IR::Addr *mtgt{tlf.createLoad(ptrA, tlf.getDoubleTy(), "[0 1; 1 0]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 0 0]"_mat, loop)};
  IR::Addr *msrc{tlf.createStow(ptrA, mtgt, "[1 0; 0 1]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 0 1]"_mat, loop)};

  poly::DepPoly *dep{poly::DepPoly::dependence(tlf.getAlloc(), msrc, mtgt)};
  std::cout << "Dep = \n" << dep << "\n";
  ASSERT_EQ(dep, nullptr);
  poly::Dependencies deps{};
  deps.check(tlf.getAlloc(), msrc, mtgt);
  ASSERT_EQ(msrc->getEdgeOut(), -1);
  ASSERT_EQ(msrc->getEdgeIn(), -1);
  ASSERT_EQ(mtgt->getEdgeOut(), -1);
  ASSERT_EQ(mtgt->getEdgeIn(), -1);
}