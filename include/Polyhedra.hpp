
#pragma once

#include "./Comparators.hpp"
#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Simplex.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <sys/types.h>
#include <type_traits>

// Can we represent Polyhedra using slack variables + equalities?
// What must we do with Polyhedra?
// 1) A*x >= 0 && c'x >= 0 <-> l_0 + l'Ax == c'x && l >= 0 && l_0 >= 0
// 2) pruning bounds

// For "1)", we'd need to recover inequalities from slack vars.
// How does moving through solutions work with a mix of non-negative and
// unbounded variables?
// i <= j - 1
// j <= J - 1
// i <= J - 1
//
// for fun, lower bounds are -2
// i >= -2
// j >= -2
// and we have symbolic J
//  c  J  i  j s0 s1 s2 s3 s4
// -1  0  1 -1  1  0  0  0  0
// -1  1  0  1  0  1  0  0  0
// -1  1  1  0  0  0  1  0  0
// -2  0  1  0  0  0  0 -1  0
// -2  0  0  1  0  0  0  0 -1
// How confident can we be about arbitrary combinations of variables vs 0 for
// comparisons?

// A*x >= 0
// representation is
// A[:,0] + A[:,1:s.size()]*s + A[:,1+s.size():end]*x >= 0
// E[:,0] + E[:,1:s.size()]*s + E[:,1+s.size():end]*x == 0
// where `s` is the vector of symbolic variables.
// These are treated as constants, and clearly separated from the dynamically
// varying values `x`.
// We have `A.numRow()` inequality constraints and `E.numRow()` equality
// constraints.
//
template <MaybeMatrix<int64_t> I64Matrix, Comparator CmptrType>
struct Polyhedra {
    // order of vars:
    // constants, loop vars, symbolic vars
    // this is because of hnf prioritizing diagonalizing leading rows
    [[no_unique_address]] IntMatrix A;
    [[no_unique_address]] I64Matrix E;
    [[no_unique_address]] CmptrType C;

    void pruneBounds() {
        Vector<int64_t> diff{A.numCol()};
        if constexpr (hasEqualities)
            removeRedundantRows(A, E);
        // NormalForm::simplifySystem(E, 1);
        for (size_t j = A.numRow(); j;) {
            for (size_t i = --j; i;) {
                if (A.numRow() <= 1)
                    return;
                diff = A(--i, _) - A(j, _);
                if (C.greaterEqual(diff)) {
                    eraseConstraint(A, i);
                    C.init(A, E);
                    --j; // `i < j`, and `i` has been removed
                } else if (C.greaterEqual(diff *= -1)) {
                    eraseConstraint(A, j);
                    C.init(A, E);
                    break; // `j` is gone
                }
            }
        }
    }

    size_t getNumVar() const { return A.numCol() - 1; }
    size_t getNumInequalityConstraints() const { return A.numRow(); }
    size_t getNumEqualityConstraints() const { return E.numRow(); }

    static constexpr bool hasEqualities =
        !std::is_same_v<I64Matrix, EmptyMatrix<int64_t>>;

    bool lessZero(const IntMatrix &A, const size_t r) const {
        return C.less(A(r, _));
    }
    bool lessEqualZero(const IntMatrix &A, const size_t r) const {
        return C.lessEqual(A(r, _));
    }
    bool greaterZero(const IntMatrix &A, const size_t r) const {
        return C.greater(A(r, _));
    }
    bool greaterEqualZero(const IntMatrix &A, const size_t r) const {
        return C.greaterEqual(A(r, _));
    }
    bool lessZero(const size_t r) const { return C.less(A(r, _)); }
    bool lessEqualZero(const size_t r) const { return C.lessEqual(A(r, _)); }
    bool greaterZero(const size_t r) const { return C.greater(A(r, _)); }
    bool greaterEqualZero(const size_t r) const {
        return C.greaterEqual(A(r, _));
    }

