#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Permutation.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>

// A' * i <= b
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest : SymbolicPolyhedra,
                        llvm::RefCountedBase<AffineLoopNest> {
    Permutation perm; // maps current to orig
    llvm::SmallVector<IntMatrix, 0> remainingA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> remainingB;
    llvm::SmallVector<IntMatrix, 0> lowerA;
    llvm::SmallVector<IntMatrix, 0> upperA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> lowerb;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> upperb;

    int64_t currentToOriginalPerm(size_t i) const { return perm(i); }

    size_t getNumLoops() const { return getNumVar(); }
    AffineLoopNest(IntMatrix Ain, llvm::SmallVector<MPoly, 8> bin,
                   PartiallyOrderedSet posetin)
        : SymbolicPolyhedra(std::move(Ain), std::move(bin), std::move(posetin)),
          perm(A.numCol()), remainingA(A.numCol()), remainingB(A.numCol()),
          lowerA(A.numCol()), upperA(A.numCol()), lowerb(A.numCol()),
          upperb(A.numCol()) {
        size_t numLoops = getNumLoops();
        size_t i = numLoops;
        pruneBounds(A, b);
        remainingA[i - 1] = A;
        remainingB[i - 1] = b;
        do {
            calculateBounds(--i);
        } while (i);
    }
    void categorizeBoundsCache(const IntMatrix &A,
                               const llvm::SmallVectorImpl<MPoly> &b,
                               size_t i) {

        categorizeBounds(lowerA[i], upperA[i], lowerb[i], upperb[i], A, b, i);
    }

    void swap(size_t _i, size_t _j) {
        if (_i == _j) {
            return;
        }
        perm.swap(_i, _j);
        for (int64_t i = std::max(_i, _j); i >= int64_t(std::min(_i, _j));
             --i) {
            calculateBounds(i);
        }
    }
    void calculateBounds0() {
        const size_t i = perm(0);
        const auto [numNeg, numPos] = countNonZeroSign(remainingA[0], i);
        if ((numNeg > 1) | (numPos > 1)) {
            IntMatrix Aold = remainingA[0];
            llvm::SmallVector<MPoly, 8> bold = remainingB[0];
            categorizeBoundsCache(Aold, bold, i);
        } else {
            categorizeBoundsCache(remainingA[0], remainingB[0], i);
            return;
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
        auto &Aold = remainingA[_i - 1];
        remainingB[_i - 1] = remainingB[_i];
        auto &bold = remainingB[_i - 1];
        removeVariable(lowerA[i], upperA[i], lowerb[i], upperb[i], Aold, bold,
                       i);
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
        auto b = remainingB.back();
        IntMatrix lwrA;
        IntMatrix uprA;
        llvm::SmallVector<MPoly, 16> lwrB;
        llvm::SmallVector<MPoly, 16> uprB;
        IntMatrix Atmp0, Atmp1, Etmp;
        llvm::SmallVector<MPoly, 16> btmp0, btmp1, qtmp;
        for (size_t _k = 0; _k < _j; ++_k) {
            if (_k != _i) {
                size_t k = perm(_k);
                categorizeBounds(lwrA, uprA, lwrB, uprB, A, b, k);
                deleteBounds(A, b, k);
                appendBounds(lwrA, uprA, lwrB, uprB, Atmp0, Atmp1, Etmp, btmp0,
                             btmp1, qtmp, A, b, k, Polynomial::Val<false>());
            }
        }
        IntMatrix Anew;
        llvm::SmallVector<MPoly, 8> bnew;
        size_t i = perm(_i);
        do {
            // `A` and `b` contain representation independent of `0..._j`,
            // except for `_i`
            size_t j = perm(_j);
            Anew = A;
            bnew = b;
            for (size_t _k = _i + 1; _k < numLoops; ++_k) {
                if (_k != _j) {
                    size_t k = perm(_k);
                    // eliminate
                    categorizeBounds(lwrA, uprA, lwrB, uprB, Anew, bnew, k);
                    deleteBounds(Anew, bnew, k);
                    appendBounds(lwrA, uprA, lwrB, uprB, Atmp0, Atmp1, Etmp,
                                 btmp0, btmp1, qtmp, Anew, bnew, k,
                                 Polynomial::Val<false>());
                }
            }
            // now depends only on `j` and `i`
            // check if we have zero iterations on loop `j`
            // pruneBounds(Anew, bnew, j);
            size_t numRows = Anew.numRow();
            for (size_t l = 0; l < numRows; ++l) {
                int64_t Ajl = Anew(l, j);
                if (Ajl >= 0) {
                    // then it is not a lower bound
                    continue;
                }
                int64_t Ail = Anew(l, i);
                for (size_t u = 0; u < numRows; ++u) {
                    int64_t Aju = Anew(u, j);
                    if (Aju <= 0) {
                        // then it is not an upper bound
                        continue;
                    }
                    int64_t Aiu = Anew(u, i);
                    int64_t c = Ajl * Aiu - Aju * Ail;
                    auto delta = bnew[l] * Aju;
                    Polynomial::fnmadd(delta, bnew[u], Ajl);
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
                                auto idelta = ail * delta;
                                Polynomial::fnmadd(idelta, bnew[il], -c);
                                // let e = 1
                                idelta -= (c * ail + 1);
                                if (knownGreaterEqualZero(idelta)) {
                                    return true;
                                } else {
                                    doesNotIterate = false;
                                }
                            }
                            if (doesNotIterate) {
                                return true;
                            }
                        } else {
                            continue;
                        }

                    } else {
                        // extend upper
                        if (c < 0) {
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
                                auto idelta = (-ail) * delta;
                                Polynomial::fnmadd(idelta, bnew[il], c);
                                idelta -= (c * ail - 1);
                                if (knownGreaterEqualZero(idelta)) {
                                    return true;
                                } else {
                                    doesNotIterate = false;
                                }
                            }
                            if (doesNotIterate) {
                                return true;
                            }
                        } else {
                            continue;
                        }
                    }
                }
            }
            ++_j;
        } while (_j != numLoops);
        return false;
    }

    static void printBound(std::ostream &os, const IntMatrix &A,
                           const llvm::SmallVector<MPoly, 8> &b, size_t i,
                           int64_t sign) {

        size_t numVar = A.numCol();
        for (size_t j = 0; j < b.size(); ++j) {
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
            bool printed = !isZero(b[j]);
            if (printed) {
                os << sign * b[j];
            }
            for (size_t k = 0; k < numVar; ++k) {
                if (k == i) {
                    continue;
                }
                if (int64_t lakj = A(j, k)) {
                    if (lakj * sign > 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    lakj = std::abs(lakj);
                    if (lakj != 1) {
                        os << lakj << "*";
                    }
                    os << "i_" << k;
                    printed = true;
                }
            }
            if (!printed) {
                os << 0;
            }
            os << std::endl;
        }
    }
    void printLowerBound(std::ostream &os, size_t i) const {
        printBound(os, lowerA[i], lowerb[i], i, -1);
    }
    void printUpperBound(std::ostream &os, size_t i) const {
        printBound(os, upperA[i], upperb[i], i, 1);
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
