#pragma once
#include "./Constraints.hpp"
#include "./NormalForm.hpp"
#include "./Rational.hpp"
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Indexing.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Invariant.hpp"
#include <bits/iterator_concepts.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <new>
#include <tuple>

// #define VERBOSESIMPLEX

/// Tableau for the Simplex algorithm.
/// We need a core Simplex type that is unmanaged
/// then for convenience, it would be nice to manage it.
/// Ideally, we could have a type hierarchy of
/// unmanaged -> managed
/// with some API to make the managed generic.
/// We also want the managed to be automatically demotable to unmanaged,
/// to avoid unnecessary specialization.
///
/// Slack variables are sorted first.
class Simplex {
  using index_type = int;
  using value_type = int64_t;

  static constexpr auto tableauOffset(unsigned conCap, unsigned varCap)
    -> size_t {
    size_t numIndex = conCap + varCap;
    if constexpr (sizeof(value_type) > sizeof(index_type))
      numIndex += (sizeof(value_type) / sizeof(index_type)) - 1;
    size_t indexBytes = (sizeof(index_type) * numIndex);
    if constexpr (sizeof(value_type) > sizeof(index_type))
      indexBytes &= (-alignof(value_type));
    return indexBytes;
  }
  [[nodiscard]] constexpr auto tableauOffset() const -> size_t {
    return tableauOffset(reservedBasicConstraints(), reservedBasicVariables());
  }
  [[gnu::returns_nonnull, nodiscard]] constexpr auto tableauPointer() const
    -> value_type * {
    void *p = const_cast<char *>(memory) + tableauOffset();
    return (value_type *)p;
  }
  [[gnu::returns_nonnull, nodiscard]] constexpr auto basicConsPointer() const
    -> index_type * {
    void *p = const_cast<char *>(memory);
    return (index_type *)p;
  }
  [[gnu::returns_nonnull, nodiscard]] constexpr auto basicVarsPointer() const
    -> index_type * {
    return basicConsPointer() + reservedBasicConstraints();
  }
  unsigned numConstraints{0};
  unsigned numVars{0};
  unsigned constraintCapacity;
  unsigned varCapacity;
#ifndef NDEBUG
  bool inCanonicalForm{false};
#endif
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) // FAM
  alignas(value_type) char memory[];
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
public:
  // tableau is constraint * var matrix w/ extra col for LHS
  // and extra row for objective function
  [[nodiscard]] static constexpr auto reservedTableau(unsigned conCap,
                                                      unsigned varCap)
    -> size_t {
    return (size_t(conCap) + 1) * (size_t(varCap) + 1);
  }
  [[nodiscard]] constexpr auto reservedTableau() const -> size_t {
    return reservedTableau(reservedBasicConstraints(),
                           reservedBasicVariables());
  }
  [[nodiscard]] constexpr auto reservedBasicConstraints() const -> size_t {
    return varCapacity;
  }
  [[nodiscard]] constexpr auto reservedBasicVariables() const -> size_t {
    return constraintCapacity;
  }

  // [[nodiscard]] constexpr auto intsNeeded() const -> size_t {
  //   return reservedTableau() + reservedBasicConstraints() +
  //          reservedBasicVariables();
  // }
  /// [ value | objective function ]
  /// [ LHS   | tableau            ]
  [[nodiscard]] constexpr auto getTableau() const -> PtrMatrix<value_type> {
    //
    return {tableauPointer(), StridedDims{
                                numConstraints + 1,
                                numVars + 1,
                                varCapacity + 1,
                              }};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getTableau() -> MutPtrMatrix<value_type> {
    return {tableauPointer(), StridedDims{
                                numConstraints + 1,
                                numVars + 1,
                                varCapacity + 1,
                              }};
  }
  [[nodiscard]] constexpr auto getConstraints() const -> PtrMatrix<value_type> {
    return {tableauPointer() + varCapacity + 1, StridedDims{
                                                  numConstraints,
                                                  numVars + 1,
                                                  varCapacity + 1,
                                                }};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getConstraints() -> MutPtrMatrix<value_type> {
    return {tableauPointer() + varCapacity + 1, StridedDims{
                                                  numConstraints,
                                                  numVars + 1,
                                                  varCapacity + 1,
                                                }};
  }
  [[nodiscard]] constexpr auto getBasicConstraints() const
    -> PtrVector<index_type> {
    return {basicConsPointer(), numVars};
  }
  [[nodiscard]] constexpr auto getBasicConstraints()
    -> MutPtrVector<index_type> {
    return {basicConsPointer(), numVars};
  }
  [[nodiscard]] constexpr auto getBasicVariables() const
    -> PtrVector<index_type> {
    return {basicVarsPointer(), numConstraints};
  }
  [[nodiscard]] constexpr auto getBasicVariables() -> MutPtrVector<index_type> {
    return {basicVarsPointer(), numConstraints};
  }
  [[nodiscard]] constexpr auto getCost() const -> PtrVector<value_type> {
    return {tableauPointer(), numVars + 1};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getCost() -> MutPtrVector<value_type> {
    return {tableauPointer(), numVars + 1};
  }
  [[nodiscard]] constexpr auto getBasicConstraint(unsigned i) const
    -> index_type {
    return getBasicConstraints()[i];
  }
  [[nodiscard]] constexpr auto getBasicVariable(unsigned i) const
    -> index_type {
    return getBasicVariables()[i];
  }
  [[nodiscard]] constexpr auto getObjectiveCoefficient(unsigned i) const
    -> value_type {
    return getCost()[++i];
  }
  [[nodiscard]] constexpr auto getObjectiveValue() -> value_type & {
    return getCost()[0];
  }
  [[nodiscard]] constexpr auto getObjectiveValue() const -> value_type {
    return getCost()[0];
  }
  constexpr void truncateConstraints(unsigned i) {
    assert(i <= numConstraints);
    numConstraints = i;
  }
  constexpr void simplifySystem() {
#ifndef NDEBUG
    inCanonicalForm = false;
#endif
    auto C{getConstraints()};
    NormalForm::solveSystemSkip(C);
    truncateConstraints(unsigned(NormalForm::numNonZeroRows(C)));
  }
#ifndef NDEBUG
  constexpr void assertCanonical() const {
    PtrMatrix<value_type> C{getTableau()};
    PtrVector<index_type> basicVars{getBasicVariables()};
    PtrVector<index_type> basicCons{getBasicConstraints()};
    for (size_t v = 0; v < basicCons.size();) {
      index_type c = basicCons[v++];
      if (c < 0) continue;
      assert(allZero(C(_(1, 1 + c), v)));
      assert(allZero(C(_(2 + c, end), v)));
      assert(size_t(basicVars[c]) == v - 1);
    }
    for (size_t c = 1; c < C.numRow(); ++c) {
      index_type v = basicVars[c - 1];
      if (size_t(v) < basicCons.size()) {
        assert(c - 1 == size_t(basicCons[v]));
        assert(C(c, v + 1) >= 0);
      }
      assert(C(c, 0) >= 0);
    }
  }
#endif
  [[nodiscard]] constexpr auto getConstants() -> MutStridedVector<int64_t> {
    return getTableau()(_(1, end), 0);
  }
  [[nodiscard]] constexpr auto getConstants() const -> StridedVector<int64_t> {
    return getTableau()(_(1, end), 0);
  }
  constexpr void setNumCons(unsigned i) {
    invariant(i <= constraintCapacity);
    numConstraints = i;
  }
  constexpr void setNumVars(unsigned i) {
    invariant(i <= varCapacity);
    numVars = i;
  }
  constexpr void truncateVars(unsigned i) {
    invariant(i <= numVars);
    numVars = i;
  }
  [[nodiscard]] constexpr auto getNumCons() const -> unsigned {
    return numConstraints;
  }
  [[nodiscard]] constexpr auto getNumVars() const -> unsigned {
    return numVars;
  }
  [[nodiscard]] constexpr auto getConCap() const -> unsigned {
    return constraintCapacity;
  }
  [[nodiscard]] constexpr auto getVarCap() const -> unsigned {
    return varCapacity;
  }
  constexpr void deleteConstraint(unsigned c) {
    auto basicCons = getBasicConstraints();
    auto basicVars = getBasicVariables();
    auto constraints = getConstraints();
    --numConstraints;
    if (auto basicVar = basicVars[c]; basicVar >= 0) basicCons[basicVar] = -1;
    if (c == numConstraints) return;
    auto basicVar = basicVars[numConstraints];
    basicVars[c] = basicVar;
    if (basicVar >= 0) basicCons[basicVar] = index_type(c);
    constraints(c, _) << constraints(numConstraints, _);
  }

  // AbstractVector
  struct Solution {
    using value_type = Rational;
    // view of tableau dropping const column
    NotNull<const Simplex> simplex;
    size_t skippedVars;
    size_t numVars;
    class iterator { // NOLINT(readability-identifier-naming)
      const Solution *sol;
      size_t i;

    public:
      using value_type = Rational;
      constexpr iterator(const Solution *s, size_t j) : sol(s), i(j) {}
      constexpr iterator() = default;
      constexpr iterator(const iterator &) = default;
      constexpr auto operator=(const iterator &) -> iterator & = default;
      auto operator*() const -> Rational { return (*sol)[i]; }
      constexpr auto operator++() -> iterator & {
        ++i;
        return *this;
      }
      constexpr auto operator++(int) -> iterator {
        auto tmp = *this;
        ++i;
        return tmp;
      }
      constexpr auto operator--() -> iterator & {
        --i;
        return *this;
      }
      constexpr auto operator--(int) -> iterator {
        auto tmp = *this;
        --i;
        return tmp;
      }
      friend constexpr auto operator==(iterator a, iterator b) -> bool {
        return a.i == b.i;
      }
      friend constexpr auto operator!=(iterator a, iterator b) -> bool {
        return a.i != b.i;
      }
      constexpr auto operator-(iterator b) const -> ptrdiff_t {
        return ptrdiff_t(i) - ptrdiff_t(b.i);
      }
      constexpr auto operator+(ptrdiff_t n) const -> iterator {
        return {sol, i + size_t(n)};
      }
    };
    [[nodiscard]] constexpr auto begin() const -> iterator { return {this, 0}; }
    [[nodiscard]] constexpr auto end() const -> iterator {
      return {this, numVars - skippedVars};
    }

    [[nodiscard]] constexpr auto operator[](size_t i) const -> Rational {
      invariant(ptrdiff_t(i) >= 0);
      i += skippedVars;
      int64_t j = simplex->getBasicConstraint(i);
      if (j < 0) return 0;
      PtrMatrix<int64_t> constraints = simplex->getConstraints();
      return Rational::create(constraints(j, 0), constraints(j, i + 1));
    }
    [[nodiscard]] constexpr auto operator[](LinAlg::OffsetEnd k) const
      -> Rational {
      size_t i = size_t(simplex->numVars) - k.offset;
      int64_t j = simplex->getBasicConstraint(i);
      if (j < 0) return 0;
      PtrMatrix<int64_t> constraints = simplex->getConstraints();
      return Rational::create(constraints(j, 0), constraints(j, i + 1));
    }
    [[nodiscard]] constexpr auto operator[](LinAlg::RelativeOffset auto i) const
      -> Rational {
      return (*this)[LinAlg::calcOffset(size(), i)];
    }
    template <typename B, typename E>
    constexpr auto operator[](Range<B, E> r) const -> Solution {
      return (*this)[LinAlg::canonicalizeRange(r, size())];
    }
    constexpr auto operator[](Range<size_t, size_t> r) const -> Solution {
      return {simplex, skippedVars + r.b, skippedVars + r.e};
    }
    [[nodiscard]] constexpr auto size() const -> size_t {
      return numVars - skippedVars;
    }
    [[nodiscard]] constexpr auto view() const -> Solution { return *this; };

    [[nodiscard]] constexpr auto denomLCM() const -> int64_t {
      int64_t l = 1;
      for (auto r : *this) l = lcm(l, r.denominator);
      return l;
    }
  };
  [[nodiscard]] constexpr auto getSolution() const -> Solution {
    return {this, 0, numVars};
  }

  /// simplex.initiateFeasible() -> bool
  /// returns `true` if infeasible, `false ` if feasible
  /// The approach is to first put the equalities into HNF
  /// then, all diagonal elements are basic variables.
  /// For each non-diagonal element, we need to add an augment variable
  /// Then we try to set all augment variables to 0.
  /// If we fail, it is infeasible.
  /// If we succeed, then the problem is feasible, and we're in
  /// canonical form.
  [[nodiscard(
    "returns `true` if infeasible; should check when calling.")]] constexpr auto
  initiateFeasible() -> bool {
    // remove trivially redundant constraints
    simplifySystem();
    // [ I;  X ; b ]
    //
    // original number of variables
    const auto numVar = ptrdiff_t(getNumVars());
    MutPtrMatrix<value_type> C{getConstraints()};
    MutPtrVector<index_type> basicCons{getBasicConstraints()};
    basicCons << -2;
    // first pass, we make sure the equalities are >= 0
    // and we eagerly try and find columns with
    // only a single non-0 element.
    for (ptrdiff_t c = 0; c < C.numRow(); ++c) {
      int64_t &Ceq = C(c, 0);
      int64_t sign = 2 * (Ceq >= 0) - 1;
      Ceq *= sign;
      for (ptrdiff_t v = 0; v < numVar; ++v)
        if (int64_t Ccv = C(c, v + 1) *= sign)
          basicCons[v] =
            (((basicCons[v] == -2) && (Ccv > 0))) ? index_type(c) : -1;
    }
    // basicCons should now contain either `-1` or an integer >= 0
    // indicating which row contains the only non-zero element; we'll
    // now fill basicVars.
    //
    auto basicVars{getBasicVariables()};
    basicVars << -1;
    for (ptrdiff_t v = 0; v < numVar; ++v) {
      if (int64_t r = basicCons[v]; r >= 0) {
        if (basicVars[r] == -1) basicVars[r] = index_type(v);
        else basicCons[v] = -1;
      }
    }
#ifndef NDEBUG
    inCanonicalForm = true;
#endif
    Vector<unsigned> augVars{};
    // upper bound number of augmentVars is constraintCapacity
    for (unsigned i = 0; i < basicVars.size(); ++i)
      if (basicVars[i] == -1) augVars.push_back(i);
    return (!augVars.empty() && removeAugmentVars(augVars));
  }
  constexpr auto removeAugmentVars(PtrVector<unsigned> augmentVars) -> bool {
    // TODO: try to avoid reallocating, via reserving enough ahead of time
    unsigned numAugment = augmentVars.size(), oldNumVar = numVars;
    assert(numAugment + numVars <= varCapacity);
    numVars += numAugment;
    MutPtrMatrix<value_type> C{getConstraints()};
    MutPtrVector<index_type> basicVars{getBasicVariables()};
    MutPtrVector<index_type> basicCons{getBasicConstraints()};
    MutPtrVector<value_type> costs{getCost()};
    costs << 0;
    C(_, _(oldNumVar + 1, end)) << 0;
    for (ptrdiff_t i = 0; i < ptrdiff_t(augmentVars.size()); ++i) {
      ptrdiff_t a = augmentVars[i];
      basicVars[a] = index_type(i) + index_type(oldNumVar);
      basicCons[i + oldNumVar] = index_type(a);
      C(a, oldNumVar + 1 + i) = 1;
      // we now zero out the implicit cost of `1`
      costs[_(begin, oldNumVar + 1)] -= C(a, _(begin, oldNumVar + 1));
    }
    assert(std::all_of(basicVars.begin(), basicVars.end(),
                       [](int64_t i) { return i >= 0; }));
    // false/0 means feasible
    // true/non-zero infeasible
    if (runCore()) return true;
    // check for any basic vars set to augment vars, and set them to some
    // other variable (column) instead.
    for (ptrdiff_t c = 0; c < C.numRow(); ++c) {
      if (ptrdiff_t(basicVars[c]) >= ptrdiff_t(oldNumVar)) {
        assert(C(c, 0) == 0);
        assert(c == basicCons[basicVars[c]]);
        assert(C(c, basicVars[c] + 1) >= 0);
        // find var to make basic in its place
        for (ptrdiff_t v = oldNumVar; v != 0;) {
          // search for a non-basic variable
          // (basicConstraints<0)
          int64_t Ccv = C(c, v--);
          if (Ccv == 0 || (basicCons[v] >= 0)) continue;
          if (Ccv < 0) C(c, _) *= -1;
          for (size_t i = 0; i < C.numRow(); ++i)
            if (i != size_t(c)) NormalForm::zeroWithRowOp(C, i, c, v + 1, 0);
          basicVars[c] = index_type(v);
          basicCons[v] = index_type(c);
          break;
        }
      }
    }
    // all augment vars are now 0
    numVars = oldNumVar;
#ifndef NDEBUG
    assertCanonical();
#endif
    return false;
  }

  // 1 based to match getBasicConstraints
  [[nodiscard]] static constexpr auto
  getEnteringVariable(PtrVector<int64_t> costs) -> Optional<int> {
    // Bland's algorithm; guaranteed to terminate
    auto f = costs.begin(), l = costs.end();
    const auto *neg = std::find_if(f, l, [](int64_t c) { return c < 0; });
    if (neg == l) return {};
    return int(std::distance(f, neg));
  }
  [[nodiscard]] static constexpr auto
  getLeavingVariable(PtrMatrix<int64_t> C, size_t enteringVariable)
    -> Optional<unsigned int> {
    // inits guarantee first valid is selected
    int64_t n = -1, d = 0;
    unsigned int j = 0;
    for (size_t i = 1; i < C.numRow(); ++i) {
      int64_t Civ = C(i, enteringVariable + 1);
      if (Civ <= 0) continue;
      int64_t Cio = C(i, 0);
      if (Cio == 0) return --i;
      invariant(Cio > 0);
      if ((n * Cio) >= (Civ * d)) continue;
      n = Civ;
      d = Cio;
      j = i;
    }
    // NOTE: if we fail to find a leaving variable, then `j = 0`,
    // and it will unsigned wrap to `size_t(-1)`, which indicates
    // an empty `Optional<unsigned int>`
    return --j;
  }
  constexpr auto makeBasic(MutPtrMatrix<int64_t> C, int64_t f, int enteringVar)
    -> int64_t {
    Optional<unsigned int> leaveOpt = getLeavingVariable(C, enteringVar);
    if (!leaveOpt) return 0; // unbounded
    unsigned int leavingVar = *leaveOpt;
    for (size_t i = 0; i < C.numRow(); ++i) {
      if (i == leavingVar + 1) continue;
      int64_t m = NormalForm::zeroWithRowOp(C, i, leavingVar + 1,
                                            enteringVar + 1, i == 0 ? f : 0);
      if (i == 0) f = m;
    }
    // update baisc vars and constraints
    MutPtrVector<index_type> basicVars{getBasicVariables()};
    int64_t oldBasicVar = basicVars[leavingVar];
    basicVars[leavingVar] = enteringVar;
    MutPtrVector<index_type> basicConstraints{getBasicConstraints()};
    basicConstraints[oldBasicVar] = -1;
    basicConstraints[enteringVar] = index_type(leavingVar);
    return f;
  }
  // run the simplex algorithm, assuming basicVar's costs have been set to
  // 0
  constexpr auto runCore(int64_t f = 1) -> Rational {
#ifndef NDEBUG
    assert(inCanonicalForm);
#endif
    //     return runCore(getCostsAndConstraints(), f);
    // }
    // Rational runCore(MutPtrMatrix<int64_t> C, int64_t f = 1) {
    MutPtrMatrix<int64_t> C{getTableau()};
    while (true) {
      // entering variable is the column
      Optional<int> enteringVariable = getEnteringVariable(C(0, _(1, end)));
      if (!enteringVariable) return Rational::create(C(0, 0), f);
      f = makeBasic(C, f, *enteringVariable);
      if (f == 0) return std::numeric_limits<int64_t>::max(); // unbounded
    }
  }
  // set basicVar's costs to 0, and then runCore()
  constexpr auto run() -> Rational {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assertCanonical();
#endif
    MutPtrVector<index_type> basicVars{getBasicVariables()};
    MutPtrMatrix<value_type> C{getTableau()};
    int64_t f = 1;
    // zero cost of basic variables to put in canonical form
    for (size_t c = 0; c < basicVars.size();) {
      int64_t v = basicVars[c++];
      if ((size_t(++v) < C.numCol()) && C(0, v))
        f = NormalForm::zeroWithRowOp(C, 0, c, v, f);
    }
    return runCore(f);
  }

  // don't touch variables lex > v
  constexpr void rLexCore(unsigned int v) {
    MutPtrMatrix<value_type> C{getTableau()};
    MutPtrVector<index_type> basicVars{getBasicVariables()};
    MutPtrVector<index_type> basicConstraints{getBasicConstraints()};
    invariant(v > 0);
    while (true) {
      // get new entering variable
      Optional<int> enteringVariable = getEnteringVariable(C(0, _(1, v)));
      if (!enteringVariable) break;
      auto ev = *enteringVariable;
      auto leaveOpt = getLeavingVariable(C, ev);
      if (!leaveOpt) break;
      unsigned int lVar = *leaveOpt;
      unsigned int leavingVariable = lVar++;
      for (size_t i = 0; i < C.numRow(); ++i)
        if (i != size_t(lVar)) NormalForm::zeroWithRowOp(C, i, lVar, ev + 1, 0);
      // update baisc vars and constraints
      int64_t oldBasicVar = basicVars[leavingVariable];
      basicVars[leavingVariable] = ev;
      if (size_t(oldBasicVar) < basicConstraints.size())
        basicConstraints[oldBasicVar] = -1;
      basicConstraints[ev] = index_type(leavingVariable);
    }
  }
  // Assumes all >v have already been lex-minimized
  // v starts at numVars-1
  // returns `false` if `0`, `true` if not zero
  // minimize v, not touching any variable lex > v
  constexpr auto rLexMin(size_t v) -> bool {
#ifndef NDEBUG
    assert(inCanonicalForm);
#endif
    MutPtrMatrix<value_type> C{getTableau()};
    MutPtrVector<index_type> basicConstraints{getBasicConstraints()};
    int64_t c = basicConstraints[v];
    if (c < 0) return false;
    if (v == 0) return true;
    // we try to zero `v` or at least minimize it.
    // set cost to 1, and then try to alkalize
    // set v and all > v to 0
    C(0, _(0, 1 + v)) << -C(++c, _(0, 1 + v));
    C(0, _(1 + v, end)) << 0;
    rLexCore(v);
    return makeZeroBasic(v);
  }
  /// makeZeroBasic(unsigned int v) -> bool
  /// Tries to make `v` non-basic if `v` is zero.
  /// Returns `false` if `v` is zero, `true` otherwise
  constexpr auto makeZeroBasic(unsigned int v) -> bool {
    MutPtrMatrix<value_type> C{getTableau()};
    MutPtrVector<index_type> basicVars{getBasicVariables()};
    MutPtrVector<index_type> basicConstraints{getBasicConstraints()};
    int64_t c = basicConstraints[v];
    int64_t cc = c++;
    // was not basic
    // not basic, v is  zero
    if (cc < 0) return false;
    // v is basic, but not zero
    if (C(c, 0) != 0) return true;
#ifndef NDEBUG
    assertCanonical();
#endif
    // so v is basic and zero.
    // We're going to try to make it non-basic
    for (ptrdiff_t ev = 0; ev < v;) {
      auto evm1 = ev++;
      if ((basicConstraints[evm1] >= 0) || (C(c, ev) == 0)) continue;
      if (C(c, ev) < 0) C(c, _) *= -1;
      for (size_t i = 1; i < C.numRow(); ++i)
        if (i != size_t(c)) NormalForm::zeroWithRowOp(C, i, c, ev, 0);
      int64_t oldBasicVar = basicVars[cc];
      assert(oldBasicVar == int64_t(v));
      basicVars[cc] = index_type(evm1);
      // if (size_t(oldBasicVar) < basicConstraints.size())
      basicConstraints[oldBasicVar] = -1;
      basicConstraints[evm1] = index_type(cc);
      break;
    }
#ifndef NDEBUG
    assertCanonical();
#endif
    return false;
  }
  constexpr auto rLexMinLast(size_t n) -> Solution {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assertCanonical();
#endif
    for (size_t v = getNumVars(), e = v - n; v != e;) rLexMin(--v);
#ifndef NDEBUG
    assertCanonical();
#endif
    return {*this, getNumVars() - n, getNumVars()};
  }
  constexpr auto rLexMinStop(size_t skippedVars) -> Solution {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assertCanonical();
#endif
    for (size_t v = getNumVars(); v != skippedVars;) rLexMin(--v);
#ifndef NDEBUG
    assertCanonical();
#endif
    return {*this, skippedVars, getNumVars()};
  }

  // reverse lexicographic ally minimize vars
  constexpr void rLexMin(Vector<Rational> &sol) {
    sol << rLexMinLast(sol.size());
  }
  // A(:,1:end)*x <= A(:,0)
  // B(:,1:end)*x == B(:,0)
  // returns a Simplex if feasible, and an empty `Optional` otherwise
  static constexpr auto positiveVariables(BumpAlloc<> &alloc,
                                          PtrMatrix<int64_t> A,
                                          PtrMatrix<int64_t> B)
    -> Optional<Simplex *> {
    invariant(A.numCol() == B.numCol());
    unsigned numVar = unsigned(A.numCol()) - 1, numSlack = unsigned(A.numRow()),
             numStrict = unsigned(B.numRow()), numCon = numSlack + numStrict,
             varCap = numVar + numSlack;
    // see how many slack vars are infeasible as solution
    // each of these will require an augment variable
    for (unsigned i = 0; i < numSlack; ++i) varCap += A(i, 0) < 0;
    // try to avoid reallocating
    auto checkpoint{alloc.checkpoint()};
    Simplex *simplex{
      Simplex::create(alloc, numCon, numVar + numSlack, numCon, varCap)};
    // construct:
    // [ I A
    //   0 B ]
    // then drop the extra variables
    slackEqualityConstraints(simplex->getConstraints()(_, _(1, end)),
                             A(_, _(1, end)), B(_, _(1, end)));
    auto consts{simplex->getConstants()};
    consts[_(0, numSlack)] << A(_, 0);
    consts[_(numSlack, numSlack + numStrict)] << B(_, 0);
    // for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    // for (size_t i = 0; i < numStrict; ++i) consts[i + numSlack] = B(i, 0);
    if (!simplex->initiateFeasible()) return simplex;
    alloc.rollback(checkpoint);
    return nullptr;
  }
  static constexpr auto positiveVariables(BumpAlloc<> &alloc,
                                          PtrMatrix<int64_t> A)
    -> Optional<Simplex *> {
    unsigned numVar = unsigned(A.numCol()) - 1, numSlack = unsigned(A.numRow()),
             numCon = numSlack, varCap = numVar + numSlack;
    // see how many slack vars are infeasible as solution
    // each of these will require an augment variable
    for (unsigned i = 0; i < numSlack; ++i) varCap += A(i, 0) < 0;
    // try to avoid reallocating
    auto checkpoint{alloc.checkpoint()};
    Simplex *simplex{
      Simplex::create(alloc, numCon, numVar + numSlack, numCon, varCap)};
    // construct:
    // [ I A ]
    // then drop the extra variables
    slackEqualityConstraints(simplex->getConstraints()(_, _(1, end)),
                             A(_, _(1, end)));
    // auto consts{simplex.getConstants()};
    // for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    simplex->getConstants() << A(_, 0);
    if (!simplex->initiateFeasible()) return simplex;
    alloc.rollback(checkpoint);
    return nullptr;
  }

  constexpr void pruneBounds(BumpAlloc<> &alloc, size_t numSlack = 0) {
    auto p = alloc.scope();
    Simplex *simplex{Simplex::create(alloc, numConstraints, numVars,
                                     constraintCapacity, varCapacity)};
    // Simplex simplex{getNumCons(), getNumVars(), getNumSlack(), 0};
    for (unsigned c = 0; c < getNumCons(); ++c) {
      *simplex << *this;
      MutPtrMatrix<int64_t> constraints = simplex->getConstraints();
      int64_t bumpedBound = ++constraints(c, 0);
      MutPtrVector<int64_t> cost = simplex->getCost();
      for (size_t v = numSlack; v < cost.size(); ++v)
        cost[v] = -constraints(c, v + 1);
      if (simplex->run() != bumpedBound) deleteConstraint(c--);
    }
  }

  constexpr void dropVariable(size_t i) {
    // We remove a variable by isolating it, and then dropping the
    // constraint. This allows us to preserve canonical form
    MutPtrVector<index_type> basicConstraints{getBasicConstraints()};
    MutPtrMatrix<value_type> C{getConstraints()};
    // ensure sure `i` is basic
    if (basicConstraints[i] < 0) makeBasic(C, 0, index_type(i));
    size_t ind = basicConstraints[i];
    size_t lastRow = size_t(C.numRow() - 1);
    if (lastRow != ind) LinAlg::swap(C, Row{ind}, Row{lastRow});
    truncateConstraints(lastRow);
  }
  constexpr void removeExtraVariables(size_t i) {
    for (size_t j = getNumVars(); j > i;) {
      dropVariable(--j);
      truncateVars(j);
    }
  }
  // static constexpr auto toMask(PtrVector<int64_t> x) -> uint64_t {
  //   assert(x.size() <= 64);
  //   uint64_t m = 0;
  //   for (auto y : x) m = ((m << 1) | (y != 0));
  //   return m;
  // }
  // [[nodiscard]] constexpr auto getBasicTrueVarMask() const -> uint64_t {
  //   const size_t numVarTotal = getNumVars();
  //   assert(numVarTotal <= 64);
  //   uint64_t m = 0;
  //   PtrVector<index_type> basicCons{getBasicConstraints()};
  //   for (size_t i = numSlack; i < numVarTotal; ++i)
  //     m = ((m << 1) | (basicCons[i] > 0));
  //   return m;
  // }
  // check if a solution exists such that `x` can be true.
  // returns `true` if unsatisfiable
  [[nodiscard]] constexpr auto unSatisfiable(BumpAlloc<> &alloc,
                                             PtrVector<int64_t> x,
                                             size_t off) const -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    if (!allGEZero(x)) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    const size_t numCon = getNumCons(), numVar = getNumVars(),
                 numFix = x.size();
    auto p = alloc.scope();
    Simplex *subSimp{Simplex::create(alloc, numCon, numVar - numFix)};
    // subSimp.tableau(0, 0) = 0;
    // subSimp.tableau(0, 1) = 0;
    auto fC{getTableau()};
    auto sC{subSimp->getTableau()};
    sC(_, 0) << fC(_, 0) - fC(_, _(1 + off, 1 + off + numFix)) * x;
    // sC(_, 0) = fC(_, 0);
    // for (size_t i = 0; i < numFix; ++i)
    //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
    sC(_, _(1, 1 + off)) << fC(_, _(1, 1 + off));
    sC(_, _(1 + off, end)) << fC(_, _(1 + off + numFix, end));
    // returns `true` if unsatisfiable
    return subSimp->initiateFeasible();
  }
  [[nodiscard]] constexpr auto satisfiable(BumpAlloc<> &alloc,
                                           PtrVector<int64_t> x,
                                           size_t off) const -> bool {
    return !unSatisfiable(alloc, x, off);
  }
  // check if a solution exists such that `x` can be true.
  // zeros remaining rows
  [[nodiscard]] constexpr auto
  unSatisfiableZeroRem(BumpAlloc<> &alloc, PtrVector<int64_t> x, size_t off,
                       size_t numRow) const -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    if (!allGEZero(x)) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    invariant(numRow <= getNumCons());
    const size_t numFix = x.size();
    auto p = alloc.scope();
    Simplex *subSimp{Simplex::create(alloc, numRow, off++)};
    auto fC{getConstraints()};
    auto sC{subSimp->getConstraints()};
    sC(_, 0) << fC(_(begin, numRow), 0) -
                  fC(_(begin, numRow), _(off, off + numFix)) * x;
    sC(_, _(1, off)) << fC(_(begin, numRow), _(1, off));
    return subSimp->initiateFeasible();
  }
  /// indsFree gives how many variables are free to take  any >= 0 value
  /// indOne is var ind greater than indsFree that's pinned to 1
  /// (i.e., indsFree + indOne == index of var pinned to 1)
  /// numRow is number of rows used, extras are dropped
  // [[nodiscard]] constexpr auto
  [[nodiscard]] inline auto unSatisfiableZeroRem(BumpAlloc<> &alloc,
                                                 size_t iFree,
                                                 std::array<size_t, 2> inds,
                                                 size_t numRow) const -> bool {
    invariant(numRow <= getNumCons());
    auto p = alloc.scope();
    Simplex *subSimp{Simplex::create(alloc, numRow, iFree++)};
    auto fC{getConstraints()};
    auto sC{subSimp->getConstraints()};
    auto r = _(0, numRow);
    sC(_, 0) << fC(r, 0) - (fC(r, inds[0] + iFree) + fC(r, inds[1] + iFree));
    sC(_, _(1, iFree)) << fC(r, _(1, iFree));
    return subSimp->initiateFeasible();
  }
  [[nodiscard]] constexpr auto satisfiableZeroRem(BumpAlloc<> &alloc,
                                                  PtrVector<int64_t> x,
                                                  size_t off,
                                                  size_t numRow) const -> bool {
    return !unSatisfiableZeroRem(alloc, x, off, numRow);
  }
  void printResult(size_t numSlack = 0) {
    auto C{getConstraints()};
    auto basicVars{getBasicVariables()};
    for (size_t i = 0; i < basicVars.size(); ++i) {
      size_t v = basicVars[i];
      if (v <= numSlack) continue;
      if (C(i, 0)) {
        if (++v < C.numCol()) {
          llvm::errs() << "v_" << v - numSlack << " = " << C(i, 0) << " / "
                       << C(i, v) << "\n";
        } else {
          llvm::errs() << "v_" << v << " = " << C(i, 0) << "\n";
          assert(false);
        }
      }
    }
  }
  static constexpr auto create(BumpAlloc<> &alloc, unsigned numCon,
                               unsigned numVar) -> NotNull<Simplex> {
    return create(alloc, numCon, numVar, numCon, numVar + numCon);
  }
  static constexpr auto create(BumpAlloc<> &alloc, unsigned numCon,
                               unsigned numVar, unsigned conCap,
                               unsigned varCap) -> NotNull<Simplex> {

    size_t memNeeded = tableauOffset(conCap, varCap) +
                       sizeof(value_type) * reservedTableau(conCap, varCap);
    auto *mem =
      (Simplex *)alloc.allocate(sizeof(Simplex) + memNeeded, alignof(Simplex));
    mem->numConstraints = numCon;
    mem->numVars = numVar;
    mem->constraintCapacity = conCap;
    mem->varCapacity = varCap;
    return mem;
  }

  static auto operator new(size_t count, unsigned conCap, unsigned varCap)
    -> void * {
    size_t memNeeded = tableauOffset(conCap, varCap) +
                       sizeof(value_type) * reservedTableau(conCap, varCap);
    // void *p = ::operator new(count + memNeeded);
    return ::operator new(count + memNeeded,
                          std::align_val_t(alignof(Simplex)));
  }
  static void operator delete(void *ptr, size_t) {
    ::operator delete(ptr, std::align_val_t(alignof(Simplex)));
  }

  static auto create(unsigned numCon, unsigned numVar)
    -> std::unique_ptr<Simplex> {
    return create(numCon, numVar, numCon, numVar + numCon);
  }
  static auto create(unsigned numCon, unsigned numVar, unsigned conCap,
                     unsigned varCap) -> std::unique_ptr<Simplex> {
    auto *ret = new (conCap, varCap) Simplex;
    ret->numConstraints = numCon;
    ret->numVars = numVar;
    ret->constraintCapacity = conCap;
    ret->varCapacity = varCap;
    return std::unique_ptr<Simplex>(ret);
  }

  static constexpr auto
  create(BumpAlloc<> &alloc, unsigned numCon,
         unsigned numVar, // NOLINT(bugprone-easily-swappable-parameters)
         unsigned numSlack) -> NotNull<Simplex> {
    unsigned conCap = numCon, varCap = numVar + numSlack + numCon;
    return create(alloc, numCon, numVar, conCap, varCap);
  }
  constexpr auto copy(BumpAlloc<> &alloc) const -> NotNull<Simplex> {
    NotNull<Simplex> res =
      create(alloc, getNumCons(), getNumVars(), getConCap(), getVarCap());
    *res << *this;
    return res;
  }
  constexpr auto operator<<(const Simplex &other) -> Simplex & {
    setNumCons(other.getNumCons());
    setNumVars(other.getNumVars());
    getTableau() << other.getTableau();
    getBasicVariables() << other.getBasicVariables();
    getBasicConstraints() << other.getBasicConstraints();
    return *this;
  }
};

static_assert(AbstractVector<Simplex::Solution>);

static_assert(AbstractVector<PtrVector<Rational>>);
static_assert(AbstractVector<LinAlg::ElementwiseVectorBinaryOp<
                LinAlg::Sub, PtrVector<Rational>, PtrVector<Rational>>>);
static_assert(std::movable<Simplex::Solution::iterator>);
static_assert(std::indirectly_readable<Simplex::Solution::iterator>);
static_assert(std::forward_iterator<Simplex::Solution::iterator>);
