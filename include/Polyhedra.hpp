
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
#include <llvm/Analysis/ScalarEvolution.h>
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
template <MaybeMatrix<int64_t> I64Matrix, Comparator CmptrType, bool NonNegative>
struct Polyhedra {
    // order of vars:
    // constants, loop vars, symbolic vars
    // this is because of hnf prioritizing diagonalizing leading rows
    [[no_unique_address]] IntMatrix A;
    [[no_unique_address]] I64Matrix E;
    [[no_unique_address]] CmptrType C;

    Polyhedra() = default;
    Polyhedra(IntMatrix Ain)
        : A(std::move(Ain)), E{}, C(LinearSymbolicComparator::construct(A)){};
    Polyhedra(IntMatrix Ain, I64Matrix Ein)
        : A(std::move(Ain)), E(std::move(Ein)),
          C(LinearSymbolicComparator::construct(A)){};

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

    void moveEqualities(IntMatrix &Aold, I64Matrix &Eold) const {
        ::moveEqualities(Aold, Eold, C);
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
        auto &&os2 = printConstraints(os << "\n", p.A,
                                      llvm::ArrayRef<const llvm::SCEV *>());
        if constexpr (hasEqualities)
            return printConstraints(
                os2, p.E, llvm::ArrayRef<const llvm::SCEV *>(), false);
        return os2;
    }
    void dump() const { llvm::errs() << *this; }
    bool isEmpty() const {
        if (A.numRow() == 0)
            return true;
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
};

typedef Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator>
    SymbolicPolyhedra;
typedef Polyhedra<IntMatrix, LinearSymbolicComparator> SymbolicEqPolyhedra;
