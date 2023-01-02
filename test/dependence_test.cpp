#include "../include/DependencyPolyhedra.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/Loops.hpp"
#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include "../include/MemoryAccess.hpp"
#include "./ArrayReference.hpp"
#include "./TestUtilities.hpp"
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
  IntMatrix Aloop{"[-2 1 0 0 -1; "
                  "0 0 0 0 1; "
                  "-2 0 1 -1 0; "
                  "0 0 0 1 0]"_mat};
  TestLoopFunction tlf;

  tlf.addLoop(std::move(Aloop), 2);
  auto &loop = tlf.alns.front();
  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = tlf.builder.getInt64Ty();
  auto ptrA = tlf.createArray();
  auto scevA = tlf.getSCEVUnknown(ptrA);

  // create arrays
  auto &builder = tlf.builder;
  llvm::Type *Float64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop.S[0];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *ivp1 = builder.CreateAdd(iv, one);
  llvm::Value *jvp1 = builder.CreateAdd(jv, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *offset01 = builder.CreateAdd(jv, builder.CreateMul(ivp1, Mv));
  auto Ageped01 = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset01}, "gep_A01");
  auto Aload01 = builder.CreateAlignedLoad(Float64, Ageped01,
                                           llvm::MaybeAlign(8), "load_A01");

  llvm::Value *offset10 = builder.CreateAdd(jvp1, builder.CreateMul(iv, Mv));
  auto Ageped10 = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset10}, "gep_A10");
  auto Aload10 = builder.CreateAlignedLoad(Float64, Ageped10,
                                           llvm::MaybeAlign(8), "load_A10");
  llvm::Value *offset11 = builder.CreateAdd(jvp1, builder.CreateMul(ivp1, Mv));
  auto Ageped11 = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset11}, "gep_A11");
  auto Astore11 = builder.CreateAlignedStore(
    builder.CreateFAdd(Aload10, Aload01, "A10 + A01"), Ageped11,
    llvm::MaybeAlign(8), false);

  // we have three array refs
  // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
  ArrayReference Asrc(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i (loop ind: 1)
    IndMat(0, 1) = 1; // j (loop ind: 0)
    MutPtrMatrix<int64_t> OffMat = Asrc.offsetMatrix();
    OffMat(0, 0) = 1;
    OffMat(1, 0) = 1;
    Asrc.sizes[0] = M;
    Asrc.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  // A[i+1, j]
  ArrayReference Atgt01(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt01.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i
    IndMat(0, 1) = 1; // j
    Atgt01.offsetMatrix()(0, 0) = 1;
    Atgt01.sizes[0] = M;
    Atgt01.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  // A[i, j+1]
  ArrayReference Atgt10(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt10.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i
    IndMat(0, 1) = 1; // j
    Atgt10.offsetMatrix()(1, 0) = 1;
    Atgt10.sizes[0] = M;
    Atgt10.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  //
  llvm::SmallVector<unsigned, 8> schLoad0(3);
  llvm::SmallVector<unsigned, 8> schStore(3);
  schStore[2] = 2;
  MemoryAccess msrc{createMemAccess(Asrc, Astore11, schStore)};
  MemoryAccess mtgt01{createMemAccess(Atgt01, Aload01, schLoad0)};
  DependencePolyhedra dep0(msrc, mtgt01);
  EXPECT_FALSE(dep0.isEmpty());
  dep0.pruneBounds();
  llvm::errs() << "Dep0 = \n" << dep0 << "\n";

  EXPECT_EQ(dep0.getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep0.getNumEqualityConstraints(), 2);
  assert(dep0.getNumInequalityConstraints() == 4);
  assert(dep0.getNumEqualityConstraints() == 2);

  llvm::SmallVector<unsigned, 8> schLoad1(3);
  schLoad1[2] = 1;
  MemoryAccess mtgt10{createMemAccess(Atgt10, Aload10, schLoad1)};
  DependencePolyhedra dep1(msrc, mtgt10);
  EXPECT_FALSE(dep1.isEmpty());
  dep1.pruneBounds();
  llvm::errs() << "Dep1 = \n" << dep1 << "\n";
  EXPECT_EQ(dep1.getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep1.getNumEqualityConstraints(), 2);
  assert(dep1.getNumInequalityConstraints() == 4);
  assert(dep1.getNumEqualityConstraints() == 2);
  // MemoryAccess mtgt1{Atgt1,nullptr,schLoad,true};
  llvm::SmallVector<Dependence, 1> dc;
  EXPECT_EQ(dc.size(), 0);
  EXPECT_EQ(Dependence::check(dc, msrc, mtgt01), 1);
  EXPECT_EQ(dc.size(), 1);
  Dependence &d(dc.front());
  EXPECT_TRUE(d.forward);
  llvm::errs() << d << "\n";
  assert(d.forward);
  assert(!allZero(d.dependenceSatisfaction.tableau(
    d.dependenceSatisfaction.tableau.numRow() - 1, _)));
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(SymmetricIndependentTest, BasicAssertions) {
  // symmetric copy
  // for(i = 0:I-1)
  //   for(j = 0:i-1)
  //     A(j,i) = A(i,j)
  //
  IntMatrix Aloop{"[-1 1 0 -1; "
                  "0 0 0 1; "
                  "-1 0 -1 1; "
                  "0 0 1 0]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(Aloop), 2);
  auto &loop = tlf.alns.front();
  // loop.pruneBounds();

  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = tlf.builder.getInt64Ty();
  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.builder;
  llvm::Type *Float64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop.S[0];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *offsetji = builder.CreateAdd(jv, builder.CreateMul(iv, Mv));
  auto Agepedji = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetji}, "gep_Aji");
  auto Aloadji = builder.CreateAlignedLoad(Float64, Agepedji,
                                           llvm::MaybeAlign(8), "load_Aji");
  llvm::Value *offsetij = builder.CreateAdd(iv, builder.CreateMul(jv, Mv));
  auto Agepedij = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetij}, "gep_Aij");
  auto Astoreij =
    builder.CreateAlignedStore(Aloadji, Agepedij, llvm::MaybeAlign(8), false);

  // we have three array refs
  // A[i, j]
  ArrayReference Asrc(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i
    IndMat(0, 1) = 1; // j
    Asrc.sizes[0] = loop.S[0];
    Asrc.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  // A[j, i]
  ArrayReference Atgt(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // j
    IndMat(1, 1) = 1; // i
    Atgt.sizes[0] = loop.S[0];
    Atgt.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  llvm::SmallVector<unsigned, 8> schLoad(3);
  llvm::SmallVector<unsigned, 8> schStore(3);
  schStore[2] = 1;
  MemoryAccess msrc{createMemAccess(Asrc, Astoreij, schStore)};
  MemoryAccess mtgt{createMemAccess(Atgt, Aloadji, schLoad)};
  DependencePolyhedra dep(msrc, mtgt);
  llvm::errs() << "Dep = \n" << dep << "\n";
  EXPECT_TRUE(dep.isEmpty());
  assert(dep.isEmpty());
  //
  llvm::SmallVector<Dependence, 0> dc;
  EXPECT_EQ(Dependence::check(dc, msrc, mtgt), 0);
  EXPECT_EQ(dc.size(), 0);
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
  IntMatrix Aloop{"[-1 1 0 -1; "
                  "0 0 0 1; "
                  "0 0 -1 1; "
                  "0 0 1 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(Aloop), 2);
  auto &loop = tlf.alns.front();
  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = tlf.builder.getInt64Ty();
  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.builder;
  llvm::Type *Float64 = builder.getDoubleTy();

  const llvm::SCEV *M = loop.S[0];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();

  llvm::Value *offsetii = builder.CreateAdd(iv, builder.CreateMul(iv, Mv));
  auto Agepedii = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetii}, "gep_Aji");
  auto Aloadii = builder.CreateAlignedLoad(Float64, Agepedii,
                                           llvm::MaybeAlign(8), "load_Aii");

  llvm::Value *offsetij = builder.CreateAdd(iv, builder.CreateMul(jv, Mv));
  auto Agepedij = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offsetij}, "gep_Aij");
  auto Astoreij =
    builder.CreateAlignedStore(Aloadii, Agepedij, llvm::MaybeAlign(8), false);

  // we have three array refs
  // A[i, j] // i*stride(A,1) + j*stride(A,2);
  ArrayReference Asrc(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
    IndMat(1, 0) = 1; // i
    IndMat(0, 1) = 1; // j
    Asrc.sizes[0] = loop.S[0];
    Asrc.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  // A[i, i]
  ArrayReference Atgt(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt.indexMatrix();
    IndMat(1, 0) = 1; // i
    IndMat(1, 1) = 1; // i
    Atgt.sizes[0] = loop.S[0];
    Atgt.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  llvm::SmallVector<unsigned, 8> schLoad(2 + 1);
  llvm::SmallVector<unsigned, 8> schStore(2 + 1);
  schStore[2] = 1;
  MemoryAccess msrc{createMemAccess(Asrc, Astoreij, schStore)};
  MemoryAccess mtgt{createMemAccess(Atgt, Aloadii, schLoad)};

  llvm::SmallVector<Dependence, 1> deps;
  EXPECT_EQ(Dependence::check(deps, msrc, mtgt), 1);
  EXPECT_FALSE(deps.back().forward); // load -> store
  llvm::errs() << "Blog post example:\n" << deps[0] << "\n";
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
  IntMatrix Aloop{"[-1 1 0 0 0 0 -1; "
                  "0 0 0 0 0 0 1; "
                  "-1 0 1 0 0 -1 0; "
                  "0 0 0 0 0 1 0; "
                  "-1 0 0 1 -1 0 0; "
                  "0 0 0 0 1 0 0]"_mat};
  TestLoopFunction tlf;
  tlf.addLoop(std::move(Aloop), 3);
  auto &loop = tlf.alns.front();
  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = tlf.builder.getInt64Ty();

  const llvm::SCEV *I = loop.S[0];
  const llvm::SCEV *J = loop.S[1];
  const llvm::SCEV *K = loop.S[2];

  auto *ptrA = tlf.createArray();
  const llvm::SCEVUnknown *scevA = tlf.getSCEVUnknown(ptrA);
  auto &builder = tlf.builder;
  llvm::Type *Float64 = builder.getDoubleTy();

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

  auto Ageped = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{offset}, "gep_A");

  auto Aload =
    builder.CreateAlignedLoad(Float64, Ageped, llvm::MaybeAlign(8), "load_A");

  auto Astore =
    builder.CreateAlignedStore(Aload, Ageped, llvm::MaybeAlign(8), false);

  // we have three array refs
  // A[i+j, j+k, i - k]
  ArrayReference Aref(scevA, loop, 3);
  {
    MutPtrMatrix<int64_t> IndMat = Aref.indexMatrix();
    IndMat(2, 0) = 1;  // i
    IndMat(1, 0) = 1;  // + j
    IndMat(1, 1) = 1;  // j
    IndMat(0, 1) = 1;  // + k
    IndMat(2, 2) = 1;  // i
    IndMat(0, 2) = -1; // -k
    Aref.sizes[0] = SE.getAddExpr(J, K);
    Aref.sizes[1] = SE.getAddExpr(I, K);
    Aref.sizes[2] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  llvm::SmallVector<unsigned, 8> schLoad(3 + 1);
  llvm::SmallVector<unsigned, 8> schStore(3 + 1);
  schStore[3] = 1;
  MemoryAccess msrc{createMemAccess(Aref, Astore, schStore)};
  MemoryAccess mtgt{createMemAccess(Aref, Aload, schLoad)};

  llvm::SmallVector<Dependence, 2> deps;
  EXPECT_EQ(Dependence::check(deps, msrc, mtgt), 2);
  assert(deps.size() == 2);
  llvm::errs() << "Rank deficicient example:\nForward:\n"
               << deps[0] << "\nReverse:\n"
               << deps[1] << "\n";
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TriangularExampleTest, BasicAssertions) {
  IntMatrix AMN{"[-1 1 0 0 -1; "
                "0 0 0 0 1; "
                "-1 0 1 -1 0; "
                "0 0 0 1 0]"_mat};
  IntMatrix AMNK{"[-1 1 0 0 0 -1; "
                 "0 0 0 0 0 1; "
                 "-1 0 1 0 -1 0; "
                 "0 0 0 0 1 0; "
                 "-1 0 1 -1 0 0; "
                 "-1 0 0 1 -1 0]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(AMN), 2);
  tlf.addLoop(std::move(AMNK), 3);
  AffineLoopNest<true> &loopMN = tlf.alns[0];
  EXPECT_FALSE(loopMN.isEmpty());
  AffineLoopNest<true> &loopMNK = tlf.alns[1];
  EXPECT_FALSE(loopMNK.isEmpty());
  EXPECT_EQ(loopMN.S.size(), loopMNK.S.size());
  for (size_t i = 0; i < loopMN.S.size(); ++i)
    EXPECT_EQ(loopMN.S[i], loopMNK.S[i]);

  llvm::ScalarEvolution &SE{tlf.SE};
  auto &builder = tlf.builder;
  llvm::IntegerType *Int64 = builder.getInt64Ty();

  // create arrays
  llvm::Type *Float64 = builder.getDoubleTy();
  llvm::Value *ptrB = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrU = tlf.createArray();

  const llvm::SCEV *M = loopMN.S[0];
  const llvm::SCEV *N = loopMN.S[1];
  llvm::Value *zero = builder.getInt64(0);
  llvm::Value *one = builder.getInt64(1);
  llvm::Value *mv = builder.CreateAdd(zero, one);
  llvm::Value *nv = builder.CreateAdd(zero, one);
  llvm::Value *kv = builder.CreateAdd(nv, one);

  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();
  llvm::Value *Nv = llvm::dyn_cast<llvm::SCEVUnknown>(N)->getValue();
  llvm::Value *Boffset = builder.CreateAdd(mv, builder.CreateMul(nv, Mv));
  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = B(n,m);
  //   }
  llvm::LoadInst *Bload = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrB,
                      llvm::SmallVector<llvm::Value *, 1>{Boffset}),
    llvm::MaybeAlign(8), "load_Bnm");
  llvm::StoreInst *Astore0 = builder.CreateAlignedStore(
    Bload,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{Boffset}),
    llvm::MaybeAlign(8), false);

  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = A(n,m) / U(n,n);
  llvm::Value *Uoffsetnn = builder.CreateAdd(nv, builder.CreateMul(nv, Nv));
  auto Uloadnn = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrU,
                      llvm::SmallVector<llvm::Value *, 1>{Uoffsetnn}),
    llvm::MaybeAlign(8), "load_Unn");
  auto Ageped0 = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{Boffset}, "gep_Anm");
  auto Aload0 = builder.CreateAlignedLoad(Float64, Ageped0, llvm::MaybeAlign(8),
                                          "load_Anm");
  auto AstoreFDiv =
    builder.CreateAlignedStore(builder.CreateFDiv(Aload0, Uloadnn, "fdiv"),
                               Ageped0, llvm::MaybeAlign(8), false);

  // for (m = 0; m < M; ++m){
  //     for (k = n+1; k < N; ++k){
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n);
  //     }
  llvm::Value *Uoffsetnk = builder.CreateAdd(nv, builder.CreateMul(kv, Nv));
  auto Uloadnk = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrU,
                      llvm::SmallVector<llvm::Value *, 1>{Uoffsetnk}),
    llvm::MaybeAlign(8), "load_Ukn");
  llvm::Value *Aoffsetmk = builder.CreateAdd(mv, builder.CreateMul(kv, Mv));
  auto Ageped1mk = builder.CreateGEP(
    Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{Aoffsetmk}, "gep_Akm");
  auto Aload1mk = builder.CreateAlignedLoad(Float64, Ageped1mk,
                                            llvm::MaybeAlign(8), "load_Akm");
  auto Aload1mn = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{Boffset}),
    llvm::MaybeAlign(8), "load_Anm");
  auto Astore2mk = builder.CreateAlignedStore(
    builder.CreateFSub(Aload1mk, builder.CreateFMul(Aload1mn, Uloadnk, "fmul"),
                       "fsub"),
    Ageped0, llvm::MaybeAlign(8), false);

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

  auto scevB = tlf.getSCEVUnknown(ptrB);
  auto scevA = tlf.getSCEVUnknown(ptrA);
  auto scevU = tlf.getSCEVUnknown(ptrU);

  // construct indices
  // ind mat, loops currently indexed from outside-in
  LinearProgramLoopBlock lblock;
  // B[n, m]
  ArrayReference BmnInd{scevB, loopMN, 2};
  {
    MutPtrMatrix<int64_t> IndMat = BmnInd.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // n
    IndMat(1, 1) = 1; // m
    BmnInd.sizes[0] = M;
    BmnInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference Amn2Ind{scevA, loopMN, 2};
  {
    MutPtrMatrix<int64_t> IndMat = Amn2Ind.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // n
    IndMat(1, 1) = 1; // m
    Amn2Ind.sizes[0] = M;
    Amn2Ind.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference Amn3Ind{scevA, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> IndMat = Amn3Ind.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // n
    IndMat(2, 1) = 1; // m
    Amn3Ind.sizes[0] = M;
    Amn3Ind.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // A[k, m]
  ArrayReference AmkInd{scevA, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> IndMat = AmkInd.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // k
    IndMat(2, 1) = 1; // m
    AmkInd.sizes[0] = M;
    AmkInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // U[k, n]
  ArrayReference UnkInd{scevU, loopMNK, 2};
  {
    MutPtrMatrix<int64_t> IndMat = UnkInd.indexMatrix();
    //     l  d
    IndMat(1, 1) = 1; // n
    IndMat(0, 0) = 1; // k
    UnkInd.sizes[0] = N;
    UnkInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // U[n, n]
  ArrayReference UnnInd{scevU, loopMN, 2};
  {
    MutPtrMatrix<int64_t> IndMat = UnnInd.indexMatrix();
    //     l  d
    IndMat(0, 1) = 1; // n
    IndMat(0, 0) = 1; // n
    UnnInd.sizes[0] = N;
    UnnInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
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
  llvm::SmallVector<unsigned, 8> sch2_0_0(2 + 1);
  llvm::SmallVector<unsigned, 8> sch2_0_1 = sch2_0_0;
  // A(n,m) = -> B(n,m) <-
  MemoryAccess mSch2_0_0(createMemAccess(BmnInd, Bload, sch2_0_0));
  lblock.addMemory(&mSch2_0_0);
  sch2_0_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch2_1_0 = sch2_0_1;
  // -> A(n,m) <- = B(n,m)
  MemoryAccess mSch2_0_1(createMemAccess(Amn2Ind, Astore0, sch2_0_1));
  assert(mSch2_0_1.getInstruction() == Astore0);
  assert(mSch2_0_1.getStore() == Astore0);
  lblock.addMemory(&mSch2_0_1);
  sch2_1_0[1] = 1;
  sch2_1_0[2] = 0;
  llvm::SmallVector<unsigned, 8> sch2_1_1 = sch2_1_0;
  // A(n,m) = -> A(n,m) <- / U(n,n); // sch2
  MemoryAccess mSch2_1_0(createMemAccess(Amn2Ind, Aload0, sch2_1_0));
  assert(mSch2_1_0.getInstruction() == Aload0);
  assert(mSch2_1_0.getLoad() == Aload0);
  lblock.addMemory(&mSch2_1_0);
  sch2_1_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch2_1_2 = sch2_1_1;
  // A(n,m) = A(n,m) / -> U(n,n) <-;
  MemoryAccess mSch2_1_1(createMemAccess(UnnInd, Uloadnn, sch2_1_1));
  lblock.addMemory(&mSch2_1_1);
  sch2_1_2[2] = 2;
  // -> A(n,m) <- = A(n,m) / U(n,n); // sch2
  MemoryAccess mSch2_1_2(createMemAccess(Amn2Ind, AstoreFDiv, sch2_1_2));
  lblock.addMemory(&mSch2_1_2);

  llvm::SmallVector<unsigned, 8> sch3_0(3 + 1);
  sch3_0[1] = 1;
  sch3_0[2] = 3;
  llvm::SmallVector<unsigned, 8> sch3_1 = sch3_0;
  // A(k,m) = A(k,m) - A(n,m)* -> U(k,n) <-;
  MemoryAccess mSch3_0(createMemAccess(UnkInd, Uloadnk, sch3_0));
  lblock.addMemory(&mSch3_0);
  sch3_1[3] = 1;
  llvm::SmallVector<unsigned, 8> sch3_2 = sch3_1;
  // A(k,m) = A(k,m) - -> A(n,m) <- *U(k,n);
  MemoryAccess mSch3_1(createMemAccess(Amn3Ind, Aload1mn, sch3_1));
  lblock.addMemory(&mSch3_1);
  sch3_2[3] = 2;
  llvm::SmallVector<unsigned, 8> sch3_3 = sch3_2;
  // A(k,m) = -> A(k,m) <- - A(n,m)*U(k,n);
  MemoryAccess mSch3_2(createMemAccess(AmkInd, Aload1mk, sch3_2));
  lblock.addMemory(&mSch3_2);
  sch3_3[3] = 3;
  // -> A(k,m) <- = A(k,m) - A(n,m)*U(k,n);
  MemoryAccess mSch3_3(createMemAccess(AmkInd, Astore2mk, sch3_3));
  lblock.addMemory(&mSch3_3);

  // for (m = 0; m < M; ++m){createMemAccess(
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = B(n,m); // sch2_0_{0-1)}
  //   }
  //   for (n = 0; n < N; ++n){
  //     A(n,m) = A(n,m) / U(n,n); // sch2_2_{0-2}
  //     for (k = n+1; k < N; ++k){
  //       A(k,m) = A(k,m) - A(n,m)*U(k,n); // sch3_{0-3}
  //     }
  //   }
  // }
  // First, comparisons of store to `A(n,m) = B(n,m)` versus...
  llvm::SmallVector<Dependence, 0> d;
  d.reserve(15);
  // // load in `A(n,m) = A(n,m) / U(n,n)`
  EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_0), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  //
  //
  // store in `A(n,m) = A(n,m) / U(n,n)`
  EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_2), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  //
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'

  EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_1), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  //
  EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_2), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_3), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  // Second, comparisons of load in `A(m,n) = A(m,n) / U(n,n)`
  // with...
  // store in `A(n,m) = A(n,m) / U(n,n)`
  EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch2_1_2), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  //
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_1), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_2), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_3), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  // Third, comparisons of store in `A(m,n) = A(m,n) / U(n,n)`
  // with...
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_1), 1);
  EXPECT_TRUE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_2), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_3), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  // Fourth, comparisons of load `A(m,n)` in
  // sch3_               3        0         1     2
  // load `A(n,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  // with...
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_2), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_3), 1);
  EXPECT_FALSE(d.back().forward);
  llvm::errs() << "dep#" << d.size() << ":\n" << d.back() << "\n";

  // Fifth, comparisons of load `A(m,k)` in
  // sch3_               3        0         1     2
  // load `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  // with...
  // store `A(k,m)` in 'A(k,m) = A(k,m) - A(n,m)*U(k,n)'
  EXPECT_EQ(Dependence::check(d, mSch3_2, mSch3_3), 2);
  EXPECT_TRUE(d[d.size() - 2].forward);
  EXPECT_FALSE(d[d.size() - 1].forward);
  llvm::errs() << "dep#" << d.size() << "\n";
  auto &forward = d[d.size() - 2];
  auto &reverse = d[d.size() - 1];
  llvm::errs() << "\nforward dependence:" << forward;
  llvm::errs() << "\nreverse dependence:" << reverse;
  assert(forward.forward);
  assert(!reverse.forward);
  EXPECT_EQ(d.size(), 16);
  EXPECT_TRUE(allZero(forward.depPoly.E(_, 0)));
  EXPECT_FALSE(allZero(reverse.depPoly.E(_, 0)));
  ptrdiff_t nonZeroInd = -1;
  for (unsigned i = 0; i < reverse.depPoly.E.numRow(); ++i) {
    bool notZero = !allZero(reverse.depPoly.getEqSymbols(i));
    // we should only find 1 non-zero
    EXPECT_FALSE((nonZeroInd != -1) & notZero);
    if (notZero) nonZeroInd = i;
  }
  // v_1 is `n` for the load
  // v_4 is `n` for the store
  // thus, we expect v_1 = v_4 + 1
  // that is, the load depends on the store from the previous iteration
  // (e.g., store when `v_4 = 0` is loaded when `v_1 = 1`.
  auto nonZero = reverse.depPoly.getCompTimeEqOffset(nonZeroInd);
  const size_t numSymbols = reverse.depPoly.getNumSymbols();
  EXPECT_EQ(numSymbols, 3);
  EXPECT_TRUE(nonZero.has_value());
  assert(nonZero.has_value());
  if (*nonZero == 1) {
    // v_1 - v_4 == 1
    // 1 - v_1 + v_4 == 0
    EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 1), -1);
    EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 4), 1);

  } else {
    // -v_1 + v_4 == -1
    // -1 + v_1 - v_4 == 0
    EXPECT_EQ(*nonZero, -1);
    EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 1), 1);
    EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 4), -1);
  }

  std::optional<BitSet<>> optDeps = lblock.optimize();
  EXPECT_TRUE(optDeps.has_value());
  // orig order (inner <-> outer): n, m
  IntMatrix optPhi2(2, 2);
  // phi2 loop order is
  optPhi2.diag() = 1;
  // the scheduler swaps the order, making `n` outermost,
  // and `m` as innermost
  // orig order (inner <-> outer): k, n, m
  // IntMatrix optPhi3{"[0 0 1; 1 0 0; 0 1 0]"_mat};
  IntMatrix optPhi3{"[1 0 0; 0 0 1; 0 1 0]"_mat};
  // phi3 loop order is [k, m, n]
  // so the schedule below places `k` as the outermost loop,
  // followed by `m`, and `n` as innermost. `n` is the reduction loop.
  // optPhi3(end, _) = std::numeric_limits<int64_t>::min();
  // assert(!optFail);
  for (auto mem : lblock.getMemoryAccesses()) {
    for (size_t nodeIndex : mem->nodeIndex) {
      Schedule &s = lblock.getNode(nodeIndex).getSchedule();
      if (mem->getNumLoops() == 2) {
        EXPECT_EQ(s.getPhi(), optPhi2);
      } else {
        assert(mem->getNumLoops() == 3);
        EXPECT_EQ(s.getPhi(), optPhi3);
      }
      //       //       llvm::errs() << "\n";
    }
  }
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(MeanStDevTest0, BasicAssertions) {
  // iOuter variant:
  // for (i = 0; i < I; ++i){
  //   x(i) = 0; // [0]
  //   for (j = 0; j < J; ++j)
  //     x(i) += A(j,i) // [1,0:2]
  //   x(i) /= J;
  //   s(i) = 0;
  //   for (j = 0; j < J; ++j){
  //     d = (A(j,i) - x(i));
  //     s(i) += d*d;
  //   }
  //   s(i) = sqrt(s(i) / (J-1));
  // }

  // jOuter variant:
  //
  // for (i = 0; i < I; ++i){
  //    x(i) = 0;
  //    s(i) = 0;
  // }
  // for (j = 0; j < J; ++j){
  //   for (i = 0; i < I; ++i){
  //      x(i) += A(j,i)
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
  TestLoopFunction tlf;
  IntMatrix TwoLoopsMat{"[-1 1 0 0 -1; "
                        "0 0 0 0 1; "
                        "-1 0 1 -1 0; "
                        "0 0 0 1 0]"_mat};
  tlf.addLoop(std::move(TwoLoopsMat), 2);
  IntMatrix OneLoopMat{"[-1 1 -1; "
                       "0 0 1]"_mat};
  tlf.addLoop(std::move(OneLoopMat), 1);

  IntMatrix TwoLoopsMatJI{"[-1 0 1 0 -1; "
                          "0 0 0 0 1; "
                          "-1 1 0 -1 0; "
                          "0 0 0 1 0]"_mat};
  tlf.addLoop(std::move(TwoLoopsMatJI), 2);
  AffineLoopNest<true> &loopJI = tlf.alns[0];
  AffineLoopNest<true> &loopI = tlf.alns[1];
  AffineLoopNest<true> &loopIJ = tlf.alns[2];

  llvm::IRBuilder<> &builder = tlf.builder;

  // create arrays
  llvm::Type *Float64 = builder.getDoubleTy();
  llvm::Value *ptrX = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrS = tlf.createArray();
  auto scevX = tlf.getSCEVUnknown(ptrX);
  auto scevA = tlf.getSCEVUnknown(ptrA);
  auto scevS = tlf.getSCEVUnknown(ptrS);

  // llvm::ConstantInt *Iv = builder.getInt64(200);
  const llvm::SCEV *I = loopJI.S[0];
  const llvm::SCEV *J = loopJI.S[1];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(I)->getValue();
  llvm::Value *Jv = llvm::dyn_cast<llvm::SCEVUnknown>(J)->getValue();
  auto Jfp = builder.CreateUIToFP(Jv, Float64);
  auto zero = builder.getInt64(0);
  auto one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);

  llvm::Value *Aoffset = builder.CreateAdd(iv, builder.CreateMul(jv, Iv));
  auto Aload_m = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{Aoffset}),
    llvm::MaybeAlign(8));
  auto Aload_s = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{Aoffset}),
    llvm::MaybeAlign(8));

  auto Xload_0 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Xload_1 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Xload_2 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));

  auto zeroFP = llvm::ConstantFP::getZero(Float64);
  auto Xstore_0 = builder.CreateAlignedStore(
    zeroFP,
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Xstore_1 = builder.CreateAlignedStore(
    builder.CreateFAdd(Xload_0, Aload_m),
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Xstore_2 = builder.CreateAlignedStore(
    builder.CreateFDiv(Xload_1, Jfp),
    builder.CreateGEP(Float64, ptrX, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));

  auto Sload_0 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrS, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Sload_1 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrS, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto Sstore_0 = builder.CreateAlignedStore(
    zeroFP,
    builder.CreateGEP(Float64, ptrS, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  auto diff = builder.CreateFSub(Aload_s, Xload_2);
  // llvm::Intrinsic::fmuladd
  auto Sstore_1 = builder.CreateAlignedStore(
    builder.CreateFAdd(Sload_0, builder.CreateFMul(diff, diff)),
    builder.CreateGEP(Float64, ptrS, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));
  llvm::Function *sqrt =
    llvm::Intrinsic::getDeclaration(&tlf.mod, llvm::Intrinsic::sqrt, Float64);
  llvm::FunctionType *sqrtTyp =
    llvm::Intrinsic::getType(tlf.ctx, llvm::Intrinsic::sqrt, {Float64});

  auto Sstore_2 = builder.CreateAlignedStore(
    builder.CreateCall(sqrtTyp, sqrt, {builder.CreateFDiv(Sload_1, Jfp)}),
    builder.CreateGEP(Float64, ptrS, llvm::SmallVector<llvm::Value *, 1>{iv}),
    llvm::MaybeAlign(8));

  // Now, create corresponding schedules
  // IntMatrix ILoop{IJLoop(_(0,2),_(0,3))};
  // LoopBlock jOuterLoopNest;
  // Array IDs are:
  // A: 0
  // x: 1
  // s: 2
  llvm::Type *Int64 = builder.getInt64Ty();
  llvm::ScalarEvolution &SE{tlf.SE};
  ArrayReference AIndIOuter{scevA, loopJI, 2};
  {
    MutPtrMatrix<int64_t> IndMat = AIndIOuter.indexMatrix();
    //     l  d
    IndMat(1, 1) = 1; // i
    IndMat(0, 0) = 1; // j
    AIndIOuter.sizes[0] = I;
    AIndIOuter.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  ArrayReference AIndJOuter{scevA, loopIJ, 2};
  {
    MutPtrMatrix<int64_t> IndMat = AIndJOuter.indexMatrix();
    //     l  d
    IndMat(0, 1) = 1; // i
    IndMat(1, 0) = 1; // j
    AIndJOuter.sizes[0] = I;
    AIndJOuter.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  ArrayReference xInd1{scevX, loopI, 1};
  {
    MutPtrMatrix<int64_t> IndMat = xInd1.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // i
    xInd1.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  ArrayReference xInd2IOuter{scevX, loopJI, 1};
  {
    MutPtrMatrix<int64_t> IndMat = xInd2IOuter.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i
    xInd2IOuter.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  ArrayReference xInd2JOuter{scevX, loopIJ, 1};
  {
    MutPtrMatrix<int64_t> IndMat = xInd2JOuter.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // i
    xInd2JOuter.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  ArrayReference sInd1{scevS, loopI, 1};
  {
    MutPtrMatrix<int64_t> IndMat = sInd1.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // i
    sInd1.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  ArrayReference sInd2IOuter{scevS, loopJI, 1};
  {
    MutPtrMatrix<int64_t> IndMat = sInd2IOuter.indexMatrix();
    //     l  d
    IndMat(1, 0) = 1; // i
    sInd2IOuter.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  ArrayReference sInd2JOuter{scevS, loopIJ, 1};
  {
    MutPtrMatrix<int64_t> IndMat = sInd2JOuter.indexMatrix();
    //     l  d
    IndMat(0, 0) = 1; // i
    sInd2JOuter.sizes[0] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }

  llvm::SmallVector<unsigned, 8> sch0_0(1 + 1);
  llvm::SmallVector<unsigned, 8> sch0_1_0(2 + 1);
  sch0_1_0[2] = 1;
  llvm::SmallVector<unsigned, 8> sch0_1_1(2 + 1);
  sch0_1_1[1] = 1;
  sch0_1_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch0_1_2(2 + 1);
  sch0_1_2[1] = 1;
  sch0_1_2[2] = 2;
  llvm::SmallVector<unsigned, 8> sch0_2(1 + 1);
  sch0_2[1] = 2;
  llvm::SmallVector<unsigned, 8> sch0_3(1 + 1);
  sch0_3[1] = 3;
  llvm::SmallVector<unsigned, 8> sch0_4(1 + 1);
  sch0_4[1] = 4;
  llvm::SmallVector<unsigned, 8> sch0_5_0(2 + 1);
  sch0_5_0[1] = 5;
  llvm::SmallVector<unsigned, 8> sch0_5_1(2 + 1);
  sch0_5_1[1] = 5;
  sch0_5_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch0_5_2(2 + 1);
  sch0_5_2[1] = 5;
  sch0_5_2[2] = 2;
  llvm::SmallVector<unsigned, 8> sch0_5_3(2 + 1);
  sch0_5_3[1] = 5;
  sch0_5_3[2] = 3;
  llvm::SmallVector<unsigned, 8> sch0_6(1 + 1);
  sch0_6[1] = 6;
  llvm::SmallVector<unsigned, 8> sch0_7(1 + 1);
  sch0_7[1] = 7;
  LinearProgramLoopBlock iOuterLoopNest;
  llvm::SmallVector<MemoryAccess, 0> iOuterMem;
  iOuterMem.emplace_back(createMemAccess(xInd1, Xstore_0, sch0_0)); // 0

  iOuterMem.emplace_back(createMemAccess(AIndIOuter, Aload_m, sch0_1_0));  // 1
  iOuterMem.emplace_back(createMemAccess(xInd2IOuter, Xload_0, sch0_1_1)); // 2

  iOuterMem.emplace_back(createMemAccess(xInd2IOuter, Xstore_1, sch0_1_2)); // 3

  iOuterMem.emplace_back(createMemAccess(xInd1, Xload_1, sch0_2));  // 4
  iOuterMem.emplace_back(createMemAccess(xInd1, Xstore_2, sch0_3)); // 5

  iOuterMem.emplace_back(createMemAccess(sInd1, Sstore_0, sch0_4));        // 6
  iOuterMem.emplace_back(createMemAccess(AIndIOuter, Aload_s, sch0_5_0));  // 7
  iOuterMem.emplace_back(createMemAccess(xInd2IOuter, Xload_2, sch0_5_1)); // 8
  iOuterMem.emplace_back(createMemAccess(sInd2IOuter, Sload_0, sch0_5_2)); // 9
  iOuterMem.emplace_back(
    createMemAccess(sInd2IOuter, Sstore_1, sch0_5_3)); // 10

  iOuterMem.emplace_back(createMemAccess(sInd1, Sload_1, sch0_6));  // 11
  iOuterMem.emplace_back(createMemAccess(sInd1, Sstore_2, sch0_7)); // 12
  for (auto &&mem : iOuterMem) iOuterLoopNest.addMemory(&mem);

  llvm::SmallVector<Dependence, 0> d;
  d.reserve(4);
  Dependence::check(d, *iOuterLoopNest.getMemoryAccess(3),
                    *iOuterLoopNest.getMemoryAccess(5));
  EXPECT_TRUE(d.back().forward);
  Dependence::check(d, *iOuterLoopNest.getMemoryAccess(5),
                    *iOuterLoopNest.getMemoryAccess(3));
  EXPECT_FALSE(d.back().forward);
  Dependence::check(d, *iOuterLoopNest.getMemoryAccess(4),
                    *iOuterLoopNest.getMemoryAccess(5));
  EXPECT_TRUE(d.back().forward);
  Dependence::check(d, *iOuterLoopNest.getMemoryAccess(5),
                    *iOuterLoopNest.getMemoryAccess(4));
  EXPECT_FALSE(d.back().forward);

  std::optional<BitSet<>> optDeps = iOuterLoopNest.optimize();
  EXPECT_TRUE(optDeps.has_value());
  llvm::DenseMap<MemoryAccess *, size_t> memAccessIds;
  llvm::MutableArrayRef<MemoryAccess *> mem =
    iOuterLoopNest.getMemoryAccesses();
  for (size_t i = 0; i < mem.size(); ++i) memAccessIds[mem[i]] = i;
  for (auto &e : iOuterLoopNest.getEdges()) {
    llvm::errs() << "\nEdge for array " << e.out->basePointer
                 << ", in ID: " << memAccessIds[e.in]
                 << "; out ID: " << memAccessIds[e.out] << "\n";
  }
  auto nodes = iOuterLoopNest.getNodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto &v = nodes[i];
    llvm::errs() << "v_" << i << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  // Graphs::print(iOuterLoopNest.fullGraph());
  for (auto mem : mem) {
    llvm::errs() << "mem->nodeIndex =" << mem->nodeIndex << ";";
    llvm::errs() << "mem =" << mem << "\n";
    for (size_t nodeIndex : mem->nodeIndex) {
      Schedule &s = nodes[nodeIndex].getSchedule();
      EXPECT_EQ(&s, &iOuterLoopNest.getNode(nodeIndex).getSchedule());
      llvm::errs() << "s.getPhi() =" << s.getPhi() << "\n";
      llvm::errs() << "s.getFusionOmega() =" << s.getFusionOmega() << "\n";
      llvm::errs() << "s.getOffsetOmega() =" << s.getOffsetOmega() << "\n";
    }
  }

  LinearProgramLoopBlock jOuterLoopNest;
  llvm::SmallVector<MemoryAccess, 0> jOuterMem;
  jOuterMem.emplace_back(createMemAccess(xInd1, Xstore_0, sch0_0)); // 0
  llvm::SmallVector<unsigned, 8> sch0_1(1 + 1);
  sch0_1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(sInd1, Sstore_0, sch0_1)); // 6
  llvm::SmallVector<unsigned, 8> sch1_0_0(2 + 1);
  sch1_0_0[0] = 1;
  llvm::SmallVector<unsigned, 8> sch1_0_1(2 + 1);
  sch1_0_1[0] = 1;
  sch1_0_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch1_0_2(2 + 1);
  sch1_0_2[0] = 1;
  sch1_0_2[2] = 2;
  jOuterMem.emplace_back(createMemAccess(AIndJOuter, Aload_m, sch1_0_0));   // 1
  jOuterMem.emplace_back(createMemAccess(xInd2JOuter, Xload_0, sch1_0_1));  // 2
  jOuterMem.emplace_back(createMemAccess(xInd2JOuter, Xstore_1, sch1_0_2)); // 3

  llvm::SmallVector<unsigned, 8> sch2_0(1 + 1);
  sch2_0[0] = 2;
  llvm::SmallVector<unsigned, 8> sch2_1(1 + 1);
  sch2_1[0] = 2;
  sch2_1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(xInd1, Xload_1, sch2_0));  // 4
  jOuterMem.emplace_back(createMemAccess(xInd1, Xstore_2, sch2_1)); // 5

  llvm::SmallVector<unsigned, 8> sch3_0_0(2 + 1);
  sch3_0_0[0] = 3;
  llvm::SmallVector<unsigned, 8> sch3_0_1(2 + 1);
  sch3_0_1[0] = 3;
  sch3_0_1[2] = 1;
  llvm::SmallVector<unsigned, 8> sch3_0_2(2 + 1);
  sch3_0_2[0] = 3;
  sch3_0_2[2] = 2;
  llvm::SmallVector<unsigned, 8> sch3_0_3(2 + 1);
  sch3_0_3[0] = 3;
  sch3_0_3[2] = 3;

  jOuterMem.emplace_back(createMemAccess(AIndJOuter, Aload_s, sch3_0_0));  // 7
  jOuterMem.emplace_back(createMemAccess(xInd2JOuter, Xload_2, sch3_0_1)); // 8
  jOuterMem.emplace_back(createMemAccess(sInd2JOuter, Sload_0, sch3_0_2)); // 9
  jOuterMem.emplace_back(
    createMemAccess(sInd2JOuter, Sstore_1, sch3_0_3)); // 10

  llvm::SmallVector<unsigned, 8> sch4_0(1 + 1);
  sch4_0[0] = 4;
  llvm::SmallVector<unsigned, 8> sch4_1(1 + 1);
  sch4_1[0] = 4;
  sch4_1[1] = 1;
  jOuterMem.emplace_back(createMemAccess(sInd1, Sload_1, sch4_0));  // 11
  jOuterMem.emplace_back(createMemAccess(sInd1, Sstore_2, sch4_1)); // 12

  for (auto &&mem : jOuterMem) jOuterLoopNest.addMemory(&mem);

  EXPECT_TRUE(jOuterLoopNest.optimize().has_value());
  for (auto &edge : jOuterLoopNest.getEdges())
    llvm::errs() << "\nedge = " << edge << "\n";

  for (size_t i = 0; i < jOuterLoopNest.numNodes(); ++i) {
    const auto &v = jOuterLoopNest.getNode(i);
    llvm::errs() << "v_" << i << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  IntMatrix optS(2);
  // we want diag, as that represents swapping loops
  optS.diag() = 1;
  IntMatrix optSinnerUndef = optS;
  optSinnerUndef(1, _) = std::numeric_limits<int64_t>::min();
  for (auto mem : jOuterLoopNest.getMemoryAccesses()) {
    for (size_t nodeIndex : mem->nodeIndex) {
      Schedule &s = jOuterLoopNest.getNode(nodeIndex).getSchedule();
      if (s.getNumLoops() == 1) EXPECT_EQ(s.getPhi()(0, 0), 1);
      else if (s.getFusionOmega()[1] < 3) EXPECT_EQ(s.getPhi(), optSinnerUndef);
      else EXPECT_EQ(s.getPhi(), optS);
    }
  }
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(DoubleDependenceTest, BasicAssertions) {

  TestLoopFunction tlf;
  auto &builder = tlf.builder;
  IntMatrix Aloop{"[-2 1 0 0 -1; "
                  "0 0 0 0 1; "
                  "-2 0 1 -1 0; "
                  "0 0 0 1 0]"_mat};
  tlf.addLoop(std::move(Aloop), 2);
  AffineLoopNest<true> &loop = tlf.alns.front();

  // create arrays
  llvm::Type *Float64 = builder.getDoubleTy();
  llvm::Value *ptrA = tlf.createArray();
  auto scevA = tlf.getSCEVUnknown(ptrA);

  const llvm::SCEV *I = loop.S[0];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(I)->getValue();
  // llvm::Value* J = loop.S[1];
  auto zero = builder.getInt64(0);
  auto one = builder.getInt64(1);
  llvm::Value *iv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);

  llvm::Value *A_ip1_jp1 =
    builder.CreateAdd(builder.CreateAdd(iv, one),
                      builder.CreateMul(builder.CreateAdd(jv, one), Iv));
  llvm::Value *A_ip1_j =
    builder.CreateAdd(iv, builder.CreateMul(builder.CreateAdd(jv, one), Iv));
  llvm::Value *A_i_jp1 =
    builder.CreateAdd(builder.CreateAdd(iv, one), builder.CreateMul(jv, Iv));

  auto Aload_ip1_j = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{A_ip1_j}),
    llvm::MaybeAlign(8));
  auto Aload_i_jp1 = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{A_i_jp1}),
    llvm::MaybeAlign(8));
  auto Astore = builder.CreateAlignedStore(
    builder.CreateFAdd(Aload_ip1_j, Aload_i_jp1),
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{A_ip1_jp1}),
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

  // we have three array refs
  // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = builder.getInt64Ty();
  ArrayReference Asrc(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
    //     l  d
    IndMat(1, 1) = 1; // i
    IndMat(0, 0) = 1; // j
    MutPtrMatrix<int64_t> OffMat = Asrc.offsetMatrix();
    OffMat(0, 0) = 1;
    OffMat(1, 0) = 1;
    Asrc.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    Asrc.sizes[0] = I;
  }

  // A[i+1, j]
  ArrayReference Atgt0(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt0.indexMatrix();
    //     l  d
    IndMat(1, 1) = 1; // i
    IndMat(0, 0) = 1; // j
                      //                   d  s
    Atgt0.offsetMatrix()(1, 0) = 1;
    Atgt0.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    Atgt0.sizes[0] = I;
  }

  // A[i, j+1]
  ArrayReference Atgt1(scevA, loop, 2);
  {
    MutPtrMatrix<int64_t> IndMat = Atgt1.indexMatrix();
    //     l  d
    IndMat(1, 1) = 1; // i
    IndMat(0, 0) = 1; // j
    Atgt1.offsetMatrix()(0, 0) = 1;
    Atgt1.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    Atgt1.sizes[0] = I;
  }

  //
  llvm::SmallVector<unsigned, 8> schLoad0(2 + 1);
  llvm::SmallVector<unsigned, 8> schStore(2 + 1);
  schStore[2] = 2;
  MemoryAccess msrc{createMemAccess(Asrc, Astore, schStore)};
  MemoryAccess mtgt0{createMemAccess(Atgt0, Aload_ip1_j, schLoad0)};
  DependencePolyhedra dep0(msrc, mtgt0);
  EXPECT_FALSE(dep0.isEmpty());
  dep0.pruneBounds();
  llvm::errs() << "Dep0 = \n" << dep0 << "\n";

  EXPECT_EQ(dep0.getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep0.getNumEqualityConstraints(), 2);
  assert(dep0.getNumInequalityConstraints() == 4);
  assert(dep0.getNumEqualityConstraints() == 2);

  llvm::SmallVector<unsigned, 8> schLoad1(2 + 1);
  schLoad1[2] = 1;
  MemoryAccess mtgt1{createMemAccess(Atgt1, Aload_i_jp1, schLoad1)};
  DependencePolyhedra dep1(msrc, mtgt1);
  EXPECT_FALSE(dep1.isEmpty());
  dep1.pruneBounds();
  llvm::errs() << "Dep1 = \n" << dep1 << "\n";
  EXPECT_EQ(dep1.getNumInequalityConstraints(), 4);
  EXPECT_EQ(dep1.getNumEqualityConstraints(), 2);
  assert(dep1.getNumInequalityConstraints() == 4);
  assert(dep1.getNumEqualityConstraints() == 2);
  llvm::SmallVector<Dependence, 1> dc;
  EXPECT_EQ(dc.size(), 0);
  EXPECT_EQ(Dependence::check(dc, msrc, mtgt0), 1);
  EXPECT_EQ(dc.size(), 1);
  Dependence &d(dc.front());
  EXPECT_TRUE(d.forward);
  llvm::errs() << d << "\n";
  assert(d.forward);
  assert(!allZero(d.dependenceSatisfaction.tableau(
    d.dependenceSatisfaction.tableau.numRow() - 1, _)));

  LinearProgramLoopBlock loopBlock;
  MemoryAccess mSchLoad0(createMemAccess(Atgt0, Aload_ip1_j, schLoad0));
  loopBlock.addMemory(&mSchLoad0);
  MemoryAccess mSchLoad1(createMemAccess(Atgt1, Aload_i_jp1, schLoad1));
  loopBlock.addMemory(&mSchLoad1);
  MemoryAccess mSchStore(createMemAccess(Asrc, Astore, schStore));
  loopBlock.addMemory(&mSchStore);

  EXPECT_TRUE(loopBlock.optimize().has_value());
  EXPECT_EQ(loopBlock.numEdges(), 2);
  llvm::DenseMap<MemoryAccess *, size_t> memAccessIds;
  for (size_t i = 0; i < loopBlock.numMemoryAccesses(); ++i)
    memAccessIds[loopBlock.getMemoryAccess(i)] = i;
  for (auto &e : loopBlock.getEdges()) {
    llvm::errs() << "\nEdge for array " << e.out->basePointer
                 << ", in ID: " << memAccessIds[e.in]
                 << "; out ID: " << memAccessIds[e.out] << "\n";
  }
  for (size_t i = 0; i < loopBlock.numNodes(); ++i) {
    const auto &v = loopBlock.getNode(i);
    llvm::errs() << "v_" << i << ":\nmem = ";
    for (auto m : v.getMemory()) llvm::errs() << m << ", ";
    llvm::errs() << v;
  }
  IntMatrix optPhi(2, 2);
  optPhi(0, _) = 1;
  optPhi(1, _) = std::numeric_limits<int64_t>::min();
  // Graphs::print(iOuterLoopNest.fullGraph());
  for (auto &mem : loopBlock.getMemoryAccesses()) {
    for (size_t nodeIndex : mem->nodeIndex) {
      Schedule &s = loopBlock.getNode(nodeIndex).getSchedule();
      EXPECT_EQ(s.getPhi(), optPhi);
    }
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
  auto &builder = tlf.builder;
  IntMatrix Aloop{"[-1 0 1 0 0 0 0 0 -1; "
                  "0 0 0 0 0 0 0 0 1; "
                  "-1 1 0 0 0 0 0 -1 0; "
                  "0 0 0 0 0 0 0 1 0; "
                  "-1 0 0 0 1 0 -1 0 0; "
                  "0 0 0 0 0 0 1 0 0; "
                  "-1 0 0 1 0 -1 0 0 0; "
                  "0 0 0 0 0 1 0 0 0]"_mat};
  tlf.addLoop(std::move(Aloop), 4);
  AffineLoopNest<true> &loop = tlf.alns.front();

  // create arrays
  llvm::Type *Float64 = builder.getDoubleTy();
  llvm::Value *ptrB = tlf.createArray();
  llvm::Value *ptrA = tlf.createArray();
  llvm::Value *ptrC = tlf.createArray();
  auto scevB = tlf.getSCEVUnknown(ptrB);
  auto scevA = tlf.getSCEVUnknown(ptrA);
  auto scevC = tlf.getSCEVUnknown(ptrC);

  // llvm::ConstantInt *Jv = builder.getInt64(100);
  const llvm::SCEV *I = loop.S[3];
  const llvm::SCEV *M = loop.S[1];
  llvm::Value *Iv = llvm::dyn_cast<llvm::SCEVUnknown>(I)->getValue();
  llvm::Value *Mv = llvm::dyn_cast<llvm::SCEVUnknown>(M)->getValue();
  // llvm::ConstantInt *Nv = builder.getInt64(400);
  auto zero = builder.getInt64(0);
  auto one = builder.getInt64(1);
  llvm::Value *mv = builder.CreateAdd(zero, one);
  llvm::Value *nv = builder.CreateAdd(zero, one);
  llvm::Value *jv = builder.CreateAdd(zero, one);
  llvm::Value *iv = builder.CreateAdd(zero, one);

  llvm::Value *Aoffset = builder.CreateAdd(mv, builder.CreateMul(nv, Mv));
  llvm::Value *Boffset = builder.CreateAdd(iv, builder.CreateMul(jv, Iv));
  llvm::Value *Coffset = builder.CreateAdd(
    builder.CreateAdd(mv, iv),
    builder.CreateMul(builder.CreateAdd(nv, jv),
                      builder.CreateSub(builder.CreateAdd(Mv, Iv), one)));
  auto Aload = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrA,
                      llvm::SmallVector<llvm::Value *, 1>{Aoffset}),
    llvm::MaybeAlign(8));
  auto Bload = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrB,
                      llvm::SmallVector<llvm::Value *, 1>{Boffset}),
    llvm::MaybeAlign(8));
  auto Cload = builder.CreateAlignedLoad(
    Float64,
    builder.CreateGEP(Float64, ptrC,
                      llvm::SmallVector<llvm::Value *, 1>{Coffset}),
    llvm::MaybeAlign(8));
  auto Cstore = builder.CreateAlignedStore(
    builder.CreateFAdd(Cload, builder.CreateFMul(Aload, Bload)),
    builder.CreateGEP(Float64, ptrC,
                      llvm::SmallVector<llvm::Value *, 1>{Coffset}),
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

  llvm::ScalarEvolution &SE{tlf.SE};
  llvm::Type *Int64 = builder.getInt64Ty();
  // B[j, i]
  ArrayReference BmnInd{scevB, loop, 2};
  {
    MutPtrMatrix<int64_t> IndMat = BmnInd.indexMatrix();
    //     l  d
    IndMat(0, 1) = 1; // i
    IndMat(1, 0) = 1; // j
    BmnInd.sizes[0] = I;
    BmnInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
  }
  // A[n, m]
  ArrayReference AmnInd{scevA, loop, 2};
  {
    MutPtrMatrix<int64_t> IndMat = AmnInd.indexMatrix();
    //     l  d
    IndMat(2, 1) = 1; // m
    IndMat(3, 0) = 1; // n
    AmnInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    AmnInd.sizes[0] = I;
  }
  // C[m+i, n+j]
  ArrayReference CmijnInd{scevC, loop, 2};
  {
    MutPtrMatrix<int64_t> IndMat = CmijnInd.indexMatrix();
    //     l  d
    IndMat(2, 1) = 1; // m
    IndMat(0, 1) = 1; // i
    IndMat(3, 0) = 1; // n
    IndMat(1, 0) = 1; // j
    CmijnInd.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    CmijnInd.sizes[0] =
      SE.getAddExpr(SE.getAddExpr(M, I), SE.getMinusOne(Int64));
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
  LinearProgramLoopBlock loopBlock;
  llvm::SmallVector<unsigned, 8> sch_0(4 + 1);
  llvm::SmallVector<unsigned, 8> sch_1 = sch_0;
  //         C[m+i,j+n] = C[m+i,j+n] + A[m,n] * -> B[i,j] <-;
  MemoryAccess msch_0(createMemAccess(BmnInd, Bload, sch_0));
  loopBlock.addMemory(&msch_0);
  sch_1[4] = 1;
  llvm::SmallVector<unsigned, 8> sch_2 = sch_1;
  //         C[m+i,j+n] = C[m+i,j+n] + -> A[m,n] <- * B[i,j];
  MemoryAccess msch_1(createMemAccess(AmnInd, Aload, sch_1));
  loopBlock.addMemory(&msch_1);
  sch_2[4] = 2;
  llvm::SmallVector<unsigned, 8> sch_3 = sch_2;
  //         C[m+i,j+n] = -> C[m+i,j+n] <- + A[m,n] * B[i,j];
  MemoryAccess msch_2(createMemAccess(CmijnInd, Cload, sch_2));
  loopBlock.addMemory(&msch_2);
  sch_3[4] = 3;
  //         -> C[m+i,j+n] <- = C[m+i,j+n] + A[m,n] * B[i,j];
  MemoryAccess msch_3(createMemAccess(CmijnInd, Cstore, sch_3));
  loopBlock.addMemory(&msch_3);

  std::optional<BitSet<>> optRes = loopBlock.optimize();
  EXPECT_TRUE(optRes.has_value());
  for (auto &mem : loopBlock.getMemoryAccesses()) {
    llvm::errs() << "mem->nodeIndex: " << mem->nodeIndex << "; ";
    llvm::errs() << "mem: " << mem << "\n";
    for (size_t nodeIndex : mem->nodeIndex) {
      Schedule &s = loopBlock.getNode(nodeIndex).getSchedule();
      llvm::errs() << "s.getPhi(): " << s.getPhi() << "\n";
      llvm::errs() << "s.getFusionOmega(): " << s.getFusionOmega() << "\n";
      llvm::errs() << "s.getOffsetOmega(): " << s.getOffsetOmega() << "\n";
      // EXPECT_EQ(s.getPhi(), optPhi);
    }
  }
}
