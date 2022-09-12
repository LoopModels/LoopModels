#include "../include/ArrayReference.hpp"
#include "../include/DependencyPolyhedra.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "Loops.hpp"
#include "Macro.hpp"
#include "MatrixStringParse.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/SmallVector.h>

TEST(DependenceTest, BasicAssertions) {

    // for (i = 0:I-2){
    //   for (j = 0:J-2){
    //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    llvm::SmallVector<Polynomial::Monomial> symbols{I, J};
    // A*x >= 0;
    // [ -2  1  0 -1  0    [ 1
    //    0  0  0  1  0  *   I   >= 0
    //   -2  0  1  0 -1      J
    //    0  0  0  0  1 ]    i
    //                       j ]
    IntMatrix Aloop{stringToIntMatrix("[-2 1 0 -1 0; "
                                      "0 0 0 1 0; "
                                      "-2 0 1 0 -1; "
                                      "0 0 0 0 1]")};
    auto loop{AffineLoopNest::construct(Aloop, symbols)};

    // we have three array refs
    // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
    ArrayReference Asrc(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        MutPtrMatrix<int64_t> OffMat = Asrc.offsetMatrix();
        OffMat(0, 0) = 1;
        OffMat(1, 0) = 1;
        Asrc.strides[0] = 1;
        Asrc.strides[1] = I;
    }
    std::cout << "AaxesSrc = " << Asrc << std::endl;

    // A[i+1, j]
    ArrayReference Atgt0(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Atgt0.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        Atgt0.offsetMatrix()(0, 0) = 1;
        Atgt0.strides[0] = 1;
        Atgt0.strides[1] = I;
    }
    std::cout << "AaxesTgt0 = \n" << Atgt0 << std::endl;

    // A[i, j+1]
    ArrayReference Atgt1(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Atgt1.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        Atgt1.offsetMatrix()(1, 0) = 1;
        Atgt1.strides[0] = 1;
        Atgt1.strides[1] = I;
    }
    std::cout << "AaxesTgt1 = \n" << Atgt1 << std::endl;

    //
    Schedule schLoad0(2);
    Schedule schStore(2);
    schStore.getOmega()[4] = 2;
    MemoryAccess msrc{Asrc, nullptr, schStore, false};
    MemoryAccess mtgt0{Atgt0, nullptr, schLoad0, true};
    DependencePolyhedra dep0(msrc, mtgt0);
    EXPECT_FALSE(dep0.isEmpty());
    dep0.pruneBounds();
    std::cout << "Dep0 = \n" << dep0 << std::endl;

    EXPECT_EQ(dep0.getNumInequalityConstraints(), 4);
    EXPECT_EQ(dep0.getNumEqualityConstraints(), 2);
    assert(dep0.getNumInequalityConstraints() == 4);
    assert(dep0.getNumEqualityConstraints() == 2);

    Schedule schLoad1(2);
    schLoad1.getOmega()[4] = 1;
    MemoryAccess mtgt1{Atgt1, nullptr, schLoad1, true};
    DependencePolyhedra dep1(msrc, mtgt1);
    EXPECT_FALSE(dep1.isEmpty());
    dep1.pruneBounds();
    std::cout << "Dep1 = \n" << dep1 << std::endl;
    EXPECT_EQ(dep1.getNumInequalityConstraints(), 4);
    EXPECT_EQ(dep1.getNumEqualityConstraints(), 2);
    assert(dep1.getNumInequalityConstraints() == 4);
    assert(dep1.getNumEqualityConstraints() == 2);
    // MemoryAccess mtgt1{Atgt1,nullptr,schLoad,true};
    llvm::SmallVector<Dependence, 1> dc;
    EXPECT_EQ(dc.size(), 0);
    EXPECT_EQ(Dependence::check(dc, msrc, mtgt0), 1);
    EXPECT_EQ(dc.size(), 1);
    Dependence &d(dc.front());
    EXPECT_TRUE(d.forward);
    std::cout << d << std::endl;
    SHOWLN(d.getNumPhiCoefficients());
    SHOWLN(d.getNumOmegaCoefficients());
    SHOWLN(d.depPoly.getDim0());
    SHOWLN(d.depPoly.getDim1());
    SHOWLN(d.depPoly.getNumVar());
    SHOWLN(d.depPoly.nullStep.size());
    SHOWLN(d.depPoly.getNumSymbols());
    SHOWLN(d.depPoly.A.numCol());
    assert(d.forward);
    assert(!allZero(d.dependenceSatisfaction.tableau(
        d.dependenceSatisfaction.tableau.numRow() - 1, _)));
}

