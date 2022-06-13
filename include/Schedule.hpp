#pragma once

#include "./ArrayReference.hpp"
#include "./Graphs.hpp"
#include "./Math.hpp"
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

// We represent a schedule as
// Phi_s'*i + omega_s <_{lex} Phi_t'*s + Omega_t
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
    llvm::SmallVector<int64_t, maxStackStorage> data;
    const size_t numLoops;
    Schedule(size_t nLoops)
        : data(llvm::SmallVector<int64_t, maxStackStorage>(
              nLoops * (nLoops + 2) + 1)),
          numLoops(nLoops){};
    SquarePtrMatrix<int64_t> getPhi() {
        // return SquarePtrMatrix<int64_t>(data.data(), numLoops);
        return SquarePtrMatrix<int64_t>{data.data(), numLoops};
    }
    llvm::MutableArrayRef<int64_t> getOmega() {
        return {data.data() + numLoops * numLoops, 2 * numLoops + 1};
    }
    SquarePtrMatrix<const int64_t> getPhi() const {
        return SquarePtrMatrix<const int64_t>{data.data(), numLoops};
    }
    llvm::ArrayRef<int64_t> getOmega() const {
        return {data.data() + numLoops * numLoops, 2 * numLoops + 1};
    }
    bool fusedThrough(const Schedule &y, const size_t numLoopsCommon) const {
        llvm::ArrayRef<int64_t> o0 = getOmega();
        llvm::ArrayRef<int64_t> o1 = y.getOmega();
        bool allEqual = true;
        for (size_t n = 0; n < numLoopsCommon; ++n) {
            allEqual &= (o0[2 * n] == o1[2 * n]);
        }
        return allEqual;
    }
    bool fusedThrough(const Schedule &y) const {
        return fusedThrough(y, std::min(numLoops, y.numLoops));
    }
};
