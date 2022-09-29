#include "../include/ArrayReference.hpp"
#include "../include/DependencyPolyhedra.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "Loops.hpp"
#include "Macro.hpp"
#include "MatrixStringParse.hpp"
#include "MemoryAccess.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Operator.h>
#include <llvm/Support/Casting.h>

TEST(TriangularExampleTest, BasicAssertions) {

    llvm::DataLayout dl("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
                        "n8:16:32:64-S128");
    llvm::TargetTransformInfo TTI{dl};
    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);

    // create arrays
    llvm::Type *Float64 = builder.getDoubleTy();
    llvm::Value *ptrB = builder.CreateIntToPtr(builder.getInt64(8000), Float64);
    llvm::Value *ptrA =
        builder.CreateIntToPtr(builder.getInt64(16000), Float64);
    llvm::Value *ptrU =
        builder.CreateIntToPtr(builder.getInt64(24000), Float64);

    llvm::ConstantInt *Mv = builder.getInt64(200);
    llvm::ConstantInt *Nv = builder.getInt64(100);
    auto zero = builder.getInt64(0);
    auto one = builder.getInt64(1);
    llvm::Value *mv = builder.CreateAdd(zero, one);
    llvm::Value *nv = builder.CreateAdd(zero, one);
    llvm::Value *kv = builder.CreateAdd(nv, one);

    llvm::Value *Boffset = builder.CreateAdd(mv, builder.CreateMul(nv, Mv));
    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n);
    //   }    auto Bload = builder.CreateLoad(
    auto Bload = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrB,
                          llvm::SmallVector<llvm::Value *, 1>{Boffset}),
        llvm::MaybeAlign(8));
    auto Astore0 = builder.CreateAlignedStore(
        Bload,
        builder.CreateGEP(Float64, ptrA,
                          llvm::SmallVector<llvm::Value *, 1>{Boffset}),
        llvm::MaybeAlign(8));

    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = A(m,n) / U(n,n);
    llvm::Value *Uoffsetnn = builder.CreateAdd(nv, builder.CreateMul(nv, Nv));
    auto Uloadnn = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrU,
                          llvm::SmallVector<llvm::Value *, 1>{Uoffsetnn}),
        llvm::MaybeAlign(8));
    auto Ageped0 = builder.CreateGEP(
        Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{Boffset});
    auto Aload0 =
        builder.CreateAlignedLoad(Float64, Ageped0, llvm::MaybeAlign(8));
    auto AstoreFDiv = builder.CreateAlignedStore(
        builder.CreateFDiv(Aload0, Uloadnn), Ageped0, llvm::MaybeAlign(8));

    // for (m = 0; m < M; ++m){
    //     for (k = n+1; k < N; ++k){
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k);
    //     }
    llvm::Value *Uoffsetnk = builder.CreateAdd(nv, builder.CreateMul(kv, Nv));
    auto Uloadnk = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrU,
                          llvm::SmallVector<llvm::Value *, 1>{Uoffsetnk}),
        llvm::MaybeAlign(8));
    llvm::Value *Aoffsetmk = builder.CreateAdd(mv, builder.CreateMul(kv, Mv));
    auto Ageped1mk = builder.CreateGEP(
        Float64, ptrA, llvm::SmallVector<llvm::Value *, 1>{Aoffsetmk});
    auto Aload1mk =
        builder.CreateAlignedLoad(Float64, Ageped1mk, llvm::MaybeAlign(8));
    auto Aload1mn = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrA,
                          llvm::SmallVector<llvm::Value *, 1>{Boffset}),
        llvm::MaybeAlign(8));
    auto Astore2mk = builder.CreateAlignedStore(
        builder.CreateFSub(Aload1mk, builder.CreateFMul(Aload1mn, Uloadnk)),
        Ageped0, llvm::MaybeAlign(8));

    SHOWLN(Aload1mk);
    for (auto &use : Aload1mk->uses())
        SHOWLN(use.getUser());
    SHOWLN(Aload1mn);
    for (auto &use : Aload1mn->uses())
        SHOWLN(use.getUser());
    SHOWLN(Uloadnk);
    for (auto &use : Uloadnk->uses())
        SHOWLN(use.getUser());
    SHOWLN(Astore2mk);
    // badly written triangular solve:
    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n);
    //   }
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = A(m,n) / U(n,n);
    //     for (k = n+1; k < N; ++k){
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k);
    //     }
    //   }
    // }

    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    llvm::SmallVector<Polynomial::Monomial> symbols{M, N};
    // Construct the loops
    IntMatrix AMN{stringToIntMatrix("[-1 1 0 -1 0; "
                                    "0 0 0 1 0; "
                                    "-1 0 1 0 -1; "
                                    "0 0 0 0 1]")};
    IntMatrix AMNK{stringToIntMatrix("[-1 1 0 -1 0 0; "
                                     "0 0 0 1 0 0; "
                                     "-1 0 1 0 -1 0; "
                                     "0 0 0 0 1 0; "
                                     "-1 0 1 0 0 -1; "
                                     "-1 0 0 0 -1 1]")};

    auto loopMN = AffineLoopNest::construct(AMN, symbols);
    auto loopMNK = AffineLoopNest::construct(AMNK, symbols);

    // construct indices

    LoopBlock lblock;
    // B[m, n]
    ArrayReference BmnInd{0, loopMN, 2};
    {
        MutPtrMatrix<int64_t> IndMat = BmnInd.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        BmnInd.strides[0] = 1;
        BmnInd.strides[1] = M;
    }
    std::cout << "Bmn = " << BmnInd << std::endl;
    // A[m, n]
    ArrayReference Amn2Ind{1, loopMN, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Amn2Ind.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        Amn2Ind.strides[0] = 1;
        Amn2Ind.strides[1] = M;
    }
    std::cout << "Amn2 = " << Amn2Ind << std::endl;
    // A[m, n]
    ArrayReference Amn3Ind{1, loopMNK, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Amn3Ind.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        Amn3Ind.strides[0] = 1;
        Amn3Ind.strides[1] = M;
    }
    std::cout << "Amn3 = " << Amn3Ind << std::endl;
    // A[m, k]
    ArrayReference AmkInd{1, loopMNK, 2};
    {
        MutPtrMatrix<int64_t> IndMat = AmkInd.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(2, 1) = 1; // k
        AmkInd.strides[0] = 1;
        AmkInd.strides[1] = M;
    }
    std::cout << "Amk = " << AmkInd << std::endl;
    // U[n, k]
    ArrayReference UnkInd{2, loopMNK, 2};
    {
        MutPtrMatrix<int64_t> IndMat = UnkInd.indexMatrix();
        IndMat(1, 0) = 1; // n
        IndMat(2, 1) = 1; // k
        UnkInd.strides[0] = 1;
        UnkInd.strides[1] = N;
    }
    std::cout << "Unk = " << UnkInd << std::endl;
    // U[n, n]
    ArrayReference UnnInd{2, loopMN, 2};
    {
        MutPtrMatrix<int64_t> IndMat = UnnInd.indexMatrix();
        IndMat(1, 0) = 1; // n
        IndMat(1, 1) = 1; // k
        UnnInd.strides[0] = 1;
        UnnInd.strides[1] = N;
    }
    std::cout << "Unn = " << UnnInd << std::endl;

    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     // sch.Omega = [ 0, _, 0, _, {0-1} ]
    //     A(m,n) = B(m,n); // sch2_0_{0-1}
    //   }
    //   for (n = 0; n < N; ++n){
    //     // sch.Omega = [ 0, _, 1, _, {0-2} ]
    //     A(m,n) = A(m,n) / U(n,n); // sch2_2_{0-2}
    //     for (k = n+1; k < N; ++k){
    //       // sch.Omega = [ 0, _, 1, _, 3, _, {0-3} ]
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k); // sch3_{0-3}
    //     }
    //   }
    //   foo(arg...) // [ 0, _, 2 ]
    // }
    // NOTE: shared ptrs get set to NULL when `lblock.memory` reallocs...
    lblock.memory.reserve(9);
    Schedule sch2_0_0(2);
    Schedule sch2_0_1 = sch2_0_0;
    // A(m,n) = -> B(m,n) <-
    lblock.memory.emplace_back(BmnInd, Bload, sch2_0_0, true);
    // MemoryAccess &mSch2_0_0 = lblock.memory.back();
    sch2_0_1.getOmega()[4] = 1;
    Schedule sch2_1_0 = sch2_0_1;
    // -> A(m,n) <- = B(m,n)
    lblock.memory.emplace_back(Amn2Ind, Astore0, sch2_0_1, false);
    // std::cout << "Amn2Ind.loop->poset.delta.size() = "
    //           << Amn2Ind.loop->poset.delta.size() << std::endl;
    // std::cout << "lblock.memory.back().ref.loop->poset.delta.size() = "
    //           << lblock.memory.back().ref.loop->poset.delta.size() <<
    //           std::endl;
    MemoryAccess &mSch2_0_1 = lblock.memory.back();
    // std::cout << "lblock.memory.back().ref.loop = "
    //           << lblock.memory.back().ref.loop << std::endl;
    // std::cout << "lblock.memory.back().ref.loop.get() = "
    //           << lblock.memory.back().ref.loop.get() << std::endl;
    // std::cout << "msch2_0_1.ref.loop = " << msch2_0_1.ref.loop << std::endl;
    // std::cout << "msch2_0_1.ref.loop.get() = " << msch2_0_1.ref.loop.get()
    //           << std::endl;
    sch2_1_0.getOmega()[2] = 1;
    sch2_1_0.getOmega()[4] = 0;
    Schedule sch2_1_1 = sch2_1_0;
    // A(m,n) = -> A(m,n) <- / U(n,n); // sch2
    lblock.memory.emplace_back(Amn2Ind, Aload0, sch2_1_0, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "msch2_0_1.ref.loop = " << msch2_0_1.ref.loop << std::endl;
    // std::cout << "msch2_0_1.ref.loop.get() = " << msch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch2_1_0 = lblock.memory.back();
    sch2_1_1.getOmega()[4] = 1;
    Schedule sch2_1_2 = sch2_1_1;
    // A(m,n) = A(m,n) / -> U(n,n) <-;
    lblock.memory.emplace_back(UnnInd, Uloadnn, sch2_1_1, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    // MemoryAccess &mSch2_1_1 = lblock.memory.back();
    sch2_1_2.getOmega()[4] = 2;
    // -> A(m,n) <- = A(m,n) / U(n,n); // sch2
    lblock.memory.emplace_back(Amn2Ind, AstoreFDiv, sch2_1_2, false);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch2_1_2 = lblock.memory.back();

    Schedule sch3_0(3);
    sch3_0.getOmega()[2] = 1;
    sch3_0.getOmega()[4] = 3;
    Schedule sch3_1 = sch3_0;
    // A(m,k) = A(m,k) - A(m,n)* -> U(n,k) <-;
    lblock.memory.emplace_back(UnkInd, Uloadnk, sch3_0, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    // MemoryAccess &mSch3_2 = lblock.memory.back();
    sch3_1.getOmega()[6] = 1;
    Schedule sch3_2 = sch3_1;
    // A(m,k) = A(m,k) - -> A(m,n) <- *U(n,k);
    lblock.memory.emplace_back(Amn3Ind, Aload1mn, sch3_1, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_1 = lblock.memory.back();
    sch3_2.getOmega()[6] = 2;
    Schedule sch3_3 = sch3_2;
    // A(m,k) = -> A(m,k) <- - A(m,n)*U(n,k);
    lblock.memory.emplace_back(AmkInd, Aload1mk, sch3_2, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_0 = lblock.memory.back();
    sch3_3.getOmega()[6] = 3;
    // -> A(m,k) <- = A(m,k) - A(m,n)*U(n,k);
    lblock.memory.emplace_back(AmkInd, Astore2mk, sch3_3, false);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_3 = lblock.memory.back();

    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n); // sch2_0_{0-1}
    //   }
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = A(m,n) / U(n,n); // sch2_2_{0-2}
    //     for (k = n+1; k < N; ++k){
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k); // sch3_{0-3}
    //     }
    //   }
    // }

    // First, comparisons of store to `A(m,n) = B(m,n)` versus...
    llvm::SmallVector<Dependence, 0> d;
    d.reserve(15);
    // std::cout << "lblock.memory[1].ref.loop->poset.delta.size() = "
    //           << lblock.memory[1].ref.loop->poset.delta.size() << std::endl;
    // std::cout << "&mSch2_0_1 = " << &mSch2_0_1 << std::endl;
    // std::cout << "&(mSch2_0_1.ref) = " << &(mSch2_0_1.ref) << std::endl;
    // std::cout << "lblock.memory[1].ref.loop = " << lblock.memory[1].ref.loop
    //           << std::endl;
    // std::cout << "lblock.memory[1].ref.loop.get() = "
    //           << lblock.memory[1].ref.loop.get() << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    // // load in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_0), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    //
    //
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_2), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'

    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_1), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    //
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_0), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_3), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Second, comparisons of load in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch2_1_2), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_1), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_0), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_3), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Third, comparisons of store in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_1), 1);
    EXPECT_TRUE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_0), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_3), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Fourth, comparisons of load `A(m,n)` in
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // with...
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_0), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_3), 1);
    EXPECT_FALSE(d.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Fifth, comparisons of load `A(m,k)` in
    // sch3_               3        0         1     2
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // with...
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // printMatrix(std::cout << "mSch3_0.schedule.getPhi() =\n", PtrMatrix<const
    // int64_t>(mSch3_0.schedule.getPhi())) << std::endl; printMatrix(std::cout
    // << "mSch3_3.schedule.getPhi() =\n", PtrMatrix<const
    // int64_t>(mSch3_3.schedule.getPhi())) << std::endl; printVector(std::cout
    // << "mSch3_0.schedule.getOmega() = ", mSch3_0.schedule.getOmega()) <<
    // std::endl; printVector(std::cout << "mSch3_3.schedule.getOmega() = ",
    // mSch3_3.schedule.getOmega()) << std::endl;
    EXPECT_EQ(Dependence::check(d, mSch3_0, mSch3_3), 2);
    EXPECT_TRUE(d[d.size() - 2].forward);
    EXPECT_FALSE(d[d.size() - 1].forward);
    std::cout << "dep#" << d.size() << std::endl;
    auto &forward = d[d.size() - 2];
    auto &reverse = d[d.size() - 1];
    std::cout << "\nforward dependence:" << forward;
    std::cout << "\nreverse dependence:" << reverse;
    assert(forward.forward);
    assert(!reverse.forward);
    EXPECT_EQ(d.size(), 16);
    // EXPECT_EQ(forward.dependenceSatisfaction.getNumConstraints(), 3);
    // EXPECT_EQ(reverse.dependenceSatisfaction.getNumConstraints(), 2);
    // EXPECT_EQ(forward.dependenceSatisfaction.getNumInequalityConstraints(),
    // 2); EXPECT_EQ(forward.dependenceSatisfaction.getNumEqualityConstraints(),
    // 1);
    // EXPECT_EQ(reverse.dependenceSatisfaction.getNumInequalityConstraints(),
    // 1); EXPECT_EQ(reverse.dependenceSatisfaction.getNumEqualityConstraints(),
    // 1);
    EXPECT_TRUE(allZero(forward.depPoly.E(_, 0)));
    EXPECT_FALSE(allZero(reverse.depPoly.E(_, 0)));
    int nonZeroInd = -1;
    for (unsigned i = 0; i < reverse.depPoly.E.numRow(); ++i) {
        bool notZero = !allZero(reverse.depPoly.getEqSymbols(i));
        // we should only find 1 non-zero
        EXPECT_FALSE((nonZeroInd != -1) & notZero);
        if (notZero)
            nonZeroInd = i;
    }
    // v_1 is `n` for the load
    // v_4 is `n` for the store
    // thus, we expect v_1 = v_4 + 1
    // that is, the load depends on the store from the previous iteration
    // (e.g., store when `v_4 = 0` is loaded when `v_1 = 1`.
    auto nonZero = reverse.depPoly.getCompTimeEqOffset(nonZeroInd);
    const size_t numSymbols = reverse.depPoly.getNumSymbols();
    EXPECT_EQ(numSymbols, 3);
    EXPECT_TRUE(nonZero.hasValue());
    if (nonZero.getValue() == 1) {
        // v_1 - v_4 == 1
        // 1 - v_1 + v_4 == 0
        EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 1), -1);
        EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 4), 1);

    } else {
        // -v_1 + v_4 == -1
        // -1 + v_1 - v_4 == 0
        EXPECT_EQ(nonZero.getValue(), -1);
        EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 1), 1);
        EXPECT_EQ(reverse.depPoly.E(nonZeroInd, numSymbols + 4), -1);
    }

    bool optFail = lblock.optimize();
    EXPECT_FALSE(optFail);
    IntMatrix optPhi2(2, 2);
    optPhi2.antiDiag() = 1;
    IntMatrix optPhi3{stringToIntMatrix("[0 0 1; 1 0 0; 0 1 0]")};
    // assert(!optFail);
    for (auto &mem : lblock.memory) {
        SHOW(mem.nodeIndex);
        CSHOWLN(mem.ref);
        Schedule &s = lblock.nodes[mem.nodeIndex].schedule;
        SHOWLN(s.getPhi());
        SHOWLN(s.getOmega());
        if (mem.getNumLoops() == 2) {
            EXPECT_EQ(s.getPhi(), optPhi2);
        } else {
            assert(mem.getNumLoops() == 3);
            EXPECT_EQ(s.getPhi(), optPhi3);
        }
        // SHOWLN(mem.schedule.getPhi());
        // SHOWLN(mem.schedule.getOmega());
        std::cout << std::endl;
    }
}

