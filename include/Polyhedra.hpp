#pragma once

#include "./Comparators.hpp"
#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "VectorGreatestCommonDivisor.hpp"
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

inline auto printPositive(llvm::raw_ostream &os, size_t stop)
  -> llvm::raw_ostream & {
  for (size_t i = 0; i < stop; ++i)
    os << "v_" << i << " >= 0\n";
  return os;
}

/// Can we represent Polyhedra using slack variables + equalities?
/// What must we do with Polyhedra?
/// 1) A*x >= 0 && c'x >= 0 <-> l_0 + l'Ax == c'x && l >= 0 && l_0 >= 0
/// 2) pruning bounds
///
/// For "1)", we'd need to recover inequalities from slack vars.
/// How does moving through solutions work with a mix of non-negative and
/// unbounded variables?
/// i <= j - 1
/// j <= J - 1
/// i <= J - 1
///
/// for fun, lower bounds are -2
/// i >= -2
/// j >= -2
/// and we have symbolic J
///  c  J  i  j s0 s1 s2 s3 s4
/// -1  0  1 -1  1  0  0  0  0
/// -1  1  0  1  0  1  0  0  0
/// -1  1  1  0  0  0  1  0  0
/// -2  0  1  0  0  0  0 -1  0
/// -2  0  0  1  0  0  0  0 -1
/// How confident can we be about arbitrary combinations of variables vs 0 for
/// comparisons?
///
/// A*x >= 0
/// representation is
/// A[:,0] + A[:,1:s.size()]*s + A[:,1+s.size():end]*x >= 0
/// E[:,0] + E[:,1:s.size()]*s + E[:,1+s.size():end]*x == 0
/// where `s` is the vector of symbolic variables.
/// These are treated as constants, and clearly separated from the dynamically
/// varying values `x`.
/// We have `A.numRow()` inequality constraints and `E.numRow()` equality
/// constraints.
///
template <MaybeMatrix<int64_t> I64Matrix, Comparator CmptrType,
          MaybeVector<const llvm::SCEV *> SymbolVec, bool NonNegative>
struct Polyhedra {
  // order of vars:
  // constants, loop vars, symbolic vars
  // this is because of hnf prioritizing diagonalizing leading rows
  // empty fields sorted first to make it easier for compiler to alias them
  [[no_unique_address]] I64Matrix E;
  [[no_unique_address]] SymbolVec S;
  [[no_unique_address]] IntMatrix A;
  [[no_unique_address]] CmptrType C;

  static constexpr bool hasEqualities =
    !std::is_same_v<I64Matrix, EmptyMatrix<int64_t>>;

  Polyhedra() = default;
  Polyhedra(IntMatrix Ain)
    : E{}, A(std::move(Ain)), C(LinearSymbolicComparator::construct(A)){};
  Polyhedra(IntMatrix Ain, I64Matrix Ein)
    : E(std::move(Ein)), A(std::move(Ain)),
      C(LinearSymbolicComparator::construct(A)){};
  Polyhedra(IntMatrix Ain, SymbolVec S)
    : E{}, S(std::move(S)), A(std::move(Ain)),
      C(LinearSymbolicComparator::construct(A)){};
  Polyhedra(IntMatrix Ain, I64Matrix Ein, SymbolVec S)
    : E(std::move(Ein)), S(std::move(S)), A(std::move(Ain)),
      C(LinearSymbolicComparator::construct(A)){};

  inline void initializeComparator() {
    if constexpr (NonNegative)
      C.initNonNegative(A, E, getNumDynamic());
    else
      C.init(A, E);
  }
  auto calcIsEmpty() -> bool { return C.isEmpty(); }
  void pruneBounds() {
    if (calcIsEmpty()) {
      A.truncate(Row{0});
      if constexpr (hasEqualities)
        E.truncate(Row{0});
    } else
      pruneBoundsUnchecked();
  }
  void pruneBoundsUnchecked() {
    const size_t dyn = getNumDynamic();
    Vector<int64_t> diff{size_t(A.numCol())};
    if constexpr (hasEqualities)
      removeRedundantRows(A, E);
    for (auto j = size_t(A.numRow()); j;) {
      bool broke = false;
      for (size_t i = --j; i;) {
        if (A.numRow() <= 1)
          return;
        diff = A(--i, _) - A(j, _);
        if (C.greaterEqual(diff)) {
          eraseConstraint(A, i);
          initializeComparator();
          --j; // `i < j`, and `i` has been removed
        } else if (diff *= -1; C.greaterEqual(diff)) {
          eraseConstraint(A, j);
          initializeComparator();
          broke = true;
          break; // `j` is gone
        }
      }
      if constexpr (NonNegative) {
        if (!broke) {
          for (size_t i = 0; i < dyn; ++i) {
            diff = A(j, _);
            --diff(end - i);
            if (C.greaterEqual(diff)) {
              eraseConstraint(A, j);
              initializeComparator();
              break; // `j` is gone
            }
          }
        }
      }
    }
    if constexpr (hasEqualities)
      for (size_t i = 0; i < E.numRow(); ++i)
        normalizeByGCD(E(i, _));
  }

