#pragma once

#include "./BitSets.hpp"
#include "./Graphs.hpp"
#include "./IntermediateRepresentation.hpp"
#include "./Math.hpp"
#include <llvm/ADT/SmallVector.h>

// Phi_s*i + phi_s <_{lex} Phi_t*s + phi_t
// means that schedule `s` executes before schedule `t`.
struct Schedule {
    // given `N` loops, `phi` is `2*N+1 x N+1`
    // even rows give offsets indicating fusion (0-indexed)
    // odd rows correspond to individual loop hyperplanes
    // last column is for the offsets, so we have
    // phi[:,0:end-1] * i + phi[:,end]
    Matrix<intptr_t,0,0,0> phi;
    size_t numLoops() const { return phi.size(1)-1; }
};