TEST(MeanStDevTest0, BasicAssertions) {

    // for (i = 0; i < I; ++i){
    //   x(i) = 0; // [0]
    //   for (j = 0; j < J; ++j)
    //     x(i) += A(i,j) // [1,0:2]
    //   x(i) /= J;
    //   s(i) = 0;
    //   for (j = 0; j < J; ++j){
    //     d = (A(i,j) - x(i));
    //     s(i) += d*d;
    //   }
    //   s(i) = sqrt(s(i) / (J-1));
    // }

    // second variant:
    //
    // for (i = 0; i < I; ++i){
    //    x(i) = 0;
    //    s(i) = 0;
    // }
    // for (j = 0; j < J; ++j){
    //   for (i = 0; i < I; ++i){
    //      x(i) += A(i,j)
    // for (i = 0; i < I; ++i){
    //   x(i) /= J;
    // for (j = 0; j < J; ++j){
    //   for (i = 0; i < I; ++i){
    //     d = (A(i,j) - x(i));
    //     s(i) += d*d;
    //   }
    // }
    // for (i = 0; i < I; ++i)
    //   s(i) = sqrt(s(i) / (J-1));
    llvm::DataLayout dl("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
                        "n8:16:32:64-S128");
    llvm::TargetTransformInfo TTI{dl};
    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);

    // create arrays
    llvm::Type *Float64 = builder.getDoubleTy();
    llvm::Value *ptrX = builder.CreateIntToPtr(builder.getInt64(8000), Float64);
    llvm::Value *ptrA =
        builder.CreateIntToPtr(builder.getInt64(16000), Float64);
    llvm::Value *ptrS =
        builder.CreateIntToPtr(builder.getInt64(24000), Float64);

    // llvm::ConstantInt *Iv = builder.getInt64(200);
    llvm::ConstantInt *Jv = builder.getInt64(100);
    auto Jfp = builder.CreateUIToFP(Jv, Float64);
    auto zero = builder.getInt64(0);
    auto one = builder.getInt64(1);
    llvm::Value *iv = builder.CreateAdd(zero, one);
    llvm::Value *jv = builder.CreateAdd(zero, one);

    llvm::Value *Aoffset = builder.CreateAdd(jv, builder.CreateMul(iv, Jv));
    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n);
    //   }    auto Bload = builder.CreateLoad(
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
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Xload_1 = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Xload_2 = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));

    auto zeroFP = llvm::ConstantFP::getZero(Float64);
    auto Xstore_0 = builder.CreateAlignedStore(
        zeroFP,
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Xstore_1 = builder.CreateAlignedStore(
        builder.CreateFAdd(Xload_0, Aload_m),
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Xstore_2 = builder.CreateAlignedStore(
        builder.CreateFDiv(Xload_1, Jfp),
        builder.CreateGEP(Float64, ptrX,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));

    auto Sload_0 = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrS,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Sload_1 = builder.CreateAlignedLoad(
        Float64,
        builder.CreateGEP(Float64, ptrS,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto Sstore_0 = builder.CreateAlignedStore(
        zeroFP,
        builder.CreateGEP(Float64, ptrS,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));
    auto diff = builder.CreateFSub(Aload_s, Xload_2);
    // llvm::Intrinsic::fmuladd
    auto Sstore_1 = builder.CreateAlignedStore(
        builder.CreateFAdd(Sload_0, builder.CreateFMul(diff, diff)),
        builder.CreateGEP(Float64, ptrS,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));

    llvm::Module M = llvm::Module("dummymod", ctx);
    // llvm::Module *M = builder.GetInsertBlock()->getParent()->getParent();
    llvm::Function *sqrt =
        llvm::Intrinsic::getDeclaration(&M, llvm::Intrinsic::sqrt, Float64);
    llvm::FunctionType *sqrtTyp =
        llvm::Intrinsic::getType(ctx, llvm::Intrinsic::sqrt, {Float64});

    auto Sstore_2 = builder.CreateAlignedStore(
        builder.CreateCall(sqrtTyp, sqrt, {builder.CreateFDiv(Sload_1, Jfp)}),
        builder.CreateGEP(Float64, ptrS,
                          llvm::SmallVector<llvm::Value *, 1>{iv}),
        llvm::MaybeAlign(8));

    // Now, create corresponding schedules
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    llvm::SmallVector<Polynomial::Monomial> symbols{I, J};
    IntMatrix TwoLoopsMat{stringToIntMatrix("[-1 1 0 -1 0; "
                                            "0 0 0 1 0; "
                                            "-1 0 1 0 -1; "
                                            "0 0 0 0 1]")};
    auto loopIJ = AffineLoopNest::construct(TwoLoopsMat, symbols);
    IntMatrix OneLoopMat{stringToIntMatrix("[-1 1 -1; "
                                           "0 0 1]")};
    auto loopI = AffineLoopNest::construct(OneLoopMat, {I});
    // IntMatrix ILoop{IJLoop(_(0,2),_(0,3))};
    // LoopBlock jOuterLoopNest;
    LoopBlock iOuterLoopNest;
    // Array IDs are:
    // A: 0
    // x: 1
    // s: 2
    ArrayReference AInd{0, loopIJ, 2};
    {
        MutPtrMatrix<int64_t> IndMat = AInd.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        AInd.strides[0] = J;
        AInd.strides[1] = 1;
    }

    ArrayReference xInd1{1, loopI, 1};
    {
        MutPtrMatrix<int64_t> IndMat = xInd1.indexMatrix();
        IndMat(0, 0) = 1; // i
        xInd1.strides[0] = 1;
    }
    ArrayReference xInd2{1, loopIJ, 1};
    {
        MutPtrMatrix<int64_t> IndMat = xInd2.indexMatrix();
        IndMat(0, 0) = 1; // i
        xInd2.strides[0] = 1;
    }

    ArrayReference sInd1{2, loopI, 1};
    {
        MutPtrMatrix<int64_t> IndMat = sInd1.indexMatrix();
        IndMat(0, 0) = 1; // i
        sInd1.strides[0] = 1;
    }
    ArrayReference sInd2{2, loopIJ, 1};
    {
        MutPtrMatrix<int64_t> IndMat = sInd2.indexMatrix();
        IndMat(0, 0) = 1; // i
        sInd2.strides[0] = 1;
    }

    Schedule sch1_0(1);
    Schedule sch2_1_0(2);
    sch2_1_0.getOmega()(2) = 1;
    Schedule sch2_1_1(2);
    sch2_1_1.getOmega()(2) = 1;
    sch2_1_1.getOmega()(4) = 1;
    Schedule sch2_1_2(2);
    sch2_1_2.getOmega()(2) = 1;
    sch2_1_2.getOmega()(4) = 2;
    Schedule sch1_2(1);
    sch1_2.getOmega()(2) = 2;
    Schedule sch1_3(1);
    sch1_3.getOmega()(2) = 3;
    Schedule sch1_4(1);
    sch1_4.getOmega()(2) = 4;
    Schedule sch2_5_0(2);
    sch2_5_0.getOmega()(2) = 5;
    Schedule sch2_5_1(2);
    sch2_5_1.getOmega()(2) = 5;
    sch2_5_1.getOmega()(4) = 1;
    Schedule sch2_5_2(2);
    sch2_5_2.getOmega()(2) = 5;
    sch2_5_2.getOmega()(4) = 2;
    Schedule sch2_5_3(2);
    sch2_5_3.getOmega()(2) = 5;
    sch2_5_3.getOmega()(4) = 3;
    Schedule sch1_6(1);
    sch1_6.getOmega()(2) = 6;
    Schedule sch1_7(1);
    sch1_7.getOmega()(2) = 7;
    SHOWLN(sch1_0.getPhi());
    SHOWLN(sch2_1_0.getPhi());
    SHOWLN(sch2_1_1.getPhi());
    SHOWLN(sch2_1_2.getPhi());
    SHOWLN(sch1_2.getPhi());
    SHOWLN(sch1_3.getPhi());
    SHOWLN(sch1_4.getPhi());
    SHOWLN(sch2_5_0.getPhi());
    SHOWLN(sch2_5_1.getPhi());
    SHOWLN(sch2_5_2.getPhi());
    SHOWLN(sch2_5_3.getPhi());
    SHOWLN(sch1_6.getPhi());
    SHOWLN(sch1_7.getPhi());
    // SHOWLN(sch1_0.getOmega());
    // SHOWLN(sch2_1_0.getOmega());
    // SHOWLN(sch2_1_1.getOmega());
    // SHOWLN(sch2_1_2.getOmega());
    // SHOWLN(sch1_2.getOmega());
    // SHOWLN(sch1_3.getOmega());
    // SHOWLN(sch1_4.getOmega());
    // SHOWLN(sch2_5_0.getOmega());
    // SHOWLN(sch2_5_1.getOmega());
    // SHOWLN(sch2_5_2.getOmega());
    // SHOWLN(sch2_5_3.getOmega());
    // SHOWLN(sch1_6.getOmega());
    // SHOWLN(sch1_7.getOmega());
    iOuterLoopNest.memory.emplace_back(xInd1, Xstore_0, sch1_0, false); // 0

    iOuterLoopNest.memory.emplace_back(AInd, Aload_m, sch2_1_0, true);  // 1
    iOuterLoopNest.memory.emplace_back(xInd2, Xload_0, sch2_1_1, true); // 2

    iOuterLoopNest.memory.emplace_back(xInd2, Xstore_1, sch2_1_2, false); // 3

    iOuterLoopNest.memory.emplace_back(xInd1, Xload_1, sch1_2, true);   // 4
    iOuterLoopNest.memory.emplace_back(xInd1, Xstore_2, sch1_3, false); // 5

    iOuterLoopNest.memory.emplace_back(sInd1, Sstore_0, sch1_4, false);   // 6
    iOuterLoopNest.memory.emplace_back(AInd, Aload_s, sch2_5_0, true);    // 7
    iOuterLoopNest.memory.emplace_back(xInd2, Xload_2, sch2_5_1, true);   // 8
    iOuterLoopNest.memory.emplace_back(sInd2, Sload_0, sch2_5_2, true);   // 9
    iOuterLoopNest.memory.emplace_back(sInd2, Sstore_1, sch2_5_3, false); // 10

    iOuterLoopNest.memory.emplace_back(sInd1, Sload_1, sch1_6, true);   // 11
    iOuterLoopNest.memory.emplace_back(sInd1, Sstore_2, sch1_7, false); // 12

    llvm::SmallVector<Dependence, 0> d;
    d.reserve(4);
    Dependence::check(d, iOuterLoopNest.memory[3], iOuterLoopNest.memory[5]);
    EXPECT_TRUE(d.back().forward);
    Dependence::check(d, iOuterLoopNest.memory[5], iOuterLoopNest.memory[3]);
    EXPECT_FALSE(d.back().forward);
    Dependence::check(d, iOuterLoopNest.memory[4], iOuterLoopNest.memory[5]);
    EXPECT_TRUE(d.back().forward);
    Dependence::check(d, iOuterLoopNest.memory[5], iOuterLoopNest.memory[4]);
    EXPECT_FALSE(d.back().forward);

    bool optFail = iOuterLoopNest.optimize();
    EXPECT_FALSE(optFail);
    llvm::DenseMap<MemoryAccess *, size_t> memAccessIds;
    for (size_t i = 0; i < iOuterLoopNest.memory.size(); ++i)
        memAccessIds[&iOuterLoopNest.memory[i]] = i;
    for (auto &e : iOuterLoopNest.edges) {
        std::cout << "\nEdge for array " << e.out->ref.arrayID
                  << ", in ID: " << memAccessIds[e.in]
                  << "; out ID: " << memAccessIds[e.out] << std::endl;
    }
    for (size_t i = 0; i < iOuterLoopNest.nodes.size(); ++i) {
        const auto &v = iOuterLoopNest.nodes[i];
        std::cout << "v_" << i << ":\nmem = ";
        for (auto m : v.memory) {
            std::cout << m << ", ";
        }
        std::cout << "\ninNeighbors = ";
        for (auto m : v.inNeighbors) {
            std::cout << m << ", ";
        }
        std::cout << "\noutNeighbors = ";
        for (auto m : v.outNeighbors) {
            std::cout << m << ", ";
        }
        std::cout << std::endl;
    }
    // Graphs::print(iOuterLoopNest.fullGraph());
    for (auto &mem : iOuterLoopNest.memory) {
        SHOW(mem.nodeIndex);
        CSHOWLN(mem.ref);
        Schedule &s = iOuterLoopNest.nodes[mem.nodeIndex].schedule;
        SHOWLN(s.getPhi());
        SHOWLN(s.getOmega());
    }
}
