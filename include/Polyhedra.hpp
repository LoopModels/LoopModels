#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Symbolics.hpp"

struct AffineLoopNest {
    Matrix<Int, 0, 0, 0> A; // somewhat triangular
    llvm::SmallVector<MPoly, 8> b;
    PartiallyOrderedSet poset;
    // llvm::SmallVector<unsigned, 8> origLoop;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> lExtrema;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> uExtrema;
    // uint32_t notAffine; // bitmask indicating non-affine loops
    size_t getNumLoops() const { return A.size(0); }
    // AffineLoopNest(Matrix<Int, 0, 0, 0> A, llvm::SmallVector<MPoly, 8> r)
    //     : A(A), b(r) {}
};