    bool equalNegative(const size_t i, const size_t j) const {
        return C.equalNegative(A(i, _), A(j, _));
    }
    bool equalNegative(const IntMatrix &A, const size_t i,
                       const size_t j) const {
        return C.equalNegative(A(i, _), A(j, _));
    }

    static bool uniqueConstraint(PtrMatrix<int64_t> A, size_t C) {
        for (size_t c = 0; c < C; ++c) {
            bool allEqual = true;
            for (size_t r = 0; r < A.numCol(); ++r)
                allEqual &= (A(c, r) == A(C, r));
            if (allEqual)
                return false;
        }
        return true;
    }

    static std::pair<size_t, size_t> countSigns(PtrMatrix<int64_t> A,
                                                size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        for (size_t j = 0; j < A.numRow(); ++j) {
            int64_t Aij = A(j, i);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    void moveEqualities(IntMatrix &Aold, I64Matrix &Eold) const {
        ::moveEqualities(Aold, Eold, C);
    }
    // returns std::make_pair(numNeg, numPos);
    // countNonZeroSign(Matrix A, i)
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    // static void categorizeBounds(IntMatrix &lA, IntMatrix &uA,
    //                              PtrMatrix<int64_t> A, size_t i) {
    //     auto [numConstraints, numLoops] = A.size();
    //     const auto [numNeg, numPos] = countNonZeroSign(A, i);
    //     lA.resize(numNeg, numLoops);
    //     uA.resize(numPos, numLoops);
    //     // fill bounds
    //     for (size_t j = 0, l = 0, u = 0; j < numConstraints; ++j) {
    //         if (int64_t Aij = A(j, i)) {
    //             if (Aij > 0) {
    //                 for (size_t k = 0; k < numLoops; ++k)
    //                     uA(u, k) = A(j, k);
    //             } else {
    //                 for (size_t k = 0; k < numLoops; ++k)
    //                     lA(l, k) = A(j, k);
    //             }
    //         }
    //     }
    // }
    inline static bool equalsNegative(llvm::ArrayRef<int64_t> x,
                                      llvm::ArrayRef<int64_t> y) {
        assert(x.size() == y.size());
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] + y[i])
                return false;
        return true;
    }
    // returns `false` if not violated, `true` if violated
    void deleteBounds(IntMatrix &A, size_t i) const {
        for (size_t j = A.numRow(); j != 0;)
            if (A(--j, i))
                eraseConstraint(A, j);
    }
    // A'x <= b
    // removes variable `i` from system
    void removeVariable(IntMatrix &A, const size_t i) { fourierMotzkin(A, i); }
    // A'x <= b
    // E'x = q
    // removes variable `i` from system
    void removeVariable(IntMatrix &A, IntMatrix &E, const size_t i) {
        if (substituteEquality(A, E, i))
            fourierMotzkin(A, i);
        if (E.numRow() > 1)
            NormalForm::simplifySystem(E);
        pruneBounds(A, E);
    }

    void removeVariable(const size_t i) {
        if constexpr (hasEqualities)
            return removeVariable(A, E, i);
        removeVariable(A, i);
    }
    void removeVariableAndPrune(const size_t i) {
        if constexpr (hasEqualities) {
            removeVariable(A, E, i);
        } else {
            removeVariable(A, i);
        }
        pruneBounds();
    }

    void dropEmptyConstraints(IntMatrix &A) const {
        for (size_t c = A.numRow(); c != 0;)
            if (allZero(A(--c, _)))
                eraseConstraint(A, c);
    }
    void dropEmptyConstraints() {
        dropEmptyConstraints(A);
        if constexpr (hasEqualities)
            dropEmptyConstraints(E);
    }

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const Polyhedra &p) {
        auto &&os2 = printConstraints(os << "\n", p.A, p.C.getNumConstTerms());
        if constexpr (hasEqualities)
            return printConstraints(os2, p.E, p.C.getNumConstTerms(), false);
        return os2;
    }
    void dump() const { llvm::errs() << *this; }
    bool isEmpty() const {
        for (size_t r = 0; r < A.numRow(); ++r)
            if (C.less(A(r, _)))
                return true;
        return false;
    }
    void truncateVars(size_t numVar) {
        if constexpr (hasEqualities)
            E.truncateCols(numVar);
        A.truncateCols(numVar);
    }
    // A*x >= 0
    // bool knownSatisfied(llvm::ArrayRef<int64_t> x) const {
    //     int64_t bc;
    //     size_t numVar = std::min(x.size(), getNumVar());
    //     for (size_t c = 0; c < getNumInequalityConstraints(); ++c) {
    //         bc = A(c, 0);
    //         for (size_t v = 0; v < numVar; ++v)
    //             bc -= A(c, v + 1) * x[v];
    //         if (bc < 0)
    //             return false;
    //     }
    //     return true;
    // }
};

