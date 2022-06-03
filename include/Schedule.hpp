#pragma once

#include "./ArrayReference.hpp"
#include "./Graphs.hpp"
#include "./Math.hpp"
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

// We represent a schedule as
// Phi_s'*i + omega_s <_{lex} Phi_t*s + Omega_t
// means that schedule `s` executes before schedule `t`.
//
// S_0 = {Phi_0, omega_0}
// S_1 = {Phi_1, omega_1}
// given i_0 and i_1, if
// Phi_0 * i_0 + omega_0 << Phi_1 * i_1 + omega_1
// then "i_0" for schedule "S_0" happens before
// "i_1" for schedule "S_1"
// 
struct Schedule {
    // given `N` loops, `P` is `N+1 x 2*N+1`
    // even rows give offsets indicating fusion (0-indexed)
    // However, all odd columns of `Phi` are structually zero,
    // so we represent it with an `N x N` matrix instead.
    static constexpr unsigned maxStackLoops = 3;
    static constexpr unsigned maxStackStorage =
        maxStackLoops * (maxStackLoops + 2) + 1;
    // 3*3+ 2*3+1 = 16
    llvm::SmallVector<intptr_t, maxStackStorage> data;
    const size_t numLoops;
    Schedule(size_t nLoops)
        : data(llvm::SmallVector<intptr_t, maxStackStorage>(
              nLoops * (nLoops + 2) + 1)),
          numLoops(nLoops){};
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
    bool sameLoop(const Schedule &y) const {
	PtrVector<const intptr_t, 0> o0 = getOmega();
	PtrVector<const intptr_t, 0> o1 = y.getOmega();
	const size_t numLoopsCommon = std::min(numLoops, y.numLoops);
	bool allEqual = true;
	for (size_t n = 0; n < numLoopsCommon; ++n){
	    allEqual &= (o0[2*n] == o1[2*n]);
	}
	return allEqual;
    }
};
