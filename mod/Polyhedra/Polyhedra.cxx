#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/Support/raw_ostream.h>
#ifndef NDEBUG
#include <iostream>
#endif

#ifndef USE_MODULE
#include "Math/VectorGreatestCommonDivisor.cxx"
#include "Math/GenericConstructors.cxx"
#include "Utilities/Invariant.cxx"
#include "Math/EmptyArrays.cxx"
#include "Math/Constraints.cxx"
#include "Polyhedra/Comparators.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#include "Alloc/Mallocator.cxx"
#else
export module Polyhedra;
import Allocator;
import Arena;
import Array;
import Comparator;
import Constraints;
import EmptyMatrix;
import Invariant;
import GenericArrayConstructors;
import VGCD;
#endif

#ifdef USE_MODULE
export namespace poly {
#else
namespace poly {
#endif
using alloc::Arena;
using math::DensePtrMatrix, math::MutDensePtrMatrix, math::EmptyMatrix,
  math::Row, math::Col, math::vector, math::square_matrix, math::_, math::end,
  math::last;
inline auto printPositive(std::ostream &os, ptrdiff_t stop) -> std::ostream & {
  for (ptrdiff_t i = 0; i < stop; ++i) os << "v_" << i << " >= 0\n";
  return os;
}
inline auto printConstraints(std::ostream &os, DensePtrMatrix<int64_t> A,
                             bool inequality = true) -> std::ostream & {
  Row numConstraints = A.numRow();
  for (ptrdiff_t c = 0; c < numConstraints; ++c) {
    printConstraint(os, A[c, _], 1, inequality);
    os << "\n";
  }
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
template <bool HasEqualities, bool HasSymbols, bool MaybeNonNeg, typename P>
struct BasePolyhedra {
  // order of vars:
  // constants, loop vars, symbolic vars
  // this is because of hnf prioritizing diagonalizing leading rows
  // empty fields sorted first to make it easier for compiler to alias them

  [[nodiscard]] constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    return static_cast<P *>(this)->getA();
  }
  [[nodiscard]] constexpr auto getE() {
    if constexpr (HasEqualities) return static_cast<P *>(this)->getE();
    else return EmptyMatrix<int64_t>();
  }
  [[nodiscard]] constexpr auto getA() const -> DensePtrMatrix<int64_t> {
    return static_cast<const P *>(this)->getA();
  }
  [[nodiscard]] constexpr auto getE() const {
    if constexpr (HasEqualities) return static_cast<const P *>(this)->getE();
    else return EmptyMatrix<int64_t>();
  }
  constexpr void truncNumInEqCon(Row<> r) {
    static_cast<P *>(this)->truncNumInEqCon(r);
  }
  constexpr void truncNumEqCon(Row<> r) {
    if constexpr (HasEqualities) static_cast<P *>(this)->truncNumEqCon(r);
  }
  [[nodiscard]] constexpr auto
  initializeComparator(alloc::Mallocator<int64_t> alloc =
                         {}) // NOLINT(performance-unnecessary-value-param)
    -> comparator::LinearSymbolicComparator {
    if constexpr (MaybeNonNeg)
      if (isNonNegative())
        return comparator::linearNonNegative(alloc, getA(), getE(),
                                             getNumDynamic());
    return comparator::linear(alloc, getA(), getE(), true);
  }
  [[nodiscard]] constexpr auto
  initializeComparator(Arena<> *alloc) -> comparator::PtrSymbolicComparator {
    if constexpr (MaybeNonNeg)
      if (isNonNegative())
        return comparator::linearNonNegative(alloc, getA(), getE(),
                                             getNumDynamic());
    return comparator::linear(alloc, getA(), getE(), true);
  }
  constexpr auto calcIsEmpty() -> bool {
    return initializeComparator().isEmpty();
  }
  constexpr auto calcIsEmpty(Arena<> alloc) -> bool {
    return initializeComparator(&alloc).isEmpty(alloc);
  }
  [[nodiscard]] constexpr auto getNumCon() const -> int {
    return static_cast<const P *>(this)->getNumCon();
  }
  constexpr void setNumConstraints(int numCon) {
    static_cast<P *>(this)->setNumConstraints(numCon);
  }
  constexpr void setNumEqConstraints(int numCon) {
    static_cast<P *>(this)->setNumEqConstraints(numCon);
  }
  constexpr void decrementNumConstraints() {
    static_cast<P *>(this)->decrementNumConstraints();
  }
  [[nodiscard]] constexpr auto isNonNegative() const -> bool {
    if constexpr (!MaybeNonNeg) return false;
    return static_cast<const P *>(this)->isNonNegative();
  }
  constexpr void pruneBounds(Arena<> alloc) {
    if (getNumCon() == 0) return;
    pruneBoundsCore<true>(&alloc);
  }
  constexpr void pruneBounds() {
    alloc::OwningArena<> alloc;
    pruneBounds(alloc);
  }
  constexpr void eraseConstraint(ptrdiff_t constraint) {
    eraseConstraintImpl(getA(), math::row(constraint));
    decrementNumConstraints();
  }
  template <bool CheckEmpty> constexpr void pruneBoundsCore(Arena<> *alloc) {
    auto diff = vector<int64_t>(alloc, ptrdiff_t(getA().numCol()));
    auto p = checkpoint(alloc);
    const ptrdiff_t dyn = getNumDynamic();
    if constexpr (HasEqualities) {
      auto [ar, er] = removeRedundantRows(getA(), getE());
      setNumConstraints(ptrdiff_t(ar));
      setNumEqConstraints(ptrdiff_t(er));
      for (ptrdiff_t i = 0; i < getNumEqualityConstraints(); ++i) {
        auto l = gcd(getE()[i, _]);
        if (l != 1) getE()[i, _] /= l;
      }
    }
    auto C = initializeComparator(alloc);
    if constexpr (CheckEmpty) {
      if (C.isEmpty(*alloc)) {
        setNumConstraints(0);
        if constexpr (HasEqualities) setNumEqConstraints(0);
        return;
      }
    }
    for (auto j = getNumCon(); j;) {
      bool broke = false;
      for (auto i = --j; i;) {
        if (getNumCon() <= 1) return;
        diff << getA()[--i, _] - getA()[j, _];
        if (C.greaterEqual(*alloc, diff)) {
          eraseConstraint(i);
          rollback(alloc, p);
          C = initializeComparator(alloc);
          --j; // `i < j`, and `i` has been removed
        } else if (diff *= -1; C.greaterEqual(*alloc, diff)) {
          eraseConstraint(j);
          rollback(alloc, p);
          C = initializeComparator(alloc);
          broke = true;
          break; // `j` is gone
        }
      }
      if constexpr (MaybeNonNeg) {
        if (isNonNegative() && !broke) {
          for (ptrdiff_t i = 0; i < dyn; ++i) {
            diff << getA()[j, _];
            --diff[last - i];
            if (C.greaterEqual(*alloc, diff)) {
              eraseConstraint(j);
              rollback(alloc, p);
              C = initializeComparator(alloc);
              break; // `j` is gone
            }
          }
        }
      }
    }
  }
  // TODO: upper bound allocation size for comparator
  // then, reuse memory instead of reallocating
  constexpr void pruneBoundsUnchecked(math::Alloc<int64_t> auto &alloc) {
    auto p = checkpoint(alloc);
    pruneBoundsCore<false>(alloc);
    rollback(alloc, p);
    if constexpr (HasEqualities)
      for (ptrdiff_t i = 0; i < getE().numRow(); ++i)
        normalizeByGCD(getE()(i, _));
    truncNumInEqCon(getNumCon());
    if constexpr (HasEqualities) truncNumEqCon(getE().numRow());
  }

  [[nodiscard]] constexpr auto getNumSymbols() const -> unsigned {
    if constexpr (!HasSymbols) return 1;
    else return static_cast<const P *>(this)->getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumDynamic() const -> ptrdiff_t {
    return ptrdiff_t(getA().numCol()) - getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumVar() const -> ptrdiff_t {
    return ptrdiff_t(getA().numCol()) - 1;
  }
  [[nodiscard]] constexpr auto getNumInequalityConstraints() const -> int {
    return getNumCon();
  }
  [[nodiscard]] constexpr auto getNumEqualityConstraints() const -> int {
    ptrdiff_t r = ptrdiff_t(getE().numRow());
    utils::invariant(r <= std::numeric_limits<int>::max());
    utils::invariant(r >= 0);
    return int(r);
  }
  constexpr void dropEmptyConstraints() {
    dropEmptyConstraints(getA());
    if constexpr (HasEqualities) dropEmptyConstraints(getE());
  }
  friend inline auto operator<<(std::ostream &os,
                                const BasePolyhedra &p) -> std::ostream & {
    printConstraints(os << "\n", p.getA(), true);
    if constexpr (MaybeNonNeg)
      if (p.isNonNegative()) printPositive(os, p.getNumDynamic());
    if constexpr (HasEqualities) return printConstraints(os, p.getE(), false);
    return os;
  }
#ifndef NDEBUG
  [[gnu::used]] void dump() const {
    std::cout << *static_cast<const P *>(this);
  }
#endif
  [[nodiscard]] auto isEmpty() const -> bool {
    return getNumCon() == 0;
    // if (A.numRow() == 0)
    //     return true;
    // for (ptrdiff_t r = 0; r < A.numRow(); ++r)
    //     if (C.less(A(r, _)))
    //         return true;
    // return false;
  }
  void truncateVars(ptrdiff_t numVar) {
    if constexpr (HasEqualities) getE().truncate(math::col(numVar));
    getA().truncate(math::col(numVar));
  }
};
} // namespace poly
