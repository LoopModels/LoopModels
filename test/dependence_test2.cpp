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


TEST(DependenceTest, BasicAssertions) {
    // for (i = 0; i < I-1; ++i){
    //   for (j = 0; j < J-1; ++j){
    //     for (k = 0; k < K-1; ++k){
    //       x[i] = f(x[i])
    //     }
    //   }
    // }

    auto I = Polynomial::Monomial(Polynomial::ID{1});
    auto J = Polynomial::Monomial(Polynomial::ID{2});
    auto K = Polynomial::Monomial(Polynomial::ID{3});

    IntMatrix Aloop(6, 3);
    llvm::SmallVector<MPoly, 8> bloop;

    // i <= I-1
    Aloop(0, 0) = 1;
    bloop.push_back(I - 1);
    // i >= 0
    Aloop(1, 0) = -1;
    bloop.push_back(0);
    // j <= J-1
    Aloop(0, 1) = 1;
    bloop.push_back(J - 1);
    // j >= 0
    Aloop(1, 1) = -1;
    bloop.push_back(0);
    // k <= K-1
    Aloop(0, 2) = 1;
    bloop.push_back(K - 1);
    // k >= 0
    Aloop(1, 2) = -1;
    bloop.push_back(0);
    
    PartiallyOrderedSet poset;
    assert(poset.delta.size() == 0);
    std::shared_ptr<AffineLoopNest> loop =
        std::make_shared<AffineLoopNest>(Aloop, bloop, poset);
    assert(loop->poset.delta.size() == 0);

    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i;
    i.emplace_back(1, VarID(0, VarType::LoopInductionVariable));
    // llvm::SmallVector<std::pair<MPoly, VarID>, 1> j;
    // j.emplace_back(1, VarID(1, VarType::LoopInductionVariable));
    // llvm::SmallVector<std::pair<MPoly, VarID>, 1> k;
    // k.emplace_back(1, VarID(1, VarType::LoopInductionVariable));

    // we have three array refs
    // x[i]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> XaxesSrc;
    XaxesSrc.emplace_back(1, i);
    ArrayReference Xref(0, loop, XaxesSrc);
    std::cout << "Xsrc = " << Xref << std::endl;
    
    Schedule schLoad(3);
    Schedule schStore(3);
    for (size_t i = 0; i < 3; ++i){
	schLoad.getPhi()(i,i) = 1;
	schStore.getPhi()(i,i) = 1;
    }
    schStore.getOmega()[6] = 1;
    llvm::SmallVector<Dependence, 0> dc;
    MemoryAccess msrc{Xref, nullptr, schStore, false};
    MemoryAccess mtgt{Xref, nullptr, schLoad, true};
    EXPECT_EQ(Dependence::check(dc, msrc, mtgt), 0);
    

}

