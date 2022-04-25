#include "../include/DependencyPolyhedra.hpp"
#include "../include/IntermediateRepresentation.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <Highs.h>

TEST(DependenceTest, BasicAssertions) {

    // for (i = 0:I-2){
    //   for (j = 0:J-2){
    //     A(i+1,j+1) = A(i+1,j) + A(i,j+1);
    //   }
    // }
    auto I = Polynomial::Monomial(Polynomial::ID{2});
    auto J = Polynomial::Monomial(Polynomial::ID{3});
    // Aloop * x <= bloop
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
    AffineLoopNest loop(Aloop, bloop, poset);

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
    ArrayReference Asrc(0, AaxesSrc); //, axes, indTo
    std::cout << "AaxesSrc = " << Asrc << std::endl;

    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    j.emplace_back(1, VarID(1, VarType::LoopInductionVariable));

    // A[i+1, j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt0;
    AaxesTgt0.emplace_back(1, ip1);
    AaxesTgt0.emplace_back(I, j);
    ArrayReference Atgt0(0, AaxesTgt0); //, axes, indTo
    std::cout << "AaxesTgt0 = \n" << Atgt0 << std::endl;

    // A[i, j+1]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> AaxesTgt1;
    AaxesTgt1.emplace_back(1, i);
    AaxesTgt1.emplace_back(I, jp1);
    ArrayReference Atgt1(0, AaxesTgt1); //, axes, indTo
    std::cout << "AaxesTgt1 = \n" << Atgt1 << std::endl;

    SymbolicPolyhedra dep0(Dependence::polyhedra(loop, loop, Asrc, Atgt0));
    std::cout << "Dep0 = \n" << dep0 << std::endl;
    SymbolicPolyhedra dep1(Dependence::polyhedra(loop, loop, Asrc, Atgt1));
    std::cout << "Dep1 = \n" << dep1 << std::endl;

    // second - first requires positive integer
    // first - second requires negative integer
    // this constraint is for dependences being satisfied, i.e. we want
    // target - src >= 0, so that target happens at same time or after
    IntegerPolyhedra dep0constraint =
        Dependence::farkasScheduleDifference(dep0, 2, false);
    IntegerPolyhedra dep1constraint =
        Dependence::farkasScheduleDifference(dep1, 2, false);

    // bounding - (target - src) >= 0, so that bounding is bigger than diff
    // where bounding = w + u'*N, so that we can minimize `w + sum(u)` as part
    // of objective, and thereby set things as happening simultaneously (w/
    // respect to a particular loop).
    IntegerPolyhedra dep0bound =
        Dependence::farkasScheduleDifference(dep0, -2, true);
    IntegerPolyhedra dep1bound =
        Dependence::farkasScheduleDifference(dep1, -2, true);

    // now, we need to gather constraints...
    //
}
