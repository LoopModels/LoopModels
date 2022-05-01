#pragma once
#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Symbolics.hpp"
#include <llvm/ADT/SmallVector.h>

// `B` is a transposed mirror in reduced form
// it is used to check whether a new row is linearly independent.
bool addIndRow(Matrix<intptr_t, 0, 0> &A, const Stride &axis, size_t j) {
    // std::ranges::fill(A.getRow(j), intptr_t(0));
    for (size_t i = 0; i < axis.size(); ++i) {
        VarID v = axis[i].second;
        if (v.isLoopInductionVariable()) {
            if (llvm::Optional<intptr_t> c =
                    axis[i].first.getCompileTimeConstant()) {
                A(j, v.getID()) = c.getValue();
                continue;
            }
        }
        return true;
    }
    return false;
}

llvm::Optional<std::pair<std::shared_ptr<AffineLoopNest>,
                         llvm::SmallVector<ArrayReference, 0>>>
orthogonalize(llvm::SmallVectorImpl<ArrayReference *> const &ai) {
    // need to construct matrix `A` of relationship
    // B*L = I
    // where L are the loop induct variables, and I are the array indices
    // e.g., if we have `C[i + j, j]`, then
    // B = [1 1; 0 1]
    // additionally, the loop is defined by the bounds
    // A*L = A*(B\^-1 * I) <= r
    // assuming that `B` is an invertible integer matrix,
    // which we can check via `lufact(B)`, and confirming that
    // the determinant == 1 or determinant == -1.
    // If so, we can then use the lufactorizationm for computing
    // A/B, to get loop bounds in terms of the indexes.
    const AffineLoopNest &alnp = *(ai[0]->loop);
    size_t numLoops = alnp.getNumLoops();
    size_t numRow = 0;
    for (auto a : ai) {
        numRow += a->dim();
    }
    Matrix<intptr_t, 0, 0> S(numRow, numLoops);
    // std::ranges::fill(S, intptr_t(0));
    size_t row = 0;
    for (auto a : ai) {
        for (auto &axis : (*a)) {
            if (addIndRow(S, axis, row)) {
                return {};
            }
            ++row;
        }
    }
    auto [K, included] = NormalForm::orthogonalize(S);
    if (size_t I = included.size()) {
        // We let
        // L = K*J
        // Originally, the loop bounds were
        // A'*L <= b
        // now, we have (A = alnp.aln->A, r = alnp.aln->r)
        // (A'*K)*J <= r
        Matrix<intptr_t, 0, 0, 0> A;
        matmultn(A, K, alnp.A);
        std::shared_ptr<AffineLoopNest> alnNew =
            std::make_shared<AffineLoopNest>(std::move(A), alnp.b, alnp.poset);
        // auto alnNew = std::make_shared<AffineLoopNest>();
        // matmultn(alnNew->A, K, alnp.A);
        // alnNew->b = alnp.aln->b;
        // alnNew->poset = alnp.aln->poset;
        // AffineLoopNestBounds alnpNew(alnNew);
        // Originally, the mapping from our loops to our indices was
        // S*L = I
        // now, we have
        // (S*K)*J = I
        auto SK = matmul(S, K);
        // llvm::SmallVector<ArrayReference*> aiNew;
        llvm::SmallVector<ArrayReference, 0> newArrayRefs;
        newArrayRefs.reserve(numRow);
        size_t i = 0;
        for (auto a : ai) {
            newArrayRefs.emplace_back(a->arrayID, alnNew);
            for (auto &axis : *a) {
                newArrayRefs.back().pushAffineAxis(axis.stride, SK.getRow(i));
                ++i;
            }
        }
        return std::make_pair(alnNew, newArrayRefs);
    }
    return {};
}
