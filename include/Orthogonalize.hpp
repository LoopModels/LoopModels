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
        numRow += a->arrayDim();
    }
    IntMatrix S(numLoops, numRow);
    size_t i = 0;
    for (auto a : ai) {
        PtrMatrix<int64_t> A = a->indexMatrix();
        for (size_t j = 0; j < numLoops; ++j) {
            for (size_t k = 0; k < A.numCol(); ++k) {
                S(j, k + i) = A(j, k);
            }
        }
        i += A.numCol();
    }
    auto [K, included] = NormalForm::orthogonalize(S);
    std::cout << "S = \n" << S << "\nK =\n" << K << std::endl;
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
        // (S'*K')*J = (K*S)'*J  = I
        IntMatrix KS(matmul(K, S));
        // auto KS = matmul(K, S);
        // llvm::SmallVector<ArrayReference*> aiNew;
        llvm::SmallVector<ArrayReference, 0> newArrayRefs;
        newArrayRefs.reserve(numRow);
        size_t i = 0;
        for (auto a : ai) {
            newArrayRefs.emplace_back(a->arrayID, alnNew, a->arrayDim());
            PtrMatrix<int64_t> A = newArrayRefs.back().indexMatrix();
            for (size_t j = 0; j < numLoops; ++j) {
                for (size_t k = 0; k < A.numCol(); ++k) {
                    A(j, k) = KS(j, k + i);
                }
            }
            i += A.numCol();
            llvm::SmallVector<std::pair<MPoly, MPoly>> &stridesOffsets =
                newArrayRefs.back().stridesOffsets;
            for (size_t d = 0; d < A.numCol(); ++d) {
                stridesOffsets[d] = a->stridesOffsets[d];
            }
        }
        return newArrayRefs;
    }
    return {};
}
