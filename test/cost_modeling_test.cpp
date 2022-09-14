#include "../include/ArrayReference.hpp"
#include "../include/DependencyPolyhedra.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "MatrixStringParse.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Operator.h>

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
    lblock.memory.emplace_back(Amn2Ind, Aload1mn, sch2_1_0, true);
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
    // assert(!optFail);
}