TEST(IndependentTest, BasicAssertions) {
    // symmetric copy
    // for(i = 0:I-1)
    //   for(j = 0:i-1)
    //     A(j,i) = A(i,j)
    //
    std::cout << "\n\n#### Starting Symmetric Copy Test ####" << std::endl;
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    llvm::SmallVector<Polynomial::Monomial> symbols{I};
    IntMatrix Aloop{stringToIntMatrix("[-1 1 -1 0; "
                                      "0 0 1 0; "
                                      "-1 0 1 -1; "
                                      "0 0 0 1]")};

    auto loop{AffineLoopNest::construct(Aloop, symbols)};
    // we have three array refs
    // A[i, j]
    ArrayReference Asrc(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        Asrc.strides[0] = 1;
        Asrc.strides[1] = I;
    }
    std::cout << "Asrc = " << Asrc << std::endl;

    // A[j, i]
    ArrayReference Atgt(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Atgt.indexMatrix();
        IndMat(1, 0) = 1; // j
        IndMat(0, 1) = 1; // i
        Atgt.strides[0] = 1;
        Atgt.strides[1] = I;
    }
    std::cout << "Atgt = " << Atgt << std::endl;

    Schedule schLoad(2);
    Schedule schStore(2);
    schStore.getOmega()[4] = 1;
    MemoryAccess msrc{Asrc, nullptr, schStore, false};
    MemoryAccess mtgt{Atgt, nullptr, schLoad, true};
    DependencePolyhedra dep(msrc, mtgt);
    std::cout << "Dep = \n" << dep << std::endl;
    SHOWLN(dep.A);
    SHOWLN(dep.E);
    EXPECT_TRUE(dep.isEmpty());
    assert(dep.isEmpty());
    //
    llvm::SmallVector<Dependence, 0> dc;
    EXPECT_EQ(Dependence::check(dc, msrc, mtgt), 0);
    EXPECT_EQ(dc.size(), 0);
}
TEST(TriangularExampleTest, BasicAssertions) {
    // badly written triangular solve:
    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n);
    //   }
    //   for (n = 0; n < N; ++n){
    //     A(m,n) /= U(n,n);
    //     for (k = n+1; k < N; ++k){
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k);
    //     }
    //   }
    // }

    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    llvm::SmallVector<Polynomial::Monomial> symbols{M, N};
    // Construct the loops
    IntMatrix AMN{(stringToIntMatrix("[-1 1 0 -1 0; "
                                     "0 0 0 1 0; "
                                     "-1 0 1 0 -1; "
                                     "0 0 0 0 1]"))};
    IntMatrix AMNK{(stringToIntMatrix("[-1 1 0 -1 0 0; "
                                      "0 0 0 1 0 0; "
                                      "-1 0 1 0 -1 0; "
                                      "0 0 0 0 1 0; "
                                      "-1 0 1 0 0 -1; "
                                      "-1 0 0 0 -1 1]"))};

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
    lblock.memory.emplace_back(BmnInd, nullptr, sch2_0_0, true);
    // MemoryAccess &mSch2_0_0 = lblock.memory.back();
    sch2_0_1.getOmega()[4] = 1;
    Schedule sch2_1_0 = sch2_0_1;
    // -> A(m,n) <- = B(m,n)
    lblock.memory.emplace_back(Amn2Ind, nullptr, sch2_0_1, false);
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
    lblock.memory.emplace_back(Amn2Ind, nullptr, sch2_1_0, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "msch2_0_1.ref.loop = " << msch2_0_1.ref.loop << std::endl;
    // std::cout << "msch2_0_1.ref.loop.get() = " << msch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch2_1_0 = lblock.memory.back();
    sch2_1_1.getOmega()[4] = 1;
    Schedule sch2_1_2 = sch2_1_1;
    // A(m,n) = A(m,n) / -> U(n,n) <-;
    lblock.memory.emplace_back(UnnInd, nullptr, sch2_1_1, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    // MemoryAccess &mSch2_1_1 = lblock.memory.back();
    sch2_1_2.getOmega()[4] = 2;
    // -> A(m,n) <- = A(m,n) / U(n,n); // sch2
    lblock.memory.emplace_back(Amn2Ind, nullptr, sch2_1_2, false);
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
    lblock.memory.emplace_back(UnkInd, nullptr, sch3_0, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    // MemoryAccess &mSch3_2 = lblock.memory.back();
    sch3_1.getOmega()[6] = 1;
    Schedule sch3_2 = sch3_1;
    // A(m,k) = A(m,k) - -> A(m,n) <- *U(n,k);
    lblock.memory.emplace_back(Amn3Ind, nullptr, sch3_1, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_1 = lblock.memory.back();
    sch3_2.getOmega()[6] = 2;
    Schedule sch3_3 = sch3_2;
    // A(m,k) = -> A(m,k) <- - A(m,n)*U(n,k);
    lblock.memory.emplace_back(AmkInd, nullptr, sch3_2, true);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_0 = lblock.memory.back();
    sch3_3.getOmega()[6] = 3;
    // -> A(m,k) <- = A(m,k) - A(m,n)*U(n,k);
    lblock.memory.emplace_back(AmkInd, nullptr, sch3_3, false);
    // std::cout << "\nPushing back" << std::endl;
    // std::cout << "mSch2_0_1.ref.loop = " << mSch2_0_1.ref.loop << std::endl;
    // std::cout << "mSch2_0_1.ref.loop.get() = " << mSch2_0_1.ref.loop.get()
    //           << std::endl;
    MemoryAccess &mSch3_3 = lblock.memory.back();
    EXPECT_EQ(lblock.memory.size(), 9);

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
    d.reserve(16);
    llvm::SmallVector<Dependence, 0> r;
    r.reserve(16);
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
    EXPECT_EQ(Dependence::check(r, mSch2_1_0, mSch2_0_1), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    //
    //
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_2), 1);
    EXPECT_EQ(Dependence::check(r, mSch2_1_2, mSch2_0_1), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'

    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_1), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_1, mSch2_0_1), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    //
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_0), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_0, mSch2_0_1), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_3), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_3, mSch2_0_1), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    EXPECT_EQ(d.size(), 5);
    EXPECT_EQ(r.size(), 5);

    // Second, comparisons of load in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch2_1_2), 1);
    EXPECT_EQ(Dependence::check(r, mSch2_1_2, mSch2_1_0), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_1), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_1, mSch2_1_0), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_0), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_0, mSch2_1_0), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // NOTE: these are two load-load comparisons!
    // Hence, `fillEdges()` will currently not add these!!
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_3), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_3, mSch2_1_0), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Third, comparisons of store in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_1), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_1, mSch2_1_2), 1);
    EXPECT_TRUE(d.back().forward);
    EXPECT_FALSE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_0), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_0, mSch2_1_2), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_3), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_3, mSch2_1_2), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;

    // Fourth, comparisons of load `A(m,n)` in
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // with...
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_0), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_0, mSch3_1), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
    std::cout << "dep#" << d.size() << ":\n" << d.back() << std::endl;
    // NOTE: this is another load-load comparison that fillEdges
    // will not add currently!
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_3), 1);
    EXPECT_EQ(Dependence::check(r, mSch3_3, mSch3_1), 1);
    EXPECT_FALSE(d.back().forward);
    EXPECT_TRUE(r.back().forward);
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
    EXPECT_EQ(Dependence::check(r, mSch3_3, mSch3_0), 2);
    EXPECT_TRUE(d[d.size() - 2].forward);
    EXPECT_FALSE(d[d.size() - 1].forward);
    EXPECT_FALSE(r[r.size() - 2].forward);
    EXPECT_TRUE(r[r.size() - 1].forward);
    std::cout << "dep#" << d.size() << std::endl;
    auto &forward = d[d.size() - 2];
    auto &reverse = d[d.size() - 1];
    std::cout << "\nforward dependence:" << forward;
    std::cout << "\nreverse dependence:" << reverse;
    assert(forward.forward);
    assert(!reverse.forward);
    EXPECT_EQ(d.size(), 16);
    EXPECT_EQ(r.size(), 16);
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
    //
    // lblock.fillEdges();
    bool optFail = lblock.optimize();
    EXPECT_FALSE(optFail);
    assert(!optFail);
    // EXPECT_FALSE(lblock.optimize());
    // -3 comes from the fact we did 3 load-load comparisons above
    // in the future, we may have `fillEdges` make load-load comparisons
    // so that we can add bounding constraints to the objective, to
    // favor putting repeated loads close together.
    // However, we would not add the scheduling constraints.
    EXPECT_EQ(lblock.edges.size(), d.size()-3);
    // std::cout << "Number of edges found: " << lblock.edges.size() <<
    // std::endl; EXPECT_EQ(lblock.edges.size(), 12); for (auto &e :
    // lblock.edges) {
    //    std::cout << "Edge:\n" << e << "\n" << std::endl;
    //}
    for (auto &mem : lblock.memory){
	SHOWLN(mem.ref);
	SHOWLN(mem.schedule.getPhi());
	SHOWLN(mem.schedule.getOmega());
	std::cout << std::endl;
    }
}
TEST(ConvReversePass, BasicAssertions) {
    // for (n = 0; n < N; ++n){
    //   for (m = 0; n < M; ++m){
    //     for (j = 0; n < J; ++j){
    //       for (i = 0; n < I; ++i){
    //         C[m+i,j+n] += A[m,n] * B[i,j];
    //       }
    //     }
    //   }
    // }
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto I = Polynomial::Monomial(Polynomial::ID{3});
    auto J = Polynomial::Monomial(Polynomial::ID{4});
    llvm::SmallVector<Polynomial::Monomial> symbols{M, N, I, J};
    // Construct the loops
    IntMatrix Aloop{stringToIntMatrix("[-1 0 1 0 0 -1 0 0 0; "
                                      "0 0 0 0 0 1 0 0 0; "
                                      "-1 1 0 0 0 0 -1 0 0; "
                                      "0 0 0 0 0 0 1 0 0; "
                                      "-1 0 0 0 1 0 0 -1 0; "
                                      "0 0 0 0 0 0 0 1 0; "
                                      "-1 0 0 1 0 0 0 0 -1; "
                                      "0 0 0 0 0 0 0 0 1]")};
    auto loop = AffineLoopNest::construct(Aloop, symbols);

    // construct indices
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> m;
    m.emplace_back(1, VarID(1, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> n;
    n.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(3, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    j.emplace_back(1, VarID(2, VarType::LoopInductionVariable));

    LoopBlock lblock;
    // B[m, n]
    ArrayReference BmnInd{0, loop, 2};
    {
        MutPtrMatrix<int64_t> IndMat = BmnInd.indexMatrix();
        IndMat(1, 0) = 1; // m
        IndMat(0, 1) = 1; // n
        BmnInd.strides[0] = 1;
        BmnInd.strides[1] = I;
    }
    std::cout << "Bmn = " << BmnInd << std::endl;
    // A[m, n]
    ArrayReference AmnInd{1, loop, 2};
    {
        MutPtrMatrix<int64_t> IndMat = AmnInd.indexMatrix();
        IndMat(1, 0) = 1; // m
        IndMat(0, 1) = 1; // n
        AmnInd.strides[0] = 1;
        AmnInd.strides[1] = I;
    }
    // C[m+i, n+j]
    ArrayReference CmijnInd{1, loop, 2};
    {
        MutPtrMatrix<int64_t> IndMat = CmijnInd.indexMatrix();
        IndMat(1, 0) = 1; // m
        IndMat(3, 0) = 1; // i
        IndMat(0, 1) = 1; // n
        IndMat(2, 1) = 1; // j
        CmijnInd.strides[0] = 1;
        CmijnInd.strides[1] = M + I - 1;
    }

    // for (n = 0; n < N; ++n){
    //   for (m = 0; n < M; ++m){
    //     for (j = 0; n < J; ++j){
    //       for (i = 0; n < I; ++i){
    //         C[m+i,j+n] = C[m+i,j+n] + A[m,n] * B[i,j];
    //       }
    //     }
    //   }
    // }
    Schedule sch_0(4);
    Schedule sch_1 = sch_0;
    //         C[m+i,j+n] = C[m+i,j+n] + A[m,n] * -> B[i,j] <-;
    lblock.memory.emplace_back(BmnInd, nullptr, sch_0, true);
    sch_1.getOmega()[8] = 1;
    Schedule sch_2 = sch_1;
    //         C[m+i,j+n] = C[m+i,j+n] + -> A[m,n] <- * B[i,j];
    lblock.memory.emplace_back(AmnInd, nullptr, sch_1, true);
    sch_2.getOmega()[8] = 2;
    Schedule sch_3 = sch_2;
    //         C[m+i,j+n] = -> C[m+i,j+n] <- + A[m,n] * B[i,j];
    lblock.memory.emplace_back(CmijnInd, nullptr, sch_2, true);
    sch_3.getOmega()[8] = 3;
    //         -> C[m+i,j+n] <- = C[m+i,j+n] + A[m,n] * B[i,j];
    lblock.memory.emplace_back(CmijnInd, nullptr, sch_3, false);

    lblock.orthogonalizeStores();
    std::cout << lblock.memory.back();
    // std::cout << "lblock.refs.size() = " << lblock.refs.size() << std::endl;
}

TEST(RankDeficientLoad, BasicAssertions) {

    // for (i = 0:I-1){
    //   for (j = 0:i){
    //     A(i,j) = A(i,i);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    llvm::SmallVector<Polynomial::Monomial> symbols{I};
    // A*x <= b
    // [ 1   0     [i        [ I - 1
    //  -1   0   *  j ]        0
    //  -1   1           <=    0
    //   0  -1 ]               0     ]
    //
    IntMatrix Aloop{stringToIntMatrix("[-1 1 -1 0; "
                                      "0 0 1 0; "
                                      "0 0 1 -1; "
                                      "0 0 0 1]")};
    auto loop = AffineLoopNest::construct(Aloop, symbols);

    // we have three array refs
    // A[i, j] // i*stride(A,1) + j*stride(A,2);
    ArrayReference Asrc(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Asrc.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(1, 1) = 1; // j
        Asrc.strides[0] = 1;
        Asrc.strides[1] = I;
    }
    std::cout << "AaxesSrc = " << Asrc << std::endl;

    // A[i, i]
    ArrayReference Atgt(0, loop, 2);
    {
        MutPtrMatrix<int64_t> IndMat = Atgt.indexMatrix();
        IndMat(0, 0) = 1; // i
        IndMat(0, 1) = 1; // i
        Atgt.strides[0] = 1;
        Atgt.strides[1] = I;
    }
    std::cout << "AaxesTgt = \n" << Atgt << std::endl;

    Schedule schLoad(2);
    Schedule schStore(2);
    schStore.getOmega()[4] = 1;
    MemoryAccess msrc{Asrc, nullptr, schStore, false};
    MemoryAccess mtgt{Atgt, nullptr, schLoad, true};

    llvm::SmallVector<Dependence, 1> deps;
    EXPECT_EQ(Dependence::check(deps, msrc, mtgt), 1);
    EXPECT_FALSE(deps.back().forward); // load -> store
    std::cout << "Blog post example:\n" << deps[0] << std::endl;
}

TEST(TimeHidingInRankDeficiency, BasicAssertions) {
    // for (i = 0; i < I; ++i)
    //   for (j = 0; j < J; ++j)
    //     for (k = 0; k < K; ++k)
    //       A(i+j, j+k, i-k) = foo(A(i+j, j+k, i-k));
    //
    // Indexed by three LIVs, and three dimensional
    // but memory access pattern is only rank 2, leaving
    // a time dimension of repeated memory accesses.
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    auto K = Polynomial::Monomial(Polynomial::ID{3});
    llvm::SmallVector<Polynomial::Monomial> symbols{I, J, K};
    // A*x <= b
    // [ 1   0  0     [i        [ I - 1
    //  -1   0  0   *  j          0
    //   0   1  0      k ]    <=  J - 1
    //   0  -1  0 ]               0
    //   0   0  1 ]               K - 1
    //   0   0 -1 ]               0     ]
    //
    IntMatrix Aloop{stringToIntMatrix("[-1 1 0 0 -1 0 0; "
                                      "0 0 0 0 1 0 0; "
                                      "-1 0 1 0 0 -1 0; "
                                      "0 0 0 0 0 1 0; "
                                      "-1 0 0 1 0 0 -1; "
                                      "0 0 0 0 0 0 1]")};
    auto loop = AffineLoopNest::construct(Aloop, symbols);

    // we have three array refs
    // A[i+j, j+k, i - k]
    ArrayReference Aref(0, loop, 3);
    {
        MutPtrMatrix<int64_t> IndMat = Aref.indexMatrix();
        IndMat(0, 0) = 1;  // i
        IndMat(1, 0) = 1;  // + j
        IndMat(1, 1) = 1;  // j
        IndMat(2, 1) = 1;  // + k
        IndMat(0, 2) = 1;  // i
        IndMat(2, 2) = -1; // -k
        Aref.strides[0] = 1;
        Aref.strides[1] = I;
        Aref.strides[2] = I * J;
    }
    std::cout << "Aref = " << Aref << std::endl;

    Schedule schLoad(3);
    Schedule schStore(3);
    schStore.getOmega()(end) = 1;
    MemoryAccess msrc{Aref, nullptr, schStore, false};
    MemoryAccess mtgt{Aref, nullptr, schLoad, true};

    llvm::SmallVector<Dependence, 2> deps;
    EXPECT_EQ(Dependence::check(deps, msrc, mtgt), 2);
    assert(deps.size() == 2);
    std::cout << "Rank deficicient example:\nForward:\n"
              << deps[0] << "\nReverse:\n"
              << deps[1] << std::endl;
}
