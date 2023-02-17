#pragma once

#include "Math/Array.hpp"
#include "Math/Comparators.hpp"
#include "Math/Constraints.hpp"
#include "Math/EmptyArrays.hpp"
#include "Math/Math.hpp"
#include "Math/NormalForm.hpp"
#include "Math/VectorGreatestCommonDivisor.hpp"
#include "Utilities/Allocators.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <memory>
#include <sys/types.h>
#include <type_traits>

inline auto printPositive(llvm::raw_ostream &os, size_t stop)
  -> llvm::raw_ostream & {
  for (size_t i = 0; i < stop; ++i) os << "v_" << i << " >= 0\n";
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
template <bool HasEqualities, bool HasSymbols, bool NonNegative, typename P>
struct BasePolyhedra {
  // order of vars:
  // constants, loop vars, symbolic vars
  // this is because of hnf prioritizing diagonalizing leading rows
  // empty fields sorted first to make it easier for compiler to alias them

  [[nodiscard]] constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    return *static_cast<P *>(this)->getA();
  }
  [[nodiscard]] constexpr auto getE() {
    if constexpr (HasEqualities) return *static_cast<P *>(this)->getE();
    else return EmptyMatrix<int64_t>();
  }
  [[nodiscard]] constexpr auto getSyms()
    -> llvm::MutableArrayRef<const llvm::SCEV *> {
    static_assert(HasSymbols);
    return *static_cast<P *>(this)->getSyms();
  }
  [[nodiscard]] constexpr auto getA() const -> DensePtrMatrix<int64_t> {
    return *static_cast<const P *>(this)->getA();
  }
  [[nodiscard]] constexpr auto getE() const {
    if constexpr (HasEqualities) return *static_cast<const P *>(this)->getE();
    else return EmptyMatrix<int64_t>();
  }
  [[nodiscard]] constexpr auto getSyms() const
    -> llvm::ArrayRef<const llvm::SCEV *> {
    static_assert(HasSymbols);
    return *static_cast<const P *>(this)->getSyms();
  }

  [[nodiscard]] constexpr auto initializeComparator(
    std::allocator<int64_t> = {}) // NOLINT(performance-unnecessary-value-param)
    -> comparator::LinearSymbolicComparator {
    if constexpr (HasEqualities)
      if constexpr (NonNegative)
        return comparator::linearNonNegative(getA(), getE(), getNumDynamic());
      else return comparator::linear(getA(), getE(), true);
    else if constexpr (NonNegative)
      return comparator::linearNonNegative(getA(), getNumDynamic());
    else return comparator::linear(getA(), true);
  }
  [[nodiscard]] constexpr auto initializeComparator(WBumpAlloc<int64_t> alloc)
    -> comparator::PtrSymbolicComparator {
    if constexpr (HasEqualities)
      if constexpr (NonNegative)
        return comparator::linearNonNegative(alloc, getA(), getE(),
                                             getNumDynamic());
      else return comparator::linear(alloc, getA(), getE(), true);
    else if constexpr (NonNegative)
      return comparator::linearNonNegative(alloc, getA(), getNumDynamic());
    else return comparator::linear(alloc, getA(), true);
  }
  constexpr void reinitComparator(WBumpAlloc<int64_t> alloc,
                                  comparator::PtrSymbolicComparator &comp) {
    if constexpr (HasEqualities)
      if constexpr (NonNegative)
        comp.initNonNegative(alloc, getA(), getE(), getNumDynamic());
      else comp.init(alloc, getA(), getE(), true);
    else if constexpr (NonNegative)
      return comp.initNonNegative(alloc, getA(), getNumDynamic());
    else return comp.init(alloc, getA(), true);
  }
  constexpr auto calcIsEmpty() -> bool {
    return initializeComparator().isEmpty();
  }
  constexpr auto calcIsEmpty(BumpAlloc<> &alloc) -> bool {
    return initializeComparator(alloc).isEmpty();
  }
  constexpr void pruneBounds() {
    if (calcIsEmpty()) {
      getA().truncate(Row{0});
      if constexpr (HasEqualities) getE().truncate(Row{0});
    } else pruneBoundsUnchecked();
  }
  // TODO: upper bound allocation size for comparator
  // then, reuse memory instead of reallocating
  template <class Allocator>
  constexpr void pruneBoundsUnchecked(Allocator alloc) {
    const size_t dyn = getNumDynamic();
    MutPtrMatrix<int64_t> A{getA()};
    Vector<int64_t> diff{unsigned(A.numCol())};
    auto p = checkpoint(alloc);
    auto C = initializeComparator(alloc);
    if constexpr (HasEqualities) removeRedundantRows(getA(), getE());
    for (auto j = size_t(getA().numRow()); j;) {
      bool broke = false;
      for (size_t i = --j; i;) {
        if (A.numRow() <= 1) return;
        diff << A(--i, _) - A(j, _);
        if (C.greaterEqual(diff)) {
          eraseConstraint(A, i);
          reinitComparator(alloc, C);
          --j; // `i < j`, and `i` has been removed
        } else if (diff *= -1; C.greaterEqual(diff)) {
          eraseConstraint(A, j);
          reinitComparator(alloc, C);
          broke = true;
          break; // `j` is gone
        }
      }
      if constexpr (NonNegative) {
        if (!broke) {
          for (size_t i = 0; i < dyn; ++i) {
            diff = A(j, _);
            --diff[last - i];
            if (C.greaterEqual(diff)) {
              eraseConstraint(A, j);
              reinitComparator(alloc, C);
              break; // `j` is gone
            }
          }
        }
      }
    }
    checkpoint(alloc, p);
    if constexpr (HasEqualities)
      for (size_t i = 0; i < getE().numRow(); ++i) normalizeByGCD(getE()(i, _));
  }

  [[nodiscard]] constexpr auto getNumSymbols() const -> size_t {
    if constexpr (HasSymbols) return 1 + getSyms().size();
    else return 1;
  }
  [[nodiscard]] constexpr auto getNumDynamic() const -> size_t {
    return size_t(getA().numCol()) - getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumVar() const -> size_t {
    return size_t(getA().numCol()) - 1;
  }
  [[nodiscard]] constexpr auto getNumInequalityConstraints() const -> size_t {
    return size_t(getA().numRow());
  }
  [[nodiscard]] constexpr auto getNumEqualityConstraints() const -> size_t {
    return size_t(getE().numRow());
  }

  // [[nodiscard]] auto lessZero(const size_t r) const -> bool {
  //   return C.less(A(r, _));
  // }
  // [[nodiscard]] auto lessEqualZero(const size_t r) const -> bool {
  //   return C.lessEqual(A(r, _));
  // }
  // [[nodiscard]] auto greaterZero(const size_t r) const -> bool {
  //   return C.greater(A(r, _));
  // }
  // [[nodiscard]] auto greaterEqualZero(const size_t r) const -> bool {
  //   return C.greaterEqual(A(r, _));
  // }

  // [[nodiscard]] auto equalNegative(const size_t i, const size_t j) const
  //   -> bool {
  //   return C.equalNegative(A(i, _), A(j, _));
  // }

  // A'x >= 0
  // E'x = 0
  // removes variable `i` from system
  constexpr void removeVariable(const size_t i) {
    auto A{getA()};
    if constexpr (HasEqualities) {
      auto E{getE()};
      if (substituteEquality(A, E, i)) {
        if constexpr (NonNegative) fourierMotzkinNonNegative(getA(), i);
        else fourierMotzkin(A, i);
      }
      if (E.numRow() > 1) NormalForm::simplifySystem(E);
    }
    if constexpr (NonNegative) fourierMotzkinNonNegative(A, i);
    else fourierMotzkin(A, i);
  }
  constexpr void removeVariableAndPrune(const size_t i) {
    removeVariable(i);
    pruneBoundsUnchecked();
  }

  constexpr void dropEmptyConstraints() {
    dropEmptyConstraints(getA());
    if constexpr (HasEqualities) dropEmptyConstraints(getE());
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const BasePolyhedra &p)
    -> llvm::raw_ostream & {
    auto &&os2 =
      printConstraints(os << "\n", p.A, llvm::ArrayRef<const llvm::SCEV *>());
    if constexpr (NonNegative) printPositive(os2, p.getNumDynamic());
    if constexpr (HasEqualities)
      return printConstraints(os2, p.E, llvm::ArrayRef<const llvm::SCEV *>(),
                              false);
    return os2;
  }
  void dump() const { llvm::errs() << *this; }
  [[nodiscard]] auto isEmpty() const -> bool {
    return getA().numRow() == 0;
    // if (A.numRow() == 0)
    //     return true;
    // for (size_t r = 0; r < A.numRow(); ++r)
    //     if (C.less(A(r, _)))
    //         return true;
    // return false;
  }
  void truncateVars(size_t numVar) {
    if constexpr (HasEqualities) getE().truncate(Col{numVar});
    getA().truncate(Col{numVar});
  }
};

// using SymbolicPolyhedra =
//   BasePolyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
//                 llvm::SmallVector<const llvm::SCEV *>, false>;
// using NonNegativeSymbolicPolyhedra =
//   BasePolyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
//                 llvm::SmallVector<const llvm::SCEV *>, true>;
// using SymbolicEqPolyhedra =
//   BasePolyhedra<IntMatrix, LinearSymbolicComparator,
//                 llvm::SmallVector<const llvm::SCEV *>, false>;
// using NonNegativeSymbolicEqPolyhedra =
//   BasePolyhedra<IntMatrix, LinearSymbolicComparator,
//                 llvm::SmallVector<const llvm::SCEV *>, true>;