typedef Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator>
    SymbolicPolyhedra;
typedef Polyhedra<IntMatrix, LinearSymbolicComparator> SymbolicEqPolyhedra;

/*
template <MaybeMatrix I64Matrix>
struct SymbolicPolyhedra : public Polyhedra<I64Matrix> {
    llvm::SmallVector<Polynomial::Monomial> monomials;
    SymbolicPolyhedra(IntMatrix A, I64Matrix E, llvm::ArrayRef<MPoly> b,
                      const PartiallyOrderedSet &poset)
        : Polyhedra<I64Matrix>(std::move(A), std::move(E)), monomials({}) {
        // use poset
        // monomials[{unsigned}_1] >= monomials[{unsigned}_2] + {int64_t}_1
        llvm::SmallVector<std::tuple<int64_t, unsigned, unsigned>> cmps;
        for (auto &bi : b) {
            for (auto &t : bi) {
                if (t.isCompileTimeConstant())
                    continue;
                bool dontPushBack = false;
                const Polynomial::Monomial &tm = t.exponent;
                for (auto &m : monomials) {
                    if (m == tm) {
                        dontPushBack = true;
                        break;
                    }
                }
                if (dontPushBack)
                    continue;
                for (size_t i = 0; i < monomials.size(); ++i) {
                    Polynomial::Monomial &m = monomials[i];
                    // loop inner triangle
                    if ((m.degree() == 1) && (t.degree() == 1)) {
                        // we can do a direct lookup
                        // interval on t - m
                        const Interval itv = poset(m.prodIDs.front().getID(),
                                                   tm.prodIDs.front().getID());
                        if ((itv.lowerBound >
                             (std::numeric_limits<int64_t>::min() >> 2)) &&
                            (itv.lowerBound <
                             (std::numeric_limits<int64_t>::max() >> 2))) {
                            // t - m >= lowerBound
                            // t >= m + lowerBound
                            cmps.emplace_back(itv.lowerBound, monomials.size(),
                                              i);
                        }
                        if ((itv.upperBound >
                             (std::numeric_limits<int64_t>::min() >> 2)) &&
                            (itv.upperBound <
                             (std::numeric_limits<int64_t>::max() >> 2))) {
                            // t - m <= upperBound
                            // m >= t + -upperBound
                            cmps.emplace_back(-itv.upperBound, i,
                                              monomials.size());
                        }
                    } else {
                        auto [itvt, itvm] = poset.unmatchedIntervals(tm, m);
                        // TODO: tighten by using matched to
                        // get less conservative bounds
                        if (itvt.knownGreaterEqual(itvm)) {
                            cmps.emplace_back(0, monomials.size(), i);
                        } else if (itvm.knownGreaterEqual(itvt)) {
                            cmps.emplace_back(0, i, monomials.size());
                        }
                    }
                }
                monomials.push_back(tm);
            }
        }
        // order of vars:
        // constants, loop vars, symbolic vars
        // this is because of hnf prioritizing diagonalizing leading rows
        const size_t origNumRow = A.numRow();
        const size_t origNumCol = A.numCol();
        const size_t numSymbolicVars = monomials.size();
        A.resize(origNumRow + cmps.size(), origNumCol + numSymbolicVars + 1);
    };
};
*/
