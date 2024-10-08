#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>

#ifndef USE_MODULE
#include "IR/Node.cxx"
#include "Polyhedra/Loops.cxx"
#include "IR/Address.cxx"
#include "Utilities/Valid.cxx"
#include "Math/Simplex.cxx"
#include "Math/Reductions.cxx"
#include "Utilities/Optional.cxx"
#include "Math/NormalForm.cxx"
#include "Math/ManagedArray.cxx"
#include "Utilities/Invariant.cxx"
#include "Math/GreatestCommonDivisor.cxx"
#include "Math/Constraints.cxx"
#include "Math/Comparisons.cxx"
#include "Math/Constructors.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#include "Polyhedra/Polyhedra.cxx"
#else
export module IR:DepPoly;
export import Polyhedra;
import Arena;
import Array;
import ArrayConstructors;
import Comparisons;
import Constraints;
import GCD;
import Invariant;
import ManagedArray;
import NormalForm;
import Optional;
import Reductions;
import Simplex;
import Valid;
import :Address;
import :AffineLoops;
import :Node;
#endif

using math::shape, math::matrix, math::PtrVector, math::PtrMatrix, utils::Valid,
  utils::Optional, utils::invariant;
#ifdef USE_MODULE
export namespace poly {
#else
namespace poly {
#endif
/// prints in current permutation order.
/// TODO: decide if we want to make poly::Loop a `SymbolicPolyhedra`
/// in which case, we have to remove `currentToOriginalPerm`,
/// which means either change printing, or move prints `<<` into
/// the derived classes.
inline auto printConstraints(std::ostream &os, DensePtrMatrix<int64_t> A,
                             math::PtrVector<IR::Value *> syms,
                             bool inequality) -> std::ostream & {
  Row numConstraints = A.numRow();
  unsigned numSyms = syms.size() + 1;
  for (ptrdiff_t c = 0; c < numConstraints; ++c) {
    printConstraint(os, A[c, _], numSyms, inequality);
    for (ptrdiff_t v = 1; v < numSyms; ++v) {
      if (int64_t Acv = A[c, v]) {
        os << (Acv > 0 ? " + " : " - ");
        Acv = math::constexpr_abs(Acv);
        if (Acv != 1) os << Acv << "*";
        os << *syms[v - 1];
      }
    }
    os << "\n";
  }
  return os;
}

/// DepPoly is a Polyhedra with equality constraints, representing the
/// overlapping iterations between two array accesses Given memory accesses
/// 0. C0*i0, over polyhedra A0 * i0 + b0 >= 0
/// 1. C1*i1, over polyhedra A1 * i1 + b1 >= 0
/// We construct a dependency polyehdra with equalities
/// C0*i0 == C1*i1
/// and inequalities
/// A0 * i0 + b0 >= 0
/// A1 * i1 + b1 >= 0
/// This can be represented as the inequality
/// [ A0  0   * [ i0    + [ b0   >= 0
///    0 A1 ]     i1 ]      b1 ]
/// and the equality
/// [ C0 -C1 ]  * [ i0    == [ 0 ]
///                 i1 ]
/// We require C0.numRow() == C1.numRow()
/// This number of rows equals the array dimensionality of the memory accesses.
/// The length of vector `i` equals the number of loops in the nest.
/// `b` may contain dynamic symbols. We match them between b0 and b1, (adding 0s
/// as necessary), so that b0 = b0_c + B0 * s b1 = b1_c + B1 * s where s is the
/// vector of dynamic symbols.
///
/// Additionally, we may have some number of time dimensions corresponding to
/// repeated memory accesses to the same address. E.g.,
/// 
///     for (int i = 0; i < N; ++i)
///       for (int j = 0; j < N; ++j)
///         for (int k = 0; k < N; ++k)
///           C[i,j] += A[i,k]*B[k,j];
/// 
/// We repeatedly access `C` across `k`.
/// We support arbitrary (so long as indexing is affine) repeat accesses to the
/// same address; this is just a trivial (matrix multiply) example.
///
/// Example:
/// for i = 1:N, j = 1:i
///     A[i,j] = foo(A[i,i])
/// labels: 0           1
///
/// Dependence Poly:
/// 1 <= i_0 <= N
/// 1 <= j_0 <= i_0
/// 1 <= i_1 <= N
/// 1 <= j_1 <= i_1
/// i_0 == i_1
/// j_0 == i_1
class DepPoly : public BasePolyhedra<true, true, false, DepPoly> {
  // initially means that the polyhedra is constructed with those as initial
  // values but that we may reduce these values through simplification/removal
  // of redundancies
  // Memory layout:
  // A, E, nullStep, s
  int numDep0Var;    // i0.size()
  int numDep1Var;    // i1.size()
  int numCon;        // initially: ineqConCapacity
  int numEqCon;      // initially: eqConCapacity
  int numDynSym;     // s.size()
  int timeDim;       // null space of memory accesses
  int conCapacity;   // A0.numRow() + A1.numRow()
  int eqConCapacity; // C0.numRow()
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) // FAM
  alignas(int64_t) char memory[];
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

