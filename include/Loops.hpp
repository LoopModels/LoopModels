#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Permutation.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>



// A' * i <= r
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest : SymbolicPolyhedra {
    Permutation perm; // maps current to orig
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> remainingA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> remainingB;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> lowerA;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> upperA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> lowerb;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> upperb;

    intptr_t currentToOriginalPerm(size_t i) const { return perm(i); }

    size_t getNumLoops() const { return getNumVar(); }
    AffineLoopNest(Matrix<intptr_t, 0, 0, 0> Ain,
                   llvm::SmallVector<MPoly, 8> bin, PartiallyOrderedSet posetin)
        : SymbolicPolyhedra(std::move(Ain), std::move(bin), std::move(posetin)),
          perm(A.size(0)), remainingA(A.size(0)), remainingB(A.size(0)),
          lowerA(A.size(0)), upperA(A.size(0)), lowerb(A.size(0)),
          upperb(A.size(0)) {
        size_t numLoops = getNumLoops();
        size_t i = numLoops;
        pruneBounds(A, b);
        remainingA[i - 1] = A;
        remainingB[i - 1] = b;
        do {
            calculateBounds(--i);
        } while (i);
    }
    void categorizeBoundsCache(const Matrix<intptr_t, 0, 0, 0> &A,
                               const llvm::SmallVectorImpl<MPoly> &b,
                               size_t i) {

        categorizeBounds(lowerA[i], upperA[i], lowerb[i], upperb[i], A, b, i);
    }

    void swap(size_t _i, size_t _j) {
        if (_i == _j) {
            return;
        }
        perm.swap(_i, _j);
        for (intptr_t i = std::max(_i, _j); i >= intptr_t(std::min(_i, _j));
             --i) {
            calculateBounds(i);
        }
    }
    void calculateBounds0() {
        const size_t i = perm(0);
        const auto [numNeg, numPos] = countNonZeroSign(remainingA[0], i);
        if ((numNeg > 1) | (numPos > 1)) {
            Matrix<intptr_t, 0, 0, 0> Aold = remainingA[0];
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
        Matrix<intptr_t, 0, 0, 0> lwrA;
        Matrix<intptr_t, 0, 0, 0> uprA;
        llvm::SmallVector<MPoly, 16> lwrB;
        llvm::SmallVector<MPoly, 16> uprB;
        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, Etmp;
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
        Matrix<intptr_t, 0, 0, 0> Anew;
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
            size_t numCols = Anew.size(1);
            for (size_t l = 0; l < numCols; ++l) {
                intptr_t Ajl = Anew(j, l);
                if (Ajl >= 0) {
                    // then it is not a lower bound
                    continue;
                }
                intptr_t Ail = Anew(i, l);
                for (size_t u = 0; u < numCols; ++u) {
                    intptr_t Aju = Anew(j, u);
                    if (Aju <= 0) {
                        // then it is not an upper bound
                        continue;
                    }
                    intptr_t Aiu = Anew(i, u);
                    intptr_t c = Ajl * Aiu - Aju * Ail;
                    auto delta = bnew[l] * Aju;
                    Polynomial::fnmadd(delta, bnew[u], Ajl);
                    // delta + c * i >= 0 -> iterates at least once
                    if (extendLower) {
                        if (c > 0) {
                            bool doesNotIterate = true;
                            // we're adding to the lower bound
                            for (size_t il = 0; il < numCols; ++il) {
                                intptr_t ail = Anew(i, il);
                                if ((ail >= 0) || (Anew(j, il) != 0)) {
                                    // (ail >= 0) means not a lower bound
                                    // (Anew(j, il) != 0) means the lower bound
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
                            for (size_t il = 0; il < numCols; ++il) {
                                intptr_t ail = Anew(i, il);
                                if ((ail <= 0) || (Anew(j, il) != 0)) {
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

    void printLowerBound(std::ostream &os, size_t i) const {
        auto &lA = lowerA[i];
        auto &lb = lowerb[i];
        for (size_t j = 0; j < lb.size(); ++j) {
            if (lA(i, j) == -1) {
                os << "i_" << i << " >= ";
            } else {
                os << -lA(i, j) << "*i_" << i << " >= ";
            }
            bool printed = !isZero(lb[j]);
            if (printed) {
                os << -lb[j];
            }
            for (size_t k = 0; k < getNumVar(); ++k) {
                if (k == i) {
                    continue;
                }
                if (intptr_t lakj = lA(k, j)) {
                    if (lakj < 0) {
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
    void printUpperBound(std::ostream &os, size_t i) const {
        auto &uA = upperA[i];
        auto &ub = upperb[i];
        for (size_t j = 0; j < ub.size(); ++j) {
            if (uA(i, j) == 1) {
                os << "i_" << i << " <= ";
            } else {
                os << uA(i, j) << "*i_" << i << " <= ";
            }
            bool printed = (!isZero(ub[j]));
            if (printed) {
                os << ub[j];
            }
            for (size_t k = 0; k < getNumVar(); ++k) {
                if (k == i) {
                    continue;
                }
                if (intptr_t uakj = uA(k, j)) {
                    if (uakj > 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    uakj = std::abs(uakj);
                    if (uakj != 1) {
                        os << uakj << "*";
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
