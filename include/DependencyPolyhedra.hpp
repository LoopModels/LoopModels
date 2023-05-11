#pragma once

#include "./Loops.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "Containers/TinyVector.hpp"
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Math.hpp"
#include "Math/Orthogonalize.hpp"
#include "Math/Polyhedra.hpp"
#include "Math/Simplex.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <tuple>
#include <utility>

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
/// ```
/// for (int i = 0; i < N; ++i)
///   for (int j = 0; j < N; ++j)
///     for (int k = 0; k < N; ++k)
///       C[i,j] += A[i,k]*B[k,j];
/// ```
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
  unsigned int numDep0Var;    // i0.size()
  unsigned int numDep1Var;    // i1.size()
  unsigned int numCon;        // initially: ineqConCapacity
  unsigned int numEqCon;      // initially: eqConCapacity
  unsigned int numDynSym;     // s.size()
  unsigned int timeDim;       // null space of memory accesses
  unsigned int conCapacity;   // A0.numRow() + A1.numRow()
  unsigned int eqConCapacity; // C0.numRow()
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

  // [[nodiscard]] static auto allocate(BumpAlloc<> &alloc, unsigned int
  // numDep0Var, unsigned int numDep1Var, unsigned int numCon, unsigned int
  // numEqCon, unsigned int numDynSym, unsigned int timeDim, unsigned int
  // conCapacity,
  //                                    unsigned int eqConCapacity)->DepPoly * {

  // }

