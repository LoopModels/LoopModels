#include "../include/ArrayReference.hpp"
#include "../include/DependencyPolyhedra.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

// TEST(RedundancyElimination, BasicAssertions) {
//     Matrix<int64_t,0,0,0> A(12,7);
//     llvm::SmallVector<int64_t, 8> b(7);
//     Matrix<int64_t,0,0,0> E(12,4);
//     llvm::SmallVector<int64_t, 8> q(4);

// }

TEST(DependenceTest, BasicAssertions) {

    // for (i = 0:I-2){
    //   for (j = 0:J-2){
    //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    // A*x <= b
    // [ 1   0     [i        [ I - 2
    //  -1   0   *  j ]        0
    //   0   1           <=    J - 2
    //   0  -1 ]               0     ]
    IntMatrix Aloop(4, 2);
    llvm::SmallVector<MPoly, 8> bloop;

    // i <= I-2
    Aloop(0, 0) = 1;
    bloop.push_back(I - 2);
    // i >= 0
    Aloop(1, 0) = -1;
    bloop.push_back(0);

    // j <= J-2
    Aloop(2, 1) = 1;
    bloop.push_back(J - 2);
    // j >= 0
    Aloop(3, 1) = -1;
    bloop.push_back(0);

    PartiallyOrderedSet poset;
    assert(poset.delta.size() == 0);
    std::shared_ptr<AffineLoopNest> loop =
        std::make_shared<AffineLoopNest>(Aloop, bloop, poset);
    assert(loop->poset.delta.size() == 0);

    // we have three array refs
    // A[i+1, j+1] // (i+1)*stride(A,1) + (j+1)*stride(A,2);
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesSrc;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> ip1;
    ip1.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    ip1.emplace_back(1, VarID(1, VarType::Constant));
    AaxesSrc.emplace_back(1, ip1);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> jp1;
    jp1.emplace_back(1, VarID(1, VarType::LoopInductionVariable));
    jp1.emplace_back(1, VarID(1, VarType::Constant));
    AaxesSrc.emplace_back(I, jp1);
    ArrayReference Asrc(0, loop, AaxesSrc);
    std::cout << "AaxesSrc = " << Asrc << std::endl;

    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    j.emplace_back(1, VarID(1, VarType::LoopInductionVariable));

    // A[i+1, j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt0;
    AaxesTgt0.emplace_back(1, ip1);
    AaxesTgt0.emplace_back(I, j);
    ArrayReference Atgt0(0, loop, AaxesTgt0);
    std::cout << "AaxesTgt0 = \n" << Atgt0 << std::endl;

    // A[i, j+1]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt1;
    AaxesTgt1.emplace_back(1, i);
    AaxesTgt1.emplace_back(I, jp1);
    ArrayReference Atgt1(0, loop, AaxesTgt1);
    std::cout << "AaxesTgt1 = \n" << Atgt1 << std::endl;

    DependencePolyhedra dep0(Asrc, Atgt0);
    EXPECT_FALSE(dep0.pruneBounds());
    std::cout << "Dep0 = \n" << dep0 << std::endl;

    EXPECT_EQ(dep0.getNumConstraints(), 4);
    EXPECT_EQ(dep0.getNumEqualityConstraints(), 2);
    assert(dep0.getNumConstraints() == 4);
    assert(dep0.getNumEqualityConstraints() == 2);

    DependencePolyhedra dep1(Asrc, Atgt1);
    EXPECT_FALSE(dep1.pruneBounds());
    std::cout << "Dep1 = \n" << dep1 << std::endl;
    EXPECT_EQ(dep1.getNumConstraints(), 4);
    EXPECT_EQ(dep1.getNumEqualityConstraints(), 2);
    assert(dep1.getNumConstraints() == 4);
    assert(dep1.getNumEqualityConstraints() == 2);

    std::cout << "Poset contents: ";
    for (auto &d : loop->poset.delta) {
        std::cout << d << ", ";
    }
    std::cout << std::endl;
    EXPECT_FALSE(dep0.isEmpty());
    EXPECT_FALSE(dep1.isEmpty());

    //
    Schedule schLoad(2);
    Schedule schStore(2);
    schLoad.getPhi()(0, 0) = 1;
    schLoad.getPhi()(1, 1) = 1;
    schStore.getPhi()(0, 0) = 1;
    schStore.getPhi()(1, 1) = 1;
    schStore.getOmega()[4] = 1;
    llvm::SmallVector<Dependence, 1> dc;
    MemoryAccess msrc{Asrc, nullptr, schStore, false};
    MemoryAccess mtgt0{Atgt0, nullptr, schLoad, true};
    // MemoryAccess mtgt1{Atgt1,nullptr,schLoad,true};
    EXPECT_EQ(dc.size(), 0);
    EXPECT_EQ(Dependence::check(dc, msrc, mtgt0), 1);
    EXPECT_EQ(dc.size(), 1);
    Dependence &d(dc.front());
    EXPECT_TRUE(d.isForward());
    std::cout << d << std::endl;
}

