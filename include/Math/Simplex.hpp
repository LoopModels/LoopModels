#pragma once
#include "./Constraints.hpp"
#include "./NormalForm.hpp"
#include "./Rational.hpp"
#include "Math/Array.hpp"
#include "Math/Indexing.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Utilities/Allocators.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
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
struct Tableau {
  int64_t *ptr{nullptr};
  // DenseDims dims;
  // DenseDims capacity;
  unsigned numConstraints{0};
  unsigned numVars{0};
  unsigned constraintCapacity;
  unsigned varCapacity;
#ifndef NDEBUG
  bool inCanonicalForm{false};
#endif
  // tableau is constraint * var matrix w/ extra col for LHS
  // and extra row for objective function
  [[nodiscard]] constexpr auto reservedTableau() const -> size_t {
    return (size_t(constraintCapacity) + 1) * (size_t(varCapacity) + 1);
  }
  [[nodiscard]] constexpr auto reservedBasicConstraints() const -> size_t {
    return varCapacity;
  }
  [[nodiscard]] constexpr auto reservedBasicVariables() const -> size_t {
    return constraintCapacity;
  }

  [[nodiscard]] constexpr auto intsNeeded() const -> size_t {
    return reservedTableau() + reservedBasicConstraints() +
           reservedBasicVariables();
  }
  /// [ value | objective function ]
  /// [ LHS   | tableau            ]
  [[nodiscard]] constexpr auto getTableau() const -> PtrMatrix<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            StridedDims{
              numConstraints + 1,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getTableau() -> MutPtrMatrix<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            StridedDims{
              numConstraints + 1,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getConstraints() const -> PtrMatrix<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables() +
              varCapacity + 1,
            StridedDims{
              numConstraints,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getConstraints() -> MutPtrMatrix<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables() +
              varCapacity + 1,
            StridedDims{
              numConstraints,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getBasicConstraints() const
    -> PtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr, numVars};
  }
  [[nodiscard]] constexpr auto getBasicConstraints() -> MutPtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr, numVars};
  }
  [[nodiscard]] constexpr auto getBasicVariables() const -> PtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints(), numConstraints};
  }
  [[nodiscard]] constexpr auto getBasicVariables() -> MutPtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints(), numConstraints};
  }
  [[nodiscard]] constexpr auto getCost() const -> PtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            numVars + 1};
  }
  [[nodiscard]] constexpr auto getCost() -> MutPtrVector<int64_t> {
    invariant(ptr != nullptr);
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            numVars + 1};
  }
  [[nodiscard]] constexpr auto getBasicConstraint(unsigned i) const -> int64_t {
    return getBasicConstraints()[i];
  }
  [[nodiscard]] constexpr auto getBasicVariable(unsigned i) const -> int64_t {
    return getBasicVariables()[i];
  }
  [[nodiscard]] constexpr auto getObjectiveCoefficient(unsigned i) const
    -> int64_t {
    return getCost()[++i];
  }
  [[nodiscard]] constexpr auto getObjectiveValue() -> int64_t & {
    return getCost()[0];
  }
  [[nodiscard]] constexpr auto getObjectiveValue() const -> int64_t {
    return getCost()[0];
  }
  constexpr void truncateConstraints(unsigned i) {
    assert(i <= numConstraints);
    numConstraints = i;
  }
  [[nodiscard]] constexpr auto getNumVar() const -> unsigned { return numVars; }
  [[nodiscard]] constexpr auto getNumConstraints() const -> unsigned {
    return numConstraints;
  }
  constexpr void hermiteNormalForm() {
#ifndef NDEBUG
    inCanonicalForm = false;
#endif
    truncateConstraints(
      unsigned(NormalForm::simplifySystemImpl(getConstraints(), 1)));
  }
