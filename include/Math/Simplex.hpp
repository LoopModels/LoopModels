#pragma once
#include "./Constraints.hpp"
#include "./NormalForm.hpp"
#include "./Rational.hpp"
#include "Math/Array.hpp"
#include "Math/Indexing.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <tuple>

// #define VERBOSESIMPLEX

// We need a core Simplex type that is unmanaged
// then for convenience, it would be nice to manage it.
// Ideally, we could have a type hierarchy of
// unmanaged -> managed
// with some API to make the managed generic.
// We also want the managed to be automatically demotable to unmanaged,
// to avoid unnecessary specialization.

struct Tableau {
  int64_t *ptr;
  unsigned numConstraints;
  unsigned numVars;
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
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            StridedDims{
              numConstraints + 1,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getTableau() -> MutPtrMatrix<int64_t> {
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            StridedDims{
              numConstraints + 1,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getConstraints() const -> PtrMatrix<int64_t> {
    return {ptr + reservedBasicConstraints() + reservedBasicVariables() +
              varCapacity + 1,
            StridedDims{
              numConstraints,
              numVars + 1,
              varCapacity + 1,
            }};
  }
  [[nodiscard]] constexpr auto getConstraints() -> MutPtrMatrix<int64_t> {
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
    return {ptr, numVars};
  }
  [[nodiscard]] constexpr auto getBasicConstraints() -> MutPtrVector<int64_t> {
    return {ptr, numVars};
  }
  [[nodiscard]] constexpr auto getBasicVariables() const -> PtrVector<int64_t> {
    return {ptr + reservedBasicVariables(), numConstraints};
  }
  [[nodiscard]] constexpr auto getBasicVariables() -> MutPtrVector<int64_t> {
    return {ptr + reservedBasicVariables(), numConstraints};
  }
  [[nodiscard]] constexpr auto getObjective() const -> PtrVector<int64_t> {
    return {ptr + reservedBasicConstraints() + reservedBasicVariables(),
            numVars + 1};
  }
  [[nodiscard]] constexpr auto getObjective() -> MutPtrVector<int64_t> {
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
    return getObjective()[++i];
  }
  [[nodiscard]] constexpr auto getObjectiveValue() const -> int64_t {
    return getObjective()[0];
  }
  constexpr void truncateConstraints(unsigned i) {
    assert(i <= numConstraints);
    numConstraints = i;
  }
  constexpr void hermiteNormalForm() {
#ifndef NDEBUG
    inCanonicalForm = false;
#endif
    truncateConstraints(
      unsigned(NormalForm::simplifySystemImpl(getConstraints(), 1)));
  }
};

struct PtrSimplex {
  // we don't use FAM here, so that we can store multiple PtrSimplex
  // in a `Dependence` struct, and to allow the managed version to
  // reallocate the memory.
  Tableau tableau;
  unsigned numSlack;
  unsigned numArtificial;

  // AbstractVector
  struct Solution {
    using value_type = Rational;
    // view of tableau dropping const column
    PtrMatrix<int64_t> tableauView;
    StridedVector<int64_t> consts;
    constexpr auto operator[](size_t i) const -> Rational {
      int64_t j = tableauView(0, i);
      if (j < 0) return 0;
      return Rational::create(consts[j], tableauView(j + numExtraRows, i));
    }
    template <typename B, typename E>
    constexpr auto operator[](Range<B, E> r) -> Solution {
      return Solution{tableauView(_, r), consts};
    }
    [[nodiscard]] constexpr auto size() const -> size_t {
      return size_t(tableauView.numCol());
    }
    [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
  };
  [[nodiscard]] constexpr auto getSolution() const -> Solution {
    return Solution{tableau(_, _(numExtraCols, end)), getConstants()};
  }
};

/// The goal here:
/// this Simplex struct will orchestrate search through the solution space
/// it will add constraints as it goes, e.g. corresponding to desired properties
/// or as we move up loop levels to maintain independence from previous ones.
struct Simplex {
  // mapped to a PtrMatrix tableau
  // row 0: indicator indicating whether that column (variable) is basic, and
  //        if so which row (constraint) is the basic one.
  // row 1: cost numerators remaining rows: tableau numerators
  // column 0: indicates whether that row (constraint) is basic,
  //           and if so which one
  // column 1: constraint values
  // note all constraints are basic once in canonical form.
  LinearAlgebra::ManagedArray<int64_t, LinearAlgebra::StridedDims, 0> tableau;
  size_t numSlackVar{0};
#ifndef NDEBUG
  bool inCanonicalForm{false};
#endif
  static constexpr size_t numExtraRows = 2;
  static constexpr size_t numExtraCols = 1;
  static constexpr auto numTableauRows(size_t i) -> Row {
    return Row{i + numExtraRows};
  }
  static constexpr auto numTableauCols(size_t j) -> Col {
    return Col{j + numExtraCols};
  }
  constexpr Simplex(const AbstractMatrix auto &A) : tableau{A} {}
  constexpr Simplex(size_t numCon, size_t numVar, size_t numSlack,
                    size_t extraStride = 0)
    : tableau(StridedDims{numCon + numExtraRows,
                          numVar + numSlack + numExtraCols,
                          numVar + numCon + 2 + extraStride}),
      numSlackVar(numSlack) {
    assert(numCon > 0);
    assert(numVar > 0);
  }
  // NOTE: all methods resizing the tableau may invalidate references to it
  constexpr void resize(size_t numCon, size_t numVar) {
    tableau.resize({numTableauRows(numCon), numTableauCols(numVar)});
  }
  constexpr void resize(size_t numCon, size_t numVar,
                        LinearAlgebra::RowStride stride) {
    tableau.resize(
      StridedDims{numTableauRows(numCon), numTableauCols(numVar), stride});
  }
  constexpr void addVars(size_t numVars) {
    Col numCol = tableau.numCol() + numVars;
    tableau.resize(
      StridedDims{tableau.numRow(), numCol,
                  LinearAlgebra::RowStride{
                    std::max(size_t(numCol), size_t(tableau.rowStride()))}});
  }
  constexpr auto addConstraint() -> MutPtrVector<int64_t> {
    tableau.resize(
      StridedDims{tableau.numRow() + 1, tableau.numCol(), tableau.rowStride()});
    tableau(last, _) << 0;
    return tableau(last, _(numExtraCols, end));
  }
  constexpr auto addConstraintAndVar() -> MutPtrVector<int64_t> {
    tableau.resize(tableau.numRow() + 1, tableau.numCol() + 1);
    tableau(last, _) << 0;
    return tableau(last, _(numExtraCols, end));
  }
  constexpr auto addConstraintsAndVars(size_t i) -> MutPtrMatrix<int64_t> {
    tableau.resize(tableau.numRow() + i, tableau.numCol() + i);
    tableau(_(last - i, end), _) << 0;
    return tableau(_(last - i, end), _(numExtraCols, end));
  }
  constexpr void reserve(size_t numCon, size_t numVar) {
    tableau.reserve(
      numTableauRows(numCon),
      Col{size_t(max(numTableauCols(numVar), tableau.rowStride()))});
  }
  constexpr void clearReserve(size_t numCon, size_t numVar) {
    numSlackVar = 0;
    tableau.clearReserve(
      numTableauRows(numCon),
      Col{size_t(max(numTableauCols(numVar), tableau.rowStride()))});
  }
  constexpr void reserveExtraRows(size_t additionalRows) {
    tableau.reserve(tableau.numRow() + additionalRows, tableau.rowStride());
  }
  constexpr void reserveExtra(Row additionalRows, Col additionalCols) {
    LinearAlgebra::RowStride newStride =
      max(tableau.numCol() + additionalCols, tableau.rowStride());
    tableau.reserve(tableau.numRow() + additionalRows, newStride);
    if (newStride == tableau.rowStride()) return;
    // copy memory, so that incrementally adding columns is cheap later.
    Col nC = tableau.numCol();
    tableau.resize(StridedDims{tableau.numRow(), nC, newStride});
  }
  constexpr void reserveExtra(size_t additional) {
    reserveExtra(additional, additional);
  }
  constexpr void truncateVars(size_t numVars) {
    tableau.truncate(numTableauCols(numVars));
  }
  constexpr void truncateConstraints(size_t numCons) {
    tableau.truncate(numTableauRows(numCons));
  }
  constexpr void resizeForOverwrite(size_t numCon, size_t numVar) {
    tableau.resizeForOverwrite(numTableauRows(numCon), numTableauCols(numVar));
  }
  constexpr void resizeForOverwrite(size_t numCon, size_t numVar,
                                    size_t stride) {
    tableau.resizeForOverwrite(numTableauRows(numCon), numTableauCols(numVar),
                               stride);
  }
  [[nodiscard]] constexpr auto getCostsAndConstraints()
    -> MutPtrMatrix<int64_t> {
    return tableau(_(numExtraRows - 1, end), _(numExtraCols, end));
  }
  [[nodiscard]] constexpr auto getCostsAndConstraints() const
    -> PtrMatrix<int64_t> {
    return tableau(_(numExtraRows - 1, end), _(numExtraCols, end));
  }
  [[nodiscard]] constexpr auto getConstraints() -> MutPtrMatrix<int64_t> {
    return tableau(_(numExtraRows, end), _(numExtraCols, end));
  }
  [[nodiscard]] constexpr auto getConstraints() const -> PtrMatrix<int64_t> {
    return tableau(_(numExtraRows, end), _(numExtraCols, end));
  }
  // note that this is 1 more than the actual number of variables
  // as it includes the constants
  [[nodiscard]] constexpr auto getNumVar() const -> size_t {
    return size_t(tableau.numCol()) - numExtraCols;
  }
  [[nodiscard]] constexpr auto getNumConstraints() const -> size_t {
    return size_t(tableau.numRow()) - numExtraRows;
  }
  [[nodiscard]] constexpr auto getNumSlack() const -> size_t {
    return numSlackVar;
  }

  constexpr void hermiteNormalForm() {
#ifndef NDEBUG
    inCanonicalForm = false;
#endif
    truncateConstraints(
      size_t(NormalForm::simplifySystemImpl(getConstraints(), 1)));
  }
  constexpr void deleteConstraint(size_t c) {
    eraseConstraintImpl(tableau, numTableauRows(c));
    tableau.truncate(tableau.numRow() - 1);
  }
  [[nodiscard]] constexpr auto getTableauRow(size_t i) const
    -> PtrVector<int64_t> {
    return tableau(i, _(numExtraCols, end));
  }
  // 1-indexed, 0 returns value for const col
  [[nodiscard]] constexpr auto getBasicConstraints() const
    -> PtrVector<int64_t> {
    return getTableauRow(0);
  }
  [[nodiscard]] constexpr auto getCost() const -> PtrVector<int64_t> {
    return getTableauRow(1);
  }
  [[nodiscard]] constexpr auto getTableauRow(size_t i)
    -> MutPtrVector<int64_t> {
    return tableau(i, _(numExtraCols, end));
  }
  // 1-indexed, 0 returns value for const col
  [[nodiscard]] constexpr auto getBasicConstraints() -> MutPtrVector<int64_t> {
    return getTableauRow(0);
  }
  [[nodiscard]] constexpr auto getCost() -> MutPtrVector<int64_t> {
    return getTableauRow(1);
  }
  [[nodiscard]] constexpr auto getTableauCol(size_t i) const
    -> StridedVector<int64_t> {
    return tableau(_(numExtraRows, end), i);
    // return StridedVector<int64_t>{tableau.data() + i +
    //                                   numExtraRows *
    //                                   tableau.rowStride(),
    //                               getNumConstraints(),
    //                               tableau.rowStride()};
  }
  // 0-indexed
  [[nodiscard]] constexpr auto getBasicVariables() const
    -> StridedVector<int64_t> {
    return getTableauCol(0);
  }
  // StridedVector<int64_t> getDenominators() const {
  //     return getTableauCol(1);
  // }
  [[nodiscard]] constexpr auto getConstants() const -> StridedVector<int64_t> {
    return getTableauCol(numExtraCols);
  }
  [[nodiscard]] constexpr auto getTableauCol(size_t i)
    -> MutStridedVector<int64_t> {
    return tableau(_(numExtraRows, end), i);
    // return MutStridedVector<int64_t>{
    //     tableau.data() + i + numExtraRows * tableau.rowStride(),
    //     getNumConstraints(), tableau.rowStride()};
  }
  [[nodiscard]] constexpr auto getBasicVariables()
    -> MutStridedVector<int64_t> {
    return getTableauCol(0);
  }
  // MutStridedVector<int64_t> getDenominators() { return
  // getTableauCol(1); }
  [[nodiscard]] constexpr auto getConstants() -> MutStridedVector<int64_t> {
    return getTableauCol(numExtraCols);
  }
  // AbstractVector
  struct Solution {
    using value_type = Rational;
    static constexpr bool canResize = false;
    // view of tableau dropping const column
    PtrMatrix<int64_t> tableauView;
    StridedVector<int64_t> consts;
    auto operator[](size_t i) const -> Rational {
      int64_t j = tableauView(0, i);
      if (j < 0) return 0;
      return Rational::create(consts[j], tableauView(j + numExtraRows, i));
    }
    template <typename B, typename E>
    auto operator[](Range<B, E> r) -> Solution {
      return Solution{tableauView(_, r), consts};
    }
    [[nodiscard]] auto size() const -> size_t {
      return size_t(tableauView.numCol());
    }
    [[nodiscard]] auto view() const -> auto & { return *this; };
  };
  [[nodiscard]] auto getSolution() const -> Solution {
    return Solution{tableau(_, _(numExtraCols, end)), getConstants()};
  }

  // returns `true` if infeasible
  // `false ` if feasible
  [[nodiscard("returns `true` if infeasible; should check when calling.")]] auto
  initiateFeasible() -> bool {
    tableau(0, 0) = 0;
    // remove trivially redundant constraints
    hermiteNormalForm();
    // [ I;  X ; b ]
    //
    // original number of variables
    const auto numVar = ptrdiff_t(getNumVar());
    MutPtrMatrix<int64_t> C{getConstraints()};
    MutPtrVector<int64_t> basicCons{getBasicConstraints()};
    basicCons << -2;
    // first pass, we make sure the equalities are >= 0
    // and we eagerly try and find columns with
    // only a single non-0 element.
    for (ptrdiff_t c = 0; c < C.numRow(); ++c) {
      int64_t &Ceq = C(c, 0);
      if (Ceq >= 0) {
        // close-open and close-close are out, open-open is in
        for (ptrdiff_t v = 1; v < numVar; ++v) {
          if (int64_t Ccv = C(c, v)) {
            if (((basicCons[v] == -2) && (Ccv > 0))) basicCons[v] = c;
            else basicCons[v] = -1;
          }
        }
      } else {
        Ceq *= -1;
        for (ptrdiff_t v = 1; v < numVar; ++v) {
          if (int64_t Ccv = -C(c, v)) {
            if (((basicCons[v] == -2) && (Ccv > 0))) basicCons[v] = c;
            else basicCons[v] = -1;
            C(c, v) = Ccv;
          }
        }
      }
    }
    // basicCons should now contain either `-1` or an integer >= 0
    // indicating which row contains the only non-zero element; we'll
    // now fill basicVars.
    //
    auto basicVars{getBasicVariables()};
    basicVars << -1;
    for (ptrdiff_t v = 1; v < numVar; ++v) {
      int64_t r = basicCons[v];
      if (r >= 0) {
        if (basicVars[r] == -1) {
          basicVars[r] = v;
        } else {
          // this is reachable, as we could have
          // [ 1  1  0
          //   0  0  1 ]
          // TODO: is their actual harm in having multiple
          // basicCons?
          basicCons[v] = -1;
        }
      }
    }
#ifndef NDEBUG
    inCanonicalForm = true;
#endif
    llvm::SmallVector<unsigned> augmentVars{};
    for (unsigned i = 0; i < basicVars.size(); ++i)
      if (basicVars[i] == -1) augmentVars.push_back(i);
    if (augmentVars.size())
      if (removeAugmentVars(augmentVars, numVar)) return true;
#ifndef NDEBUG
    assertCanonical();
#endif
    return false;
  }
  auto removeAugmentVars(llvm::ArrayRef<unsigned> augmentVars, ptrdiff_t numVar)
    -> bool {
    // TODO: try to avoid reallocating, via reserving enough ahead of time
    addVars(augmentVars.size()); // NOTE: invalidates all refs
    MutPtrMatrix<int64_t> C{getConstraints()};
    MutStridedVector<int64_t> basicVars{getBasicVariables()};
    MutPtrVector<int64_t> basicCons{getBasicConstraints()};
    MutPtrVector<int64_t> costs{getCost()};
    tableau(1, _) << 0;
    for (ptrdiff_t i = 0; i < ptrdiff_t(augmentVars.size()); ++i) {
      ptrdiff_t a = augmentVars[i];
      basicVars[a] = i + numVar;
      basicCons[i + numVar] = a;
      C(a, numVar + i) = 1;
      // we now zero out the implicit cost of `1`
      costs[_(begin, numVar)] -= C(a, _(begin, numVar));
    }
    // false/0 means feasible
    // true/non-zero infeasible
    if (runCore() != 0) return true;
    for (ptrdiff_t c = 0; c < C.numRow(); ++c) {
      if (basicVars[c] >= numVar) {
        assert(C(c, 0) == 0);
        assert(c == basicCons[basicVars[c]]);
        assert(C(c, basicVars[c]) >= 0);
        // find var to make basic in its place
        for (ptrdiff_t v = numVar; v != 0;) {
          // search for a non-basic variable
          // (basicConstraints<0)
          assert(v > 1);
          if ((basicCons[--v] >= 0) || (C(c, v) == 0)) continue;
          if (C(c, v) < 0) C(c, _) *= -1;
          for (size_t i = 0; i < C.numRow(); ++i)
            if (i != size_t(c)) NormalForm::zeroWithRowOperation(C, i, c, v, 0);
          basicVars[c] = v;
          basicCons[v] = c;
          break;
        }
      }
    }
    // all augment vars are now 0
    truncateVars(numVar);
    return false;
  }
  // 1 based to match getBasicConstraints
  [[nodiscard]] static auto getEnteringVariable(PtrVector<int64_t> costs)
    -> Optional<unsigned int> {
    // Bland's algorithm; guaranteed to terminate
    auto f = costs.begin();
    auto l = costs.end();
    auto neg = std::find_if(f + 1, l, [](int64_t c) { return c < 0; });
    if (neg == l) return {};
    return unsigned(std::distance(f, neg));
  }
  [[nodiscard]] static auto getLeavingVariable(MutPtrMatrix<int64_t> C,
                                               size_t enteringVariable)
    -> Optional<unsigned int> {
    // inits guarantee first valid is selected
    // we need
    int64_t n = -1;
    int64_t d = 0;
    unsigned int j = 0;
    for (size_t i = 1; i < C.numRow(); ++i) {
      int64_t Civ = C(i, enteringVariable);
      if (Civ > 0) {
        int64_t Ci0 = C(i, 0);
        if (Ci0 == 0) return --i;
        assert(Ci0 > 0);
        if ((n * Ci0) < (Civ * d)) {
          n = Civ;
          d = Ci0;
          j = i;
        }
      }
    }
    // note, if we fail to find a leaving variable, then `j = 0`,
    // and it will unsigned wrap to `size_t(-1)`, which indicates
    // an empty `Optional<unsigned int>`
    return --j;
  }
  auto makeBasic(MutPtrMatrix<int64_t> C, int64_t f,
                 unsigned int enteringVariable) -> int64_t {
    Optional<unsigned int> leaveOpt = getLeavingVariable(C, enteringVariable);
    if (!leaveOpt) return 0; // unbounded
    unsigned int leavingVariable = *leaveOpt;
    for (size_t i = 0; i < C.numRow(); ++i)
      if (i != leavingVariable + 1) {
        int64_t m = NormalForm::zeroWithRowOperation(
          C, i, leavingVariable + 1, enteringVariable, i == 0 ? f : 0);
        if (i == 0) f = m;
      }
    // update baisc vars and constraints
    MutStridedVector<int64_t> basicVars{getBasicVariables()};
    int64_t oldBasicVar = basicVars[leavingVariable];
    basicVars[leavingVariable] = enteringVariable;
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    basicConstraints[oldBasicVar] = -1;
    basicConstraints[enteringVariable] = leavingVariable;
    return f;
  }
  // run the simplex algorithm, assuming basicVar's costs have been set to
  // 0
  auto runCore(int64_t f = 1) -> Rational {
#ifndef NDEBUG
    assert(inCanonicalForm);
#endif
    //     return runCore(getCostsAndConstraints(), f);
    // }
    // Rational runCore(MutPtrMatrix<int64_t> C, int64_t f = 1) {
    auto C{getCostsAndConstraints()};
    while (true) {
      // entering variable is the column
      Optional<unsigned int> enteringVariable = getEnteringVariable(C(0, _));
      if (!enteringVariable) return Rational::create(C(0, 0), f);
      f = makeBasic(C, f, *enteringVariable);
      if (f == 0) return std::numeric_limits<int64_t>::max(); // unbounded
    }
  }
  // set basicVar's costs to 0, and then runCore()
  auto run() -> Rational {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assertCanonical();
#endif
    MutStridedVector<int64_t> basicVars{getBasicVariables()};
    MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
    int64_t f = 1;
    // zero cost of basic variables to put in canonical form
    for (size_t c = 0; c < basicVars.size();) {
      int64_t v = basicVars[c++];
      if ((size_t(v) < C.numCol()) && C(0, v))
        f = NormalForm::zeroWithRowOperation(C, 0, c, v, f);
    }
    return runCore(f);
  }
#ifndef NDEBUG
  void assertCanonical() const {
    PtrMatrix<int64_t> C{getCostsAndConstraints()};
    StridedVector<int64_t> basicVars{getBasicVariables()};
    PtrVector<int64_t> basicConstraints{getBasicConstraints()};
    for (size_t v = 1; v < C.numCol(); ++v) {
      int64_t c = basicConstraints[v];
      if (c < 0) continue;
      assert(allZero(C(_(1, 1 + c), v)));
      assert(allZero(C(_(2 + c, end), v)));
      assert(size_t(basicVars[c]) == v);
    }
    for (size_t c = 1; c < C.numRow(); ++c) {
      int64_t v = basicVars[c - 1];
      if (size_t(v) < basicConstraints.size()) {
        assert(c - 1 == size_t(basicConstraints[v]));
        assert(C(c, v) >= 0);
      }
      assert(C(c, 0) >= 0);
    }
  }
// #else
//   static constexpr void assertCanonical() {}
#endif

  // don't touch variables lex < v
  void lexCoreOpt(unsigned int v) {
    MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
    MutStridedVector<int64_t> basicVars{getBasicVariables()};
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    while (true) {
      // get new entering variable
      Optional<unsigned int> enteringVariable =
        getEnteringVariable(C(0, _(v, end)));
      if (!enteringVariable) break;
      auto ev = *enteringVariable + v;
      auto leaveOpt = getLeavingVariable(C, ev);
      if (!leaveOpt) break;
      unsigned int _leavingVariable = *leaveOpt;
      unsigned int leavingVariable = _leavingVariable++;
      for (size_t i = 0; i < C.numRow(); ++i)
        if (i != size_t(_leavingVariable))
          NormalForm::zeroWithRowOperation(C, i, _leavingVariable, ev, 0);
      // update baisc vars and constraints
      int64_t oldBasicVar = basicVars[leavingVariable];
      basicVars[leavingVariable] = ev;
      if (size_t(oldBasicVar) < basicConstraints.size())
        basicConstraints[oldBasicVar] = -1;
      basicConstraints[ev] = leavingVariable;
    }
  }
  // Assumes all <v have already been lex-minimized
  // v starts at 1
  // returns `false` if `0`, `true` if not zero
  // minimize v, not touching any variable lex < v
  auto lexMinimize(size_t v) -> bool {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assert(v >= 1);
#endif
    MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    int64_t c = basicConstraints[v];
    if (c < 0) return false;
    // we try to zero `v` or at least minimize it.
    // implicitly, set cost to -1, and then see if we can make it
    // basic
    C(0, 0) = -C(++c, 0);
    C(0, _(1, v + 1)) << 0;
    C(0, _(v + 1, end)) << -C(c, _(v + 1, end));
    assert((C(c, v) != 0) || (C(c, 0) == 0));
    assert(allZero(C(_(1, c), v)));
    assert(allZero(C(_(c + 1, end), v)));
    lexCoreOpt(v);
    return makeZeroBasic(v);
  }
  auto makeZeroBasic(unsigned int v) -> bool {
    MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
    MutStridedVector<int64_t> basicVars{getBasicVariables()};
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    int64_t c = basicConstraints[v];
    int64_t cc = c++;
    if ((cc < 0) || (C(c, 0))) return cc >= 0;
// search for entering variable
#ifndef NDEBUG
    assertCanonical();
#endif
    for (auto ev = ptrdiff_t(C.numCol()); ev > v + 1;) {
      // search for a non-basic variable (basicConstraints<0)
      if ((basicConstraints[--ev] >= 0) || (C(c, ev) == 0)) continue;
      if (C(c, ev) < 0) C(c, _) *= -1;
      for (size_t i = 1; i < C.numRow(); ++i)
        if (i != size_t(c)) NormalForm::zeroWithRowOperation(C, i, c, ev, 0);
      int64_t oldBasicVar = basicVars[cc];
      assert(oldBasicVar == int64_t(v));
      basicVars[cc] = ev;
      // if (size_t(oldBasicVar) < basicConstraints.size())
      basicConstraints[oldBasicVar] = -1;
      basicConstraints[ev] = cc;
      break;
    }
#ifndef NDEBUG
    assertCanonical();
#endif
    return false;
  }
  // lex min the range [l, u), not touching any variable lex < l
  void lexMinimize(size_t l, size_t u) {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assert(l >= 1);
    assert(u > l);
#endif
    MutPtrMatrix<int64_t> C{getCostsAndConstraints()};
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    C(0, _) << 0;
    // for (size_t v = l; v < u; ++v)
    //     C(0, v) = (u - l) + u - v;
    C(0, _(l, u)) << 1;
    for (size_t v = l; v < u; ++v) {
      int64_t c = basicConstraints[v];
      if (c >= 0) NormalForm::zeroWithRowOperation(C, 0, ++c, v, 0);
    }
    lexCoreOpt(l - 1);
    for (size_t v = l; v < u; ++v) makeZeroBasic(v);
  }
  void lexMinimize(Range<size_t, size_t> r) { lexMinimize(r.b, r.e); }
  // lexicographically minimize vars [0, numVars)
  // false means no problems, true means there was a problem
  void lexMinimize(Vector<Rational> &sol) {
#ifndef NDEBUG
    assert(inCanonicalForm);
    assertCanonical();
#endif
    for (size_t v = 0; v < sol.size();) lexMinimize(++v);
    copySolution(sol);
#ifndef NDEBUG
    assertCanonical();
#endif
  }
  void copySolution(Vector<Rational> &sol) {
    MutPtrMatrix<int64_t> C{getConstraints()};
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    for (size_t v = 0; v < sol.size();) {
      size_t sv = v++;
      int64_t c = basicConstraints[v];
      sol[sv] = c >= 0 ? Rational::create(C(c, 0), C(c, v)) : Rational{0, 1};
    }
  }
  // A(:,1:end)*x <= A(:,0)
  // B(:,1:end)*x == B(:,0)
  // returns a Simplex if feasible, and an empty `Optional` otherwise
  static auto positiveVariables(PtrMatrix<int64_t> A, PtrMatrix<int64_t> B)
    -> std::optional<Simplex> {
    size_t numVar = size_t(A.numCol());
    assert(numVar == size_t(B.numCol()));
    size_t numSlack = size_t(A.numRow());
    size_t numStrict = size_t(B.numRow());
    size_t numCon = numSlack + numStrict;
    size_t extraStride = 0;
    // see how many slack vars are infeasible as solution
    for (unsigned i = 0; i < numSlack; ++i) extraStride += A(i, 0) < 0;
    // try to avoid reallocating
    Simplex simplex{numCon, numVar, numSlack, extraStride};
    // construct:
    // [ I A
    //   0 B ]
    // then drop the extra variables
    slackEqualityConstraints(
      simplex.getConstraints()(_(0, numCon), _(1, numVar + numSlack)),
      A(_(0, numSlack), _(1, numVar)), B(_(0, numStrict), _(1, numVar)));
    auto consts{simplex.getConstants()};
    for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    for (size_t i = 0; i < numStrict; ++i) consts[i + numSlack] = B(i, 0);
    if (simplex.initiateFeasible()) return {};
    return simplex;
  }
  static auto positiveVariables(PtrMatrix<int64_t> A)
    -> std::optional<Simplex> {
    size_t numVar = size_t(A.numCol());
    size_t numSlack = size_t(A.numRow());
    size_t numCon = numSlack;
    size_t extraStride = 0;
    // see how many slack vars are infeasible as solution
    for (unsigned i = 0; i < numSlack; ++i) extraStride += A(i, 0) < 0;
    // try to avoid reallocating
    Simplex simplex{numCon, numVar, numSlack, extraStride};
    // construct:
    // [ I A
    //   0 B ]
    // then drop the extra variables
    slackEqualityConstraints(
      simplex.getConstraints()(_(0, numCon), _(1, numVar + numSlack)),
      A(_(0, numSlack), _(1, numVar)));
    auto consts{simplex.getConstants()};
    for (size_t i = 0; i < numSlack; ++i) consts[i] = A(i, 0);
    if (simplex.initiateFeasible()) return {};
    return simplex;
  }

  void pruneBounds() {
    Simplex simplex{getNumConstraints(), getNumVar(), getNumSlack(), 0};
    for (size_t c = 0; c < getNumConstraints(); ++c) {
      simplex = *this;
      MutPtrMatrix<int64_t> constraints = simplex.getConstraints();
      int64_t bumpedBound = ++constraints(c, 0);
      MutPtrVector<int64_t> cost = simplex.getCost();
      for (size_t v = numSlackVar; v < cost.size(); ++v)
        cost[v] = -constraints(c, v);
      if (simplex.run() != bumpedBound) deleteConstraint(c--); // redundant
    }
  }

  void removeVariable(size_t i) {
    // We remove a variable by isolating it, and then dropping the
    // constraint. This allows us to preserve canonical form
    MutPtrVector<int64_t> basicConstraints{getBasicConstraints()};
    MutPtrMatrix<int64_t> C{getConstraints()};
    // ensure sure `i` is basic
    if (basicConstraints[i] < 0) makeBasic(C, 0, i);
    size_t ind = basicConstraints[i];
    size_t lastRow = size_t(C.numRow() - 1);
    if (lastRow != ind) swap(C, Row{ind}, Row{lastRow});
    truncateConstraints(lastRow);
  }
  void removeExtraVariables(size_t i) {
    for (size_t j = getNumVar(); j > i;) {
      removeVariable(--j);
      truncateVars(j);
    }
  }
  static auto toMask(PtrVector<int64_t> x) -> uint64_t {
    assert(x.size() <= 64);
    uint64_t m = 0;
    for (auto y : x) m = ((m << 1) | (y != 0));
    return m;
  }
  [[nodiscard]] auto getBasicTrueVarMask() const -> uint64_t {
    const size_t numVarTotal = getNumVar();
    assert(numVarTotal <= 64);
    uint64_t m = 0;
    PtrVector<int64_t> basicCons{getBasicConstraints()};
    for (size_t i = numSlackVar; i < numVarTotal; ++i)
      m = ((m << 1) | (basicCons[i] > 0));
    return m;
  }
  // check if a solution exists such that `x` can be true.
  // returns `true` if unsatisfiable
  [[nodiscard]] auto unSatisfiable(PtrVector<int64_t> x, size_t off) const
    -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    for (auto y : x)
      if (y < 0) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    const size_t numCon = getNumConstraints();
    const size_t numVar = getNumVar();
    const size_t numFix = x.size();
    Simplex subSimp{numCon, numVar - numFix, 0};
    subSimp.tableau(0, 0) = 0;
    subSimp.tableau(0, 1) = 0;
    auto fC{getCostsAndConstraints()};
    auto sC{subSimp.getCostsAndConstraints()};
    sC(_, 0) << fC(_, 0) - fC(_, _(1 + off, 1 + off + numFix)) * x;
    // sC(_, 0) = fC(_, 0);
    // for (size_t i = 0; i < numFix; ++i)
    //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
    sC(_, _(1, 1 + off)) << fC(_, _(1, 1 + off));
    sC(_, _(1 + off, end)) << fC(_, _(1 + off + numFix, end));
    // returns `true` if unsatisfiable
    return subSimp.initiateFeasible();
  }
  [[nodiscard]] auto satisfiable(PtrVector<int64_t> x, size_t off) const
    -> bool {
    return !unSatisfiable(x, off);
  }
  // check if a solution exists such that `x` can be true.
  // zeros remaining rows
  [[nodiscard]] auto unSatisfiableZeroRem(PtrVector<int64_t> x, size_t off,
                                          size_t numRow) const -> bool {
    // is it a valid solution to set the first `x.size()` variables to
    // `x`? first, check that >= 0 constraint is satisfied
    for (auto y : x)
      if (y < 0) return true;
    // approach will be to move `x.size()` variables into the
    // equality constraints, and then check if the remaining sub-problem
    // is satisfiable.
    assert(numRow <= getNumConstraints());
    const size_t numFix = x.size();
    Simplex subSimp{numRow, 1 + off, 0};
    subSimp.tableau(0, 0) = 0;
    subSimp.tableau(0, 1) = 0;
    // auto fC{getCostsAndConstraints()};
    // auto sC{subSimp.getCostsAndConstraints()};
    auto fC{getConstraints()};
    auto sC{subSimp.getConstraints()};
    sC(_, 0) << fC(_(begin, numRow), 0) -
                  fC(_(begin, numRow), _(1 + off, 1 + off + numFix)) * x;
    // sC(_, 0) = fC(_, 0);
    // for (size_t i = 0; i < numFix; ++i)
    //     sC(_, 0) -= x(i) * fC(_, i + 1 + off);
    sC(_, _(1, 1 + off)) << fC(_(begin, numRow), _(1, 1 + off));
    assert(sC(_, _(1, 1 + off)) == fC(_(begin, numRow), _(1, 1 + off)));
    return subSimp.initiateFeasible();
  }
  [[nodiscard]] auto satisfiableZeroRem(PtrVector<int64_t> x, size_t off,
                                        size_t numRow) const -> bool {
    return !unSatisfiableZeroRem(x, off, numRow);
  }
  void printResult() {
    auto C{getConstraints()};
    auto basicVars{getBasicVariables()};
    // llvm::errs() << "Simplex solution:" << "\n";
    for (size_t i = 0; i < basicVars.size(); ++i) {
      size_t v = basicVars[i];
      if (v <= numSlackVar) continue;
      if (C(i, 0)) {
        if (v < C.numCol()) {
          llvm::errs() << "v_" << v - numSlackVar << " = " << C(i, 0) << " / "
                       << C(i, v) << "\n";
        } else {
          llvm::errs() << "v_" << v << " = " << C(i, 0) << "\n";
          assert(false);
        }
      }
    }
  }
  friend inline auto operator<<(llvm::raw_ostream &os, const Simplex &s)
    -> llvm::raw_ostream & {
    return os << "\nSimplex; tableau = " << s.tableau;
  }
  /*
  std::tuple<Simplex, IntMatrix, uint64_t> rotate(const IntMatrix &A)
  const { PtrMatrix<const int64_t> C{getConstraints()};
      // C is
      // C(:,0) = C(:,1:numSlackVar)*s_0 + C(:,numSlackVar+1:end)*x
      // we implicitly have additional slack vars `s_1`
      // that define lower bounds of `x` as being 0.
      // 0 = I * s_1 - I * x
      // Calling `rotate(A)` defines `x = A*y`, and returns a simplex
      // in terms of `y`.
      // Thus, we have
      // C(:,0) = C(:,1:numSlackVar)*s_0 + (C(:,numSlackVar+1:end)*A)*y
      // 0 = I * s_1 - A * y
      // The tricky part is that if a row of `A` contains
      // i) more than 1 non-zero, or
      // ii) a negative entry
      // we do not have a clear 0 lower bound on `y`.
      // If we do have a clear 0 lower bound, we can avoid doing work
      // for that row, dropping it.
      // Else, we'll have compute it, calculating the offset needed
      // to lower bound it at 0.
      //
      // Idea for algorithm for getting a lower bound on a var `v`:
      // substitute v_i = v_i^+ - v_i^-
      // then add cost 2v_i^+ - v_i^-; minimize
      // while v_i^- > 0, redefine `v_i` to be offset by the value of
  `v_i`.

      const size_t numVarTotal = getNumVar();
      const size_t numVar = numVarTotal - numSlackVar;
      assert(A.numCol() == numVar);
      assert(A.numRow() == numVar);
      assert(numVar <= 64);
      uint64_t knownNonNegative = 0;
      // llvm::SmallVector<bool,64> knownNonNegative(numVar);
      for (size_t r = 0; r < numVar; ++r) {
          int nonNegativeIndex = -1;
          for (size_t c = 0; c < numVar; ++c) {
              if (int64_t Arc = A(r, c)) {
                  if ((Arc > 0) && (nonNegativeIndex == -1)) {
                      nonNegativeIndex = c;
                  } else {
                      nonNegativeIndex = -1;
                      break;
                  }
              }
          }
          // `A` is assumed to be full rank, so we can only hit a
  particular
          // `nonNegativeIndex != -1` once, meaning we do not risk
  flipping
          // a `true` back off with `^`.
          if (nonNegativeIndex >= 0)
              knownNonNegative ^= (uint64_t(1) <<
  uint64_t(nonNegativeIndex));
          // knownNonNegative[nonNegativeIndex] = true;
      }
      // all `false` indices of `knownNonNegative` indicate
      size_t numPositive = std::popcount(knownNonNegative);
      size_t numUnknownSign = numVar - numPositive;
      // Now, we create structure
      // C(:,0) = C(:,1:numSlackVar)*s_0 +
  (C(:,numSlackVar+1:end)*A(:,nn))*z
      //  + (C(:,numSlackVar+1:end)*A(:,!nn))*(y^+ - y^-)
      // C(:,0) = C(:,1:numSlackVar)*s_0 + (C(:,numSlackVar+1:end)*A)*z^*
      //  - (C(:,numSlackVar+1:end)*A(:,!nn))*y^-
      // 0 = I(!nn,:) * s_1 - A(!nn,:)*(y^+ - y^-)
      // where `nn` refers to the known non-negative indices
      // and we have separated `y` into `z`, `y^+`, and `y^-`.
      // z = y[nn]
      // y^+ - y^- = y[!nn]
      // y^+ >= 0
      // y^- >= 0
      //
      // Layout is as follows:
      // [1, s_0, s_1, z^*, y^-, aug]
      // where z^* is `z` intermixed with `y^+`
      // We will proceed by trying to maximize `y^-`,
      // shifting it until we get the maximum value
      // to be `0`, in which case we can drop it and let `y^+ -> z`.
      // once all `y^-` are gone, can we drop `s_1`???
      // TODO: see if this is legal, if so we probably want to separate
  them
      // We can then finally return the simplex as well as the shifts
  needed
      // for positivity.
      // `aug` are augments to get the simplex into canonical form.
      std::tuple<Simplex, IntMatrix, uint64_t> ret{
          {}, {numUnknownSign, numVar}, knownNonNegative};

      Simplex &simplex{std::get<0>(ret)};
      // IntMatrix &S{std::get<1>(ret)}; // S for Shift
      const size_t numConstraintsOld = getNumConstraints();
      // one additional constraint for each unknown sign
      size_t numConstraintsNew = numConstraintsOld + numUnknownSign;
      // numTrueBasic is the number of non-slack variables in the old
  simplex
      // that are basic
      // we'll add a temporary slack variable for each of these for sake
  of
      // initializing the tableau in canonical form.
      uint64_t basicTrueVarMask = getBasicTrueVarMask();
      size_t numTrueBasic = std::popcount(basicTrueVarMask);
      // additional variables are numUnownSign s_1s + numUnknownSign y^-s
  +
      // numTrueBasic.
      size_t numVarTotalNew = numVarTotal + numUnknownSign +
  numUnknownSign; size_t numVarTotalNewAug = numVarTotalNew +
  numTrueBasic; size_t s1Offset = 1 + numSlackVar; size_t zStarOffset =
  s1Offset + numUnknownSign; size_t yMinusOffset = zStarOffset + numVar;
      simplex.numSlackVar = numSlackVar + numUnknownSign;
      // resize instead of resizeForOverwrite because we want lots of 0s
      // maybe we should check if resizeForOverwrite + explicitly writing
  them
      // is faster
      simplex.resize(numConstraintsNew, numVarTotalNewAug);
      PtrMatrix<int64_t> D{simplex.getConstraints()};
      // first block of `D` corresponds to `s_0`, and is a copy of the
  slack
      // vars
      for (size_t j = 0; j < numConstraintsOld; ++j)
          for (size_t i = 0; i <= numSlackVar; ++i)
              D(j, i) = C(j, i);
      // next block of `D` is 0 (corresponding to s_1)
      // next block is C(:,trueVars)*A
      matmul(D.view(0, numConstraintsOld, zStarOffset, yMinusOffset),
             C.view(0, numConstraintsOld, zStarOffset, yMinusOffset), A);
      // then we have -C(:,trueVars)*A(:,!nn), corresponding to y^-
      for (size_t j = 0; j < numConstraintsOld; ++j) {
          uint64_t m = knownNonNegative;
          size_t k = numSlackVar + 1;
          for (size_t i = 0; i < numUnknownSign; ++i) {
              uint64_t o = std::countr_one(m);
              k += o;
              m >>= ++o;
              D(j, i + yMinusOffset) = -D(j, k++);
          }
      }
      // the final block corresponds to the augments; first, we set
  Cons=-1
      // so that we can also set these at the same time.
      PtrVector<int64_t> basicCons{simplex.getBasicConstraints()};
      for (auto &&x : basicCons)
          x = -1;
      MutStridedVector<int64_t> basicVars{simplex.getBasicVariables()};
      PtrVector<int64_t> costs{simplex.getCost()};
      if (numTrueBasic) {
          uint64_t m = basicTrueVarMask;
          size_t k = 0;
          for (size_t i = 0; i < numTrueBasic; ++i) {
              uint64_t o = std::countr_zero(m);
              k += o;
              m >>= ++o;
              for (size_t j = 0; j < numVarTotalNew; ++j)
                  costs[j] -= D(k, j);
              size_t c = numVarTotalNew + i;
              basicCons[c] = k;
              basicVars[k] = c;
              D(k++, c) = 1;
          }
      }
      // now for the new constraints
      // first block, corresponding to `s_0`; it is `0`
      // second block corresponds to `z^*`; we have rows of `A`
  corresponding
      // to unknown sign.
      // we also handle the y^- block here.
      {
          uint64_t m = knownNonNegative;
          size_t k = 0;
          for (size_t i = 0; i < numUnknownSign; ++i) {
              uint64_t o = std::countr_one(m);
              k += o;
              m >>= ++o;
              // copy A(k,:) into D(numConstraintsOld+i, ...)
              for (size_t j = 0; j < numVar; ++j)
                  D(numConstraintsOld + i, j + zStarOffset) = -A(k, j);
              size_t k2 = 0;
              size_t m2 = knownNonNegative;
              for (size_t j = 0; j < numUnknownSign; ++j) {
                  uint64_t o2 = std::countr_one(m2);
                  k2 += o2;
                  m2 >>= ++o2;
                  D(numConstraintsOld + i, j + yMinusOffset) = A(k, k2++);
              }
              ++k;
          }
      }
      // now the s_1 block is `I`
      for (size_t i = 0; i < numUnknownSign; ++i)
          D(numConstraintsOld + i, s1Offset + i) = 1;
      // finally, the augment block is `0`.

      // now we set the initial basicVars and basicCons
      // we already set the augments, corresponding to old constraints
      // where true variables were basic.
      // Now we set those corresponding to slack variables.
      PtrVector<const int64_t> basicConsOld{getBasicConstraints()};
      for (size_t i = 1; i <= numSlackVar; ++i) {
          int64_t j = basicConsOld[i];
          if (j >= 0) {
              basicCons[i] = j;
              basicVars[j] = i;
          }
      }
      // now we set those corresponding to the new constraints.
      // for the new constraints, it is simply `I` corresponding to `s_1`
      for (size_t i = 0; i < numUnknownSign; ++i) {
          basicCons[numVarTotal + i] = numConstraintsOld + i;
          basicVars[numConstraintsOld + i] = numVarTotal + i;
      }
      // now, initialize costs to remove augment vars
      if (numTrueBasic) {
          int64_t r = simplex.runCore();
          assert(r == 0);
          simplex.truncateVars(numVarTotalNew);
          // we re-zero costs so `numVarTotalNew > yMinusOffset` can
          // assume that costs are 0
          if (numUnknownSign)
              for (auto &&c : costs)
                  c = 0;
      }
      // now our variables are (no augment)
      // [1, s_0, s_1, z^*, y^-]
      // now, our goal is to eliminate `y^-`
      if (numVarTotal) {
          auto CC{simplex.getCostsAndConstraints()};
          while (true) {
              size_t i = numVarTotalNew;
              size_t j = zStarOffset + ((--i) - yMinusOffset);
              costs[i] = -1;
              int64_t c = basicCons[i];
              if (c != -1)
                  NormalForm::zeroWithRowOperation(CC, 0, ++c, j, 0);
              simplex.runCore();
              if ((basicCons[i] == -1) || (D(i, 0) == 0)) {
                  // i == 0
                  numVarTotalNew = i;
                  simplex.truncateVars(i);
                  if (numVarTotalNew == yMinusOffset)
                      break;
              } else {
                  // redefine variable, add offset to `S`
              }
              for (auto &&c : costs)
                  c = 0;
          }
      }
      return ret;
  }
  */
};
static_assert(AbstractVector<Simplex::Solution>);
