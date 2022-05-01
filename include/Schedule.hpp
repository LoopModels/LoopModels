#pragma once

#include "./BitSets.hpp"
#include "./Graphs.hpp"
#include "./IntermediateRepresentation.hpp"
#include "./Math.hpp"
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

// We represent a schedule as 
// Phi_s'*i + omega_s <_{lex} Phi_t*s + Omega_t
// means that schedule `s` executes before schedule `t`.
struct Schedule {
    // given `N` loops, `P` is `N+1 x 2*N+1`
    // even rows give offsets indicating fusion (0-indexed)
    // However, all odd columns of `Phi` are structually zero,
    // so we represent it with an `N x N` matrix instead.
    static constexpr unsigned maxStackLoops = 3;
    static constexpr unsigned maxStackSorage =
        maxStackLoops * (maxStackLoops + 2) + 1;
    // 3*3+ 2*3+1 = 16
    llvm::SmallVector<intptr_t, maxStackSorage> data;
    const size_t numLoops;
    SquarePtrMatrix<intptr_t> getPhi() {
        // return SquarePtrMatrix<intptr_t>(data.data(), numLoops);
        return SquarePtrMatrix<intptr_t>{data.data(), numLoops};
    }
    PtrVector<intptr_t, 0> getOmega() {
        return PtrVector<intptr_t, 0>{data.data() + numLoops * numLoops,
                                      2 * numLoops + 1};
    }
    SquarePtrMatrix<const intptr_t> getPhi() const {
        // return SquarePtrMatrix<intptr_t>(data.data(), numLoops);
        return SquarePtrMatrix<const intptr_t>{data.data(), numLoops};
    }
    PtrVector<const intptr_t, 0> getOmega() const {
	// return llvm::ArrayRef<typename T>
        return PtrVector<const intptr_t, 0>{data.data() + numLoops * numLoops,
                                      2 * numLoops + 1};
    }
};