#ifndef NDEBUG
  void assertCanonical() const {
    PtrMatrix<int64_t> C{getTableau()};
    PtrVector<int64_t> basicVars{getBasicVariables()};
    PtrVector<int64_t> basicCons{getBasicConstraints()};
    for (size_t v = 0; v < basicCons.size();) {
      int64_t c = basicCons[v++];
      if (c < 0) continue;
      assert(allZero(C(_(1, 1 + c), v)));
      assert(allZero(C(_(2 + c, end), v)));
      assert(size_t(basicVars[c]) == v - 1);
    }
    for (size_t c = 1; c < C.numRow(); ++c) {
      int64_t v = basicVars[c - 1];
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
  static constexpr auto create(BumpAlloc<> &alloc, unsigned conCap,
                               unsigned varCap) -> Tableau {
    Tableau tab{{conCap, varCap}};
    tab.ptr = alloc.allocate<int64_t>(tab.intsNeeded());
    return tab;
  }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static constexpr auto create(BumpAlloc<> &alloc, unsigned numCon,
                               unsigned numVar, unsigned conCap,
                               unsigned varCap) -> Tableau {
    Tableau tab{create(alloc, conCap, varCap)};
    tab.numConstraints = numCon;
    tab.numVars = numVar;
    return tab;
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
    if (basicVar >= 0) basicCons[basicVar] = c;
    constraints(c, _) << constraints(numConstraints, _);
  }

private:
  constexpr Tableau(std::array<unsigned, 2> capacity)
    : constraintCapacity(capacity[0]), varCapacity(capacity[1]) {}
};

struct Simplex {
  // we don't use FAM here, so that we can store multiple PtrSimplex
  // in a `Dependence` struct, and to allow the managed version to
  // reallocate the memory.
  Tableau tableau;
  unsigned numSlack;

  // AbstractVector
  struct Solution {
    using value_type = Rational;
    // view of tableau dropping const column
    Tableau tableau;
    size_t skippedVars{0};
    [[nodiscard]] constexpr auto operator[](size_t i) const -> Rational {
      i += skippedVars;
      int64_t j = tableau.getBasicConstraint(i);
      if (j < 0) return 0;
      PtrMatrix<int64_t> constraints = tableau.getConstraints();
      return Rational::create(constraints(j, 0), constraints(j, i + 1));
    }
    template <typename B, typename E>
    constexpr auto operator[](Range<B, E> r) const -> Solution {
      return (*this)[LinearAlgebra::canonicalizeRange(r, size())];
    }
    constexpr auto operator[](Range<size_t, size_t> r) const -> Solution {
      Tableau t = tableau;
      t.numVars = r.e;
      return {t, skippedVars + r.b};
    }
    [[nodiscard]] constexpr auto size() const -> size_t {
      return size_t(tableau.numVars) - skippedVars;
    }
    [[nodiscard]] constexpr auto view() const -> Solution { return *this; };
  };
  [[nodiscard]] constexpr auto getSolution() const -> Solution {
    return {tableau, 0};
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
  [[nodiscard("returns `true` if infeasible; should check when calling.")]] auto
  initiateFeasible() -> bool {
    // remove trivially redundant constraints
    tableau.hermiteNormalForm();
    // [ I;  X ; b ]
    //
    // original number of variables
    const auto numVar = ptrdiff_t(tableau.getNumVar());
    MutPtrMatrix<int64_t> C{tableau.getConstraints()};
    MutPtrVector<int64_t> basicCons{tableau.getBasicConstraints()};
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
          basicCons[v] = (((basicCons[v] == -2) && (Ccv > 0))) ? c : -1;
    }
    // basicCons should now contain either `-1` or an integer >= 0
    // indicating which row contains the only non-zero element; we'll
    // now fill basicVars.
    //
    auto basicVars{tableau.getBasicVariables()};
    basicVars << -1;
    for (ptrdiff_t v = 0; v < numVar; ++v) {
      if (int64_t r = basicCons[v]; r >= 0) {
        if (basicVars[r] == -1) basicVars[r] = v;
        else basicCons[v] = -1;
      }
    }
#ifndef NDEBUG
    tableau.inCanonicalForm = true;
#endif
    Vector<unsigned> augVars{};
    // upper bound number of augmentVars is constraintCapacity
    for (unsigned i = 0; i < basicVars.size(); ++i)
      if (basicVars[i] == -1) augVars.push_back(i);
    return (augVars.size() && removeAugmentVars(augVars));
  }
  auto removeAugmentVars(PtrVector<unsigned> augmentVars) -> bool {
    // TODO: try to avoid reallocating, via reserving enough ahead of time
    unsigned numAugment = augmentVars.size(), oldNumVar = tableau.numVars;
    assert(numAugment + tableau.numVars <= tableau.varCapacity);
    tableau.numVars += numAugment;
    MutPtrMatrix<int64_t> C{tableau.getConstraints()};
    MutPtrVector<int64_t> basicVars{tableau.getBasicVariables()};
    MutPtrVector<int64_t> basicCons{tableau.getBasicConstraints()};
    MutPtrVector<int64_t> costs{tableau.getCost()};
    costs << 0;
    for (ptrdiff_t i = 0; i < ptrdiff_t(augmentVars.size()); ++i) {
      ptrdiff_t a = augmentVars[i];
      basicVars[a] = i + oldNumVar;
      basicCons[i + oldNumVar] = a;
      C(a, oldNumVar + i) = 1;
      // we now zero out the implicit cost of `1`
      costs[_(begin, oldNumVar)] -= C(a, _(begin, oldNumVar));
    }
    assert(std::all_of(basicVars.begin(), basicVars.end(),
                       [](int64_t i) { return i >= 0; }));
    // false/0 means feasible
    // true/non-zero infeasible
    if (runCore()) return true;
    // check for any basic vars set to augment vars, and set them to some
    // other variable (column) instead.
    for (ptrdiff_t c = 0; c < C.numRow(); ++c) {
      if (basicVars[c] >= oldNumVar) {
        assert(C(c, 0) == 0);
        assert(c == basicCons[basicVars[c]]);
        assert(C(c, basicVars[c]) >= 0);
        // find var to make basic in its place
        for (ptrdiff_t v = oldNumVar; v != 0;) {
          // search for a non-basic variable
          // (basicConstraints<0)
          int64_t Ccv = C(c, v--);
          if (Ccv == 0 || (basicCons[v] >= 0)) continue;
          if (Ccv < 0) C(c, _) *= -1;
          for (size_t i = 0; i < C.numRow(); ++i)
            if (i != size_t(c)) NormalForm::zeroWithRowOp(C, i, c, v + 1, 0);
          basicVars[c] = v;
          basicCons[v] = c;
          break;
        }
      }
    }
    // all augment vars are now 0
    tableau.numVars = oldNumVar;
#ifndef NDEBUG
    tableau.assertCanonical();
#endif
    return false;
  }

  // 1 based to match getBasicConstraints
  [[nodiscard]] static constexpr auto
  getEnteringVariable(PtrVector<int64_t> costs) -> Optional<unsigned int> {
    // Bland's algorithm; guaranteed to terminate
    auto f = costs.begin(), l = costs.end();
    auto neg = std::find_if(f, l, [](int64_t c) { return c < 0; });
    if (neg == l) return {};
    return unsigned(std::distance(f, neg));
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
      int64_t Ci0 = C(i, 0);
      if (Ci0 == 0) return --i;
      assert(Ci0 > 0);
      if ((n * Ci0) >= (Civ * d)) continue;
      n = Civ;
      d = Ci0;
      j = i;
    }
    // NOTE: if we fail to find a leaving variable, then `j = 0`,
    // and it will unsigned wrap to `size_t(-1)`, which indicates
    // an empty `Optional<unsigned int>`
    return --j;
  }
  auto makeBasic(MutPtrMatrix<int64_t> C, int64_t f, unsigned int enteringVar)
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
    MutPtrVector<int64_t> basicVars{tableau.getBasicVariables()};
    int64_t oldBasicVar = basicVars[leavingVar];
    basicVars[leavingVar] = enteringVar;
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    basicConstraints[oldBasicVar] = -1;
    basicConstraints[enteringVar] = leavingVar;
    return f;
  }
  // run the simplex algorithm, assuming basicVar's costs have been set to
  // 0
  auto runCore(int64_t f = 1) -> Rational {
#ifndef NDEBUG
    assert(tableau.inCanonicalForm);
#endif
    //     return runCore(getCostsAndConstraints(), f);
    // }
    // Rational runCore(MutPtrMatrix<int64_t> C, int64_t f = 1) {
    MutPtrMatrix<int64_t> C{tableau.getTableau()};
    while (true) {
      // entering variable is the column
      Optional<unsigned int> enteringVariable =
        getEnteringVariable(C(0, _(1, end)));
      if (!enteringVariable) return Rational::create(C(0, 0), f);
      f = makeBasic(C, f, *enteringVariable);
      if (f == 0) return std::numeric_limits<int64_t>::max(); // unbounded
    }
  }
  // set basicVar's costs to 0, and then runCore()
  auto run() -> Rational {
#ifndef NDEBUG
    assert(tableau.inCanonicalForm);
    tableau.assertCanonical();
#endif
    MutPtrVector<int64_t> basicVars{tableau.getBasicVariables()};
    MutPtrMatrix<int64_t> C{tableau.getTableau()};
    int64_t f = 1;
    // zero cost of basic variables to put in canonical form
    for (size_t c = 0; c < basicVars.size();) {
      int64_t v = basicVars[c++];
      if ((size_t(++v) < C.numCol()) && C(0, v))
        f = NormalForm::zeroWithRowOp(C, 0, c, v, f);
    }
    return runCore(f);
  }

  // don't touch variables lex < v
  void lexCoreOpt(unsigned int v) {
    MutPtrMatrix<int64_t> C{tableau.getTableau()};
    MutPtrVector<int64_t> basicVars{tableau.getBasicVariables()};
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    while (true) {
      // get new entering variable
      Optional<unsigned int> enteringVariable =
        getEnteringVariable(C(0, _(v + 1, end)));
      if (!enteringVariable) break;
      auto ev = *enteringVariable + v;
      auto leaveOpt = getLeavingVariable(C, ev);
      if (!leaveOpt) break;
      unsigned int _lVar = *leaveOpt;
      unsigned int leavingVariable = _lVar++;
      for (size_t i = 0; i < C.numRow(); ++i)
        if (i != size_t(_lVar))
          NormalForm::zeroWithRowOp(C, i, _lVar, ev + 1, 0);
      // update baisc vars and constraints
      int64_t oldBasicVar = basicVars[leavingVariable];
      basicVars[leavingVariable] = ev;
      if (size_t(oldBasicVar) < basicConstraints.size())
        basicConstraints[oldBasicVar] = -1;
      basicConstraints[ev] = leavingVariable;
    }
  }
  // Assumes all <v have already been lex-minimized
  // v starts at 0
  // returns `false` if `0`, `true` if not zero
  // minimize v, not touching any variable lex < v
  auto lexMinimize(size_t v) -> bool {
#ifndef NDEBUG
    assert(tableau.inCanonicalForm);
#endif
    MutPtrMatrix<int64_t> C{tableau.getTableau()};
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    int64_t c = basicConstraints[v];
    if (c < 0) return false;
    // we try to zero `v` or at least minimize it.
    // implicitly, set cost to -1, and then see if we can make it
    // basic
    C(0, 0) = -C(++c, 0);
    //   we set  all prev and `v` to 0
    C(0, _(1, v + 2)) << 0;
    C(0, _(v + 2, end)) << -C(c, _(v + 2, end));
    assert((C(c, v + 1) != 0) || (C(c, 0) == 0));
    assert(allZero(C(_(1, c), v + 1)));
    assert(allZero(C(_(c + 1, end), v + 1)));
    lexCoreOpt(v);
    return makeZeroBasic(v);
  }
  /// makeZeroBasic(unsigned int v) -> bool
  /// Tries to make `v` non-basic if `v` is zero.
  /// Returns `false` if `v` is zero, `true` otherwise
  auto makeZeroBasic(unsigned int v) -> bool {
    MutPtrMatrix<int64_t> C{tableau.getTableau()};
    MutPtrVector<int64_t> basicVars{tableau.getBasicVariables()};
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    int64_t c = basicConstraints[v];
    int64_t cc = c++;
    // was not basic
    // not basic, v is  zero
    if (cc < 0) return false;
    // v is basic, but not zero
    if (C(c, 0) != 0) return true;
#ifndef NDEBUG
    tableau.assertCanonical();
#endif
    // so v is basic and zero.
    // We're going to try to make it non-basic
    for (auto ev = ptrdiff_t(C.numCol()); --ev > v + 1;) {
      // search for a non-basic variable (basicConstraints<0)
      auto evm1 = ev - 1;
      if ((basicConstraints[evm1] >= 0) || (C(c, ev) == 0)) continue;
      if (C(c, ev) < 0) C(c, _) *= -1;
      for (size_t i = 1; i < C.numRow(); ++i)
        if (i != size_t(c)) NormalForm::zeroWithRowOp(C, i, c, ev, 0);
      int64_t oldBasicVar = basicVars[cc];
      assert(oldBasicVar == int64_t(v));
      basicVars[cc] = evm1;
      // if (size_t(oldBasicVar) < basicConstraints.size())
      basicConstraints[oldBasicVar] = -1;
      basicConstraints[evm1] = cc;
      break;
    }
#ifndef NDEBUG
    tableau.assertCanonical();
#endif
    return false;
  }
  // lexicographically minimize vars [0, numVars)
  // false means no problems, true means there was a problem
  void lexMinimize(Vector<Rational> &sol) {
#ifndef NDEBUG
    assert(tableau.inCanonicalForm);
    tableau.assertCanonical();
#endif
    for (size_t v = 0; v < sol.size(); v++) lexMinimize(v);
    copySolution(sol);
#ifndef NDEBUG
    tableau.assertCanonical();
#endif
  }
  void copySolution(Vector<Rational> &sol) {
    MutPtrMatrix<int64_t> C{tableau.getConstraints()};
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    for (size_t v = 0; v < sol.size(); v++) {
      int64_t c = basicConstraints[v];
      sol[v] = c >= 0 ? Rational::create(C(c, 0), C(c, v + 1)) : Rational{0, 1};
    }
  }
  // A(:,1:end)*x <= A(:,0)
  // B(:,1:end)*x == B(:,0)
  // returns a Simplex if feasible, and an empty `Optional` otherwise
  static auto positiveVariables(BumpAlloc<> &alloc, PtrMatrix<int64_t> A,
                                PtrMatrix<int64_t> B)
    -> std::optional<Simplex> {
    invariant(A.numCol() == B.numCol());
    unsigned numVar = unsigned(A.numCol()), numSlack = unsigned(A.numRow()),
             numStrict = unsigned(B.numRow()), numCon = numSlack + numStrict,
             varCap = numVar + numSlack;
    // see how many slack vars are infeasible as solution
    // each of these will require an augment variable
    for (unsigned i = 0; i < numSlack; ++i) varCap += A(i, 0) < 0;
    // try to avoid reallocating
    auto checkPoint{alloc.checkPoint()};
    Simplex simplex{Simplex::create(alloc, numCon, numVar + numSlack, numCon,
                                    varCap, numSlack)};
    // construct:
    // [ I A
    //   0 B ]
    // then drop the extra variables
    slackEqualityConstraints(
      simplex.tableau.getConstraints()(_(0, numCon), _(1, numVar + numSlack)),
      A(_(0, numSlack), _(1, numVar)), B(_(0, numStrict), _(1, numVar)));
    auto consts{simplex.tableau.getConstants()};
    consts[_(0, numSlack)] << A(_, 0);
    consts[_(numSlack, numSlack + numStrict)] << B(_, 0);
    // for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    // for (size_t i = 0; i < numStrict; ++i) consts[i + numSlack] = B(i, 0);
    if (!simplex.initiateFeasible()) return simplex;
    alloc.checkPoint(checkPoint);
    return {};
  }
  static auto positiveVariables(BumpAlloc<> &alloc, PtrMatrix<int64_t> A)
    -> std::optional<Simplex> {
    unsigned numVar = unsigned(A.numCol()), numSlack = unsigned(A.numRow()),
             numCon = numSlack, varCap = numVar + numSlack;
    // see how many slack vars are infeasible as solution
    // each of these will require an augment variable
    for (unsigned i = 0; i < numSlack; ++i) varCap += A(i, 0) < 0;
    // try to avoid reallocating
    auto checkPoint{alloc.checkPoint()};
    Simplex simplex{
      Simplex::create(alloc, numCon, numVar, numCon, varCap, numSlack)};
    // construct:
    // [ I A ]
    // then drop the extra variables
    slackEqualityConstraints(
      simplex.tableau.getConstraints()(_(0, numCon), _(1, numVar + numSlack)),
      A(_(0, numSlack), _(1, numVar)));
    // auto consts{simplex.tableau.getConstants()};
    // for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    simplex.tableau.getConstants() << A(_, 0);
    if (!simplex.initiateFeasible()) return simplex;
    alloc.checkPoint(checkPoint);
    return {};
  }

  void pruneBounds(BumpAlloc<> &alloc) {
    auto p = alloc.checkPoint();
    Simplex simplex{Simplex::create(alloc, tableau.numConstraints,
                                    tableau.numVars, tableau.constraintCapacity,
                                    tableau.varCapacity, numSlack)};
    // Simplex simplex{getNumConstraints(), getNumVar(), getNumSlack(), 0};
    for (unsigned c = 0; c < tableau.getNumConstraints(); ++c) {
      simplex << *this;
      MutPtrMatrix<int64_t> constraints = simplex.tableau.getConstraints();
      int64_t bumpedBound = ++constraints(c, 0);
      MutPtrVector<int64_t> cost = simplex.tableau.getCost();
      for (size_t v = numSlack; v < cost.size(); ++v)
        cost[v] = -constraints(c, v + 1);
      if (simplex.run() != bumpedBound) tableau.deleteConstraint(c--);
    }
    alloc.checkPoint(p);
  }

  void removeVariable(size_t i) {
    // We remove a variable by isolating it, and then dropping the
    // constraint. This allows us to preserve canonical form
    MutPtrVector<int64_t> basicConstraints{tableau.getBasicConstraints()};
    MutPtrMatrix<int64_t> C{tableau.getConstraints()};
    // ensure sure `i` is basic
    if (basicConstraints[i] < 0) makeBasic(C, 0, i);
    size_t ind = basicConstraints[i];
    size_t lastRow = size_t(C.numRow() - 1);
    if (lastRow != ind) swap(C, Row{ind}, Row{lastRow});
    tableau.truncateConstraints(lastRow);
  }
  void removeExtraVariables(size_t i) {
    for (size_t j = tableau.getNumVar(); j > i;) {
      removeVariable(--j);
      tableau.truncateVars(j);
    }
  }
  static auto toMask(PtrVector<int64_t> x) -> uint64_t {
    assert(x.size() <= 64);
    uint64_t m = 0;
    for (auto y : x) m = ((m << 1) | (y != 0));
    return m;
  }
  [[nodiscard]] auto getBasicTrueVarMask() const -> uint64_t {
    const size_t numVarTotal = tableau.getNumVar();
    assert(numVarTotal <= 64);
    uint64_t m = 0;
    PtrVector<int64_t> basicCons{tableau.getBasicConstraints()};
    for (size_t i = numSlack; i < numVarTotal; ++i)
      m = ((m << 1) | (basicCons[i] > 0));
    return m;
  }
  // check if a solution exists such that `x` can be true.
  // returns `true` if unsatisfiable
  [[nodiscard]] auto unSatisfiable(BumpAlloc<> &alloc, PtrVector<int64_t> x,
                                   size_t off) const -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    for (auto y : x)
      if (y < 0) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    const size_t numCon = tableau.getNumConstraints(),
                 numVar = tableau.getNumVar(), numFix = x.size();
    auto p = alloc.checkPoint();
    Simplex subSimp{Simplex::create(alloc, numCon, numVar - numFix, 0)};
    // subSimp.tableau(0, 0) = 0;
    // subSimp.tableau(0, 1) = 0;
    auto fC{tableau.getTableau()};
    auto sC{subSimp.tableau.getTableau()};
    sC(_, 0) << fC(_, 0) - fC(_, _(1 + off, 1 + off + numFix)) * x;
    // sC(_, 0) = fC(_, 0);
    // for (size_t i = 0; i < numFix; ++i)
    //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
    sC(_, _(1, 1 + off)) << fC(_, _(1, 1 + off));
    sC(_, _(1 + off, end)) << fC(_, _(1 + off + numFix, end));
    // returns `true` if unsatisfiable
    bool res = subSimp.initiateFeasible();
    alloc.checkPoint(p);
    return res;
  }
  [[nodiscard]] auto satisfiable(BumpAlloc<> &alloc, PtrVector<int64_t> x,
                                 size_t off) const -> bool {
    return !unSatisfiable(alloc, x, off);
  }
  // check if a solution exists such that `x` can be true.
  // zeros remaining rows
  [[nodiscard]] auto unSatisfiableZeroRem(BumpAlloc<> &alloc,
                                          PtrVector<int64_t> x, size_t off,
                                          size_t numRow) const -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    for (auto y : x)
      if (y < 0) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    assert(numRow <= tableau.getNumConstraints());
    const size_t numFix = x.size();
    auto p = alloc.checkPoint();
    Simplex subSimp{Simplex::create(alloc, numRow, 1 + off, 0)};
    // subSimp.tableau(0, 0) = 0;
    // subSimp.tableau(0, 1) = 0;
    // auto fC{getCostsAndConstraints()};
    // auto sC{subSimp.getCostsAndConstraints()};
    auto fC{tableau.getConstraints()};
    auto sC{subSimp.tableau.getConstraints()};
    sC(_, 0) << fC(_(begin, numRow), 0) -
                  fC(_(begin, numRow), _(1 + off, 1 + off + numFix)) * x;
    // sC(_, 0) = fC(_, 0);
    // for (size_t i = 0; i < numFix; ++i)
    //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
    sC(_, _(1, 1 + off)) << fC(_(begin, numRow), _(1, 1 + off));
    assert(sC(_, _(1, 1 + off)) == fC(_(begin, numRow), _(1, 1 + off)));
    bool res = subSimp.initiateFeasible();
    alloc.checkPoint(p);
    return res;
  }
  [[nodiscard]] auto satisfiableZeroRem(BumpAlloc<> &alloc,
                                        PtrVector<int64_t> x, size_t off,
                                        size_t numRow) const -> bool {
    return !unSatisfiableZeroRem(alloc, x, off, numRow);
  }
  void printResult() {
    auto C{tableau.getConstraints()};
    auto basicVars{tableau.getBasicVariables()};
    // llvm::errs() << "Simplex solution:" << "\n";
    for (size_t i = 0; i < basicVars.size(); ++i) {
      size_t v = basicVars[i];
      if (v <= numSlack) continue;
      if (C(i, 0)) {
        if (v < C.numCol()) {
          llvm::errs() << "v_" << v - numSlack << " = " << C(i, 0) << " / "
                       << C(i, v) << "\n";
        } else {
          llvm::errs() << "v_" << v << " = " << C(i, 0) << "\n";
          assert(false);
        }
      }
    }
  }
  static constexpr auto
  create(BumpAlloc<> &alloc, unsigned numCon,
         unsigned numVar, // NOLINT(bugprone-easily-swappable-parameters)
         unsigned numSlack) -> Simplex {
    unsigned conCap = numCon, varCap = numVar + numSlack + numCon;
    return create(alloc, numCon, numVar, conCap, varCap, numSlack);
  }
  static constexpr auto
  create(BumpAlloc<> &alloc, unsigned numCon, unsigned numVar, unsigned conCap,
         unsigned varCap, // NOLINT(bugprone-easily-swappable-parameters)
         unsigned numSlack) -> Simplex {
    Tableau tableau = Tableau::create(alloc, numCon, numVar, conCap, varCap);
    return Simplex{tableau, numSlack};
  }
  constexpr auto operator<<(const Simplex &other) -> Simplex & {
    tableau.setNumCons(other.tableau.getNumCons());
    tableau.setNumVars(other.tableau.getNumVars());
    tableau.getTableau() << other.tableau.getTableau();
    tableau.getBasicVariables() << other.tableau.getBasicVariables();
    tableau.getBasicConstraints() << other.tableau.getBasicConstraints();
    numSlack = other.numSlack;
    return *this;
  }
};

static_assert(AbstractVector<Simplex::Solution>);
