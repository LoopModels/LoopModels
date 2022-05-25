#include "../include/ArrayReference.hpp"
#include "../include/DependencyPolyhedra.hpp"
#include "../include/ILPConstraintElimination.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include <Highs.h>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <memory>

// ipoly =
// -v_5 <= 0
// -v_6 <= 0
// -v_7 <= 0
// -v_8 <= 0
// -v_9 <= 0
// -v_10 <= 0
// -v_11 <= 0
// -v_12 <= 0
// -v_13 <= 0
// -v_14 <= 0
// -v_15 <= 0
// -v_16 <= 0
// -v_4 - v_5 + v_8 - 2v_9 - 2v_12 + v_15 - v_16 == 0
// v_0 - v_9 + v_10 + v_13 - v_14 == 0
// v_1 + v_11 + v_15 - v_16 == 0
// -v_2 - v_13 + v_14 == 0
// -v_3 - v_12 - v_15 + v_16 == 0
// -v_6 + v_9 == 0
// -v_7 + v_12 == 0

TEST(ConstraintValidation, BasicAssertions) {
    Matrix<intptr_t, 0, 0, 0> A(17, 12);
    llvm::SmallVector<intptr_t, 8> b(12);
    Matrix<intptr_t, 0, 0, 0> E(17, 7);
    llvm::SmallVector<intptr_t, 8> q(7);
    for (size_t i = 0; i < 12; ++i) {
        A(i + 5, i) = -1;
    }
    E(4, 0) = -1;
    E(5, 0) = -1;
    E(8, 0) = 1;
    E(9, 0) = -2;
    E(12, 0) = -2;
    E(15, 0) = 1;
    E(16, 0) = -1;
    q[0] = 0;

    E(0, 1) = 1;
    E(9, 1) = -1;
    E(10, 1) = 1;
    E(13, 1) = 1;
    E(14, 1) = -1;
    q[1] = 0;

    E(1, 2) = 1;
    E(11, 2) = 1;
    E(15, 2) = 1;
    E(16, 2) = -1;
    q[2] = 0;

    E(2, 3) = -1;
    E(13, 3) = -1;
    E(14, 3) = 1;
    q[3] = 0;

    E(3, 4) = -1;
    E(12, 4) = -1;
    E(15, 4) = -1;
    E(16, 4) = 1;
    q[4] = 0;

    E(6, 5) = -1;
    E(9, 5) = 1;
    q[5] = 0;

    E(7, 6) = -1;
    E(12, 6) = 1;
    q[6] = 0;

    IntegerEqPolyhedra ipoly(A, b, E, q);

    Matrix<intptr_t, 0, 0, 0> Ac(A), Anew;
    llvm::SmallVector<intptr_t, 8> bc(b), bnew;
    Matrix<intptr_t, 0, 0, 0> Ec(E), Enew;
    llvm::SmallVector<intptr_t, 8> qc(q), qnew;
    for (size_t i = 16; i >= 8; --i) {
        // ipoly.removeVariable(i);
        // ipoly.A.reduceNumRows(i);
        // ipoly.E.reduceNumRows(i);

        fourierMotzkin(Anew, bnew, Enew, qnew, Ac, bc, Ec, qc, i);

        std::swap(Anew, Ac);
        std::swap(bnew, bc);
        std::swap(Enew, Ec);
        std::swap(qnew, qc);
        Ac.reduceNumRows(i);
        Ec.reduceNumRows(i);
        IntegerPolyhedra::moveEqualities(Ac, bc, Ec, qc);
        std::cout << "following fM=\n"
                  << IntegerEqPolyhedra(Ac, bc, Ec, qc) << std::endl;
        pruneBounds(Ac, bc, Ec, qc);

        // std::cout << "pruned ipoly =\n"
        //           << ipoly << "\n\npruned via ILP=\n"
        //           << IntegerEqPolyhedra(Ac, bc, Ec, qc) << std::endl;
        std::cout << "pruned via ILP=\n"
                  << IntegerEqPolyhedra(Ac, bc, Ec, qc) << std::endl;
    }

    std::cout << "pruned via ILP=\n"
              << IntegerEqPolyhedra(Ac, bc, Ec, qc) << std::endl;
    // std::cout << "pruned ipoly =\n"
    //           << ipoly << "\n\npruned via ILP=\n"
    //           << IntegerEqPolyhedra(Ac, bc, Ec, qc) << std::endl;

    // std::cout << "A =\n" << Ac << "\nb=[";
    // for (auto &bi : bc) {
    //     std::cout << bi << ", ";
    // }
    // std::cout << "]\nE =\n" << Ec << "\nq=[";
    // for (auto &qi : qc) {
    //     std::cout << qi << ", ";
    // }
    // std::cout << "]" << std::endl;
}
/*
TEST(DependenceTest, BasicAssertions) {

    // for (i = 0:I-2){
    //   for (j = 0:J-2){
    //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{3});
    auto J = Polynomial::Monomial(Polynomial::ID{4});

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
    // A[i+1, j+1]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesSrc;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> ip1;
    ip1.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    ip1.emplace_back(1, VarID(1, VarType::Constant));
    AaxesSrc.emplace_back(1, ip1);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> jp1;
    jp1.emplace_back(1, VarID(1, VarType::LoopInductionVariable));
    jp1.emplace_back(1, VarID(1, VarType::Constant));
    AaxesSrc.emplace_back(I, jp1);
    ArrayReference Asrc(0, loop, AaxesSrc); //, axes, indTo
    std::cout << "AaxesSrc = " << Asrc << std::endl;

    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    j.emplace_back(1, VarID(1, VarType::LoopInductionVariable));

    // A[i+1, j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt0;
    AaxesTgt0.emplace_back(1, ip1);
    AaxesTgt0.emplace_back(I, j);
    ArrayReference Atgt0(0, loop, AaxesTgt0); //, axes, indTo
    std::cout << "AaxesTgt0 = \n" << Atgt0 << std::endl;

    // A[i, j+1]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt1;
    AaxesTgt1.emplace_back(1, i);
    AaxesTgt1.emplace_back(I, jp1);
    ArrayReference Atgt1(0, loop, AaxesTgt1); //, axes, indTo
    std::cout << "AaxesTgt1 = \n" << Atgt1 << std::endl;

    DependencePolyhedra dep0(Asrc, Atgt0);
    std::cout << "Dep0 = \n" << dep0 << std::endl;
    DependencePolyhedra dep1(Asrc, Atgt1);
    std::cout << "Dep1 = \n" << dep1 << std::endl;

    std::cout << "Poset contents: ";
    for (auto &d : loop->poset.delta) {
        std::cout << d << ", ";
    }
    std::cout << std::endl;
    EXPECT_FALSE(dep0.isEmpty());
    EXPECT_FALSE(dep1.isEmpty());
}
TEST(IndependentTest, BasicAssertions) {
    // symmetric copy
    // for(i = 0:I-1){
    //   for(j = 0:i-1){
    //     A(j,i) = A(i,j)
    //   }
    // }
    //

    auto I = Polynomial::Monomial(Polynomial::ID{3});

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
    ArrayReference Asrc(0, loop, AaxesSrc); //, axes, indTo
    std::cout << "Asrc = " << Asrc << std::endl;

    // A[j, i]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt;
    AaxesTgt.emplace_back(1, j);
    AaxesTgt.emplace_back(I, i);
    ArrayReference Atgt(0, loop, AaxesTgt); //, axes, indTo
    std::cout << "Atgt = " << Atgt << std::endl;

    DependencePolyhedra dep(Asrc, Atgt);
    std::cout << "Dep = \n" << dep << std::endl;
    EXPECT_TRUE(dep.isEmpty());
}
TEST(TriangularExampleTest, BasicAssertions) {}

*/
