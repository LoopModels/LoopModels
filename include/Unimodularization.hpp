#pragma once
#include "./LinearDiophantine.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
#include <cstdint>

// function unimod_hnf(A)
//     H, U = Matrix.(hnf_with_transform(MatrixSpace(ZZ, size(A')...)(A')))
//     (isdiag(H) && all(isone, @views H[diagind(H)])) || return nothing
//     [A; Int.(inv(U' .// 1))[size(A, 1)+1:end, :]]
// end

// if `A` can be unimodularized, returns the inverse of the unimodularized `A`
[[maybe_unused]] static llvm::Optional<SquareMatrix<int64_t>>
unimodularize(IntMatrix A) {
    llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> OHNF =
        NormalForm::hermite(std::move(A));
    if (!OHNF.hasValue()) {
        return {};
    }
    auto &[H, U] = OHNF.getValue();
    for (size_t m = 0; m < H.numCol(); ++m) {
        if (H(m, m) != 1) {
            // unimodularization was not succesful
            return {};
        }
    }
    return std::move(U);
}