  // [[nodiscard]] static auto allocate(Arena<> *alloc, unsigned
  // numDep0Var, unsigned numDep1Var, unsigned numCon, unsigned
  // numEqCon, unsigned numDynSym, unsigned timeDim, unsigned
  // conCapacity,
  //                                    unsigned eqConCapacity)->DepPoly * {

  // }

public:
  constexpr explicit DepPoly(int nd0, int nd1, int nds, int td, int conCap,
                             int eqConCap)
    : numDep0Var(nd0), numDep1Var(nd1), numCon(conCap), numEqCon(eqConCap),
      numDynSym(nds), timeDim(td), conCapacity(conCap),
      eqConCapacity(eqConCap) {}
  [[nodiscard]] constexpr auto getTimeDim() const -> int {
    invariant(timeDim >= 0);
    return timeDim;
  }
  constexpr void setTimeDim(int dim) {
    invariant(dim >= 0);
    timeDim = dim;
  }
  [[nodiscard]] constexpr auto getDim0() const -> int {
    invariant(numDep0Var >= 0);
    return numDep0Var;
  }
  [[nodiscard]] constexpr auto getDim1() const -> int {
    invariant(numDep1Var >= 0);
    return numDep1Var;
  }
  [[nodiscard]] constexpr auto getNumDynSym() const -> int {
    invariant(numDynSym >= 0);
    return numDynSym;
  }
  [[nodiscard]] constexpr auto getNumCon() const -> int {
    invariant(numCon >= 0);
    return numCon;
  }
  [[nodiscard]] constexpr auto getNumEqCon() const -> int {
    invariant(numEqCon >= 0);
    return numEqCon;
  }
  [[nodiscard]] constexpr auto getNumVar() const -> int {
    invariant(numDep0Var >= 0);
    invariant(numDep1Var >= 0);
    invariant(timeDim >= 0);
    invariant(numDynSym >= 0);
    return numDep0Var + numDep1Var + timeDim + numDynSym;
  }
  [[nodiscard]] constexpr auto getNumPhiCoef() const -> int {
    invariant(numDep0Var >= 0);
    invariant(numDep1Var >= 0);
    return numDep0Var + numDep1Var;
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoef() -> int { return 2; }
  [[nodiscard]] constexpr auto getNumScheduleCoef() const -> int {
    return getNumPhiCoef() + 2;
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> int {
    invariant(numCon >= 0);
    invariant(numEqCon >= 0);
    return 1 + numCon + 2 * numEqCon;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> int {
    invariant(numDynSym >= 0);
    return numDynSym + 1;
  }
  constexpr void setNumConstraints(int con) {
    invariant(con >= 0);
    numCon = con;
  }
  constexpr void setNumEqConstraints(int con) {
    invariant(con >= 0);
    numEqCon = con;
  }
  constexpr void decrementNumConstraints() { invariant(numCon-- > 0); }
  constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    void *p = memory;
    return {(int64_t *)p,
            math::DenseDims<>{math::row(numCon), math::col(getNumVar() + 1)}};
  }
  constexpr auto getE() -> MutDensePtrMatrix<int64_t> {
    void *p = memory;
    return {(int64_t *)p + size_t(conCapacity) * (getNumVar() + 1),
            math::DenseDims<>{math::row(numEqCon), math::col(getNumVar() + 1)}};
  }
  constexpr auto getNullStep() -> math::MutPtrVector<int64_t> {
    void *p = memory;
    return {((int64_t *)p) +
              (size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1),
            math::length(timeDim)};
  }
  [[nodiscard]] constexpr auto getNullStep(ptrdiff_t i) const -> int64_t {
    invariant(i >= 0);
    invariant(i < timeDim);
    const void *p = memory;
    return ((int64_t *)
              p)[(size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1) + i];
  }
  auto getSyms() -> math::MutPtrVector<IR::Value *> {
    char *p = memory;
    return {
      reinterpret_cast<IR::Value **>(
        p + sizeof(int64_t) *
              ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
      math::length(numDynSym)};
  }
  [[nodiscard]] auto getA() const -> DensePtrMatrix<int64_t> {
    const char *p = memory;
    return {const_cast<int64_t *>(reinterpret_cast<const int64_t *>(p)),
            math::DenseDims<>{math::row(numCon), math::col(getNumVar() + 1)}};
  }
  [[nodiscard]] auto getA(Row<> r, Col<> c) -> int64_t & {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return p[ptrdiff_t(r) * (getNumVar() + 1) + ptrdiff_t(c)];
  }
  [[nodiscard]] auto getA(Row<> r, Col<> c) const -> int64_t {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return p[ptrdiff_t(r) * (getNumVar() + 1) + ptrdiff_t(c)];
  }
  [[nodiscard]] auto getE() const -> DensePtrMatrix<int64_t> {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return {const_cast<int64_t *>(p + size_t(conCapacity) * (getNumVar() + 1)),
            math::DenseDims<>{math::row(numEqCon), math::col(getNumVar() + 1)}};
  }
  [[nodiscard]] auto getE(Row<> r, Col<> c) -> int64_t & {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return p[(conCapacity + ptrdiff_t(r)) * (getNumVar() + 1) + ptrdiff_t(c)];
  }
  [[nodiscard]] auto getE(Row<> r, Col<> c) const -> int64_t {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return p[(conCapacity + ptrdiff_t(r)) * (getNumVar() + 1) + ptrdiff_t(c)];
  }
  [[nodiscard]] auto getNullStep() const -> PtrVector<int64_t> {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return {const_cast<int64_t *>(p + (size_t(conCapacity) + eqConCapacity) *
                                        (getNumVar() + 1)),
            math::length(timeDim)};
  }
  [[nodiscard]] auto getSyms() const -> PtrVector<IR::Value *> {
    const char *p = memory;
    return {reinterpret_cast<IR::Value **>(
              const_cast<char *>(p) +
              sizeof(int64_t) *
                ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
            math::length(numDynSym)};
  }
  auto getSymbols(ptrdiff_t i) -> math::MutPtrVector<int64_t> {
    return getA()[i, _(math::begin, getNumSymbols())];
  }
  [[nodiscard]] auto getInEqSymbols(ptrdiff_t i) const -> PtrVector<int64_t> {
    return getA()[i, _(math::begin, getNumSymbols())];
  }
  [[nodiscard]] auto getEqSymbols(ptrdiff_t i) const -> PtrVector<int64_t> {
    return getE()[i, _(math::begin, getNumSymbols())];
  }
  [[nodiscard]] auto
  getCompTimeInEqOffset(ptrdiff_t i) const -> std::optional<int64_t> {
    if (!allZero(getA()[i, _(1, getNumSymbols())])) return {};
    return getA()[i, 0];
  }
  [[nodiscard]] auto
  getCompTimeEqOffset(ptrdiff_t i) const -> std::optional<int64_t> {
    if (!allZero(getE()[i, _(1, getNumSymbols())])) return {};
    return getE()[i, 0];
  }
  static constexpr auto findFirstNonEqual(PtrVector<int64_t> x,
                                          PtrVector<int64_t> y) -> ptrdiff_t {
    return std::distance(
      x.begin(), std::mismatch(x.begin(), x.end(), y.begin(), y.end()).first);
  }
  static auto nullSpace(Valid<const IR::Addr> x,
                        Valid<const IR::Addr> y) -> math::DenseMatrix<int64_t> {
    unsigned numLoopsCommon =
               findFirstNonEqual(x->getFusionOmega(), y->getFusionOmega()),
             xDim = x->numDim(), yDim = y->numDim();
    math::DenseMatrix<int64_t> A(
      math::DenseDims<>{math::row(numLoopsCommon), math::col(xDim + yDim)});
    if (!numLoopsCommon) return A;
    // indMats cols are [outerMostLoop,...,innerMostLoop]
    PtrMatrix<int64_t> indMatX = x->indexMatrix(), indMatY = y->indexMatrix();
    // unsigned indDepth = std::min(x->getNaturalDepth(), y->getNaturalDepth());
    // for (ptrdiff_t i = 0; i < std::min(numLoopsCommon, indDepth); ++i) {
    for (ptrdiff_t i = 0; i < numLoopsCommon; ++i) {
      if (i < indMatX.numCol()) A[i, _(0, xDim)] << indMatX[_, i];
      else A[i, _(0, xDim)] << 0;
      if (i < indMatY.numCol()) A[i, _(xDim, end)] << indMatY[_, i];
      else A[i, _(xDim, end)] << 0;
    }
    // for (ptrdiff_t i = indDepth; i < numLoopsCommon; ++i) A[i, _] << 0;
    // returns rank x num loops
    return math::orthogonalNullSpace(std::move(A));
  }
  static auto nullSpace(Valid<const IR::Addr> x) -> math::DenseMatrix<int64_t> {
    unsigned numLoopsCommon = x->getCurrentDepth(), dim = x->numDim(),
             natDepth = x->getNaturalDepth();
    math::DenseMatrix<int64_t> A(
      math::DenseDims<>{math::row(numLoopsCommon), math::col(dim)});
    if (!numLoopsCommon) return A;
    // indMats cols are [outerMostLoop,...,innerMostLoop]
    A[_(0, natDepth), _] << x->indexMatrix().t();
    if (natDepth < numLoopsCommon) A[_(natDepth, end), _] << 0;
    // returns rank x num loops
    return math::orthogonalNullSpace(std::move(A));
  }
  static auto symbolIndex(math::PtrVector<IR::Value *> s,
                          IR::Value *v) -> Optional<unsigned> {
    auto b = s.begin(), e = s.end();
    const auto *it = std::find(b, e, v);
    if (it == e) return {};
    return it - b;
  }
  auto symbolIndex(IR::Value *v) -> Optional<unsigned> {
    return symbolIndex(getSyms(), v);
  }
  /// Returns a map of s1's content's to s0's
  /// Values >= s0.size() are new symbols
  static auto mergeMap(math::Vector<unsigned> &map,
                       math::PtrVector<IR::Value *> s0,
                       math::PtrVector<IR::Value *> s1) -> unsigned {
    map.resizeForOverwrite(s1.size());
    unsigned n = s0.size();
    for (ptrdiff_t i = 0; i < s1.size(); ++i) {
      Optional<unsigned> j = symbolIndex(s0, s1[i]);
      map[i] = j ? *j : n++;
    }
    return n;
  }
  static void fillSyms(llvm::MutableArrayRef<const llvm::SCEV *> s,
                       std::array<llvm::ArrayRef<const llvm::SCEV *>, 2> sa,
                       math::Vector<unsigned> &map) {
    auto [sa0, sa1] = sa;
    size_t n = sa0.size();
    std::copy_n(sa0.begin(), n, s.begin());
    for (size_t i = 0; i < sa1.size(); ++i)
      if (unsigned j = map[i]; j >= n) s[j] = sa1[i];
  }
  [[nodiscard]] constexpr auto neededBytes() const -> size_t {
    return sizeof(DepPoly) +
           sizeof(int64_t) *
             ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim) +
           sizeof(const llvm::SCEV *) * numDynSym;
  }
  auto copy(Arena<> *alloc) const -> Valid<DepPoly> {
    auto *p = alloc->template allocate<DepPoly>(neededBytes());
    std::memcpy(p, this, neededBytes());
    return Valid<DepPoly>{p};
  }
  static auto dependence(Valid<Arena<>> alloc, Valid<const IR::Addr> aix,
                         Valid<const IR::Addr> aiy) -> DepPoly * {
    assert(aix->sizesMatch(aiy));
    unsigned numDep0Var = aix->getCurrentDepth(),
             numDep1Var = aiy->getCurrentDepth(),
             numVar = numDep0Var + numDep1Var;
    Valid<const poly::Loop> loopx = aix->getAffLoop();
    Valid<const poly::Loop> loopy = aiy->getAffLoop();
    PtrMatrix<int64_t> Ax{loopx->getOuterA(numDep0Var)},
      Ay{loopy->getOuterA(numDep1Var)};
    auto Sx{loopx->getSyms()}, Sy{loopy->getSyms()};
    // numLoops x numDim
    PtrMatrix<int64_t> Cx{aix->indexMatrix()}, Cy{aiy->indexMatrix()},
      Ox{aix->offsetMatrix()}, Oy{aiy->offsetMatrix()};
    invariant(Cx.numRow(), Cy.numRow());
    invariant(Cx.numCol() <= numDep0Var);
    invariant(Cy.numCol() <= numDep1Var);
    auto [nc0, nv0] = shape(Ax);
    auto [nc1, nv1] = shape(Ay);

    math::Vector<unsigned> map;
    unsigned numDynSym = mergeMap(map, Sx, Sy);
    invariant(ptrdiff_t(map.size()), ptrdiff_t(Sy.size()));
    unsigned numSym = numDynSym + 1;
    math::DenseMatrix<int64_t> NS{nullSpace(aix, aiy)};
    ptrdiff_t timeDim = ptrdiff_t{NS.numRow()},
              numCols = numVar + timeDim + numDynSym + 1,
              conCapacity = ptrdiff_t(Ax.numRow() + Ay.numRow()) + numVar,
              eqConCapacity = ptrdiff_t(Cx.numRow()) + timeDim;

    size_t memNeeded =
      sizeof(int64_t) * ((conCapacity + eqConCapacity) * numCols + timeDim) +
      sizeof(const llvm::SCEV *) * numDynSym;

    auto p = alloc->checkpoint();
    auto *mem = (DepPoly *)alloc->allocate(sizeof(DepPoly) + memNeeded);
    auto *dp = std::construct_at(mem, numDep0Var, numDep1Var, numDynSym,
                                 timeDim, conCapacity, eqConCapacity);

    // numDep1Var = nv1;
    ptrdiff_t nc = nc0 + nc1, indexDim{aix->numDim()};
    auto nullStep{dp->getNullStep()};
    for (ptrdiff_t i = 0; i < timeDim; ++i) nullStep[i] = norm2(NS[i, _]);
    //           column meansing in in order
    // const size_t numSymbols = getNumSymbols();
    auto A{dp->getA()};
    auto E{dp->getE()};
    A << 0;
    E << 0;
    // A.resize(nc + numVar, numSymbols + numVar + nullDim);
    // E.resize(indexDim + nullDim, A.numCol());
    // ma0 loop
    for (ptrdiff_t i = 0; i < nc0; ++i) {
      A[i, _(0, 1 + Sx.size())] << Ax[i, _(0, 1 + Sx.size())];
      A[i, _(numSym, numSym + numDep0Var)]
        << Ax[i, _(1 + Sx.size(), 1 + Sx.size() + numDep0Var)];
    }
    for (ptrdiff_t i = 0; i < nc1; ++i) {
      A[nc0 + i, 0] = Ay[i, 0];
      for (ptrdiff_t j = 0; j < map.size(); ++j)
        A[nc0 + i, 1 + map[j]] = Ay[i, 1 + j];
      for (ptrdiff_t j = 0; j < numDep1Var; ++j)
        A[nc0 + i, j + numSym + numDep0Var] = Ay[i, j + 1 + Sy.size()];
    }
    A[_(nc, end), _(numSym, numSym + numVar)].diag() << 1;
    // indMats are [outerMostLoop, ..., innerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E[i,:]* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    E[_(0, indexDim), 0] << aix->getOffsetOmega() - aiy->getOffsetOmega();
    for (ptrdiff_t i = 0; i < indexDim; ++i) {
      E[i, 1 + _(0, Ox.numCol())] << Ox[i, _];
      E[i, _(0, Cx.numCol()) + numSym] << Cx[i, _];
      for (ptrdiff_t j = 0, J = ptrdiff_t(Oy.numCol()); j < J; ++j)
        E[i, 1 + map[j]] -= Oy[i, j];
      E[i, _(0, Cy.numCol()) + numSym + numDep0Var] << -Cy[i, _];
    }
    for (ptrdiff_t i = 0; i < timeDim; ++i) {
      for (ptrdiff_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS[i, j];
        E[indexDim + i, j + numSym] = nsij;
        E[indexDim + i, j + numSym + numDep0Var] = -nsij;
      }
      E[indexDim + i, numSym + numVar + i] = 1;
    }
    dp->pruneBounds(*alloc);
    if (dp->getNumCon()) return dp;
    alloc->rollback(p);
    return nullptr;
  }
  // self dependence
  static auto self(Arena<> *alloc, Valid<const IR::Addr> ai) -> Valid<DepPoly> {
    Valid<const poly::Loop> loop = ai->getAffLoop();
    unsigned numDepVar = ai->getCurrentDepth(), numVar = numDepVar + numDepVar;
    PtrMatrix<int64_t> B{loop->getOuterA(numDepVar)};
    auto S{loop->getSyms()};
    // numLoops x numDim
    PtrMatrix<int64_t> C{ai->indexMatrix()}, O{ai->offsetMatrix()};

    auto [nco, nv] = shape(B);
    math::DenseMatrix<int64_t> NS{nullSpace(ai)};
    ptrdiff_t numDynSym = ptrdiff_t(S.size()), numSym = numDynSym + 1,
              timeDim = ptrdiff_t{NS.numRow()},
              numCols = numVar + timeDim + numDynSym + 1,
              conCapacity = 2 * ptrdiff_t(B.numRow()) + numVar,
              eqConCapacity = ptrdiff_t(C.numRow()) + timeDim;

    size_t memNeeded =
      sizeof(int64_t) * ((conCapacity + eqConCapacity) * numCols + timeDim) +
      sizeof(const llvm::SCEV *) * numDynSym;

    auto *mem = (DepPoly *)alloc->allocate(sizeof(DepPoly) + memNeeded);
    auto *dp = std::construct_at(mem, numDepVar, numDepVar, numDynSym, timeDim,
                                 conCapacity, eqConCapacity);

    // numDep1Var = nv1;
    ptrdiff_t nc = nco + nco, index_dim{ai->numDim()};
    auto nullStep{dp->getNullStep()};
    for (ptrdiff_t i = 0; i < timeDim; ++i) nullStep[i] = norm2(NS[i, _]);
    //           column meansing in in order
    // const size_t numSymbols = getNumSymbols();
    auto A{dp->getA()};
    auto E{dp->getE()};
    A << 0;
    E << 0;
    // A.resize(nc + numVar, numSymbols + numVar + nullDim);
    // E.resize(indexDim + nullDim, A.numCol());
    // ma0 loop
    for (ptrdiff_t i = 0; i < nco; ++i) {
      for (ptrdiff_t j = 0; j < numSym; ++j) A[i + nco, j] = A[i, j] = B[i, j];
      for (ptrdiff_t j = 0; j < numDepVar; ++j)
        A[i + nco, j + numSym + numDepVar] = A[i, j + numSym] =
          B[i, j + numSym];
    }
    A[_(nc, end), _(numSym, numSym + numVar)].diag() << 1;
    // L254: Assertion `col < numCol()` failed
    // indMats are [innerMostLoop, ..., outerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (ptrdiff_t i = 0; i < index_dim; ++i) {
      for (ptrdiff_t j = 0; j < C.numCol(); ++j) {
        int64_t Cji = C[i, j];
        E[i, j + numSym] = Cji;
        E[i, j + numSym + numDepVar] = -Cji;
      }
    }
    for (ptrdiff_t i = 0; i < timeDim; ++i) {
      for (ptrdiff_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS[i, j];
        E[index_dim + i, j + numSym] = nsij;
        E[index_dim + i, j + numSym + numDepVar] = -nsij;
      }
      E[index_dim + i, numSym + numVar + i] = 1;
    }
    dp->pruneBounds(*alloc);
    invariant(dp->getNumCon() > 0);
    return dp;
  }
  // `direction = true` means second dep follow first
  // lambda_0 + lambda*A*x = delta + c'x
  // x = [s, i]
  // delta =

  // order of variables:
  // [ lambda, phi, omega, w, u ]
  // new, post-change:
  // [ lambda, omega, phi, w, u ]
  //
  //
  // constraint order corresponds to old variables, will be in same order
  //
  // Time parameters are carried over into farkas polys
  [[nodiscard]] auto
  farkasPair(Arena<> *alloc) const -> std::array<math::Simplex *, 2> {

    auto A{getA()}, E{getE()};
    const ptrdiff_t numEqualityConstraintsOld = ptrdiff_t(E.numRow());
    const ptrdiff_t numInequalityConstraintsOld = ptrdiff_t(A.numRow());

    const ptrdiff_t numPhiCoefs = getNumPhiCoef();
    const ptrdiff_t numScheduleCoefs = numPhiCoefs + getNumOmegaCoef();
    const ptrdiff_t numBoundingCoefs = getNumSymbols();

    const ptrdiff_t numConstraintsNew = ptrdiff_t(A.numCol()) - getTimeDim();
    const ptrdiff_t numVarInterest = numScheduleCoefs + numBoundingCoefs;

    // lambda_0 + lambda'*A*i == psi'i
    // we represent equal constraint as
    // lambda_0 + lambda'*A*i - psi'i == 0
    // lambda_0 + (lambda'*A* - psi')i == 0
    // forward (0 -> 1, i.e. 1 >= 0):
    // psi'i = Phi_1'i_1 - Phi_0'i_0
    // backward (1 -> 0, i.e. 0 >= 1):
    // psi'i = Phi_0'i_0 - Phi_1'i_1
    // first, lambda_0:
    const ptrdiff_t ineqEnd = 1 + numInequalityConstraintsOld;
    const ptrdiff_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
    const ptrdiff_t numLambda = posEqEnd + numEqualityConstraintsOld;
    const ptrdiff_t numVarNew = numVarInterest + numLambda;
    invariant(ptrdiff_t(getNumLambda()), numLambda);
    // std::array<Valid<Simplex>, 2> pair;
    math::Simplex *fw = math::Simplex::create(
      alloc, math::row(numConstraintsNew), math::col(numVarNew), 0);
    // Simplex &fw(pair[0]);
    // fw.resize(numConstraintsNew, numVarNew + 1);
    auto fCF{fw->getConstraints()};
    fCF << 0;
    math::MutPtrMatrix<int64_t> fC{fCF[_, _(1, end)]};
    // fC(_, 0) << 0;
    fC[0, 0] = 1; // lambda_0
    fC[_, _(1, 1 + numInequalityConstraintsOld)]
      << A[_, _(math::begin, numConstraintsNew)].t();
    // fC(_, _(ineqEnd, posEqEnd)) = E.t();
    // fC(_, _(posEqEnd, numVarNew)) = -E.t();
    // loading from `E` is expensive
    // NOTE: if optimizing expression templates, should also
    // go through and optimize loops like this
    for (ptrdiff_t j = 0; j < numConstraintsNew; ++j) {
      for (ptrdiff_t i = 0; i < numEqualityConstraintsOld; ++i) {
        int64_t Eji = E[i, j];
        fC[j, i + ineqEnd] = Eji;
        fC[j, i + posEqEnd] = -Eji;
      }
    }
    // schedule
    // direction = true (aka forward=true)
    // mean x -> y, hence schedule y - schedule x >= 0
    //
    // if direction==true (corresponds to forward==true),
    // [numDep0Var...numVar) - [0...numDep0Var) + offset
    // else
    // [0...numDep0Var) - [numDep0Var...numVar) - offset
    // aka, we have
    // if direction
    // lambda_0 + lambda' * (b - A*i) + [0...numDep0Var) -
    // [numDep0Var...numVar) - offset == 0
    // else
    // lambda_0 + lambda' * (b - A*i) - [0...numDep0Var) +
    // [numDep0Var...numVar) + offset == 0
    //
    // boundAbove means we have
    // ... == w + u'*N + psi
    // -1 as we flip sign
    for (ptrdiff_t i = 0; i < numBoundingCoefs; ++i)
      fC[i, i + numScheduleCoefs + numLambda] = -1;

    // so far, both have been identical

    math::Simplex *bw = math::Simplex::create(
      alloc, math::row(numConstraintsNew), math::col(numVarNew), 0);
    auto bCF{bw->getConstraints()};
    bCF << fCF;
    // bCF(_, _(0, numVarNew + 1)) << fCF(_, _(0, numVarNew + 1));
    math::MutPtrMatrix<int64_t> bC{bCF[_, _(1, end)]};

    // equality constraints get expanded into two inequalities
    // a == 0 ->
    // even row: a <= 0
    // odd row: -a <= 0
    // fw means x'Al = x'(depVar1 - depVar0)
    // x'Al + x'(depVar0 - depVar1) = 0
    // so, for fw, depVar0 is positive and depVar1 is negative
    // note that we order the coefficients outer->inner
    // so that the ILP rLexMin on coefficients
    // will tend to preserve the initial order (which is
    // better than tending to reverse the initial order).
    fC[0, numLambda] = 1;
    fC[0, 1 + numLambda] = -1;
    bC[0, numLambda] = -1;
    bC[0, 1 + numLambda] = 1;
    for (ptrdiff_t i = 0; i < numPhiCoefs; ++i) {
      int64_t s = (2 * (i < numDep0Var) - 1);
      fC[i + numBoundingCoefs, i + numLambda + 2] = s;
      bC[i + numBoundingCoefs, i + numLambda + 2] = -s;
    }
    // note that delta/constant coef is handled as last `s`
    return {fw, bw};
  }

  /// Returns `true` if the array accesses are guaranteed independent
  /// conditioning on partial schedules xPhi and yPhi
  /// How this works:
  /// We create a new dependency polyhedra, and set the schedules `xPhi` and
  /// `yPhi` equal to one another, in addition to the equalities imposed
  /// by the need for the addresses to be equal.
  /// If that polyhedra is empty, then conditioning on these schedules,
  /// no intersection is left.
  [[nodiscard]] auto checkSat(Arena<> alloc, Valid<const poly::Loop> xLoop,
                              const int64_t *xOff, DensePtrMatrix<int64_t> xPhi,
                              Valid<const poly::Loop> yLoop,
                              const int64_t *yOff,
                              DensePtrMatrix<int64_t> yPhi) -> bool {
    // we take in loops because we might be moving deeper inside the loopnest
    // we take in offsets, because we might be offsetting the loops
    Row numPhi = xPhi.numRow();
    invariant(yPhi.numRow(), numPhi);
    DensePtrMatrix<int64_t> E{getE()};
    ptrdiff_t xNumLoops = ptrdiff_t(xPhi.numCol()),
              yNumLoops = ptrdiff_t(yPhi.numCol());
    if ((numDep0Var == xNumLoops) || allZero(xPhi[_, _(numDep0Var, end)]))
      xNumLoops = numDep0Var;
    else invariant(numDep0Var < xNumLoops);
    if ((numDep1Var == yNumLoops) || allZero(yPhi[_, _(numDep1Var, end)]))
      yNumLoops = numDep1Var;
    else invariant(numDep1Var < yNumLoops);
    unsigned numSym = getNumSymbols(), numSymX = numSym + xNumLoops,
             numSymD0 = numSym + numDep0Var, nCol = numSymX + yNumLoops;
    MutDensePtrMatrix<int64_t> B{matrix<int64_t>(
      &alloc, math::row(numEqCon + ptrdiff_t(numPhi)), math::col(nCol))};
    bool extend = (numDep0Var != xNumLoops) || (numDep1Var != yNumLoops);
    // we truncate time dim
    if (extend || timeDim) {
      for (ptrdiff_t r = 0; r < numEqCon; ++r) {
        B[r, _(0, numSymD0)] << E[r, _(0, numSymD0)];
        B[r, _(numDep0Var, xNumLoops) + numSym] << 0;
        B[r, _(0, numDep1Var) + numSymX] << E[r, _(0, numDep1Var) + numSymD0];
        B[r, _(numDep1Var, yNumLoops) + numSymX] << 0;
      }
    } else std::copy_n(E.begin(), E.size(), B.begin());
    if (xOff)
      for (ptrdiff_t c = 0; c < numDep0Var; ++c)
        if (int64_t mlt = xOff[c])
          B[_(0, numEqCon), 0] -= mlt * B[_(0, numEqCon), numSym + c];
    if (yOff)
      for (ptrdiff_t c = 0; c < numDep1Var; ++c)
        if (int64_t mlt = yOff[c])
          B[_(0, numEqCon), 0] -= mlt * B[_(0, numEqCon), numSymX + c];
    for (ptrdiff_t r = 0; r < numPhi; ++r) {
      B[r + numEqCon, _(0, numSym)] << 0;
      B[r + numEqCon, _(0, xNumLoops) + numSym] << xPhi[r, _(0, xNumLoops)];
      B[r + numEqCon, _(0, yNumLoops) + numSymX] << -yPhi[r, _(0, yNumLoops)];
    }
    ptrdiff_t rank = ptrdiff_t(math::NormalForm::simplifySystemImpl(B));
    // `B` is the new `EqCon`; if phi didn't add rank, then it isn't empty
    if (rank <= numEqCon) return false;
    unsigned numConstraints =
      extend ? (xLoop->getNumCon() + xNumLoops + yLoop->getNumCon() + yNumLoops)
             : numCon;
    size_t memNeeded =
      sizeof(int64_t) * (size_t(numConstraints + rank) * nCol) +
      sizeof(const llvm::SCEV *) * numDynSym;
    auto *mem = (DepPoly *)alloc.allocate(sizeof(DepPoly) + memNeeded);
    auto *dp = std::construct_at(mem, xNumLoops, yNumLoops, numDynSym, 0,
                                 numConstraints, rank);
    MutDensePtrMatrix<int64_t> A{dp->getA()};
    if (extend) {
      MutDensePtrMatrix<int64_t> Ax{xLoop->getA()}, Ay{yLoop->getA()};
      auto xS{xLoop->getSyms()}, yS{yLoop->getSyms()};
      math::Vector<unsigned> map;
      unsigned xNumSym = xS.size() + 1, xCon = xLoop->getNumCon(),
               yNumSym = yS.size() + 1, yCon = yLoop->getNumCon(),
               nDS = mergeMap(map, xS, yS), nLoop = xNumLoops + yNumLoops;
      // numSyms should be the same; we aren't pruning symbols
      invariant(numSym, 1 + nDS);
      for (ptrdiff_t r = 0; r < xCon; ++r) {
        A[r, _(0, xNumSym)] << Ax[r, _(0, xNumSym)];
        A[r, _(xNumSym, numSym)] << 0;
        A[r, _(0, xNumLoops) + numSym] << Ax[r, _(0, xNumLoops) + xNumSym];
        A[r, _(0, yNumLoops) + numSymX] << 0;
      }
      for (ptrdiff_t r = 0; r < yCon; ++r) {
        A[r + xCon, _(0, numSym)] << 0;
        for (ptrdiff_t j = 0; j < map.size(); ++j)
          A[r + xCon, 1 + map[j]] = Ay[r, 1 + j];
        A[r + xCon, _(0, xNumLoops) + numSym] << 0;
        A[r + xCon, _(0, yNumLoops) + numSymX]
          << Ay[r, _(0, yNumLoops) + yNumSym];
      }
      std::fill(A.begin() + size_t(xCon + yCon) * nCol, A.end(), 0);
      A[_(0, nLoop) + (xCon + yCon), _(0, nLoop) + numSym].diag() << 1;
    } else dp->getA() << getA()[_, _(0, nCol)]; // truncate time
    if (xOff)
      for (ptrdiff_t c = 0; c < xNumLoops; ++c)
        if (int64_t mlt = xOff[c]) A[_, 0] -= mlt * A[_, numSym + c];
    if (yOff)
      for (ptrdiff_t c = 0; c < yNumLoops; ++c)
        if (int64_t mlt = yOff[c]) A[_, 0] -= mlt * A[_, numSymX + c];
    dp->getE() << B[_(0, rank), _];
    dp->pruneBounds(alloc);
    return dp->getNumCon() == 0;
  }
  friend inline auto operator<<(std::ostream &os,
                                const DepPoly &p) -> std::ostream & {
    return printConstraints(
      printPositive(printConstraints(os << "\n", p.getA(), p.getSyms(), true),
                    p.getNumDynamic()),
      p.getE(), p.getSyms(), false);
  }

}; // class DepPoly
} // namespace poly
