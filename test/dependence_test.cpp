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
//     Matrix<intptr_t,0,0,0> A(12,7);
//     llvm::SmallVector<intptr_t, 8> b(7);
//     Matrix<intptr_t,0,0,0> E(12,4);
//     llvm::SmallVector<intptr_t, 8> q(4);

// }

TEST(DependenceTest, BasicAssertions) {

    // for (i = 0:I-2){
    //   for (j = 0:J-2){
    //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    // A'*x <= b
    // [ 1   0     [i        [ I - 2
    //  -1   0   *  j ]        0
    //   0   1           <=    J - 2
    //   0  -1 ]               0     ]
    Matrix<intptr_t, 0, 0, 0> Aloop(2, 4);
    llvm::SmallVector<MPoly, 8> bloop;

    // i <= I-2
    Aloop(0, 0) = 1;
    bloop.push_back(I - 2);
    // i >= 0
    Aloop(0, 1) = -1;
    bloop.push_back(0);

    // j <= J-2
    Aloop(1, 2) = 1;
    bloop.push_back(J - 2);
    // j >= 0
    Aloop(1, 3) = -1;
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
    llvm::Optional<Dependence> dc(
        Dependence::check(Asrc, schStore, Atgt0, schLoad));
    EXPECT_TRUE(dc.hasValue());
    Dependence &d(dc.getValue());
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

    Matrix<intptr_t, 0, 0, 0> Aloop(2, 4);
    llvm::SmallVector<MPoly, 8> bloop;

    // i <= I-1
    Aloop(0, 0) = 1;
    bloop.push_back(I - 1);
    // i >= 0
    Aloop(0, 1) = -1;
    bloop.push_back(0);

    // j <= i-1
    Aloop(0, 2) = -1;
    Aloop(1, 2) = 1;
    bloop.push_back(-1);
    // j >= 0
    Aloop(1, 3) = -1;
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
    llvm::Optional<Dependence> dc(
        Dependence::check(Asrc, schStore, Atgt, schLoad));
    EXPECT_FALSE(dc.hasValue());
}
/*
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
    Matrix<intptr_t, 0, 0, 0> AMN(2, 4);
    llvm::SmallVector<MPoly, 8> bMN;
    Matrix<intptr_t, 0, 0, 0> AMNK(3, 6);
    llvm::SmallVector<MPoly, 8> bMNK;

    // m <= M-1
    AMN(0, 0) = 1;
    bMN.push_back(M - 1);
    AMNK(0, 0) = 1;
    bMNK.push_back(M - 1);
    // m >= 0
    AMN(0, 1) = -1;
    bMN.push_back(0);
    AMNK(0, 1) = -1;
    bMNK.push_back(0);

    // n <= N-1
    AMN(1, 2) = 1;
    bMN.push_back(N - 1);
    AMNK(1, 2) = 1;
    bMNK.push_back(N - 1);
    // n >= 0
    AMN(1, 3) = -1;
    bMN.push_back(0);
    AMNK(1, 3) = -1;
    bMNK.push_back(0);

    // k <= N-1
    AMNK(2, 4) = 1;
    bMNK.push_back(N - 1);
    // k >= n+1 -> n - k <= -1
    AMNK(1, 5) = 1;
    AMNK(2, 5) = -1;
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
    lblock.refs.reserve(6);
    // B[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> BmnAxis;
    BmnAxis.emplace_back(1, m);
    BmnAxis.emplace_back(M, n);
    const size_t BmnInd = lblock.refs.size();
    lblock.refs.emplace_back(0, loopMN, BmnAxis);
    std::cout << "Bmn = " << lblock.refs.back() << std::endl;
    // A[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AmnAxis;
    AmnAxis.emplace_back(1, m);
    AmnAxis.emplace_back(M, n);
    const size_t Amn2Ind = lblock.refs.size();
    lblock.refs.emplace_back(1, loopMN, AmnAxis);
    std::cout << "Amn2 = " << lblock.refs.back() << std::endl;
    // A[m, n]
    const size_t Amn3Ind = lblock.refs.size();
    lblock.refs.emplace_back(1, loopMNK, AmnAxis);
    std::cout << "Amn3 = " << lblock.refs.back() << std::endl;
    // A[m, k]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AmkAxis;
    AmkAxis.emplace_back(1, m);
    AmkAxis.emplace_back(M, k);
    const size_t AmkInd = lblock.refs.size();
    lblock.refs.emplace_back(1, loopMNK, AmkAxis);
    std::cout << "Amk = " << lblock.refs.back() << std::endl;
    // U[n, k]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> UnkAxis;
    UnkAxis.emplace_back(1, n);
    UnkAxis.emplace_back(N, k);
    const size_t UnkInd = lblock.refs.size();
    lblock.refs.emplace_back(2, loopMNK, UnkAxis);
    std::cout << "Unk = " << lblock.refs.back() << std::endl;
    // U[n, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> UnnAxis;
    UnnAxis.emplace_back(1, n);
    UnnAxis.emplace_back(N, n);
    const size_t UnnInd = lblock.refs.size();
    lblock.refs.emplace_back(2, loopMN, UnnAxis);
    std::cout << "Unn = " << lblock.refs.back() << std::endl;

    // for (m = 0; m < M; ++m){
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = B(m,n); // sch2
    //   }
    //   for (n = 0; n < N; ++n){
    //     A(m,n) = A(m,n) / U(n,n); // sch2
    //     for (k = n+1; k < N; ++k){
    //       A(m,k) = A(m,k) - A(m,n)*U(n,k); // sch3
    //     }
    //   }
    // }
    Schedule sch2(2);
    SquarePtrMatrix<intptr_t> Phi2 = sch2.getPhi();
    // Phi0 = [1 0; 0 1]
    Phi2(0, 0) = 1;
    Phi2(1, 1) = 1;
    // A(m,n) = -> B(m,n) <-
    lblock.memory.emplace_back(&(lblock.refs[BmnInd]), nullptr, sch2, true);
    sch2.getOmega()[4] = 1;
    // -> A(m,n) <- = B(m,n)
    lblock.memory.emplace_back(&(lblock.refs[Amn2Ind]), nullptr, sch2, false);
    sch2.getOmega()[2] = 1;
    sch2.getOmega()[4] = 0;
    // A(m,n) = -> A(m,n) <- / U(n,n); // sch2
    lblock.memory.emplace_back(&(lblock.refs[Amn2Ind]), nullptr, sch2, true);
    sch2.getOmega()[4] = 1;
    // A(m,n) = A(m,n) / -> U(n,n) <-;
    lblock.memory.emplace_back(&(lblock.refs[UnnInd]), nullptr, sch2, true);
    sch2.getOmega()[4] = 2;
    // -> A(m,n) <- = A(m,n) / U(n,n); // sch2
    lblock.memory.emplace_back(&(lblock.refs[Amn2Ind]), nullptr, sch2, false);

    Schedule sch3(3);
    SquarePtrMatrix<intptr_t> Phi3 = sch3.getPhi();
    Phi3(0, 0) = 1;
    Phi3(1, 1) = 1;
    Phi3(2, 2) = 1;
    // A(m,k) = A(m,k) - A(m,n)* -> U(n,k) <-;
    lblock.memory.emplace_back(&(lblock.refs[UnkInd]), nullptr, sch3, true);
    sch3.getOmega()[6] = 1;
    // A(m,k) = A(m,k) - -> A(m,n) <- *U(n,k);
    lblock.memory.emplace_back(&(lblock.refs[Amn3Ind]), nullptr, sch3, true);
    sch3.getOmega()[6] = 2;
    // A(m,k) = -> A(m,k) <- - A(m,n)*U(n,k);
    lblock.memory.emplace_back(&(lblock.refs[AmkInd]), nullptr, sch3, true);
    sch3.getOmega()[6] = 3;
    // -> A(m,k) <- = A(m,k) - A(m,n)*U(n,k);
    lblock.memory.emplace_back(&(lblock.refs[AmkInd]), nullptr, sch3, false);

    lblock.fillEdges();
    std::cout << "Edges found: " << lblock.edges.size() << std::endl;
}
*/
