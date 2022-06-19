#pragma once
#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Symbolics.hpp"
#include <llvm/ADT/SmallVector.h>

llvm::Optional<llvm::SmallVector<ArrayReference, 0>>
orthogonalize(llvm::SmallVectorImpl<ArrayReference *> const &ai) {
    // need to construct matrix `A` of relationship
    // B*L = I
    // where L are the loop induct variables, and I are the array indices
    // e.g., if we have `C[i + j, j]`, then
    // B = [1 1; 0 1]
    // additionally, the loop is defined by the bounds
    // A*L = A*(B\^-1 * I) <= r
    // assuming that `B` is an invertible integer matrix (i.e. is unimodular),
    const AffineLoopNest &alnp = *(ai[0]->loop);
    size_t numLoops = alnp.getNumLoops();
    size_t numRow = 0;
    for (auto a : ai) {
        numRow += a->dim();
    }
    IntMatrix S(numLoops, numRow);
    size_t row = 0;
    for (auto a : ai) {
        PtrMatrix<int64_t> A = a->indexMatrix();
        for (size_t r = 0; r < A.numRow(); ++r) {
            for (size_t l = 0; l < numLoops; ++l) {
                S(l, row) = A(r, l);
            }
            ++row;
        }
    }
    auto [K, included] = NormalForm::orthogonalize(S);
    if (included.size()) {
        // We let
        // L = K'*J
        // Originally, the loop bounds were
        // A*L <= b
        // now, we have (A = alnp.aln->A, r = alnp.aln->r)
        // (A*K')*J <= r
        llvm::IntrusiveRefCntPtr<AffineLoopNest> alnNew =
            llvm::makeIntrusiveRefCnt<AffineLoopNest>(matmulnt(alnp.A, K),
                                                      alnp.b, alnp.poset);
        // auto alnNew = std::make_shared<AffineLoopNest>();
        // matmultn(alnNew->A, K, alnp.A);
        // alnNew->b = alnp.aln->b;
        // alnNew->poset = alnp.aln->poset;
        // AffineLoopNestBounds alnpNew(alnNew);
        // Originally, the mapping from our loops to our indices was
        // S'*L = I
        // now, we have
        // (S'*K')*J = I
        IntMatrix SK(matmultt(S, K));
        // auto KS = matmul(K, S);
        // llvm::SmallVector<ArrayReference*> aiNew;
        llvm::SmallVector<ArrayReference, 0> newArrayRefs;
        newArrayRefs.reserve(numRow);
        size_t i = 0;
        for (auto a : ai) {
            newArrayRefs.emplace_back(a->arrayID, alnNew, a->dim());
            PtrMatrix<int64_t> A = newArrayRefs.back().indexMatrix();
            for (size_t j = 0; j < A.length(); ++j) {
                A[j] = SK[i + j];
            }
            i += A.length();
            llvm::SmallVector<std::pair<MPoly, MPoly>> &stridesOffsets =
                newArrayRefs.back().stridesOffsets;
            for (size_t d = 0; d < A.numRow(); ++d) {
                stridesOffsets[d] = a->stridesOffsets[d];
            }
        }
        return newArrayRefs;
    }
    return {};
}