public:
  constexpr explicit DepPoly(unsigned int nd0, unsigned int nd1,
                             unsigned int nds, unsigned int td,
                             unsigned int conCap, unsigned int eqConCap)
    : numDep0Var(nd0), numDep1Var(nd1), numCon(conCap), numEqCon(eqConCap),
      numDynSym(nds), timeDim(td), conCapacity(conCap),
      eqConCapacity(eqConCap) {}
  [[nodiscard]] constexpr auto getTimeDim() const -> unsigned int {
    return timeDim;
  }
  constexpr void setTimeDim(unsigned dim) { timeDim = dim; }
  [[nodiscard]] constexpr auto getDim0() const -> unsigned int {
    return numDep0Var;
  }
  [[nodiscard]] constexpr auto getDim1() const -> unsigned int {
    return numDep1Var;
  }
  [[nodiscard]] constexpr auto getNumDynSym() const -> unsigned int {
    return numDynSym;
  }
  [[nodiscard]] constexpr auto getNumCon() const -> unsigned int {
    return numCon;
  }
  [[nodiscard]] constexpr auto getNumEqCon() const -> unsigned int {
    return numEqCon;
  }
  [[nodiscard]] constexpr auto getNumVar() const -> unsigned int {
    return numDep0Var + numDep1Var + timeDim + numDynSym;
  }
  [[nodiscard]] constexpr auto getNumPhiCoef() const -> unsigned int {
    return numDep0Var + numDep1Var;
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoef() -> unsigned int {
    return 2;
  }
  [[nodiscard]] constexpr auto getNumScheduleCoef() const -> unsigned int {
    return getNumPhiCoef() + 2;
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> unsigned {
    return 1 + numCon + 2 * numEqCon;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> unsigned int {
    return numDynSym + 1;
  }
  constexpr void setNumConstraints(unsigned int con) { numCon = con; }
  constexpr void setNumEqConstraints(unsigned int con) { numEqCon = con; }
  constexpr void decrementNumConstraints() { invariant(numCon-- > 0); }
  constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    void *p = memory;
    return {(int64_t *)p, DenseDims{numCon, getNumVar() + 1}};
  }
  constexpr auto getE() -> MutDensePtrMatrix<int64_t> {
    void *p = memory;
    return {(int64_t *)p + size_t(conCapacity) * (getNumVar() + 1),
            DenseDims{numEqCon, getNumVar() + 1}};
  }
  constexpr auto getNullStep() -> MutPtrVector<int64_t> {
    void *p = memory;
    return {((int64_t *)p) +
              (size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1),
            timeDim};
  }
  [[nodiscard]] constexpr auto getNullStep(size_t i) const -> int64_t {
    invariant(i < timeDim);
    const void *p = memory;
    return ((int64_t *)
              p)[(size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1) + i];
  }
  auto getSyms() -> llvm::MutableArrayRef<const llvm::SCEV *> {
    char *p = memory;
    return {
      reinterpret_cast<const llvm::SCEV **>(
        p + sizeof(int64_t) *
              ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
      numDynSym};
  }
  [[nodiscard]] auto getA() const -> DensePtrMatrix<int64_t> {
    const char *p = memory;
    return {const_cast<int64_t *>(reinterpret_cast<const int64_t *>(p)),
            DenseDims{numCon, getNumVar() + 1}};
  }
  [[nodiscard]] auto getA(Row r, Col c) -> int64_t & {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return p[size_t(r) * (getNumVar() + 1) + size_t(c)];
  }
  [[nodiscard]] auto getA(Row r, Col c) const -> int64_t {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return p[size_t(r) * (getNumVar() + 1) + size_t(c)];
  }
  [[nodiscard]] auto getE() const -> DensePtrMatrix<int64_t> {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return {const_cast<int64_t *>(p + size_t(conCapacity) * (getNumVar() + 1)),
            DenseDims{numEqCon, getNumVar() + 1}};
  }
  [[nodiscard]] auto getE(Row r, Col c) -> int64_t & {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return p[(conCapacity + size_t(r)) * (getNumVar() + 1) + size_t(c)];
  }
  [[nodiscard]] auto getE(Row r, Col c) const -> int64_t {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return p[(conCapacity + size_t(r)) * (getNumVar() + 1) + size_t(c)];
  }
  [[nodiscard]] auto getNullStep() const -> PtrVector<int64_t> {
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return {const_cast<int64_t *>(p + (size_t(conCapacity) + eqConCapacity) *
                                        (getNumVar() + 1)),
            timeDim};
  }
  auto getSyms() const -> llvm::ArrayRef<const llvm::SCEV *> {
    const char *p = memory;
    return {
      reinterpret_cast<const llvm::SCEV *const *>(
        p + sizeof(int64_t) *
              ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
      numDynSym};
  }
  auto getSymbols(size_t i) -> MutPtrVector<int64_t> {
    return getA()(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getInEqSymbols(size_t i) const -> PtrVector<int64_t> {
    return getA()(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getEqSymbols(size_t i) const -> PtrVector<int64_t> {
    return getE()(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getCompTimeInEqOffset(size_t i) const
    -> std::optional<int64_t> {
    if (!allZero(getA()(i, _(1, getNumSymbols())))) return {};
    return getA()(i, 0);
  }
  [[nodiscard]] auto getCompTimeEqOffset(size_t i) const
    -> std::optional<int64_t> {
    if (!allZero(getE()(i, _(1, getNumSymbols())))) return {};
    return getE()(i, 0);
  }
  static constexpr auto findFirstNonEqual(PtrVector<int64_t> x,
                                          PtrVector<int64_t> y) -> size_t {
    return std::distance(
      x.begin(), std::mismatch(x.begin(), x.end(), y.begin(), y.end()).first);
  }
  static auto nullSpace(NotNull<const ArrayIndex> x,
                        NotNull<const ArrayIndex> y) -> DenseMatrix<int64_t> {
    const size_t numLoopsCommon =
      findFirstNonEqual(x->getFusionOmega(), y->getFusionOmega());
    const size_t xDim = x->getArrayDim();
    const size_t yDim = y->getArrayDim();
    DenseMatrix<int64_t> A(DenseDims{numLoopsCommon, xDim + yDim});
    if (!numLoopsCommon) return A;
    // indMats cols are [innerMostLoop, ..., outerMostLoop]
    PtrMatrix<int64_t> indMatX = x->indexMatrix();
    PtrMatrix<int64_t> indMatY = y->indexMatrix();
    for (size_t i = 0; i < numLoopsCommon; ++i) {
      A(i, _(begin, xDim)) << indMatX(i, _);
      A(i, _(xDim, end)) << indMatY(i, _);
    }
    // returns rank x num loops
    return orthogonalNullSpace(std::move(A));
  }
  static auto symbolIndex(llvm::ArrayRef<const llvm::SCEV *> s,
                          const llvm::SCEV *v) -> Optional<unsigned int> {
    auto b = s.begin(), e = s.end();
    const auto *it = std::find(b, e, v);
    if (it == e) return {};
    return it - b;
  }
  auto symbolIndex(const llvm::SCEV *v) -> Optional<unsigned int> {
    return symbolIndex(getSyms(), v);
  }
  /// Returns a map of s1's content's to s0's
  /// Values >= s0.size() are new symbols
  static auto mergeMap(Vector<unsigned> &map,
                       llvm::ArrayRef<const llvm::SCEV *> s0,
                       llvm::ArrayRef<const llvm::SCEV *> s1) -> unsigned {
    map.resizeForOverwrite(s1.size());
    size_t n = s0.size();
    for (size_t i = 0; i < s1.size(); ++i) {
      Optional<unsigned int> j = symbolIndex(s0, s1[i]);
      map[i] = j ? *j : n++;
    }
    return n;
  }
  static void fillSyms(llvm::MutableArrayRef<const llvm::SCEV *> s,
                       std::array<llvm::ArrayRef<const llvm::SCEV *>, 2> sa,
                       Vector<unsigned int> &map) {
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
  auto copy(BumpAlloc<> &alloc) const -> NotNull<DepPoly> {
    auto *p = alloc.template allocate<DepPoly>(neededBytes());
    std::memcpy(p, this, neededBytes());
    return NotNull<DepPoly>{p};
  }
  static auto dependence(BumpAlloc<> &alloc, NotNull<const ArrayIndex> aix,
                         NotNull<const ArrayIndex> aiy) -> DepPoly * {
    assert(aix->sizesMatch(aiy));
    NotNull<const AffineLoopNest<>> loopx = aix->getLoop();
    NotNull<const AffineLoopNest<>> loopy = aiy->getLoop();
    DensePtrMatrix<int64_t> Ax{loopx->getA()}, Ay{loopy->getA()};
    auto Sx{loopx->getSyms()}, Sy{loopy->getSyms()};
    // numLoops x numDim
    PtrMatrix<int64_t> Cx{aix->indexMatrix()}, Cy{aiy->indexMatrix()},
      Ox{aix->offsetMatrix()}, Oy{aiy->offsetMatrix()};
    invariant(Cx.numCol(), Cy.numCol());

    auto [nc0, nv0] = Ax.size();
    auto [nc1, nv1] = Ay.size();
    unsigned numDep0Var = loopx->getNumLoops();
    unsigned numDep1Var = loopy->getNumLoops();
    unsigned numVar = numDep0Var + numDep1Var;

    Vector<unsigned> map;
    unsigned numDynSym = mergeMap(map, Sx, Sy);
    invariant(size_t(map.size()), size_t(Sy.size()));
    unsigned numSym = numDynSym + 1;
    DenseMatrix<int64_t> NS{nullSpace(aix, aiy)};
    unsigned timeDim = unsigned{NS.numRow()};

    unsigned numCols = numVar + timeDim + numDynSym + 1;

    unsigned conCapacity = unsigned(Ax.numRow() + Ay.numRow()) + numVar;
    unsigned eqConCapacity = unsigned(Cx.numCol()) + timeDim;

    size_t memNeeded =
      sizeof(int64_t) * ((conCapacity + eqConCapacity) * numCols + timeDim) +
      sizeof(const llvm::SCEV *) * numDynSym;

    auto p = alloc.checkpoint();
    auto *mem =
      (DepPoly *)alloc.allocate(sizeof(DepPoly) + memNeeded, alignof(DepPoly));
    auto *dp = std::construct_at(mem, numDep0Var, numDep1Var, numDynSym,
                                 timeDim, conCapacity, eqConCapacity);

    // numDep1Var = nv1;
    const Row nc = nc0 + nc1;
    const size_t indexDim{aix->getArrayDim()};
    auto nullStep{dp->getNullStep()};
    for (size_t i = 0; i < timeDim; ++i) nullStep[i] = selfDot(NS(i, _));
    //           column meansing in in order
    // const size_t numSymbols = getNumSymbols();
    auto A{dp->getA()};
    auto E{dp->getE()};
    A << 0;
    E << 0;
    // A.resize(nc + numVar, numSymbols + numVar + nullDim);
    // E.resize(indexDim + nullDim, A.numCol());
    // ma0 loop
    for (size_t i = 0; i < nc0; ++i) {
      A(i, _(0, 1 + Sx.size())) << Ax(i, _(0, 1 + Sx.size()));
      A(i, _(numSym, numSym + numDep0Var))
        << Ax(i, _(1 + Sx.size(), 1 + Sx.size() + numDep0Var));
    }
    for (size_t i = 0; i < nc1; ++i) {
      A(nc0 + i, 0) = Ay(i, 0);
      for (size_t j = 0; j < map.size(); ++j)
        A(nc0 + i, 1 + map[j]) = Ay(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        A(nc0 + i, j + numSym + numDep0Var) = Ay(i, j + 1 + Sy.size());
    }
    A(_(nc, end), _(numSym, numSym + numVar)).diag() << 1;
    // L254: Assertion `col < numCol()` failed
    // indMats are [innerMostLoop, ..., outerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (size_t i = 0; i < indexDim; ++i) {
      E(i, _(0, Ox.numCol())) << Ox(i, _(0, Ox.numCol()));
      E(i, _(numSym, numDep0Var + numSym)) << Cx(_(0, numDep0Var), i);
      E(i, 0) -= Oy(i, 0);
      for (size_t j = 0; j < Oy.numCol() - 1; ++j)
        E(i, 1 + map[j]) -= Oy(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        E(i, j + numSym + numDep0Var) = -Cy(j, i);
    }
    for (size_t i = 0; i < timeDim; ++i) {
      for (size_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS(i, j);
        E(indexDim + i, j + numSym) = nsij;
        E(indexDim + i, j + numSym + numDep0Var) = -nsij;
      }
      E(indexDim + i, numSym + numDep0Var + numDep1Var + i) = 1;
    }
    dp->pruneBounds(alloc);
    if (dp->getNumCon()) return dp;
    alloc.rollback(p);
    return nullptr;
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
  [[nodiscard]] auto farkasPair(BumpAlloc<> &alloc) const
    -> std::array<NotNull<Simplex>, 2> {

    auto A{getA()}, E{getE()};
    const size_t numEqualityConstraintsOld = size_t(E.numRow());
    const size_t numInequalityConstraintsOld = size_t(A.numRow());

    const size_t numPhiCoefs = getNumPhiCoef();
    const size_t numScheduleCoefs = numPhiCoefs + getNumOmegaCoef();
    const size_t numBoundingCoefs = getNumSymbols();

    const size_t numConstraintsNew = size_t(A.numCol()) - getTimeDim();
    const size_t numVarInterest = numScheduleCoefs + numBoundingCoefs;

    // lambda_0 + lambda'*A*i == psi'i
    // we represent equal constraint as
    // lambda_0 + lambda'*A*i - psi'i == 0
    // lambda_0 + (lambda'*A* - psi')i == 0
    // forward (0 -> 1, i.e. 1 >= 0):
    // psi'i = Phi_1'i_1 - Phi_0'i_0
    // backward (1 -> 0, i.e. 0 >= 1):
    // psi'i = Phi_0'i_0 - Phi_1'i_1
    // first, lambda_0:
    const size_t ineqEnd = 1 + numInequalityConstraintsOld;
    const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
    const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
    const size_t numVarNew = numVarInterest + numLambda;
    invariant(size_t(getNumLambda()), numLambda);
    // std::array<NotNull<Simplex>, 2> pair;
    NotNull<Simplex> fw =
      Simplex::create(alloc, numConstraintsNew, numVarNew, 0);
    // Simplex &fw(pair[0]);
    // fw.resize(numConstraintsNew, numVarNew + 1);
    auto fCF{fw->getConstraints()};
    fCF << 0;
    MutPtrMatrix<int64_t> fC{fCF(_, _(1, end))};
    // fC(_, 0) << 0;
    fC(0, 0) = 1; // lambda_0
    fC(_, _(1, 1 + numInequalityConstraintsOld))
      << A(_, _(begin, numConstraintsNew)).transpose();
    // fC(_, _(ineqEnd, posEqEnd)) = E.transpose();
    // fC(_, _(posEqEnd, numVarNew)) = -E.transpose();
    // loading from `E` is expensive
    // NOTE: if optimizing expression templates, should also
    // go through and optimize loops like this
    for (size_t j = 0; j < numConstraintsNew; ++j) {
      for (size_t i = 0; i < numEqualityConstraintsOld; ++i) {
        int64_t Eji = E(i, j);
        fC(j, i + ineqEnd) = Eji;
        fC(j, i + posEqEnd) = -Eji;
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
    for (size_t i = 0; i < numBoundingCoefs; ++i)
      fC(i, i + numScheduleCoefs + numLambda) = -1;

    // so far, both have been identical

    NotNull<Simplex> bw =
      Simplex::create(alloc, numConstraintsNew, numVarNew, 0);
    auto bCF{bw->getConstraints()};
    bCF << fCF;
    // bCF(_, _(0, numVarNew + 1)) << fCF(_, _(0, numVarNew + 1));
    MutPtrMatrix<int64_t> bC{bCF(_, _(1, end))};

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
    fC(0, numLambda) = 1;
    fC(0, 1 + numLambda) = -1;
    bC(0, numLambda) = -1;
    bC(0, 1 + numLambda) = 1;
    for (size_t i = 0; i < numPhiCoefs; ++i) {
      int64_t s = (2 * (i < numDep0Var) - 1);
      fC(i + numBoundingCoefs, i + numLambda + 2) = s;
      bC(i + numBoundingCoefs, i + numLambda + 2) = -s;
    }
    // note that delta/constant coef is handled as last `s`
    return {fw, bw};
  }
  friend inline auto operator<<(llvm::raw_ostream &os, const DepPoly &p)
    -> llvm::raw_ostream & {
    return printConstraints(
      printPositive(printConstraints(os << "\n", p.getA(), p.getSyms()),
                    p.getNumDynamic()),
      p.getE(), p.getSyms(), false);
  }

}; // class DepPoly

/// Dependence
/// Represents a dependence relationship between two memory accesses.
/// It contains simplices representing constraints that affine schedules
/// are allowed to take.
class Dependence {
  // Plan here is...
  // depPoly gives the constraints
  // dependenceFwd gives forward constraints
  // dependenceBwd gives forward constraints
  // isBackward() indicates whether backward is non-empty
  // bounding constraints, used for ILP solve, are reverse,
  // i.e. fwd uses dependenceBwd and bwd uses dependenceFwd.
  //
  // Consider the following simple example dependencies:
  // for (k = 0; k < K; ++k)
  //   for (i = 0; i < I; ++i)
  //     for (j = 0; j < J; ++j)
  //       for (l = 0; l < L; ++l)
  //         A(i, j) = f(A(i+1, j), A(i, j-1), A(j, j), A(j, i), A(i, j -
  //         k))
  // label:     0             1        2          3        4        5
  // We have...
  ////// 0 <-> 1 //////
  // i_0 = i_1 + 1
  // j_0 = j_1
  // null spaces: [k_0, l_0], [k_1, l_1]
  // forward:  k_0 <= k_1 - 1
  //           l_0 <= l_1 - 1
  // backward: k_0 >= k_1
  //           l_0 >= l_1
  //
  //
  ////// 0 <-> 2 //////
  // i_0 = i_1
  // j_0 = j_1 - 1
  // null spaces: [k_0, l_0], [k_1, l_1]
  // forward:  k_0 <= k_1 - 1
  //           l_0 <= l_1 - 1
  // backward: k_0 >= k_1
  //           l_0 >= l_1
  //
  ////// 0 <-> 3 //////
  // i_0 = j_1
  // j_0 = j_1
  // null spaces: [k_0, l_0], [i_1, k_1, l_1]
  // forward:  k_0 <= k_1 - 1
  //           l_0 <= l_1 - 1
  // backward: k_0 >= k_1
  //           l_0 >= l_1
  //
  // i_0 = j_1, we essentially lose the `i` dimension.
  // Thus, to get fwd/bwd, we take the intersection of nullspaces to get
  // the time dimension?
  // TODO: try and come up with counter examples where this will fail.
  //
  ////// 0 <-> 4 //////
  // i_0 = j_1
  // j_0 = i_1
  // null spaces: [k_0, l_0], [k_1, l_1]
  // if j_0 > i_0) [store first]
  //   forward:  k_0 >= k_1
  //             l_0 >= l_1
  //   backward: k_0 <= k_1 - 1
  //             l_0 <= l_1 - 1
  // else (if j_0 <= i_0) [load first]
  //   forward:  k_0 <= k_1 - 1
  //             l_0 <= l_1 - 1
  //   backward: k_0 >= k_1
  //             l_0 >= l_1
  //
  // Note that the dependency on `l` is broken when we can condition on
  // `i_0
  // != j_0`, meaning that we can fully reorder interior loops when we can
  // break dependencies.
  //
  //
  ////// 0 <-> 5 //////
  // i_0 = i_1
  // j_0 = j_1 - k_1
  //
  //
  //

  [[no_unique_address]] NotNull<DepPoly> depPoly;
  [[no_unique_address]] NotNull<Simplex> dependenceSatisfaction;
  [[no_unique_address]] NotNull<Simplex> dependenceBounding;
  [[no_unique_address]] MemoryAccess in;
  [[no_unique_address]] MemoryAccess out;
  [[no_unique_address]] std::array<uint8_t, 7> satLvl{255, 255, 255, 255,
                                                      255, 255, 255};
  [[no_unique_address]] bool forward;

public:
  [[nodiscard]] constexpr auto input() const -> const MemoryAccess & {
    return in;
  }
  [[nodiscard]] constexpr auto output() const -> const MemoryAccess & {
    return out;
  }
  constexpr Dependence(NotNull<DepPoly> poly,
                       std::array<NotNull<Simplex>, 2> depSatBound,
                       std::array<NotNull<ArrayIndex>, 2> inOut, bool fwd)
    : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
      dependenceBounding(depSatBound[1]), in(inOut[0]), out(inOut[1]),
      forward(fwd) {}
  using BitSet = MemoryAccess::BitSet;
  [[nodiscard]] constexpr auto getSatLvl() -> std::array<uint8_t, 7> & {
    return satLvl;
  }
  [[nodiscard]] constexpr auto getSatLvl() const -> std::array<uint8_t, 7> {
    return satLvl;
  }
  constexpr auto stashSatLevel() -> Dependence & {
    assert(satLvl.back() == 255 || "satLevel overflow");
    std::copy_backward(satLvl.begin(), satLvl.end() - 1, satLvl.end());
    satLvl.front() = 255;
    return *this;
  }
  constexpr void popSatLevel() {
    std::copy(satLvl.begin() + 1, satLvl.end(), satLvl.begin());
#ifndef NDEBUG
    satLvl.back() = 255;
#endif
  }
  constexpr auto satLevel() -> uint8_t & { return satLvl.front(); }
  [[nodiscard]] constexpr auto getArrayPointer() -> const llvm::SCEV * {
    return in.getArrayPointer();
  }
  /// indicates whether forward is non-empty
  [[nodiscard]] constexpr auto isForward() const -> bool { return forward; }
  [[nodiscard]] constexpr auto nodesIn() const -> const BitSet & {
    return in.getNodes();
  }
  [[nodiscard]] constexpr auto nodesOut() const -> const BitSet & {
    return out.getNodes();
  }
  [[nodiscard]] constexpr auto getDynSymDim() const -> size_t {
    return depPoly->getNumDynSym();
  }
  [[nodiscard]] auto inputIsLoad() const -> bool { return in.isLoad(); }
  [[nodiscard]] auto outputIsLoad() const -> bool { return out.isLoad(); }
  [[nodiscard]] auto inputIsStore() const -> bool { return in.isStore(); }
  [[nodiscard]] auto outputIsStore() const -> bool { return out.isStore(); }
  /// getInIndMat() -> getInNumLoops() x arrayDim()
  [[nodiscard]] auto getInIndMat() const -> PtrMatrix<int64_t> {
    return in.indexMatrix();
  }
  constexpr void addEdge(size_t i) {
    in.addEdgeOut(i);
    out.addEdgeIn(i);
  }
  /// getOutIndMat() -> getOutNumLoops() x arrayDim()
  [[nodiscard]] constexpr auto getOutIndMat() const -> PtrMatrix<int64_t> {
    return out.indexMatrix();
  }
  [[nodiscard]] constexpr auto getInOutPair() const
    -> std::array<MemoryAccess, 2> {
    return {in, out};
  }
  // returns the memory access pair, placing the store first in the pair
  [[nodiscard]] auto getStoreAndOther() const -> std::array<MemoryAccess, 2> {
    if (in.isStore()) return {in, out};
    return {out, in};
  }
  [[nodiscard]] constexpr auto getInNumLoops() const -> size_t {
    return in.getNumLoops();
  }
  [[nodiscard]] constexpr auto getOutNumLoops() const -> size_t {
    return out.getNumLoops();
  }
  [[nodiscard]] constexpr auto isInactive(size_t depth) const -> bool {
    return (depth >= std::min(out.getNumLoops(), in.getNumLoops()));
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> size_t {
    return depPoly->getNumLambda() << 1;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> size_t {
    return depPoly->getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumPhiCoefficients() const -> size_t {
    return depPoly->getNumPhiCoef();
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoefficients() -> size_t {
    return DepPoly::getNumOmegaCoef();
  }
  [[nodiscard]] constexpr auto getNumDepSatConstraintVar() const -> size_t {
    return dependenceSatisfaction->getNumVars();
  }
  [[nodiscard]] constexpr auto getNumDepBndConstraintVar() const -> size_t {
    return dependenceBounding->getNumVars();
  }
  // returns `w`
  [[nodiscard]] constexpr auto getNumDynamicBoundingVar() const -> size_t {
    return getNumDepBndConstraintVar() - getNumDepSatConstraintVar();
  }
  constexpr void validate() {
    assert(getInNumLoops() + getOutNumLoops() == getNumPhiCoefficients());
    // 2 == 1 for const offset + 1 for w
    assert(2 + depPoly->getNumLambda() + getNumPhiCoefficients() +
             getNumOmegaCoefficients() ==
           size_t(dependenceSatisfaction->getConstraints().numCol()));
  }
  [[nodiscard]] constexpr auto getDepPoly() -> NotNull<DepPoly> {
    return depPoly;
  }
  [[nodiscard]] constexpr auto getNumConstraints() const -> size_t {
    return dependenceBounding->getNumCons() +
           dependenceSatisfaction->getNumCons();
  }
  [[nodiscard]] auto getSatConstants() const -> StridedVector<int64_t> {
    return dependenceSatisfaction->getConstants();
  }
  [[nodiscard]] auto getBndConstants() const -> StridedVector<int64_t> {
    return dependenceBounding->getConstants();
  }
  [[nodiscard]] auto getSatConstraints() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction->getConstraints();
  }
  [[nodiscard]] auto getBndConstraints() const -> PtrMatrix<int64_t> {
    return dependenceBounding->getConstraints();
  }
  [[nodiscard]] auto getSatLambda() const -> PtrMatrix<int64_t> {
    return getSatConstraints()(_, _(1, 1 + depPoly->getNumLambda()));
  }
  [[nodiscard]] auto getBndLambda() const -> PtrMatrix<int64_t> {
    return getBndConstraints()(_, _(1, 1 + depPoly->getNumLambda()));
  }
  [[nodiscard]] auto getSatPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getSatConstraints()(_, _(l, l + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getSatPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getSatConstraints()(_, _(l, l + depPoly->getDim0()));
  }
  [[nodiscard]] auto getSatPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda() + depPoly->getDim0();
    return getSatConstraints()(_, _(l, l + depPoly->getDim1()));
  }
  [[nodiscard]] auto getBndPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getBndConstraints()(_, _(l, l + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getBndPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getBndConstraints()(_, _(l, l + depPoly->getDim0()));
  }
  [[nodiscard]] auto getBndPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda() + depPoly->getDim0();
    return getBndConstraints()(_, _(l, l + depPoly->getDim1()));
  }
  [[nodiscard]] auto getSatOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getSatConstraints()(_, _(l, l + getNumOmegaCoefficients()));
  }
  [[nodiscard]] auto getBndOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getBndConstraints()(_, _(l, l + getNumOmegaCoefficients()));
  }
  [[nodiscard]] auto getSatW() const -> StridedVector<int64_t> {
    return getSatConstraints()(_, 1 + depPoly->getNumLambda() +
                                    getNumPhiCoefficients() +
                                    getNumOmegaCoefficients());
  }
  [[nodiscard]] auto getBndCoefs() const -> PtrMatrix<int64_t> {
    size_t lb = 1 + depPoly->getNumLambda() + getNumPhiCoefficients() +
                getNumOmegaCoefficients();
    return getBndConstraints()(_, _(lb, end));
  }
  [[nodiscard]] auto splitSatisfaction() const
    -> std::tuple<StridedVector<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>, PtrMatrix<int64_t>, PtrMatrix<int64_t>,
                  StridedVector<int64_t>> {
    PtrMatrix<int64_t> phiCoefsIn = getSatPhi1Coefs(),
                       phiCoefsOut = getSatPhi0Coefs();
    if (forward) std::swap(phiCoefsIn, phiCoefsOut);
    return {getSatConstants(), getSatLambda(),     phiCoefsIn,
            phiCoefsOut,       getSatOmegaCoefs(), getSatW()};
  }
  [[nodiscard]] auto splitBounding() const
    -> std::tuple<StridedVector<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>, PtrMatrix<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>> {
    PtrMatrix<int64_t> phiCoefsIn = getBndPhi1Coefs(),
                       phiCoefsOut = getBndPhi0Coefs();
    if (forward) std::swap(phiCoefsIn, phiCoefsOut);
    return {getBndConstants(), getBndLambda(),     phiCoefsIn,
            phiCoefsOut,       getBndOmegaCoefs(), getBndCoefs()};
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 NotNull<const AffineSchedule> schIn,
                                 NotNull<const AffineSchedule> schOut) const
    -> bool {
    size_t numLoopsIn = in.getNumLoops();
    size_t numLoopsOut = out.getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numLoopsTotal = numLoopsIn + numLoopsOut;
    size_t numVar = numLoopsIn + numLoopsOut + 2;
    invariant(size_t(dependenceSatisfaction->getNumVars()), numVar);
    auto p = alloc.scope();
    auto schv = vector(alloc, numVar, int64_t(0));
    const SquarePtrMatrix<int64_t> inPhi = schIn->getPhi();
    const SquarePtrMatrix<int64_t> outPhi = schOut->getPhi();
    auto inFusOmega = schIn->getFusionOmega();
    auto outFusOmega = schOut->getFusionOmega();
    auto inOffOmega = schIn->getOffsetOmega();
    auto outOffOmega = schOut->getOffsetOmega();
    const size_t numLambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (size_t i = 0; i <= numLoopsCommon; ++i) {
      if (int64_t o2idiff = outFusOmega[i] - inFusOmega[i])
        return (o2idiff > 0);
      // we should not be able to reach `numLoopsCommon`
      // because at the very latest, this last schedule value
      // should be different, because either:
      // if (numLoopsX == numLoopsY){
      //   we're at the inner most loop, where one of the instructions
      //   must have appeared before the other.
      // } else {
      //   the loop nests differ in depth, in which case the deeper
      //   loop must appear either above or below the instructions
      //   present at that level
      // }
      assert(i != numLoopsCommon);
      // forward means offset is 2nd - 1st
      schv[0] = outOffOmega[i];
      schv[1] = inOffOmega[i];
      schv[_(2, 2 + numLoopsIn)] << inPhi(last - i, _);
      schv[_(2 + numLoopsIn, 2 + numLoopsTotal)] << outPhi(last - i, _);
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction->unSatisfiable(alloc, schv, numLambda) ||
          dependenceBounding->unSatisfiable(alloc, schv, numLambda)) {
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
      }
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 PtrVector<unsigned> inFusOmega,
                                 PtrVector<unsigned> outFusOmega) const
    -> bool {
    size_t numLoopsIn = in.getNumLoops();
    size_t numLoopsOut = out.getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numVar = numLoopsIn + numLoopsOut + 2;
    invariant(dependenceSatisfaction->getNumVars() == numVar);
    auto p = alloc.scope();
    auto schv = vector(alloc, numVar, int64_t(0));
    // Vector<int64_t> schv(dependenceSatisfaction->getNumVars(),int64_t(0));
    const size_t numLambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (size_t i = 0; i <= numLoopsCommon; ++i) {
      if (int64_t o2idiff = outFusOmega[i] - inFusOmega[i])
        return (o2idiff > 0);
      // we should not be able to reach `numLoopsCommon`
      // because at the very latest, this last schedule value
      // should be different, because either:
      // if (numLoopsX == numLoopsY){
      //   we're at the inner most loop, where one of the instructions
      //   must have appeared before the other.
      // } else {
      //   the loop nests differ in depth, in which case the deeper
      //   loop must appear either above or below the instructions
      //   present at that level
      // }
      assert(i != numLoopsCommon);
      schv[2 + i] = 1;
      schv[2 + numLoopsIn + i] = 1;
      // forward means offset is 2nd - 1st
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction->unSatisfiable(alloc, schv, numLambda) ||
          dependenceBounding->unSatisfiable(alloc, schv, numLambda)) {
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
      }
      schv[2 + i] = 0;
      schv[2 + numLoopsIn + i] = 0;
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 NotNull<const AffineSchedule> sx,
                                 NotNull<const AffineSchedule> sy,
                                 size_t d) const -> bool {
    const size_t numLambda = depPoly->getNumLambda();
    const size_t nLoopX = depPoly->getDim0();
    const size_t nLoopY = depPoly->getDim1();
    const size_t numLoopsTotal = nLoopX + nLoopY;
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    sch[0] = sx->getOffsetOmega()[d];
    sch[1] = sy->getOffsetOmega()[d];
    sch[_(2, nLoopX + 2)] << sx->getSchedule(d)[_(end - nLoopX, end)];
    sch[_(nLoopX + 2, numLoopsTotal + 2)]
      << sy->getSchedule(d)[_(end - nLoopY, end)];
    return dependenceSatisfaction->satisfiable(alloc, sch, numLambda);
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc, size_t d) const -> bool {
    const size_t numLambda = depPoly->getNumLambda();
    const size_t numLoopsX = depPoly->getDim0();
    const size_t numLoopsTotal = numLoopsX + depPoly->getDim1();
    Vector<int64_t> sch(numLoopsTotal + 2, int64_t(0));
    invariant(size_t(sch.size()), numLoopsTotal + 2);
    sch[2 + d] = 1;
    sch[2 + d + numLoopsX] = 1;
    return dependenceSatisfaction->satisfiable(alloc, sch, numLambda);
  }
  static auto checkDirection(BumpAlloc<> &alloc,
                             const std::array<NotNull<Simplex>, 2> &p,
                             NotNull<const ArrayIndex> x,
                             NotNull<const ArrayIndex> y,
                             NotNull<const AffineSchedule> xSchedule,
                             NotNull<const AffineSchedule> ySchedule,
                             size_t numLambda, Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    const size_t numLoopsX = x->getNumLoops();
    const size_t numLoopsY = y->getNumLoops();
#ifndef NDEBUG
    const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
#endif
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    SquarePtrMatrix<int64_t> xPhi = xSchedule->getPhi();
    SquarePtrMatrix<int64_t> yPhi = ySchedule->getPhi();
    PtrVector<int64_t> xOffOmega = xSchedule->getOffsetOmega();
    PtrVector<int64_t> yOffOmega = ySchedule->getOffsetOmega();
    PtrVector<int64_t> xFusOmega = xSchedule->getFusionOmega();
    PtrVector<int64_t> yFusOmega = ySchedule->getFusionOmega();
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    // i iterates from outer-most to inner most common loop
    for (size_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (yFusOmega[i] != xFusOmega[i]) return yFusOmega[i] > xFusOmega[i];
      // we should not be able to reach `numLoopsCommon`
      // because at the very latest, this last schedule value
      // should be different, because either:
      // if (numLoopsX == numLoopsY){
      //   we're at the inner most loop, where one of the instructions
      //   must have appeared before the other.
      // } else {
      //   the loop nests differ in depth, in which case the deeper
      //   loop must appear either above or below the instructions
      //   present at that level
      // }
      assert(i != numLoopsCommon);
      sch[0] = xOffOmega[i];
      sch[1] = yOffOmega[i];
      sch[_(2, 2 + numLoopsX)] << xPhi(last - i, _);
      sch[_(2 + numLoopsX, 2 + numLoopsTotal)] << yPhi(last - i, _);
      if (fxy->unSatisfiableZeroRem(alloc, sch, numLambda,
                                    size_t(nonTimeDim))) {
        assert(!fyx->unSatisfiableZeroRem(alloc, sch, numLambda,
                                          size_t(nonTimeDim)));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim)))
        return true;
    }
    // assert(false);
    // return false;
  }
  // returns `true` if forward, x->y
  static auto checkDirection(BumpAlloc<> &alloc,
                             const std::array<NotNull<Simplex>, 2> &p,
                             NotNull<const ArrayIndex> x,
                             NotNull<const ArrayIndex> y, size_t numLambda,
                             Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    size_t numLoopsX = x->getNumLoops(), nTD = size_t(nonTimeDim);
#ifndef NDEBUG
    const size_t numLoopsCommon = std::min(numLoopsX, y->getNumLoops());
#endif
    PtrVector<int64_t> xFusOmega = x->getFusionOmega();
    PtrVector<int64_t> yFusOmega = y->getFusionOmega();
    auto chkp = alloc.scope();
    // i iterates from outer-most to inner most common loop
    for (size_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (yFusOmega[i] != xFusOmega[i]) return yFusOmega[i] > xFusOmega[i];
      // we should not be able to reach `numLoopsCommon`
      // because at the very latest, this last schedule value
      // should be different, because either:
      // if (numLoopsX == numLoopsY){
      //   we're at the inner most loop, where one of the instructions
      //   must have appeared before the other.
      // } else {
      //   the loop nests differ in depth, in which case the deeper
      //   loop must appear either above or below the instructions
      //   present at that level
      // }
      assert(i < numLoopsCommon);
      std::array<size_t, 2> inds{2 + i, 2 + i + numLoopsX};
      if (fxy->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) {
        assert(!fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) return true;
    }
    invariant(false);
    return false;
  }
  static auto timelessCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                            NotNull<ArrayIndex> x, NotNull<ArrayIndex> y)
    -> Dependence {
    std::array<NotNull<Simplex>, 2> pair{dxy->farkasPair(alloc)};
    const size_t numLambda = dxy->getNumLambda();
    invariant(dxy->getTimeDim(), unsigned(0));
    if (checkDirection(alloc, pair, x, y, numLambda, dxy->getA().numCol())) {
      pair[0]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
      return Dependence{dxy, pair, {x, y}, true};
    }
    pair[1]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
    std::swap(pair[0], pair[1]);
    return Dependence{dxy, pair, {y, x}, false};
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  static auto timeCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                        NotNull<ArrayIndex> x, NotNull<ArrayIndex> y)
    -> TinyVector<Dependence, 2> {
    std::array<NotNull<Simplex>, 2> pair(dxy->farkasPair(alloc));
    // copy backup
    std::array<NotNull<Simplex>, 2> farkasBackups{pair[0]->copy(alloc),
                                                  pair[1]->copy(alloc)};
    const size_t numInequalityConstraintsOld =
      dxy->getNumInequalityConstraints();
    const size_t numEqualityConstraintsOld = dxy->getNumEqualityConstraints();
    const size_t ineqEnd = 1 + numInequalityConstraintsOld;
    const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
    const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
    const size_t numScheduleCoefs = dxy->getNumScheduleCoef();
    invariant(numLambda, size_t(dxy->getNumLambda()));
    NotNull<ArrayIndex> in = x, out = y;
    const bool isFwd = checkDirection(alloc, pair, x, y, numLambda,
                                      dxy->getA().numCol() - dxy->getTimeDim());
    if (isFwd) {
      std::swap(farkasBackups[0], farkasBackups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0]->truncateVars(1 + numLambda + numScheduleCoefs);
    auto dep0 = Dependence{dxy->copy(alloc), pair, {in, out}, isFwd};
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0.getNumPhiCoefficients());
    // pair is invalid
    const size_t timeDim = dxy->getTimeDim();
    invariant(timeDim > 0);
    // 1 + because we're indexing into A and E, ignoring the constants
    const size_t numVar = 1 + dxy->getNumVar() - timeDim;
    // remove the time dims from the deps
    // dep0.depPoly->truncateVars(numVar);

    // dep0.depPoly->setTimeDim(0);
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0.getNumPhiCoefficients());
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be absolutely
    // insane
    Vector<bool, 16> timeDirection(timeDim);
    size_t t = 0;
    auto fE{farkasBackups[0]->getConstraints()(_, _(1, end))};
    auto sE{farkasBackups[1]->getConstraints()(_, _(1, end))};
    do {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = dxy->getNullStep(t);
      size_t v = numVar + t, i = 0;
      while (true) {
        for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
          int64_t Acv = dxy->getA(c, v);
          if (!Acv) continue;
          Acv *= step;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *1
        }
        for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
          // each of these actually represents 2 inds
          int64_t Ecv = dxy->getE(c, v);
          if (!Ecv) continue;
          Ecv *= step;
          fE(0, c + ineqEnd) -= Ecv;
          fE(0, c + posEqEnd) += Ecv;
          sE(0, c + ineqEnd) -= Ecv;
          sE(0, c + posEqEnd) += Ecv;
        }
        if (i++ != 0) break; // break after undoing
        timeDirection[t] =
          checkDirection(alloc, farkasBackups, *out, *in, numLambda,
                         dxy->getA().numCol() - dxy->getTimeDim());
        step *= -1; // flip to undo, then break
      }
    } while (++t < timeDim);
    t = 0;
    do {
      // checkDirection(farkasBackups, x, y, numLambda) == false
      // correct time direction would make it return true
      // thus sign = timeDirection[t] ? 1 : -1
      int64_t step = (2 * timeDirection[t] - 1) * dxy->getNullStep(t);
      size_t v = numVar + t;
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        int64_t Acv = dxy->getA(c, v);
        if (!Acv) continue;
        Acv *= step;
        dxy->getA(c, 0) -= Acv;
        fE(0, c + 1) -= Acv; // *1
        sE(0, c + 1) -= Acv; // *-1
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy->getE(c, v);
        if (!Ecv) continue;
        Ecv *= step;
        dxy->getE(c, 0) -= Ecv;
        fE(0, c + ineqEnd) -= Ecv;
        fE(0, c + posEqEnd) += Ecv;
        sE(0, c + ineqEnd) -= Ecv;
        sE(0, c + posEqEnd) += Ecv;
      }
    } while (++t < timeDim);
    // dxy->truncateVars(numVar);
    // dxy->setTimeDim(0);
    farkasBackups[0]->truncateVars(1 + numLambda + numScheduleCoefs);
    auto dep1 = Dependence{dxy, farkasBackups, {out, in}, !isFwd};
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0.getNumPhiCoefficients());
    return {dep0, dep1};
  }

  static auto check(BumpAlloc<> &alloc, NotNull<ArrayIndex> x,
                    NotNull<ArrayIndex> y) -> TinyVector<Dependence, 2> {
    // TODO: implement gcd test
    // if (x.gcdKnownIndependent(y)) return {};
    DepPoly *dxy{DepPoly::dependence(alloc, x, y)};
    if (!dxy) return {};
    assert(x->getNumLoops() == dxy->getDim0());
    assert(y->getNumLoops() == dxy->getDim1());
    assert(x->getNumLoops() + y->getNumLoops() == dxy->getNumPhiCoef());
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    if (dxy->getTimeDim()) return timeCheck(alloc, dxy, x, y);
    return {timelessCheck(alloc, dxy, x, y)};
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const Dependence &d)
    -> llvm::raw_ostream & {
    os << "Dependence Poly ";
    if (d.forward) os << "x -> y:";
    else os << "y -> x:";
    os << "\n\tInput:\n" << *d.in.getArrayRef();
    os << "\n\tOutput:\n" << *d.out.getArrayRef();
    os << "\nA = " << d.depPoly->getA() << "\nE = " << d.depPoly->getE()
       << "\nSchedule Constraints:"
       << d.dependenceSatisfaction->getConstraints()
       << "\nBounding Constraints:" << d.dependenceBounding->getConstraints();
    return os << "\n";
  }
};
