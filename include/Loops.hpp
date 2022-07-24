#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Permutation.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include "Comparators.hpp"
#include "Constraints.hpp"
#include "EmptyArrays.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>

// A' * i <= b
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest : Polyhedra<EmptyMatrix<int64_t>, SymbolicComparator>,
                        llvm::RefCountedBase<AffineLoopNest> {
    Permutation perm; // maps current to orig
    llvm::SmallVector<IntMatrix, 0> remainingA;
    llvm::SmallVector<IntMatrix, 0> lowerA;
    llvm::SmallVector<IntMatrix, 0> upperA;

    int64_t currentToOriginalPerm(size_t i) const { return perm(i); }

    static std::pair<IntMatrix, llvm::SmallVector<Polynomial::Monomial>>
    toMatrixRepr(const IntMatrix &A, const PartiallyOrderedSet &poset) {
	std::pair<IntMatrix, llvm::SmallVector<Polynomial::Monomial>> ret;
        IntMatrix &B = ret.first;
        llvm::SmallVector<Polynomial::Monomial> &monomials = ret.second;
	
	
        return ret;
    }

    size_t getNumLoops() const { return getNumVar(); }
    AffineLoopNest(const IntMatrix &Ain, llvm::ArrayRef<MPoly> b,
                   PartiallyOrderedSet posetin)
        : Polyhedra{.A = std::move(Ain),
                    .E = EmptyMatrix<int64_t>{},
                    .C = SymbolicComparator::construct(b, std::move(posetin))},
          perm(A.numCol()), remainingA(A.numCol()), lowerA(A.numCol()),
          upperA(A.numCol()) {
        size_t numLoops = getNumLoops();
        size_t i = numLoops;
        pruneBounds(A);
        remainingA[i - 1] = A;
        do {
            calculateBounds(--i);
        } while (i);
    }
    AffineLoopNest(IntMatrix Ain, SymbolicComparator C)
        : Polyhedra<EmptyMatrix<int64_t>,
                    SymbolicComparator>{.A = std::move(Ain),
                                        .E = EmptyMatrix<int64_t>{},
                                        .C = std::move(C)},
          perm(A.numCol()), remainingA(A.numCol()), lowerA(A.numCol()),
          upperA(A.numCol()) {
        size_t numLoops = getNumLoops();
        size_t i = numLoops;
        pruneBounds(A);
        remainingA[i - 1] = A;
        do {
            calculateBounds(--i);
        } while (i);
    }
    void categorizeBoundsCache(const IntMatrix &A, size_t i) {

        categorizeBounds(lowerA[i], upperA[i], A, i);
    }

    void swap(size_t _i, size_t _j) {
        if (_i == _j)
            return;
        perm.swap(_i, _j);
        for (int64_t i = std::max(_i, _j); i >= int64_t(std::min(_i, _j)); --i)
            calculateBounds(i);
    }
    void calculateBounds0() {
        const size_t i = perm(0);
        const auto [numNeg, numPos] = countNonZeroSign(remainingA[0], i);
        if ((numNeg > 1) | (numPos > 1)) {
            IntMatrix Aold = remainingA[0];
            categorizeBoundsCache(Aold, i);
        } else {
            categorizeBoundsCache(remainingA[0], i);
        }
    }
    // `_i` is w/ respect to current order, `i` for original order.
    void calculateBounds(const size_t _i) {
        if (_i == 0) {
            return calculateBounds0();
        }
        const size_t i = perm(_i);
        // const size_t iNext = perm(_i - 1);
        remainingA[_i - 1] = remainingA[_i];
        removeVariable(lowerA[i], upperA[i], remainingA[_i - 1], i);
    }
    // returns true if extending (extendLower ? lower : upper) bound of `_i`th
    // loop by `extend` doesn't result in the innermost loop experiencing any
    // extra iterations.
    // if `extendLower`, `min(i) - extend`
    // else `max(i) + extend`
    bool zeroExtraIterationsUponExtending(size_t _i, bool extendLower) const {
        size_t _j = _i + 1;
        const size_t numLoops = getNumLoops();
        if (_j == numLoops) {
            return false;
        }
        // eliminate variables 0..._j
        auto A = remainingA.back();
        IntMatrix lwrA;
        IntMatrix uprA;
        IntMatrix Atmp0, Atmp1, Etmp;
        for (size_t _k = 0; _k < _j; ++_k)
            if (_k != _i)
                fourierMotzkin(A, perm(_k));
        IntMatrix Anew;
        size_t i = perm(_i);
        llvm::SmallVector<int64_t> delta, idelta;
        delta.resize_for_overwrite(C.numConstantTerms());
        idelta.resize_for_overwrite(C.numConstantTerms());
        do {
            // `A` and `b` contain representation independent of `0..._j`,
            // except for `_i`
            size_t j = perm(_j);
            Anew = A;
            for (size_t _k = _i + 1; _k < numLoops; ++_k)
                if (_k != _j)
                    // eliminate
                    fourierMotzkin(Anew, perm(_k));
            // now depends only on `j` and `i`
            // check if we have zero iterations on loop `j`
            // pruneBounds(Anew, bnew, j);
            size_t numRows = Anew.numRow();
            for (size_t l = 0; l < numRows; ++l) {
                int64_t Ajl = Anew(l, j);
                if (Ajl >= 0)
                    // then it is not a lower bound
                    continue;
                int64_t Ail = Anew(l, i);
                for (size_t u = 0; u < numRows; ++u) {
                    int64_t Aju = Anew(u, j);
                    if (Aju <= 0)
                        // then it is not an upper bound
                        continue;
                    int64_t Aiu = Anew(u, i);
                    int64_t c = Ajl * Aiu - Aju * Ail;
                    for (size_t i = 0; i < delta.size(); ++i)
                        delta[i] = Anew(l, i) * Aju - Anew(u, i) * Ajl;
                    // delta + c * i >= 0 -> iterates at least once
                    if (extendLower) {
                        if (c > 0) {
                            bool doesNotIterate = true;
                            // we're adding to the lower bound
                            for (size_t il = 0; il < numRows; ++il) {
                                int64_t ail = Anew(il, i);
                                if ((ail >= 0) || (Anew(il, j) != 0)) {
                                    // (ail >= 0) means not a lower bound
                                    // (Anew(il, j) != 0) means the lower bound
                                    // is a function of `j` if we're adding
                                    // beyond what `j` defines as the bound
                                    // here, then `j` won't undergo extra
                                    // iterations, due to being sandwiched
                                    // between this bound, and whatever bound it
                                    // was that defines the extrema we're adding
                                    // to here.
                                    continue;
                                }
                                // recall: ail < 0
                                //
                                // ail * i <= bnew[il]
                                // i >= bnew[il] / ail
                                //
                                // ail * (i - e + e) <= bnew[il]
                                // ail * (i - e) <= bnew[il] - ail*e
                                // (i - e) >= (bnew[il] - ail*e) / ail
                                ///
                                // we want to check
                                // delta + c * (i - e) >= 0
                                // ail * (delta + c * (i - e)) <= 0
                                // ail*delta + c*(ail * (i - e)) <= 0
                                //
                                // let c = c, for brevity
                                //
                                // c*ail*(i-e) <= c*(bnew[il] - ail*e)
                                // we also wish to check
                                // ail*delta + s*c*(ail*(i-e)) <= 0
                                //
                                // ail*delta + c*(ail*(i-e)) <=
                                // ail*delta + c*(bnew[il] - ail*e)
                                // thus, if
                                // ail*delta + c*(bnew[il] - ail*e) <= 0
                                // the loop iterates at least once
                                // we'll check if it is known that this is
                                // false, i.e. if
                                //
                                // ail*delta + c*(bnew[il] - ail*e) - 1 >= 0
                                //
                                // then the following must be
                                // false:
                                // ail*delta + c*(bnew[il] - ail*e) - 1 <= -1
                                for (size_t i = 0; i < delta.size(); ++i)
                                    idelta[i] =
                                        (-ail) * delta[i] - Anew(il, i) * c;
                                idelta[0] -= (c * ail - 1);
                                if (C.greaterEqual(idelta))
                                    return true;
                                doesNotIterate = false;
                            }
                            if (doesNotIterate)
                                return true;
                        } else {
                            continue;
                        }

                    } else {
                        if (c >= 0)
                            continue;
                        // extend upper
                        bool doesNotIterate = true;
                        // does `imax + e` iterate at least once?
                        for (size_t il = 0; il < numRows; ++il) {
                            int64_t ail = Anew(il, i);
                            if ((ail <= 0) || (Anew(il, j) != 0)) {
                                // not an upper bound
                                continue;
                            }
                            // ail > 0, c < 0
                            // ail * i <= ubi
                            // c*ail*i >= c*ubi
                            // c*ail*(i+e-e) >= c*ubi
                            // c*ail*(i+e) >= c*ubi + c*ail*e
                            //
                            // iterates at least once if:
                            // delta + c * (i+e) >= 0
                            // ail*(delta + c * (i+e)) >= 0
                            // ail*delta + ail*c*(i+e) >= 0
                            // note
                            // ail*delta + ail*c*(i+e) >=
                            // ail*delta + c*ubi + c*ail+e
                            // so proving
                            // ail*delta + c*ubi + c*ail+e >= 0
                            // proves the loop iterates at least once
                            // we check if this is known to be false, i.e.
                            // if this is known to be true:
                            // - ail*delta - c*ubi - c*ail+e -1 >= 0
                            for (size_t i = 0; i < delta.size(); ++i)
                                idelta[i] = (-ail) * delta[i] - Anew(il, i) * c;
                            idelta[0] -= (c * ail - 1);
                            if (C.greaterEqual(idelta))
                                return true;
                            doesNotIterate = false;
                        }
                        if (doesNotIterate)
                            return true;
                    }
                }
            }
            ++_j;
        } while (_j != numLoops);
        return false;
    }

    void printBound(std::ostream &os, const IntMatrix &A, size_t i,
                    int64_t sign) const {

        size_t numVar = A.numCol();
        for (size_t j = 0; j < A.numRow(); ++j) {
            if (A(j, i) == sign) {
                if (sign < 0) {
                    os << "i_" << i << " >= ";
                } else {
                    os << "i_" << i << " <= ";
                }
            } else if (sign < 0) {
                os << sign * A(j, i) << "*i_" << i << " >= ";
            } else {
                os << sign * A(j, i) << "*i_" << i << " <= ";
            }
            llvm::ArrayRef<int64_t> b = getSymbol(A, j);
            bool printed = !allZero(b);
            if (printed)
                C.printSymbol(os, b, sign);
            for (size_t k = 0; k < numVar; ++k) {
                if (k == i)
                    continue;
                if (int64_t lakj = A(j, k)) {
                    if (lakj * sign > 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    lakj = std::abs(lakj);
                    if (lakj != 1)
                        os << lakj << "*";
                    os << "i_" << k;
                    printed = true;
                }
            }
            if (!printed)
                os << 0;
            os << std::endl;
        }
    }
    void printLowerBound(std::ostream &os, size_t i) const {
        printBound(os, lowerA[i], i, -1);
    }
    void printUpperBound(std::ostream &os, size_t i) const {
        printBound(os, upperA[i], i, 1);
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const AffineLoopNest &alnb) {
        const size_t numLoops = alnb.getNumVar();
        for (size_t _i = 0; _i < numLoops; ++_i) {
            os << "Variable " << _i << " lower bounds: " << std::endl;
            size_t i = alnb.currentToOriginalPerm(_i);
            alnb.printLowerBound(os, i);
            os << "Variable " << _i << " upper bounds: " << std::endl;
            alnb.printUpperBound(os, i);
        }
        return os;
    }
    void dump() const { std::cout << *this; }
};