TEST(IndependentTest, BasicAssertions) {
    // symmetric copy
    // for(i = 0:I-1){
    //   for(j = 0:i-1){
    //     A(j,i) = A(i,j)
    //   }
    // }
    //
    std::cout << "\n\n#### Starting Symmetric Copy Test ####" << std::endl;
    auto I = Polynomial::Monomial(Polynomial::ID{1});

    IntMatrix Aloop(4, 2);
    llvm::SmallVector<MPoly, 8> bloop;

    // i <= I-1
    Aloop(0, 0) = 1;
    bloop.push_back(I - 1);
    // i >= 0
    Aloop(1, 0) = -1;
    bloop.push_back(0);

    // j <= i-1
    Aloop(2, 0) = -1;
    Aloop(2, 1) = 1;
    bloop.push_back(-1);
    // j >= 0
    Aloop(3, 1) = -1;
    bloop.push_back(0);

    PartiallyOrderedSet poset;
    assert(poset.delta.size() == 0);
    std::shared_ptr<AffineLoopNest> loop =
        std::make_shared<AffineLoopNest>(Aloop, bloop, poset);
    assert(loop->poset.delta.size() == 0);

    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    j.emplace_back(1, VarID(1, VarType::LoopInductionVariable));

    // we have three array refs
    // A[i, j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesSrc;
    AaxesSrc.emplace_back(1, i);
    AaxesSrc.emplace_back(I, j);
    ArrayReference Asrc(0, loop, AaxesSrc);
    std::cout << "Asrc = " << Asrc << std::endl;

    // A[j, i]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt;
    AaxesTgt.emplace_back(1, j);
    AaxesTgt.emplace_back(I, i);
    ArrayReference Atgt(0, loop, AaxesTgt);
    std::cout << "Atgt = " << Atgt << std::endl;

    DependencePolyhedra dep(Asrc, Atgt);
    std::cout << "Dep = \n" << dep << std::endl;
    EXPECT_TRUE(dep.isEmpty());
    //
    Schedule schLoad(2);
    Schedule schStore(2);
    schLoad.getPhi()(0, 0) = 1;
    schLoad.getPhi()(1, 1) = 1;
    schStore.getPhi()(0, 0) = 1;
    schStore.getPhi()(1, 1) = 1;
    schStore.getOmega()[4] = 1;
    llvm::SmallVector<Dependence, 0> dc;
    MemoryAccess msrc{Asrc, nullptr, schStore, false};
    MemoryAccess mtgt{Atgt, nullptr, schLoad, true};
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
    //       A(m,k) -= A(m,n)*U(n,k);
    //     }
    //   }
    // }

    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    // Construct the loops
    IntMatrix AMN(4, 2);
    llvm::SmallVector<MPoly, 8> bMN;
    IntMatrix AMNK(6, 3);
    llvm::SmallVector<MPoly, 8> bMNK;

    // m <= M-1
    AMN(0, 0) = 1;
    bMN.push_back(M - 1);
    AMNK(0, 0) = 1;
    bMNK.push_back(M - 1);
    // m >= 0
    AMN(1, 0) = -1;
    bMN.push_back(0);
    AMNK(1, 0) = -1;
    bMNK.push_back(0);

    // n <= N-1
    AMN(2, 1) = 1;
    bMN.push_back(N - 1);
    AMNK(2, 1) = 1;
    bMNK.push_back(N - 1);
    // n >= 0
    AMN(3, 1) = -1;
    bMN.push_back(0);
    AMNK(3, 1) = -1;
    bMNK.push_back(0);

    // k <= N-1
    AMNK(4, 2) = 1;
    bMNK.push_back(N - 1);
    // k >= n+1 -> n - k <= -1
    AMNK(5, 1) = 1;
    AMNK(5, 2) = -1;
    bMNK.push_back(-1);

    PartiallyOrderedSet poset;
    std::shared_ptr<AffineLoopNest> loopMN =
        std::make_shared<AffineLoopNest>(AMN, bMN, poset);
    std::shared_ptr<AffineLoopNest> loopMNK =
        std::make_shared<AffineLoopNest>(AMNK, bMNK, poset);

    // construct indices
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> m;
    m.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> n;
    n.emplace_back(1, VarID(1, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> k;
    k.emplace_back(1, VarID(2, VarType::LoopInductionVariable));

    LoopBlock lblock;
    // B[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> BmnAxis;
    BmnAxis.emplace_back(1, m);
    BmnAxis.emplace_back(M, n);
    const ArrayReference BmnInd{0, loopMN, BmnAxis};
    std::cout << "Bmn = " << BmnInd << std::endl;
    // A[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AmnAxis;
    AmnAxis.emplace_back(1, m);
    AmnAxis.emplace_back(M, n);
    ArrayReference Amn2Ind{1, loopMN, AmnAxis};
    std::cout << "Amn2 = " << Amn2Ind << std::endl;
    // A[m, n]
    ArrayReference Amn3Ind{1, loopMNK, AmnAxis};
    std::cout << "Amn3 = " << Amn3Ind << std::endl;
    // A[m, k]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AmkAxis;
    AmkAxis.emplace_back(1, m);
    AmkAxis.emplace_back(M, k);
    ArrayReference AmkInd{1, loopMNK, AmkAxis};
    std::cout << "Amk = " << AmkInd << std::endl;
    // U[n, k]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> UnkAxis;
    UnkAxis.emplace_back(1, n);
    UnkAxis.emplace_back(N, k);
    ArrayReference UnkInd{2, loopMNK, UnkAxis};
    std::cout << "Unk = " << UnkInd << std::endl;
    // U[n, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> UnnAxis;
    UnnAxis.emplace_back(1, n);
    UnnAxis.emplace_back(N, n);
    ArrayReference UnnInd{2, loopMN, UnnAxis};
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
    // }
    // NOTE: shared ptrs get set to NULL when `lblock.memory` reallocs...
    lblock.memory.reserve(9);
    Schedule sch2_0_0(2);
    SquarePtrMatrix<int64_t> Phi2 = sch2_0_0.getPhi();
    // Phi0 = [1 0; 0 1]
    Phi2(0, 0) = 1;
    Phi2(1, 1) = 1;
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
    //           << lblock.memory.back().ref.loop->poset.delta.size() << std::endl;
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
    SquarePtrMatrix<int64_t> Phi3 = sch3_0.getPhi();
    Phi3(0, 0) = 1;
    Phi3(1, 1) = 1;
    Phi3(2, 2) = 1;
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
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    //
    //
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch2_1_2), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;

    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'

    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_1), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    //
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_0), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_0_1, mSch3_3), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;

    // Second, comparisons of load in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // store in `A(m,n) = A(m,n) / U(n,n)`
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch2_1_2), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;

    //
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_1), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_0), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_0, mSch3_3), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;

    // Third, comparisons of store in `A(m,n) = A(m,n) / U(n,n)`
    // with...
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_1), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_0), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch2_1_2, mSch3_3), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;

    // Fourth, comparisons of load `A(m,n)` in
    // sch3_               3        0         1     2
    // load `A(m,n)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // with...
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_0), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    EXPECT_EQ(Dependence::check(d, mSch3_1, mSch3_3), 1);
    EXPECT_FALSE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    
    // Fifth, comparisons of load `A(m,k)` in
    // sch3_               3        0         1     2
    // load `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // with...
    // store `A(m,k)` in 'A(m,k) = A(m,k) - A(m,n)*U(n,k)'
    // printMatrix(std::cout << "mSch3_0.schedule.getPhi() =\n", PtrMatrix<const int64_t>(mSch3_0.schedule.getPhi())) << std::endl;
    // printMatrix(std::cout << "mSch3_3.schedule.getPhi() =\n", PtrMatrix<const int64_t>(mSch3_3.schedule.getPhi())) << std::endl;
    // printVector(std::cout << "mSch3_0.schedule.getOmega() = ", mSch3_0.schedule.getOmega()) << std::endl;
    // printVector(std::cout << "mSch3_3.schedule.getOmega() = ", mSch3_3.schedule.getOmega()) << std::endl;
    EXPECT_EQ(Dependence::check(d, mSch3_0, mSch3_3), 1);
    EXPECT_TRUE(d.back().isForward());
    std::cout << "dep#"<<d.size()<<":\n" << d.back()<<std::endl;
    assert(d.back().isForward());
    EXPECT_EQ(d.size(), 15);
    /*
    lblock.fillEdges();
    std::cout << "Number of edges found: " << lblock.edges.size() << std::endl;
    EXPECT_EQ(lblock.edges.size(), 12);
    for (auto &e : lblock.edges) {
        std::cout << "Edge:\n" << e << "\n" << std::endl;
    }
    */
}
TEST(ConvReversePass, BasicAssertions) {
    // for (n = 0; n < N; ++n){
    //   for (m = 0; n < M; ++m){
    //     for (j = 0; n < J; ++j){
    //       for (i = 0; n < I; ++j){
    //         C[m+i,j+n] += A[m,n] * B[i,j];
    //       }
    //     }
    //   }
    // }
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto I = Polynomial::Monomial(Polynomial::ID{3});
    auto J = Polynomial::Monomial(Polynomial::ID{4});
    // Construct the loops
    IntMatrix Aloop(8, 4);
    llvm::SmallVector<MPoly, 8> bloop;

    // n <= N-1
    Aloop(0, 0) = 1;
    bloop.push_back(N - 1);
    // n >= 0
    Aloop(1, 0) = -1;
    bloop.push_back(0);
    // m <= M-1
    Aloop(2, 1) = 1;
    bloop.push_back(M - 1);
    // m >= 0
    Aloop(3, 1) = -1;
    bloop.push_back(0);
    // j <= J-1
    Aloop(4, 2) = 1;
    bloop.push_back(J - 1);
    // j >= 0
    Aloop(5, 2) = -1;
    bloop.push_back(0);
    // i <= I-1
    Aloop(6, 3) = 1;
    bloop.push_back(I - 1);
    // i >= 0
    Aloop(7, 3) = -1;
    bloop.push_back(0);

    PartiallyOrderedSet poset;
    std::shared_ptr<AffineLoopNest> loop =
        std::make_shared<AffineLoopNest>(Aloop, bloop, poset);

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
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> BijAxis;
    BijAxis.emplace_back(1, i);
    BijAxis.emplace_back(I, j);
    ArrayReference BmnInd{0, loop, BijAxis};
    std::cout << "Bmn = " << BmnInd << std::endl;
    // A[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AmnAxis;
    AmnAxis.emplace_back(1, m);
    AmnAxis.emplace_back(M, n);
    ArrayReference AmnInd{1, loop, AmnAxis};
    // C[m+i, n+j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> CmijnAxis;
    {
        llvm::SmallVector<std::pair<MPoly, VarID>, 1> mpi = m;
        mpi.push_back(i.front());
        CmijnAxis.emplace_back(1, std::move(mpi));
        llvm::SmallVector<std::pair<MPoly, VarID>, 1> npj = n;
        npj.push_back(j.front());
        CmijnAxis.emplace_back(M + I - 1, std::move(npj));
    }
    ArrayReference CmijnInd{1, loop, CmijnAxis};

    // for (n = 0; n < N; ++n){
    //   for (m = 0; n < M; ++m){
    //     for (j = 0; n < J; ++j){
    //       for (i = 0; n < I; ++j){
    //         C[m+i,j+n] = C[m+i,j+n] + A[m,n] * B[i,j];
    //       }
    //     }
    //   }
    // }
    Schedule sch_0(4);
    SquarePtrMatrix<int64_t> Phi = sch_0.getPhi();
    // Phi0 = [1 0; 0 1]
    Phi(0, 0) = 1;
    Phi(1, 1) = 1;
    Phi(2, 2) = 1;
    Phi(3, 3) = 1;
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
