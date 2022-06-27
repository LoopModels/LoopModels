#pragma once
#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

IntMatrix orthogonalize(IntMatrix A) {
    if ((A.numCol() < 2) || (A.numRow() == 0))
        return A;
    normalizeByGCD(A.getRow(0));
    if (A.numRow() == 1)
        return A;
    llvm::SmallVector<Rational, 8> buff;
    buff.resize_for_overwrite(A.numCol());
    for (size_t i = 1; i < A.numRow(); ++i) {
        for (size_t j = 0; j < A.numCol(); ++j)
            buff[j] = A(i, j);
        for (size_t j = 0; j < i; ++j) {
            int64_t n = 0;
            int64_t d = 0;
            for (size_t k = 0; k < A.numCol(); ++k) {
                n += A(i, k) * A(j, k);
                d += A(j, k) * A(j, k);
            }
            for (size_t k = 0; k < A.numCol(); ++k) {
                buff[k] -= Rational::createPositiveDenominator(A(j, k) * n, d);
            }
        }
        int64_t lm = 1;
        for (size_t k = 0; k < A.numCol(); ++k)
            lm = lcm(lm, buff[k].denominator);
        for (size_t k = 0; k < A.numCol(); ++k)
            A(i, k) = buff[k].numerator * (lm / buff[k].denominator);
    }
    return A;
}

IntMatrix orthogonalNullSpace(IntMatrix A) {
    return orthogonalize(NormalForm::nullSpace(std::move(A)));
    // IntMatrix NS{NormalForm::nullSpace(std::move(A))};
    // std::cout << "Pre-Orth NS =\n" << NS << std::endl;
    // IntMatrix ONS{orthogonalize(std::move(NS))};
    // std::cout << "Post-Orth NS =\n" << ONS << std::endl;
    // return ONS;
}

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
    // std::cout << "S = \n" << S << "\nK =\n" << K << std::endl;
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