  [[nodiscard]] constexpr auto getNumSymbols() const -> size_t {
    return 1 + S.size();
  }
  [[nodiscard]] constexpr auto getNumDynamic() const -> size_t {
    return size_t(A.numCol()) - getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumVar() const -> size_t {
    return size_t(A.numCol()) - 1;
  }
  [[nodiscard]] constexpr auto getNumInequalityConstraints() const -> size_t {
    return size_t(A.numRow());
  }
  [[nodiscard]] constexpr auto getNumEqualityConstraints() const -> size_t {
    return size_t(E.numRow());
  }

  // static bool lessZero(const IntMatrix &A, const size_t r) const {
  //     return C.less(A(r, _));
  // }
  // static bool lessEqualZero(const IntMatrix &A, const size_t r) const {
  //     return C.lessEqual(A(r, _));
  // }
  // static bool greaterZero(const IntMatrix &A, const size_t r) const {
  //     return C.greater(A(r, _));
  // }
  // static bool greaterEqualZero(const IntMatrix &A, const size_t r) const {
  //     return C.greaterEqual(A(r, _));
  // }
  [[nodiscard]] auto lessZero(const size_t r) const -> bool {
    return C.less(A(r, _));
  }
  [[nodiscard]] auto lessEqualZero(const size_t r) const -> bool {
    return C.lessEqual(A(r, _));
  }
  [[nodiscard]] auto greaterZero(const size_t r) const -> bool {
    return C.greater(A(r, _));
  }
  [[nodiscard]] auto greaterEqualZero(const size_t r) const -> bool {
    return C.greaterEqual(A(r, _));
  }

  [[nodiscard]] auto equalNegative(const size_t i, const size_t j) const
    -> bool {
    return C.equalNegative(A(i, _), A(j, _));
  }
  // static bool equalNegative(const IntMatrix &A, const size_t i,
  //                    const size_t j) {
  //     return C.equalNegative(A(i, _), A(j, _));
  // }

  // A'x >= 0
  // E'x = 0
  // removes variable `i` from system
  void removeVariable(const size_t i) {
    if constexpr (hasEqualities) {
      if (substituteEquality(A, E, i)) {
        if constexpr (NonNegative)
          fourierMotzkinNonNegative(A, i);
        else
          fourierMotzkin(A, i);
      }
      if (E.numRow() > 1)
        NormalForm::simplifySystem(E);
    }
    if constexpr (NonNegative)
      fourierMotzkinNonNegative(A, i);
    else
      fourierMotzkin(A, i);
  }
  void removeVariableAndPrune(const size_t i) {
    removeVariable(i);
    pruneBoundsUnchecked();
  }

  void dropEmptyConstraints() {
    dropEmptyConstraints(A);
    if constexpr (hasEqualities)
      dropEmptyConstraints(E);
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const Polyhedra &p)
    -> llvm::raw_ostream & {
    auto &&os2 =
      printConstraints(os << "\n", p.A, llvm::ArrayRef<const llvm::SCEV *>());
    if constexpr (NonNegative)
      printPositive(os2, p.getNumDynamic());
    if constexpr (hasEqualities)
      return printConstraints(os2, p.E, llvm::ArrayRef<const llvm::SCEV *>(),
                              false);
    return os2;
  }
  void dump() const { llvm::errs() << *this; }
  [[nodiscard]] auto isEmpty() const -> bool {
    return A.numRow() == 0;
    // if (A.numRow() == 0)
    //     return true;
    // for (size_t r = 0; r < A.numRow(); ++r)
    //     if (C.less(A(r, _)))
    //         return true;
    // return false;
  }
  void truncateVars(size_t numVar) {
    if constexpr (hasEqualities)
      E.truncate(Col{numVar});
    A.truncate(Col{numVar});
  }
};

using SymbolicPolyhedra =
  Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
            llvm::SmallVector<const llvm::SCEV *>, false>;
using NonNegativeSymbolicPolyhedra =
  Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
            llvm::SmallVector<const llvm::SCEV *>, true>;
using SymbolicEqPolyhedra =
  Polyhedra<IntMatrix, LinearSymbolicComparator,
            llvm::SmallVector<const llvm::SCEV *>, false>;
using NonNegativeSymbolicEqPolyhedra =
  Polyhedra<IntMatrix, LinearSymbolicComparator,
            llvm::SmallVector<const llvm::SCEV *>, true>;
