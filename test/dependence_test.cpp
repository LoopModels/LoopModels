#include "./ArrayReference.hpp"
#include "./TestUtilities.hpp"
#include "IR/CostModeling.hpp"
#include "LinearProgramming/LoopBlock.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include <Math/Array.hpp>
#include <Math/Comparisons.hpp>
#include <Math/Math.hpp>
#include <Utilities/MatrixStringParse.hpp>
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
#include <utility>

namespace poly {
using math::DenseMatrix, math::DenseDims, math::PtrMatrix, math::MutPtrMatrix,
  math::Vector, math::IntMatrix, math::Col, math::end, math::last, math::_,
  utils::operator""_mat;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(DependenceTest, BasicAssertions) {

  // for (i = 0:I-2){
  //   for (j = 0:J-2){
  //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
  //   }
  // }
  // A*x >= 0;
  // [ -2  1  0 -1  0    [ 1
  //    0  0  0  1  0  *   I   >= 0
  //   -2  0  1  0 -1      J
  //    0  0  0  0  1 ]    i
  //                       j ]
  IntMatrix loopA{"[-2 1 0 0 -1; "   // j <= I - 2
                  "0 0 0 0 1; "      // j >= 0
                  "-2 0 1 -1 0; "    // i <= J - 2
                  "0 0 0 1 0]"_mat}; // i >= 0
  TestLoopFunction tlf;

  tlf.addLoop(std::move(loopA), 2);
  auto *loop = tlf.getLoopNest(0);
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = tlf.getInt64Ty();
  auto *ptrA = tlf.createArray();
  const auto *scevA = tlf.getSCEVUnknown(ptrA);

  // create arrays
  auto &builder = tlf.getBuilder();
  llvm::Type *f64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop->getSyms()[0];
  llvm::Value *zero = tlf.getConstInt(0);
  llvm::Value *one = tlf.getConstInt(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *ivp1 = builder.CreateAdd(iv, one);
  llvm::Value *jvp1 = builder.CreateAdd(jv, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *iOffset = builder.CreateMul(ivp1, Mv);
  llvm::Value *offset01 = builder.CreateAdd(jv, iOffset);
  auto *gepedA01 = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset01}, "gep_A01");
  auto *loadA01 =
    builder.CreateAlignedLoad(f64, gepedA01, llvm::MaybeAlign(8), "load_A01");

  llvm::Value *offset10 = builder.CreateAdd(jvp1, builder.CreateMul(iv, Mv));
  auto *gepedA10 = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset10}, "gep_A10");
  auto *loadA10 =
    builder.CreateAlignedLoad(f64, gepedA10, llvm::MaybeAlign(8), "load_A10");
  llvm::Value *offset11 = builder.CreateAdd(jvp1, iOffset);
  auto *gepedA11 = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset11}, "gep_A11");
  auto *storeA11 = builder.CreateAlignedStore(
    builder.CreateFAdd(loadA10, loadA01, "A10 + A01"), gepedA11,
    llvm::MaybeAlign(8), false);

  constexpr size_t i = 0, j = 1;
  // we have three array refs
  // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
  ArrayReference srcA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = srcA.indexMatrix();
    //     l  d
    indMat(i, 0) = 1;
    indMat(j, 1) = 1;
    MutPtrMatrix<int64_t> offMat = srcA.offsetMatrix();
    offMat(i, 0) = 1;
    offMat(j, 0) = 1;
    srcA.sizes[0] = M;
    srcA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  // A[i+1, j]
  ArrayReference tgtA01(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA01.indexMatrix();
    //     l  d
    indMat(i, 0) = 1;
    indMat(j, 1) = 1;
    tgtA01.offsetMatrix()(i, 0) = 1;
    tgtA01.sizes[0] = M;
    tgtA01.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  // A[i, j+1]
  ArrayReference tgtA10(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA10.indexMatrix();
    //     l  d
    indMat(i, 0) = 1; // i
    indMat(j, 1) = 1; // j
    tgtA10.offsetMatrix()(j, 0) = 1;
    tgtA10.sizes[0] = M;
    tgtA10.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  //
  Vector<unsigned, 4> schLoad0(3, 0);
  Vector<unsigned, 4> schStore(3, 0);
  schStore[2] = 2;
  utils::OwningArena<> alloc;
  IR::Addr *msrc{createMemAccess(&alloc, srcA, storeA11, schStore)};
  IR::Addr *mtgt01{createMemAccess(&alloc, tgtA01, loadA01, schLoad0)};
  poly::DepPoly *dep0{poly::DepPoly::dependence(&alloc, *msrc, *mtgt01)};
  EXPECT_FALSE(dep0->isEmpty());
  dep0->pruneBounds();
  llvm::errs() << "Dep0 = \n" << dep0 << "\n";

  EXPECT_EQ(dep0->getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep0->getNumEqualityConstraints(), 2);
  assert(dep0->getNumInequalityConstraints() == 4);
  assert(dep0->getNumEqualityConstraints() == 2);

  Vector<unsigned, 4> schLoad1(3, 0);
  schLoad1[2] = 1;
  IR::Addr *mtgt10{createMemAccess(&alloc, tgtA10, loadA10, schLoad1)};
  poly::DepPoly *dep1{poly::DepPoly::dependence(&alloc, *msrc, *mtgt10)};
  EXPECT_FALSE(dep1->isEmpty());
  dep1->pruneBounds();
  llvm::errs() << "Dep1 = \n" << dep1 << "\n";
  EXPECT_EQ(dep1->getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep1->getNumEqualityConstraints(), 2);
  assert(dep1->getNumInequalityConstraints() == 4);
  assert(dep1->getNumEqualityConstraints() == 2);
  // MemoryAccess mtgt1{Atgt1,nullptr,schLoad,true};
  poly::Dependence::check(&alloc, *msrc, *mtgt01);
  EXPECT_EQ(msrc->getEdgeIn(), nullptr);
  EXPECT_NE(msrc->getEdgeOut(), nullptr);
  EXPECT_NE(mtgt01->getEdgeIn(), nullptr);
  EXPECT_EQ(mtgt01->getEdgeOut(), nullptr);
  EXPECT_EQ(mtgt01->getEdgeIn(), msrc->getEdgeOut());
  poly::Dependence *e01 = mtgt01->getEdgeIn();
  llvm::errs() << *e01 << "\n";
  EXPECT_FALSE(allZero(msrc->getEdgeOut()->getSatConstraints()(last, _)));
  poly::Dependence::check(&alloc, *mtgt01, *msrc);
  poly::Dependence *e01rev = msrc->getEdgeIn();
  EXPECT_NE(e01rev, nullptr);
  EXPECT_NE(mtgt01->getEdgeOut(), nullptr);
  EXPECT_EQ(mtgt01->getEdgeOut(), msrc->getEdgeIn());
  // should still be e0
  EXPECT_EQ(msrc->getEdgeOut(), e01);
  EXPECT_EQ(mtgt01->getEdgeIn(), e01);

  poly::Dependence::check(&alloc, *msrc, *mtgt10);
  poly::Dependence *e10 = mtgt10->getEdgeIn();
  EXPECT_EQ(e10, msrc->getEdgeOut());
  // it should've been pushed to the font of `msrc`'s outputs
  EXPECT_EQ(e10->getNextOutput(), e01);
  EXPECT_EQ(mtgt10->getEdgeOut(), nullptr);

  llvm::errs() << *e10 << "\n";
  EXPECT_FALSE(allZero(e10->getSatConstraints()(last, _)));
  poly::Dependence::check(&alloc, *mtgt10, *msrc);
  auto e10rev = msrc->getEdgeIn();
  EXPECT_EQ(e10rev->getNextInput(), e01rev);
  EXPECT_EQ(e10rev, mtgt10->getEdgeOut());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(SymmetricIndependentTest, BasicAssertions) {
  // symmetric copy
  // for(i = 0:I-1)
  //   for(j = 0:i-1)
  //     A(j,i) = A(i,j)
  //
  IntMatrix loopA{"[-1 1 0 -1; "
                  "0 0 0 1; "
                  "-1 0 -1 1; "
                  "0 0 1 0]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(loopA), 2);
  auto *loop = tlf.getLoopNest(0);
  // loop.pruneBounds();

  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = tlf.getInt64Ty();
  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.getBuilder();
  llvm::Type *f64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop->getSyms()[0];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *offsetji = builder.CreateAdd(jv, builder.CreateMul(iv, Mv));
  auto *gepedAji = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetji}, "gep_Aji");
  auto *loadAji =
    builder.CreateAlignedLoad(f64, gepedAji, llvm::MaybeAlign(8), "load_Aji");
  llvm::Value *offsetij = builder.CreateAdd(iv, builder.CreateMul(jv, Mv));
  auto *gepedAij = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetij}, "gep_Aij");
  auto *storeAij =
    builder.CreateAlignedStore(loadAji, gepedAij, llvm::MaybeAlign(8), false);

  constexpr size_t i = 0, j = 1;
  // we have three array refs
  // A[i, j]
  ArrayReference srcA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = srcA.indexMatrix();
    //     l  d
    indMat(i, 0) = 1;
    indMat(j, 1) = 1;
    srcA.sizes[0] = loop->getSyms()[0];
    srcA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  // A[j, i]
  ArrayReference tgtA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA.indexMatrix();
    //     l  d
    indMat(j, 0) = 1;
    indMat(i, 1) = 1;
    tgtA.sizes[0] = loop->getSyms()[0];
    tgtA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  Vector<unsigned, 4> schLoad(3, 0);
  Vector<unsigned, 4> schStore(3, 0);
  schStore[2] = 1;
  utils::OwningArena<> alloc;
  IR::Addr *msrc{createMemAccess(&alloc, srcA, storeAij, schStore)};
  IR::Addr *mtgt{createMemAccess(&alloc, tgtA, loadAji, schLoad)};
  poly::DepPoly *dep{poly::DepPoly::dependence(&alloc, *msrc, *mtgt)};
  llvm::errs() << "Dep = \n" << dep << "\n";
  EXPECT_TRUE(dep == nullptr);
  assert(dep == nullptr);
  poly::Dependence::check(&alloc, *msrc, *mtgt);
  EXPECT_EQ(msrc->getEdgeOut(), nullptr);
  EXPECT_EQ(msrc->getEdgeIn(), nullptr);
  EXPECT_EQ(mtgt->getEdgeOut(), nullptr);
  EXPECT_EQ(mtgt->getEdgeIn(), nullptr);
}

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
  IntMatrix loopA{"[-1 1 -1 0; "   // i <= I-1
                  "0 0 1 0; "      // i >= 0
                  "0 0 1 -1; "     // j <= i
                  "0 0 0 1]"_mat}; // j >= 0
  TestLoopFunction tlf;
  tlf.addLoop(std::move(loopA), 2);
  auto *loop = tlf.getLoopNest(0);
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = tlf.getInt64Ty();
  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.getBuilder();
  llvm::Type *f64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop->getSyms()[0];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *offsetii = builder.CreateAdd(iv, builder.CreateMul(iv, Mv));
  auto *gepedAii = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetii}, "gep_Aji");
  auto *loadAii =
    builder.CreateAlignedLoad(f64, gepedAii, llvm::MaybeAlign(8), "load_Aii");

  llvm::Value *offsetij = builder.CreateAdd(iv, builder.CreateMul(jv, Mv));
  auto *gepedAij = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetij}, "gep_Aij");
  auto *storeAij =
    builder.CreateAlignedStore(loadAii, gepedAij, llvm::MaybeAlign(8), false);
  constexpr size_t i = 0, j = 1;
  // we have three array refs
  // A[i, j] // i*stride(A,1) + j*stride(A,2);
  ArrayReference srcA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = srcA.indexMatrix();
    indMat(i, 0) = 1; // i
    indMat(j, 1) = 1; // j
    srcA.sizes[0] = loop->getSyms()[0];
    srcA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  // A[i, i]
  ArrayReference tgtA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA.indexMatrix();
    indMat(i, 0) = 1; // i
    indMat(i, 1) = 1; // i
    tgtA.sizes[0] = loop->getSyms()[0];
    tgtA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  Vector<unsigned, 4> schLoad(2 + 1, 0);
  Vector<unsigned, 4> schStore(2 + 1, 0);
  schStore[2] = 1;
  utils::OwningArena<> alloc;
  IR::Addr *msrc{createMemAccess(&alloc, srcA, storeAij, schStore)};
  IR::Addr *mtgt{createMemAccess(&alloc, tgtA, loadAii, schLoad)};

  poly::Dependence::check(&alloc, *msrc, *mtgt);
  poly::Dependence *e = msrc->getEdgeOut();
  EXPECT_EQ(e, mtgt->getEdgeIn());
  EXPECT_NE(e, nullptr);
  EXPECT_EQ(msrc->getEdgeIn(), nullptr);
  EXPECT_EQ(mtgt->getEdgeOut(), nullptr);
  llvm::errs() << "Blog post example:\n" << e[0] << "\n";
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
  IntMatrix loopA{"[-1 1 0 0 0 0 -1; "
                  "0 0 0 0 0 0 1; "
                  "-1 0 1 0 0 -1 0; "
                  "0 0 0 0 0 1 0; "
                  "-1 0 0 1 -1 0 0; "
                  "0 0 0 0 1 0 0]"_mat};
  constexpr size_t i = 0, j = 1, k = 2;
  TestLoopFunction tlf;
  tlf.addLoop(std::move(loopA), 3);
  auto *loop = tlf.getLoopNest(0);
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = tlf.getInt64Ty();

  const llvm::SCEV *II = loop->getSyms()[0];
  const llvm::SCEV *J = loop->getSyms()[1];
  const llvm::SCEV *K = loop->getSyms()[2];

  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.getBuilder();
  llvm::Type *f64 = builder.getDoubleTy();

  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *kv = builder.CreateAdd(zero, one);

  llvm::Value *M = builder.getInt64(128);
  llvm::Value *N = builder.getInt64(1024);

  llvm::Value *offset = builder.CreateAdd(
    builder.CreateSub(iv, kv),
    builder.CreateMul(
      M, builder.CreateAdd(builder.CreateAdd(jv, kv),
                           builder.CreateMul(N, builder.CreateAdd(iv, jv)))));

  auto *gepedA = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset}, "gep_A");

  auto *loadA =
    builder.CreateAlignedLoad(f64, gepedA, llvm::MaybeAlign(8), "load_A");

  auto *storeA =
    builder.CreateAlignedStore(loadA, gepedA, llvm::MaybeAlign(8), false);

  // we have three array refs
  // A[i+j, j+k, i - k]
  ArrayReference refA(scevA, loop, 3);
  {
    MutPtrMatrix<int64_t> indMat = refA.indexMatrix();
    indMat(i, 0) = 1;  // i
    indMat(j, 0) = 1;  // + j
    indMat(j, 1) = 1;  // j
    indMat(k, 1) = 1;  // + k
    indMat(i, 2) = 1;  // i
    indMat(k, 2) = -1; // -k
    refA.sizes[0] = SE.getAddExpr(J, K);
    refA.sizes[1] = SE.getAddExpr(II, K);
    refA.sizes[2] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  Vector<unsigned, 4> schLoad(3 + 1, 0);
  Vector<unsigned, 4> schStore(3 + 1, 0);
  schStore[3] = 1;
  utils::OwningArena<> alloc;
  IR::Addr *msrc{createMemAccess(&alloc, refA, storeA, schStore)};
  IR::Addr *mtgt{createMemAccess(&alloc, refA, loadA, schLoad)};

  poly::Dependence::check(&alloc, *msrc, *mtgt);
  poly::Dependence *e0 = msrc->getEdgeIn();
  poly::Dependence *e1 = msrc->getEdgeOut();
  EXPECT_NE(e0, nullptr);
  EXPECT_NE(e1, nullptr);
  EXPECT_EQ(e0, mtgt->getEdgeOut());
  EXPECT_EQ(e1, mtgt->getEdgeIn());
  llvm::errs() << "Rank deficicient example:\nForward:\n"
               << e0 << "\nReverse:\n"
               << e1 << "\n";
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TriangularExampleTest, BasicAssertions) {
  IntMatrix matAmn{"[-1 1 0 -1 0; "
                   "0 0 0 1 0; "
                   "-1 0 1 0 -1; "
                   "0 0 0 0 1]"_mat};
  IntMatrix matAmnk{"[-1 1 0 -1 0 0; "
                    "0 0 0 1 0 0; "
                    "-1 0 1 0 -1 0; "
                    "0 0 0 0 1 0; "
                    "-1 0 1 0 0 -1; "
                    "-1 0 0 0 -1 1]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(matAmn), 2);
  tlf.addLoop(std::move(matAmnk), 3);
  poly::Loop *loopMN = tlf.getLoopNest(0);
  EXPECT_FALSE(loopMN->isEmpty());
  poly::Loop *loopMNK = tlf.getLoopNest(1);
  EXPECT_FALSE(loopMNK->isEmpty());
  EXPECT_EQ(loopMN->getSyms().size(), loopMNK->getSyms().size());
  for (size_t i = 0; i < loopMN->getSyms().size(); ++i)
    EXPECT_EQ(loopMN->getSyms()[i], loopMNK->getSyms()[i]);

  llvm::ScalarEvolution &SE{tlf.getSE()};
  auto &builder = tlf.getBuilder();
  llvm::IntegerType *i64 = builder.getInt64Ty();

  // create arrays
  llvm::Type *f64 = builder.getDoubleTy();
  llvm::Value *ptrB = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrU = tlf.createArray();

  const llvm::SCEV *M = loopMN->getSyms()[0];
  const llvm::SCEV *N = loopMN->getSyms()[1];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *mv = builder.CreateAdd(zero, one);
  llvm::Value *nv = builder.CreateAdd(zero, one);
  llvm::Value *kv = builder.CreateAdd(nv, one);

  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();
  llvm::Value *Nv = llvm::dyn_cast<llvm::SCEVUnknown>(N)->getValue();
  llvm::Value *offsetB = builder.CreateAdd(mv, builder.CreateMul(nv, Mv));
  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = B(n,m);
  //   }
  llvm::LoadInst *loadB = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrB, llvm::SmallVector<llvm::Value *, 1>{offsetB}),
    llvm::MaybeAlign(8), "load_Bnm");
  llvm::StoreInst *storeA0 = builder.CreateAlignedStore(
    loadB,
    builder.CreateGEP(f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetB}),
    llvm::MaybeAlign(8), false);

  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = A(n,m) / U(n,n);
  llvm::Value *offsetUnn = builder.CreateAdd(nv, builder.CreateMul(nv, Nv));
  auto *loadUnn = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrU,
                      llvm::SmallVector<llvm::Value *, 1>{offsetUnn}),
    llvm::MaybeAlign(8), "load_Unn");
  auto *gepA0 = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetB}, "gep_Anm");
  auto *loadA0 =
    builder.CreateAlignedLoad(f64, gepA0, llvm::MaybeAlign(8), "load_Anm");
  auto *storeAFDiv =
    builder.CreateAlignedStore(builder.CreateFDiv(loadA0, loadUnn, "fdiv"),
                               gepA0, llvm::MaybeAlign(8), false);

  // for (m = 0; m < M; ++m){
  //     for (k = n+1; k < N; ++k){
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n);
  //     }
  llvm::Value *offsetUnk = builder.CreateAdd(nv, builder.CreateMul(kv, Nv));
  auto *loadUnk = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrU,
                      llvm::SmallVector<llvm::Value *, 1>{offsetUnk}),
    llvm::MaybeAlign(8), "load_Ukn");
  llvm::Value *offsetAmk = builder.CreateAdd(mv, builder.CreateMul(kv, Mv));
  auto *getA1mk = builder.CreateGEP(
    f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetAmk}, "gep_Akm");
  auto *loadA1mk =
    builder.CreateAlignedLoad(f64, getA1mk, llvm::MaybeAlign(8), "load_Akm");
  auto *loadA1mn = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetB}),
    llvm::MaybeAlign(8), "load_Anm");
  auto *storeA2mk = builder.CreateAlignedStore(
    builder.CreateFSub(loadA1mk, builder.CreateFMul(loadA1mn, loadUnk, "fmul"),
                       "fsub"),
    gepA0, llvm::MaybeAlign(8), false);

  // badly written triangular solve:
  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = B(n,m);
  //   }
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = A(n,m) / U(n,n);
  //     for (k = n+1; k < N; ++k){
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n);
  //     }
  //   }
  // }

  const auto *scevB = tlf.getSCEVUnknown(ptrB);
  const auto *scevA = tlf.getSCEVUnknown(ptrA);
  const auto *scevU = tlf.getSCEVUnknown(ptrU);
  constexpr size_t m = 0, n = 1, k = 2;
  // construct indices
  // ind mat, loops currently indexed from outside-in
  lp::LoopBlock lblock;
  // B[n, m]
  ArrayReference indBmn{scevB, loopMN, 2};
  {
    MutPtrMatrix<int64_t> indMat = indBmn.indexMatrix();
    //     l  d
    indMat(n, 0) = 1; // n
    indMat(m, 1) = 1; // m
    indBmn.sizes[0] = M;
    indBmn.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference indAmn2{scevA, loopMN, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAmn2.indexMatrix();
    //     l  d
    indMat(n, 0) = 1; // n
    indMat(m, 1) = 1; // m
    indAmn2.sizes[0] = M;
    indAmn2.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference indAmn3{scevA, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAmn3.indexMatrix();
    //     l  d
    indMat(n, 0) = 1; // n
    indMat(m, 1) = 1; // m
    indAmn3.sizes[0] = M;
    indAmn3.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // A[k, m]
  ArrayReference indAmk{scevA, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAmk.indexMatrix();
    //     l  d
    indMat(k, 0) = 1; // k
    indMat(m, 1) = 1; // m
    indAmk.sizes[0] = M;
    indAmk.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // U[k, n]
  ArrayReference indUnk{scevU, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> indMat = indUnk.indexMatrix();
    //     l  d
    indMat(n, 1) = 1; // n
    indMat(k, 0) = 1; // k
    indUnk.sizes[0] = N;
    indUnk.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // U[n, n]
  ArrayReference indUnn{scevU, loopMN, 2};
  {
    MutPtrMatrix<int64_t> indMat = indUnn.indexMatrix();
    //     l  d
    indMat(n, 1) = 1; // n
    indMat(n, 0) = 1; // n
    indUnn.sizes[0] = N;
    indUnn.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     // sch.Omega = [ 0, _, 0, _, {0-1} ]
  //     A(n,m) = B(n,m); // sch2_0_{0-1}
  //   }
  //   for (n = 0; n < N; ++n){
  //     // sch.Omega = [ 0, _, 1, _, {0-2} ]
  //     A(n,m) = A(n,m) / U(n,n); // sch2_2_{0-2}
  //     for (k = n+1; k < N; ++k){
  //       // sch.Omega = [ 0, _, 1, _, 3, _, {0-3} ]
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n); // sch3_{0-3}
  //     }
  //   }
  //   foo(arg...) // [ 0, _, 2 ]
  // }
  // NOTE: shared ptrs get set to NULL when `lblock.memory` reallocs...
  IR::AddrChain addr;
  Vector<unsigned, 4> sch2t0t0(2 + 1, 0);
  Vector<unsigned, 4> sch2t0t1{sch2t0t0};
  utils::OwningArena<> alloc;
  // A(n,m) = -> B(n,m) <-
  IR::Addr *mSch2t0t0(createMemAccess(&alloc, indBmn, loadB, sch2t0t0));
  addr.addAddr(mSch2t0t0);
  sch2t0t1[2] = 1;
  Vector<unsigned, 4> sch2t1t0{sch2t0t1};
  // -> A(n,m) <- = B(n,m)
  IR::Addr *mSch2t0t1(createMemAccess(&alloc, indAmn2, storeA0, sch2t0t1));
  assert(mSch2t0t1->getInstruction() == storeA0);
  assert(mSch2t0t1->isStore());
  addr.addAddr(mSch2t0t1);
  sch2t1t0[1] = 1;
  sch2t1t0[2] = 0;
  Vector<unsigned, 4> sch2t1t1{sch2t1t0};
  // A(n,m) = -> A(n,m) <- / U(n,n); // sch2
  IR::Addr *mSch2t1t0(createMemAccess(&alloc, indAmn2, loadA0, sch2t1t0));
  assert(mSch2t1t0->getInstruction() == loadA0);
  assert(mSch2t1t0->isLoad());
  addr.addAddr(mSch2t1t0);
  sch2t1t1[2] = 1;
  Vector<unsigned, 4> sch2t1t2{sch2t1t1};
  // A(n,m) = A(n,m) / -> U(n,n) <-;
  IR::Addr *mSch2t1t1(createMemAccess(&alloc, indUnn, loadUnn, sch2t1t1));
  addr.addAddr(mSch2t1t1);
  sch2t1t2[2] = 2;
  // -> A(n,m) <- = A(n,m) / U(n,n); // sch2
  IR::Addr *mSch2t1t2(createMemAccess(&alloc, indAmn2, storeAFDiv, sch2t1t2));
  addr.addAddr(mSch2t1t2);

  Vector<unsigned, 4> sch3t0(3 + 1, 0);
  sch3t0[1] = 1;
  sch3t0[2] = 3;
  Vector<unsigned, 4> sch3t1{sch3t0};
  // A(k,m) = A(k,m) - A(n,m)* -> U(k,n) <-;
  IR::Addr *mSch3t0(createMemAccess(&alloc, indUnk, loadUnk, sch3t0));
  addr.addAddr(mSch3t0);
  sch3t1[3] = 1;
  Vector<unsigned, 4> sch3t2{sch3t1};
  // A(k,m) = A(k,m) - -> A(n,m) <- *U(k,n);
  IR::Addr *mSch3t1(createMemAccess(&alloc, indAmn3, loadA1mn, sch3t1));
  addr.addAddr(mSch3t1);
  sch3t2[3] = 2;
  Vector<unsigned, 8> sch3t3{sch3t2};
  // A(k,m) = -> A(k,m) <- - A(n,m)*U(k,n);
  IR::Addr *mSch3t2(createMemAccess(&alloc, indAmk, loadA1mk, sch3t2));
  addr.addAddr(mSch3t2);
  sch3t3[3] = 3;
  // -> A(k,m) <- = A(k,m) - A(n,m)*U(k,n);
  IR::Addr *mSch3t3(createMemAccess(&alloc, indAmk, storeA2mk, sch3t3));
  addr.addAddr(mSch3t3);

  // for (m = 0; m < M; ++m){createMemAccess(
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = B(n,m); // sch2_0_{0-1)}
  //   }
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = A(n,m) / U(n,n); // sch2t2_{0-2}
  //     for (k = n+1; k < N; ++k){
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n); // sch3_{0-3}
  //     }
  //   }
  // }
  // First, comparisons of store to `A(n,m) = B(n,m)` versus...
  // // load in `A(n,m) = A(n,m) / U(n,n)`
  {
    poly::Dependence::check(&alloc, *mSch2t0t1, *mSch2t1t0);
    EXPECT_EQ(mSch2t0t1->getEdgeIn(), nullptr);
    EXPECT_EQ(mSch2t1t0->getEdgeOut(), nullptr);
    EXPECT_EQ(mSch2t0t1->getEdgeOut(), mSch2t1t0->getEdgeIn());
    poly::Dependence *dep = mSch2t1t0->getEdgeIn();
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 0 << ":\n" << *dep << "\n";
  }
  //
  //
  // store in `A(n,m) = A(n,m) / U(n,n)`
  {
    poly::Dependence::check(&alloc, *mSch2t0t1, *mSch2t1t2);
    auto dep = mSch2t0t1->getEdgeOut();
    EXPECT_EQ(dep, mSch2t1t2->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 1 << ":\n" << *dep << "\n";
  }
  //
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t0t1, *mSch3t1);
    auto dep = mSch2t0t1->getEdgeOut();
    EXPECT_EQ(dep, mSch3t1->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 2 << ":\n" << *dep << "\n";
  }
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  //
  {
    poly::Dependence::check(&alloc, *mSch2t0t1, *mSch3t2);
    auto dep = mSch2t0t1->getEdgeOut();
    EXPECT_EQ(dep, mSch3t2->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 3 << ":\n" << *dep << "\n";
  }
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t0t1, *mSch3t3);
    auto dep = mSch2t0t1->getEdgeOut();
    EXPECT_EQ(dep, mSch3t3->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 4 << ":\n" << *dep << "\n";
  }

  // Second, comparisons of load in `A(m,n) = A(m,n) / U(n,n)`
  // with...
  // store in `A(n,m) = A(n,m) / U(n,n)`
  {
    poly::Dependence::check(&alloc, *mSch2t1t0, *mSch2t1t2);
    auto dep = mSch2t1t0->getEdgeOut();
    EXPECT_EQ(dep, mSch2t1t2->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 5 << ":\n" << dep[0] << "\n";
  }

  //
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t0, *mSch3t1);
    auto dep = mSch2t1t0->getEdgeOut();
    EXPECT_EQ(dep, mSch3t1->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 6 << ":\n" << *dep << "\n";
  }
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t0, *mSch3t2);
    auto dep = mSch2t1t0->getEdgeOut();
    EXPECT_EQ(dep, mSch3t2->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 7 << ":\n" << *dep << "\n";
  }
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t0, *mSch3t3);
    auto dep = mSch2t1t0->getEdgeOut();
    EXPECT_EQ(dep, mSch3t3->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 8 << ":\n" << *dep << "\n";
  }

  // Third, comparisons of store in `A(m,n) = A(m,n) / U(n,n)`
  // with...
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t2, *mSch3t1);
    auto dep = mSch2t1t2->getEdgeOut();
    EXPECT_EQ(dep, mSch3t1->getEdgeIn());
    EXPECT_TRUE(dep->isForward());
    llvm::errs() << "dep#" << 9 << ":\n" << *dep << "\n";
  }
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t2, *mSch3t2);
    auto dep = mSch2t1t2->getEdgeOut();
    EXPECT_EQ(dep, mSch3t2->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 10 << ":\n" << *dep << "\n";
  }
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch2t1t2, *mSch3t3);
    auto dep = mSch2t1t2->getEdgeOut();
    EXPECT_EQ(dep, mSch3t3->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 11 << ":\n" << *dep << "\n";
  }

  // Fourth, comparisons of load `A(m,n)` in
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  // with...
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch3t1, *mSch3t2);
    auto dep = poly::Dependence::check(&alloc, *mSch3t1, *mSch3t2);
    EXPECT_EQ(dep, mSch3t2->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 12 << ":\n" << *dep << "\n";
  }
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch3t1, *mSch3t3);
    auto dep = mSch3t1->getEdgeOut();
    EXPECT_EQ(dep, mSch3t3->getEdgeIn());
    EXPECT_FALSE(dep->isForward());
    llvm::errs() << "dep#" << 13 << ":\n" << *dep << "\n";
  }

  // Fifth, comparisons of load `A(m,k)` in
  // sch3_               3        0         1     2
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  // with...
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  {
    poly::Dependence::check(&alloc, *mSch3t2, *mSch3t3);
    auto *forward = mSch3t2->getEdgeOut();
    auto *reverse = mSch3t2->getEdgeIn();
    EXPECT_EQ(forward, mSch3t3->getEdgeIn());
    EXPECT_EQ(reverse, mSch3t3->getEdgeOut());
    EXPECT_TRUE(forward->isForward());
    EXPECT_FALSE(reverse->isForward());
    llvm::errs() << "dep# 14 and 15\n";
    llvm::errs() << "\nforward dependence:\n" << forward;
    llvm::errs() << "\nreverse dependence:\n" << reverse;
    assert(forward->isForward());
    assert(!reverse->isForward());
    auto fwdDepPoly = forward->getDepPoly();
    auto revDepPoly = reverse->getDepPoly();
    EXPECT_TRUE(allZero(fwdDepPoly->getE()(_, 0)));
    EXPECT_FALSE(allZero(revDepPoly->getE()(_, 0)));

    ptrdiff_t nonZeroInd = -1;
    for (unsigned i = 0; i < revDepPoly->getE().numRow(); ++i) {
      bool notZero = !allZero(revDepPoly->getEqSymbols(i));
      // we should only find 1 non-zero
      EXPECT_FALSE((nonZeroInd != -1) & notZero);
      if (notZero) nonZeroInd = i;
    }
    // vt1 is `n` for the load
    // v_4 is `n` for the store
    // thus, we expect vt1 = v_4 + 1
    // that is, the load depends on the store from the previous iteration
    // (e.g., store when `v_4 = 0` is loaded when `vt1 = 1`.
    auto nonZero = revDepPoly->getCompTimeEqOffset(nonZeroInd);
    const size_t numSymbols = revDepPoly->getNumSymbols();
    EXPECT_EQ(numSymbols, 3);
    EXPECT_TRUE(nonZero.has_value());
    assert(nonZero.has_value());
    if (*nonZero == 1) {
      // vt1 - v_4 == 1
      // 1 - vt1 + v_4 == 0
      EXPECT_EQ(revDepPoly->getE()(nonZeroInd, numSymbols + 1), -1);
      EXPECT_EQ(revDepPoly->getE()(nonZeroInd, numSymbols + 4), 1);

    } else {
      // -vt1 + v_4 == -1
      // -1 + vt1 - v_4 == 0
      EXPECT_EQ(*nonZero, -1);
      EXPECT_EQ(revDepPoly->getE()(nonZeroInd, numSymbols + 1), 1);
      EXPECT_EQ(revDepPoly->getE()(nonZeroInd, numSymbols + 4), -1);
    }
  }

  std::optional<BitSet<std::array<uint64_t, 2>>> optDeps = lblock.optimize();
  EXPECT_TRUE(optDeps.has_value());
  // orig order (inner <-> outer): n, m
  DenseMatrix<int64_t> optPhi2{DenseDims{2, 2}, 0};
  // phi2 loop order is
  optPhi2.antiDiag() << 1;
  // the scheduler swaps the order, making `n` outermost,
  // and `m` as innermost
  // orig order (outer <-> inner): m, n, k
  DenseMatrix<int64_t> optPhi3{"[0 0 1; 1 0 0; 0 1 0]"_mat};
  // phi3 loop order (outer <-> inner) is [m, k, n]
  // so the schedule preserves `m` as the outermost loop,
  // followed by `k`, and `n` as innermost. `n` is the reduction loop.
  for (auto *mem : lblock.getMem()) {
    size_t nodeIndex = mem->getNode();
    AffineSchedule s = lblock.getNode(nodeIndex).getSchedule();
    if (mem->getNumLoops() == 2) {
      EXPECT_EQ(s.getPhi(), optPhi2);
    } else {
      assert(mem->getNumLoops() == 3);
      EXPECT_EQ(s.getPhi(), optPhi3);
    }
    EXPECT_TRUE(allZero(s.getFusionOmega()));
    EXPECT_TRUE(allZero(s.getOffsetOmega()));
  }
  auto *LTS = CostModeling::LoopTreeSchedule::init(alloc, lblock);
  LTS->printDotFile(alloc, llvm::errs());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(MeanStDevTest0, BasicAssertions) {
  // iOuter variant:
  // for (i = 0; i < I; ++i){
  //   x(i) = 0; // [0]
  //   for (j = 0; j < J; ++j)
  //     x(i) += A(j,i); // [1,0:2]
  //   x(i) /= J;
  //   s(i) = 0;
  //   for (j = 0; j < J; ++j){
  //     d = (A(j,i) - x(i));
  //     s(i) += d*d;
  //   }
  //   s(i) = sqrt(s(i) / (J-1));
  // }
  constexpr size_t i = 0, j = 1;
  // jOuter variant:
  //
  // for (i = 0; i < I; ++i){
  //    x(i) = 0;
  //    s(i) = 0;
  // }
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //      x(i) += A(j,i);
  // for (i = 0; i < I; ++i){
  //   x(i) /= J;
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //     d = (A(j,i) - x(i));
  //     s(i) += d*d;
  //   }
  // }
  // for (i = 0; i < I; ++i)
  //   s(i) = sqrt(s(i) / (J-1));
  constexpr size_t jo = 0, ii = 1;
  TestLoopFunction tlf;
  IntMatrix twoLoopsMat{"[-1 1 0 -1 0; "
                        "0 0 0 1 0; "
                        "-1 0 1 0 -1; "
                        "0 0 0 0 1]"_mat};
  tlf.addLoop(std::move(twoLoopsMat), 2);
  IntMatrix oneLoopMat{"[-1 1 -1; "
                       "0 0 1]"_mat};
  tlf.addLoop(std::move(oneLoopMat), 1);

  IntMatrix twoLoopsMatJI{"[-1 0 1 -1 0; "
                          "0 0 0 1 0; "
                          "-1 1 0 0 -1; "
                          "0 0 0 0 1]"_mat};
  tlf.addLoop(std::move(twoLoopsMatJI), 2);
  poly::Loop *loopIJ = tlf.getLoopNest(0);
  poly::Loop *loopI = tlf.getLoopNest(1);
  poly::Loop *loopJI = tlf.getLoopNest(2);
  std::swap(loopJI->getSyms()[0], loopJI->getSyms()[1]);
  llvm::IRBuilder<> &builder = tlf.getBuilder();

  // create arrays
  llvm::Value *ptrX = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrS = tlf.createArray();
  const auto *scevX = tlf.getSCEVUnknown(ptrX);
  const auto *scevA = tlf.getSCEVUnknown(ptrA);
  const auto *scevS = tlf.getSCEVUnknown(ptrS);

  // llvm::ConstantInt *Iv = builder.getInt64(200);
  const llvm::SCEV *II = loopIJ->getSyms()[0];
  const llvm::SCEV *J = loopIJ->getSyms()[1];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(II)->getValue();
  llvm::Value *Jv = llvm::dyn_cast<llvm::SCEVUnknown>(J)->getValue();
  auto *Jfp = tlf.CreateUIToF64(Jv);
  auto *zero = builder.getInt64(0);
  auto *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);

  llvm::Value *offsetA = builder.CreateAdd(iv, builder.CreateMul(jv, Iv));
  auto *loadAm = tlf.CreateLoad(ptrA, offsetA);
  auto *loadAs = tlf.CreateLoad(ptrA, offsetA);
  auto *loadX0 = tlf.CreateLoad(ptrX, iv);
  auto *loadX1 = tlf.CreateLoad(ptrX, iv);
  auto *loadX2 = tlf.CreateLoad(ptrX, iv);

  auto *zeroFP = tlf.getZeroF64();
  auto *storeX0 = tlf.CreateStore(zeroFP, ptrX, iv);
  auto *storeX1 = tlf.CreateStore(tlf.CreateFAdd(loadX0, loadAm), ptrX, iv);
  auto *storeX2 = tlf.CreateStore(tlf.CreateFDiv(loadX1, Jfp), ptrX, iv);

  auto *loadS0 = tlf.CreateLoad(ptrS, iv);
  auto *loadS1 = tlf.CreateLoad(ptrS, iv);

  auto *storeS0 = tlf.CreateStore(zeroFP, ptrS, iv);
  auto *diff = tlf.CreateFSub(loadAs, loadX2);
  // llvm::Intrinsic::fmuladd
  auto *storeS1 = tlf.CreateStore(
    tlf.CreateFAdd(loadS0, tlf.CreateFMul(diff, diff)), ptrS, iv);

  auto *storeS2 =
    tlf.CreateStore(tlf.CreateSqrt(tlf.CreateFDiv(loadS1, Jfp)), ptrS, iv);

  // Now, create corresponding schedules
  // IntMatrix ILoop{IJLoop(_(0,2),_(0,3))};
  // LoopBlock jOuterLoopNest;
  // Array IDs are:
  // A: 0
  // x: 1
  // s: 2
  llvm::Type *i64 = builder.getInt64Ty();
  llvm::ScalarEvolution &SE{tlf.getSE()};
  ArrayReference indAiOuter{scevA, loopIJ, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAiOuter.indexMatrix();
    //     l  d
    indMat(i, 1) = 1; // i
    indMat(j, 0) = 1; // j
    indAiOuter.sizes[0] = II;
    indAiOuter.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  ArrayReference indAjOuter{scevA, loopJI, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAjOuter.indexMatrix();
    //     l  d
    indMat(ii, 1) = 1; // i
    indMat(jo, 0) = 1; // j
    indAjOuter.sizes[0] = II;
    indAjOuter.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  ArrayReference xInd1{scevX, loopI, 1};
  {
    MutPtrMatrix<int64_t> indMat = xInd1.indexMatrix();
    //     l  d
    indMat(i, 0) = 1; // i
    xInd1.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  ArrayReference xInd2IOuter{scevX, loopIJ, 1};
  {
    MutPtrMatrix<int64_t> indMat = xInd2IOuter.indexMatrix();
    //     l  d
    indMat(i, 0) = 1; // i
    xInd2IOuter.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  ArrayReference xInd2JOuter{scevX, loopJI, 1};
  {
    MutPtrMatrix<int64_t> indMat = xInd2JOuter.indexMatrix();
    //     l  d
    indMat(ii, 0) = 1; // i
    xInd2JOuter.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  ArrayReference sInd1{scevS, loopI, 1};
  {
    MutPtrMatrix<int64_t> indMat = sInd1.indexMatrix();
    //     l  d
    indMat(i, 0) = 1; // i
    sInd1.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  ArrayReference sInd2IOuter{scevS, loopIJ, 1};
  {
    MutPtrMatrix<int64_t> indMat = sInd2IOuter.indexMatrix();
    //     l  d
    indMat(i, 0) = 1; // i
    sInd2IOuter.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  ArrayReference sInd2JOuter{scevS, loopJI, 1};
  {
    MutPtrMatrix<int64_t> indMat = sInd2JOuter.indexMatrix();
    //     l  d
    indMat(ii, 0) = 1; // i
    sInd2JOuter.sizes[0] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }

  Vector<unsigned, 4> sch0t0(1 + 1, 0);
  Vector<unsigned, 4> sch0t1t0(2 + 1, 0);
  sch0t1t0[2] = 1;
  Vector<unsigned, 4> sch0t1t1(2 + 1, 0);
  sch0t1t1[1] = 1;
  sch0t1t1[2] = 1;
  Vector<unsigned, 4> sch0t1t2(2 + 1, 0);
  sch0t1t2[1] = 1;
  sch0t1t2[2] = 2;
  Vector<unsigned, 4> sch0t2(1 + 1, 0);
  sch0t2[1] = 2;
  Vector<unsigned, 4> sch0t3(1 + 1, 0);
  sch0t3[1] = 3;
  Vector<unsigned, 4> sch0t4(1 + 1, 0);
  sch0t4[1] = 4;
  Vector<unsigned, 4> sch0t5t0(2 + 1, 0);
  sch0t5t0[1] = 5;
  Vector<unsigned, 4> sch0t5t1(2 + 1, 0);
  sch0t5t1[1] = 5;
  sch0t5t1[2] = 1;
  Vector<unsigned, 4> sch0t5t2(2 + 1, 0);
  sch0t5t2[1] = 5;
  sch0t5t2[2] = 2;
  Vector<unsigned, 4> sch0t5t3(2 + 1, 0);
  sch0t5t3[1] = 5;
  sch0t5t3[2] = 3;
  Vector<unsigned, 4> sch0t6(1 + 1, 0);
  sch0t6[1] = 6;
  Vector<unsigned, 4> sch0t7(1 + 1, 0);
  sch0t7[1] = 7;
  lp::LoopBlock iOuterLoopNest;
  llvm::SmallVector<IR::Addr *> iOuterMem;

  OwningArena<> alloc;
  iOuterMem.emplace_back(createMemAccess(&alloc, xInd1, storeX0, sch0t0)); // 0

  iOuterMem.emplace_back(
    createMemAccess(&alloc, indAiOuter, loadAm, sch0t1t0)); // 1
  iOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2IOuter, loadX0, sch0t1t1)); // 2

  iOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2IOuter, storeX1, sch0t1t2)); // 3

  iOuterMem.emplace_back(createMemAccess(&alloc, xInd1, loadX1, sch0t2));  // 4
  iOuterMem.emplace_back(createMemAccess(&alloc, xInd1, storeX2, sch0t3)); // 5

  iOuterMem.emplace_back(createMemAccess(&alloc, sInd1, storeS0, sch0t4)); // 6
  iOuterMem.emplace_back(
    createMemAccess(&alloc, indAiOuter, loadAs, sch0t5t0)); // 7
  iOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2IOuter, loadX2, sch0t5t1)); // 8
  iOuterMem.emplace_back(
    createMemAccess(&alloc, sInd2IOuter, loadS0, sch0t5t2)); // 9
  iOuterMem.emplace_back(
    createMemAccess(&alloc, sInd2IOuter, storeS1, sch0t5t3)); // 10

  iOuterMem.emplace_back(createMemAccess(&alloc, sInd1, loadS1, sch0t6));  // 11
  iOuterMem.emplace_back(createMemAccess(&alloc, sInd1, storeS2, sch0t7)); // 12
  for (auto &&mem : iOuterMem) iOuterLoopNest.addMemory(mem);
  {
    auto d0 = poly::Dependence::check(&alloc, *iOuterLoopNest.getIR::Addr(3),
                                      *iOuterLoopNest.getIR::Addr(5));
    EXPECT_EQ(d0.size(), 1);
    EXPECT_TRUE(d0[0].isForward());
    auto d1 = poly::Dependence::check(&alloc, *iOuterLoopNest.getIR::Addr(5),
                                      *iOuterLoopNest.getIR::Addr(3));
    EXPECT_EQ(d1.size(), 1);
    EXPECT_FALSE(d1[0].isForward());
    auto d2 = poly::Dependence::check(&alloc, *iOuterLoopNest.getIR::Addr(4),
                                      *iOuterLoopNest.getIR::Addr(5));
    EXPECT_EQ(d2.size(), 1);
    EXPECT_TRUE(d2[0].isForward());
    auto d3 = poly::Dependence::check(&alloc, *iOuterLoopNest.getIR::Addr(5),
                                      *iOuterLoopNest.getIR::Addr(4));
    EXPECT_EQ(d3.size(), 1);
    EXPECT_FALSE(d3[0].isForward());
  }
  std::optional<BitSet<std::array<uint64_t, 2>>> optDeps =
    iOuterLoopNest.optimize();
  EXPECT_TRUE(optDeps.has_value());
  map<IR::Addr *, size_t> memAccessIds;
  MutPtrVector<IR::Addr *> mem = iOuterLoopNest.getMem();
  for (size_t jj = 0; jj < mem.size(); ++jj) memAccessIds[mem[jj]] = jj;
  for (auto &e : iOuterLoopNest.getEdges()) {
    auto [in, out] = e.getInOutPair();
    llvm::errs() << "Edge for array " << e.getArrayPointer()
                 << ", in ID: " << memAccessIds[in]
                 << "; out ID: " << memAccessIds[out] << "\n";
  }
  auto nodes = iOuterLoopNest.getNodes();
  for (size_t jj = 0; jj < nodes.size(); ++jj) {
    const auto &v = nodes[jj];
    llvm::errs() << "v_" << jj << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  // Graphs::print(iOuterLoopNest.fullGraph());
  for (auto *memi : mem) {
    llvm::errs() << "mem->nodeIndex =" << memi->getNode() << ";";
    llvm::errs() << "mem =" << memi << "\n";
    size_t nodeIndex = memi->getNode();
    AffineSchedule s = nodes[nodeIndex].getSchedule();
    EXPECT_EQ(s.data(), iOuterLoopNest.getNode(nodeIndex).getSchedule().data());
    llvm::errs() << "s.getPhi() =" << s.getPhi() << "\n";
    llvm::errs() << "s.getFusionOmega() =" << s.getFusionOmega() << "\n";
    llvm::errs() << "s.getOffsetOmega() =" << s.getOffsetOmega() << "\n";
  }

  lp::LoopBlock jOuterLoopNest;
  llvm::SmallVector<IR::Addr *> jOuterMem;
  jOuterMem.emplace_back(createMemAccess(&alloc, xInd1, storeX0, sch0t0)); // 0
  Vector<unsigned, 4> sch0t1(1 + 1, 0);
  sch0t1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(&alloc, sInd1, storeS0, sch0t1)); // 6
  Vector<unsigned, 4> sch1t0t0(2 + 1, 0);
  sch1t0t0[0] = 1;
  Vector<unsigned, 4> sch1t0t1(2 + 1, 0);
  sch1t0t1[0] = 1;
  sch1t0t1[2] = 1;
  Vector<unsigned, 4> sch1t0t2(2 + 1, 0);
  sch1t0t2[0] = 1;
  sch1t0t2[2] = 2;
  jOuterMem.emplace_back(
    createMemAccess(&alloc, indAjOuter, loadAm, sch1t0t0)); // 1
  jOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2JOuter, loadX0, sch1t0t1)); // 2
  jOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2JOuter, storeX1, sch1t0t2)); // 3

  Vector<unsigned, 4> sch2t0(1 + 1, 0);
  sch2t0[0] = 2;
  Vector<unsigned, 4> sch2t1(1 + 1, 0);
  sch2t1[0] = 2;
  sch2t1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(&alloc, xInd1, loadX1, sch2t0));  // 4
  jOuterMem.emplace_back(createMemAccess(&alloc, xInd1, storeX2, sch2t1)); // 5

  Vector<unsigned, 4> sch3t0t0(2 + 1, 0);
  sch3t0t0[0] = 3;
  Vector<unsigned, 4> sch3t0t1(2 + 1, 0);
  sch3t0t1[0] = 3;
  sch3t0t1[2] = 1;
  Vector<unsigned, 4> sch3t0t2(2 + 1, 0);
  sch3t0t2[0] = 3;
  sch3t0t2[2] = 2;
  Vector<unsigned, 4> sch3t0t3(2 + 1, 0);
  sch3t0t3[0] = 3;
  sch3t0t3[2] = 3;

  jOuterMem.emplace_back(
    createMemAccess(&alloc, indAjOuter, loadAs, sch3t0t0)); // 7
  jOuterMem.emplace_back(
    createMemAccess(&alloc, xInd2JOuter, loadX2, sch3t0t1)); // 8
  jOuterMem.emplace_back(
    createMemAccess(&alloc, sInd2JOuter, loadS0, sch3t0t2)); // 9
  jOuterMem.emplace_back(
    createMemAccess(&alloc, sInd2JOuter, storeS1, sch3t0t3)); // 10

  Vector<unsigned, 4> sch4t0(1 + 1, 0);
  sch4t0[0] = 4;
  Vector<unsigned, 4> sch4t1(1 + 1, 0);
  sch4t1[0] = 4;
  sch4t1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(&alloc, sInd1, loadS1, sch4t0));  // 11
  jOuterMem.emplace_back(createMemAccess(&alloc, sInd1, storeS2, sch4t1)); // 12

  for (auto &&memj : jOuterMem) jOuterLoopNest.addMemory(memj);

  EXPECT_TRUE(jOuterLoopNest.optimize().has_value());
  for (auto &edge : jOuterLoopNest.getEdges())
    llvm::errs() << "\nedge = " << edge << "\n";

  for (size_t jj = 0; jj < jOuterLoopNest.numNodes(); ++jj) {
    const auto &v = jOuterLoopNest.getNode(jj);
    llvm::errs() << "v_" << jj << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  DenseMatrix<int64_t> optS{SquareDims{2}, 0};
  // we want antiDiag, as that represents swapping loops
  optS.antiDiag() << 1;
  for (auto *memi : jOuterLoopNest.getMem()) {
    size_t nodeIndex = memi->getNode();
    AffineSchedule s = jOuterLoopNest.getNode(nodeIndex).getSchedule();
    if (s.getNumLoops() == 1) EXPECT_EQ(s.getPhi()(0, 0), 1);
    else EXPECT_EQ(s.getPhi(), optS);
  }
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(DoubleDependenceTest, BasicAssertions) {

  TestLoopFunction tlf;
  auto &builder = tlf.getBuilder();
  IntMatrix loopA{"[-2 1 0 -1 0; "
                  "0 0 0 1 0; "
                  "-2 0 1 0 -1; "
                  "0 0 0 0 1]"_mat};
  tlf.addLoop(std::move(loopA), 2);
  poly::Loop *loop = tlf.getLoopNest(0);

  // create arrays
  llvm::Type *f64 = builder.getDoubleTy();
  llvm::Value *ptrA = tlf.createArray();
  const auto *scevA = tlf.getSCEVUnknown(ptrA);

  const llvm::SCEV *II = loop->getSyms()[0];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(II)->getValue();
  // llvm::Value* J = loop->getSyms()[1];
  auto *zero = builder.getInt64(0);
  auto *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);

  llvm::Value *offsetAip1jp1 =
    builder.CreateAdd(builder.CreateAdd(iv, one),
                      builder.CreateMul(builder.CreateAdd(jv, one), Iv));
  llvm::Value *offsetAip1j =
    builder.CreateAdd(iv, builder.CreateMul(builder.CreateAdd(jv, one), Iv));
  llvm::Value *offsetAijp1 =
    builder.CreateAdd(builder.CreateAdd(iv, one), builder.CreateMul(jv, Iv));

  auto *loadAip1j = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{offsetAip1j}),
    llvm::MaybeAlign(8));
  auto *loadijp1 = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{offsetAijp1}),
    llvm::MaybeAlign(8));
  auto *storeA = builder.CreateAlignedStore(
    builder.CreateFAdd(loadAip1j, loadijp1),
    builder.CreateGEP(f64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{offsetAip1jp1}),
    llvm::MaybeAlign(8));

  // for (i = 0:I-2){
  //   for (j = 0:J-2){
  //     A(j+1,i+1) = A(j,i+1) + A(j+1,i);
  //   }
  // }
  // A*x >= 0;
  // [ -2  1  0 -1  0    [ 1
  //    0  0  0  1  0  *   I   >= 0
  //   -2  0  1  0 -1      J
  //    0  0  0  0  1 ]    i
  //                       j ]
  constexpr size_t i = 0, j = 1;
  // we have three array refs
  // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = builder.getInt64Ty();
  ArrayReference srcA(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = srcA.indexMatrix();
    //     l  d
    indMat(i, 1) = 1; // i
    indMat(j, 0) = 1; // j
    MutPtrMatrix<int64_t> offMat = srcA.offsetMatrix();
    offMat(i, 0) = 1;
    offMat(j, 0) = 1;
    srcA.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
    srcA.sizes[0] = II;
  }

  // A[i+1, j]
  ArrayReference tgtA0(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA0.indexMatrix();
    //     l  d
    indMat(i, 1) = 1; // i
    indMat(j, 0) = 1; // j
                      //                   d  s
    tgtA0.offsetMatrix()(1, 0) = 1;
    tgtA0.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
    tgtA0.sizes[0] = II;
  }

  // A[i, j+1]
  ArrayReference tgtA1(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> indMat = tgtA1.indexMatrix();
    //     l  d
    indMat(i, 1) = 1; // i
    indMat(j, 0) = 1; // j
    tgtA1.offsetMatrix()(0, 0) = 1;
    tgtA1.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
    tgtA1.sizes[0] = II;
  }

  //
  Vector<unsigned, 4> schLoad0(2 + 1, 0);
  Vector<unsigned, 4> schStore(2 + 1, 0);
  schStore[2] = 2;
  OwningArena<> alloc;
  IR::Addr *msrc{createMemAccess(&alloc, srcA, storeA, schStore)};
  IR::Addr *mtgt0{createMemAccess(&alloc, tgtA0, loadAip1j, schLoad0)};
  DepPoly *dep0{DepPoly::dependence(alloc, *msrc, *mtgt0)};
  EXPECT_FALSE(dep0->isEmpty());
  dep0->pruneBounds();
  llvm::errs() << "Dep0 = \n" << dep0 << "\n";

  EXPECT_EQ(dep0->getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep0->getNumEqualityConstraints(), 2);
  assert(dep0->getNumInequalityConstraints() == 4);
  assert(dep0->getNumEqualityConstraints() == 2);

  Vector<unsigned, 4> schLoad1(2 + 1, 0);
  schLoad1[2] = 1;
  IR::Addr *mtgt1{createMemAccess(&alloc, tgtA1, loadijp1, schLoad1)};
  DepPoly *dep1{DepPoly::dependence(alloc, *msrc, *mtgt1)};
  EXPECT_FALSE(dep1->isEmpty());
  dep1->pruneBounds();
  llvm::errs() << "Dep1 = \n" << dep1 << "\n";
  EXPECT_EQ(dep1->getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep1->getNumEqualityConstraints(), 2);
  assert(dep1->getNumInequalityConstraints() == 4);
  assert(dep1->getNumEqualityConstraints() == 2);
  auto d = poly::Dependence::check(&alloc, *msrc, *mtgt0);
  EXPECT_EQ(d.size(), 1);
  EXPECT_TRUE(d[0].isForward());
  llvm::errs() << d[0] << "\n";
  assert(d[0].isForward());
  assert(!allZero(d[0].getSatConstraints()(last, _)));

  lp::LoopBlock loopBlock;
  IR::Addr *mSchLoad0(createMemAccess(&alloc, tgtA0, loadAip1j, schLoad0));
  loopBlock.addMemory(mSchLoad0);
  IR::Addr *mSchLoad1(createMemAccess(&alloc, tgtA1, loadijp1, schLoad1));
  loopBlock.addMemory(mSchLoad1);
  IR::Addr *mSchStore(createMemAccess(&alloc, srcA, storeA, schStore));
  loopBlock.addMemory(mSchStore);

  EXPECT_TRUE(loopBlock.optimize().has_value());
  EXPECT_EQ(loopBlock.numEdges(), 2);
  map<IR::Addr *, size_t> memAccessIds;
  for (size_t jj = 0; jj < loopBlock.numIR::Addres(); ++jj)
    memAccessIds[loopBlock.getIR::Addr(jj)] = jj;
  for (auto &e : loopBlock.getEdges()) {
    auto [in, out] = e.getInOutPair();
    llvm::errs() << "\nEdge for array " << *e.getArrayPointer()
                 << ", in ID: " << memAccessIds[in]
                 << "; out ID: " << memAccessIds[out] << "\n";
  }
  for (size_t jj = 0; jj < loopBlock.numNodes(); ++jj) {
    const auto &v = loopBlock.getNode(jj);
    llvm::errs() << "v_" << jj << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  DenseMatrix<int64_t> optPhi(DenseDims{2, 2}, 0);
  optPhi(0, _) << 1;
  optPhi(1, 0) = 1;
  optPhi(1, 1) = 0;
  // Graphs::print(iOuterLoopNest.fullGraph());
  for (auto &mem : loopBlock.getMem()) {
    size_t nodeIndex = mem->getNode();
    AffineSchedule s = loopBlock.getNode(nodeIndex).getSchedule();
    EXPECT_EQ(s.getPhi(), optPhi);
    EXPECT_TRUE(allZero(s.getOffsetOmega()));
    EXPECT_TRUE(allZero(s.getFusionOmega()));
  }
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
  auto &builder = tlf.getBuilder();
  // syms: N, M, J, I
  IntMatrix loopA{"[-1 0 1 0 0 0 -1 0 0; "
                  "0 0 0 0  0 0 1 0 0; "
                  "-1 1 0 0 0 -1 0 0 0; "
                  "0 0 0 0  0 1 0 0 0; "
                  "-1 0 0 0 1 0 0 0 -1; "
                  "0 0 0 0 0 0 0 0 1; "
                  "-1 0 0 1 0 0 0 -1 0; "
                  "0 0 0 0 0 0 0 1 0]"_mat};
  tlf.addLoop(std::move(loopA), 4);
  poly::Loop *loop = tlf.getLoopNest(0);

  // create arrays
  llvm::Type *f64 = builder.getDoubleTy();
  llvm::Value *ptrB = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrC = tlf.createArray();
  const auto *scevB = tlf.getSCEVUnknown(ptrB);
  const auto *scevA = tlf.getSCEVUnknown(ptrA);
  const auto *scevC = tlf.getSCEVUnknown(ptrC);

  // llvm::ConstantInt *Jv = builder.getInt64(100);
  constexpr size_t n = 0, m = 1, j = 2, i = 3;
  const llvm::SCEV *II = loop->getSyms()[i];
  const llvm::SCEV *M = loop->getSyms()[m];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(II)->getValue();
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();
  // llvm::ConstantInt *Nv = builder.getInt64(400);
  auto *zero = builder.getInt64(0);
  auto *one = builder.getInt64(1);
  llvm::Value *mv = builder.CreateAdd(zero, one);
  llvm::Value *nv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *iv = builder.CreateAdd(zero, one);

  llvm::Value *offseA = builder.CreateAdd(mv, builder.CreateMul(nv, Mv));
  llvm::Value *offsetB = builder.CreateAdd(iv, builder.CreateMul(jv, Iv));
  llvm::Value *offsetC = builder.CreateAdd(
    builder.CreateAdd(mv, iv),
    builder.CreateMul(builder.CreateAdd(nv, jv),
                      builder.CreateSub(builder.CreateAdd(Mv, Iv), one)));
  auto *loadA = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offseA}),
    llvm::MaybeAlign(8));
  auto *loadB = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrB, llvm::SmallVector<llvm::Value *, 1>{offsetB}),
    llvm::MaybeAlign(8));
  auto *loadC = builder.CreateAlignedLoad(
    f64,
    builder.CreateGEP(f64, ptrC, llvm::SmallVector<llvm::Value *, 1>{offsetC}),
    llvm::MaybeAlign(8));
  auto *storeC = builder.CreateAlignedStore(
    builder.CreateFAdd(loadC, builder.CreateFMul(loadA, loadB)),
    builder.CreateGEP(f64, ptrC, llvm::SmallVector<llvm::Value *, 1>{offsetC}),
    llvm::MaybeAlign(8));

  // for (n = 0; n < N; ++n){
  //   for (m = 0; n < M; ++m){
  //     for (j = 0; n < J; ++j){
  //       for (i = 0; n < I; ++i){
  //         C[n+j,m+i] += A[n,m] * B[j,i];
  //       }
  //     }
  //   }
  // }
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::Type *i64 = builder.getInt64Ty();
  // B[j, i]
  ArrayReference indBmn{scevB, loop, 2};
  {
    MutPtrMatrix<int64_t> indMat = indBmn.indexMatrix();
    //     l  d
    indMat(i, 1) = 1; // i
    indMat(j, 0) = 1; // j
    indBmn.sizes[0] = II;
    indBmn.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference indAmn{scevA, loop, 2};
  {
    MutPtrMatrix<int64_t> indMat = indAmn.indexMatrix();
    //     l  d
    indMat(m, 1) = 1; // m
    indMat(n, 0) = 1; // n
    indAmn.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
    indAmn.sizes[0] = II;
  }
  // C[n+j, m+i]
  ArrayReference indCmijn{scevC, loop, 2};
  {
    MutPtrMatrix<int64_t> indMat = indCmijn.indexMatrix();
    //     l  d
    indMat(m, 1) = 1; // m
    indMat(i, 1) = 1; // i
    indMat(n, 0) = 1; // n
    indMat(j, 0) = 1; // j
    indCmijn.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
    indCmijn.sizes[0] =
      SE.getAddExpr(SE.getAddExpr(M, II), SE.getMinusOne(i64));
  }

  // for (n = 0; n < N; ++n){
  //   for (m = 0; n < M; ++m){
  //     for (j = 0; n < J; ++j){
  //       for (i = 0; n < I; ++i){
  //         C[n+j,m+i] = C[n+j,m+i] + A[n,m] * B[j,i];
  //       }
  //     }
  //   }
  // }
  lp::LoopBlock loopBlock;
  Vector<unsigned, 8> scht0(4 + 1, 0);
  Vector<unsigned, 8> scht1{scht0};
  Arena<> *alloc = tlf.getAlloc();
  //         C[m+i,j+n] = C[m+i,j+n] + A[m,n] * -> B[i,j] <-;
  IR::Addr *mscht0(createMemAccess(&alloc, indBmn, loadB, scht0));
  loopBlock.addMemory(mscht0);
  scht1[4] = 1;
  Vector<unsigned, 8> scht2{scht1};
  //         C[m+i,j+n] = C[m+i,j+n] + -> A[m,n] <- * B[i,j];
  IR::Addr *mscht1(createMemAccess(&alloc, indAmn, loadA, scht1));
  loopBlock.addMemory(mscht1);
  scht2[4] = 2;
  Vector<unsigned, 8> scht3{scht2};
  //         C[m+i,j+n] = -> C[m+i,j+n] <- + A[m,n] * B[i,j];
  IR::Addr *mscht2(createMemAccess(&alloc, indCmijn, loadC, scht2));
  loopBlock.addMemory(mscht2);
  scht3[4] = 3;
  //         -> C[m+i,j+n] <- = C[m+i,j+n] + A[m,n] * B[i,j];
  IR::Addr *mscht3(createMemAccess(&alloc, indCmijn, storeC, scht3));
  loopBlock.addMemory(mscht3);

  std::optional<BitSet<std::array<uint64_t, 2>>> optRes = loopBlock.optimize();
  EXPECT_TRUE(optRes.has_value());
  for (auto &mem : loopBlock.getMem()) {
    llvm::errs() << "mem->nodeIndex: " << mem->getNode() << "; ";
    llvm::errs() << "mem: " << mem << "\n";
    size_t nodeIndex = mem->getNode();
    AffineSchedule s = loopBlock.getNode(nodeIndex).getSchedule();
    llvm::errs() << "s.getPhi(): " << s.getPhi() << "\n";
    llvm::errs() << "s.getFusionOmega(): " << s.getFusionOmega() << "\n";
    llvm::errs() << "s.getOffsetOmega(): " << s.getOffsetOmega() << "\n";
  }
}
} // namespace poly
