#pragma once

#include "./Loops.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
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
#include <llvm/ADT/DenseMap.h>
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
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) // FAM
  [[gnu::aligned(alignof(int64_t))]] std::byte memory[8];

  constexpr DepPoly(unsigned int numDep0Var, unsigned int numDep1Var,
                    unsigned int numDynSym, unsigned int timeDim,
                    unsigned int conCapacity, unsigned int eqConCapacity)
    : numDep0Var(numDep0Var), numDep1Var(numDep1Var), numCon(conCapacity),
      numEqCon(eqConCapacity), numDynSym(numDynSym), timeDim(timeDim),
      conCapacity(conCapacity), eqConCapacity(eqConCapacity) {}
  // [[nodiscard]] static auto allocate(BumpAlloc<> &alloc, unsigned int
  // numDep0Var, unsigned int numDep1Var, unsigned int numCon, unsigned int
  // numEqCon, unsigned int numDynSym, unsigned int timeDim, unsigned int
  // conCapacity,
  //                                    unsigned int eqConCapacity)->DepPoly * {

  // }

public:
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
  [[nodiscard]] constexpr auto getNumPhiCoefficients() const -> unsigned int {
    return numDep0Var + numDep1Var;
  }
  static constexpr auto getNumOmegaCoefficients() -> unsigned int { return 2; }
  [[nodiscard]] constexpr auto getNumScheduleCoefficients() const
    -> unsigned int {
    return getNumPhiCoefficients() + 2;
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
  auto getA() -> MutDensePtrMatrix<int64_t> {
    return {reinterpret_cast<int64_t *>(memory),
            DenseDims{numCon, getNumVar() + 1}};
  }
  auto getE() -> MutDensePtrMatrix<int64_t> {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return {p + size_t(conCapacity) * (getNumVar() + 1),
            DenseDims{numEqCon, getNumVar() + 1}};
  }
  auto getNullStep() -> MutPtrVector<int64_t> {
    auto *p = reinterpret_cast<int64_t *>(memory);
    return {p + (size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1),
            timeDim};
  }
  [[nodiscard]] auto getNullStep(size_t i) const -> int64_t {
    invariant(i < timeDim);
    const auto *p = reinterpret_cast<const int64_t *>(memory);
    return (p + (size_t(conCapacity) + eqConCapacity) * (getNumVar() + 1))[i];
  }
  auto getSyms() -> llvm::MutableArrayRef<const llvm::SCEV *> {
    std::byte *p = memory;
    return {
      reinterpret_cast<const llvm::SCEV **>(
        p + sizeof(int64_t) *
              ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
      numDynSym};
  }
  [[nodiscard]] auto getA() const -> DensePtrMatrix<int64_t> {
    const std::byte *p = memory;
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
    const std::byte *p = memory;
    return {
      reinterpret_cast<const llvm::SCEV *const *>(
        p + sizeof(int64_t) *
              ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim)),
      numDynSym};
  }
  constexpr auto getSymbols(size_t i) -> MutPtrVector<int64_t> {
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
  static constexpr auto nullSpace(NotNull<const MemoryAccess> x,
                                  NotNull<const MemoryAccess> y)
    -> DenseMatrix<int64_t> {
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
      A(i, _(begin, xDim)) = indMatX(i, _);
      A(i, _(xDim, end)) = indMatY(i, _);
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
  static auto mergeMap(llvm::ArrayRef<const llvm::SCEV *> s0,
                       llvm::ArrayRef<const llvm::SCEV *> s1)
    -> Vector<unsigned int> {
    Vector<unsigned int> map;
    map.resizeForOverwrite(s1.size());
    for (size_t n = s0.size(), i = 0; i < s1.size(); ++i) {
      Optional<unsigned int> j = symbolIndex(s0, s1[i]);
      map[i] = j ? *j : n++;
    }
    return map;
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
    return sizeof(int64_t) *
             ((conCapacity + eqConCapacity) * (getNumVar() + 1) + timeDim) +
           sizeof(const llvm::SCEV *) * numDynSym;
  }
  auto copy(BumpAlloc<> &alloc) const -> NotNull<DepPoly> {
    auto *p = alloc.template allocate<DepPoly>(neededBytes());
    std::memcpy(p, this, neededBytes());
    return NotNull<DepPoly>{p};
  }
  static auto dependence(BumpAlloc<> &alloc, NotNull<const MemoryAccess> ma0,
                         NotNull<const MemoryAccess> ma1) -> DepPoly * {
    assert(ma0->sizesMatch(ma1));
    NotNull<AffineLoopNest<>> loop0 = ma0->getLoop();
    NotNull<AffineLoopNest<>> loop1 = ma1->getLoop();
    DensePtrMatrix<int64_t> A0{loop0->getA()}, A1{loop1->getA()};
    auto S0{loop0->getSyms()}, S1{loop1->getSyms()};
    PtrMatrix<int64_t> C0 = ma0->indexMatrix(); // numLoops x numDim
    PtrMatrix<int64_t> C1 = ma1->indexMatrix();
    PtrMatrix<int64_t> O0 = ma0->offsetMatrix();
    PtrMatrix<int64_t> O1 = ma1->offsetMatrix();
    invariant(C0.numCol(), C1.numCol());

    auto [nc0, nv0] = A0.size();
    auto [nc1, nv1] = A1.size();
    unsigned numDep0Var = loop0->getNumLoops();
    unsigned numDep1Var = loop1->getNumLoops();
    unsigned numVar = numDep0Var + numDep1Var;

    auto map = mergeMap(S0, S1);
    invariant(unsigned(map.size()), unsigned(S1.size()));
    invariant(size_t(map.size()), size_t(S1.size()));
    unsigned numDynSym = S0.size() + map.size();
    unsigned numSym = numDynSym + 1;
    IntMatrix NS{nullSpace(ma0, ma1)};
    unsigned timeDim = unsigned{NS.numRow()};

    unsigned numCols = numVar + timeDim + numDynSym + 1;

    unsigned conCapacity = unsigned(A0.numRow() + A1.numRow()) + numVar;
    unsigned eqConCapacity = unsigned(C0.numCol()) + timeDim;

    size_t memNeeded =
      sizeof(int64_t) * ((conCapacity + eqConCapacity) * numCols + timeDim) +
      sizeof(const llvm::SCEV *) * numDynSym;

    auto p = alloc.checkpoint();
    auto *mem = alloc.allocate(memNeeded, alignof(DepPoly));
    auto *dp = new (mem) DepPoly(numDep0Var, numDep1Var, numDynSym, timeDim,
                                 conCapacity, eqConCapacity);

    // numDep1Var = nv1;
    const Row nc = nc0 + nc1;
    const size_t indexDim{ma0->getArrayDim()};
    auto nullStep{dp->getNullStep()};
    for (size_t i = 0; i < timeDim; ++i) nullStep[i] = selfDot(NS(i, _));
    //           column meansing in in order
    // const size_t numSymbols = getNumSymbols();
    auto A{dp->getA()};
    auto E{dp->getE()};
    // A.resize(nc + numVar, numSymbols + numVar + nullDim);
    // E.resize(indexDim + nullDim, A.numCol());
    // ma0 loop
    for (size_t i = 0; i < nc0; ++i) {
      A(i, _(0, 1 + S0.size())) << A0(i, _(0, 1 + S0.size()));
      A(i, _(numSym, numSym + numDep0Var))
        << A0(i, _(1 + S0.size(), 1 + S0.size() + numDep0Var));
    }
    for (size_t i = 0; i < nc1; ++i) {
      A(nc0 + i, 0) = A1(i, 0);
      for (size_t j = 0; j < map.size(); ++j)
        A(nc0 + i, 1 + map[j]) = A1(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        A(nc0 + i, j + numSym + numDep0Var) = A1(i, j + 1 + S1.size());
    }
    A(_(nc, end), _(numSym, numSym + numVar)).diag() << 1;
    // L254: Assertion `col < numCol()` failed
    // indMats are [innerMostLoop, ..., outerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (size_t i = 0; i < indexDim; ++i) {
      E(i, _(0, O0.numCol())) = O0(i, _(0, O0.numCol()));
      E(i, _(numSym, numDep0Var + numSym)) << C0(_(0, numDep0Var), i);
      E(i, 0) -= O1(i, 0);
      for (size_t j = 0; j < O1.numCol() - 1; ++j)
        E(i, 1 + map[j]) -= O1(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        E(i, j + numSym + numDep0Var) = -C1(j, i);
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
    if (dp->getNumCon() == 0) {
      alloc.rollback(p);
      return nullptr;
    }
    return dp;
  }
  // `direction = true` means second dep follow first
  // lambda_0 + lambda*A*x = delta + c'x
  // x = [s, i]

  // order of variables:
  // [ lambda, schedule coefs on loops, const schedule coef, w, u ]
  //
  //
  // constraint order corresponds to old variables, will be in same order
  //
  // Time parameters are carried over into farkas polys
  [[nodiscard]] auto farkasPair(BumpAlloc<> &alloc) const
    -> std::array<Simplex, 2> {

    auto A{getA()}, E{getE()};
    const size_t numEqualityConstraintsOld = size_t(E.numRow());
    const size_t numInequalityConstraintsOld = size_t(A.numRow());

    const size_t numPhiCoefs = getNumPhiCoefficients();
    const size_t numScheduleCoefs = numPhiCoefs + getNumOmegaCoefficients();
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
    // std::array<Simplex, 2> pair;
    Simplex fw = Simplex::create(alloc, numConstraintsNew, numVarNew + 1, 0);
    // Simplex &fw(pair[0]);
    // fw.resize(numConstraintsNew, numVarNew + 1);
    MutPtrMatrix<int64_t> fC{fw.tableau.getConstraints()(_, _(1, end))};
    fC(_, 0) << 0;
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
    // if (direction==true & boundAbove == false){
    //   sign = 1
    // } else {
    //   sign = -1
    // }
    //
    // boundAbove means we have
    // ... == w + u'*N + psi
    // -1 as we flip sign
    for (size_t i = 0; i < numBoundingCoefs; ++i)
      fC(i, i + numScheduleCoefs + numLambda) = -1;

    // so far, both have been identical

    // Simplex &bw(pair[1]);
    // bw.resize(numConstraintsNew, numVarNew + 1);
    Simplex bw = Simplex::create(alloc, numConstraintsNew, numVarNew + 1, 0);
    MutPtrMatrix<int64_t> bC{bw.tableau.getConstraints()(_, _(1, end))};

    bC(_, _(begin, numVarNew)) << fC(_, _(begin, numVarNew));
    // for (size_t i = 0; i < numConstraintsNew; ++i)
    //     for (size_t j = 0; j < numVarNew; ++j)
    //         bC(i, j) = fC(i, j);

    // equality constraints get expanded into two inequalities
    // a == 0 ->
    // even row: a <= 0
    // odd row: -a <= 0
    // fw means x'Al = x'(depVar1 - depVar0)
    // x'Al + x'(depVar0 - depVar1) = 0
    // so, for fw, depVar0 is positive and depVar1 is negative
    // note that we order the coefficients inner->outer
    // so that the ILP minimizing coefficients
    // will tend to preserve the initial order (which is
    // probably better than tending to reverse the initial order).
    for (size_t i = 0; i < numPhiCoefs; ++i) {
      int64_t s = (2 * (i < numDep0Var) - 1);
      fC(i + numBoundingCoefs, i + numLambda) = s;
      bC(i + numBoundingCoefs, i + numLambda) = -s;
    }
    // for (size_t i = 0; i < numDep0Var; ++i) {
    //     fC(numDep0Var - 1 - i + numBoundingCoefs, i + numLambda) = 1;
    //     bC(numDep0Var - 1 - i + numBoundingCoefs, i + numLambda) = -1;
    // }
    // for (size_t i = 0; i < numPhiCoefs - numDep0Var; ++i) {
    //     fC(numPhiCoefs - 1 - i + numBoundingCoefs,
    //        i + numDep0Var + numLambda) = -1;
    //     bC(numPhiCoefs - 1 - i + numBoundingCoefs,
    //        i + numDep0Var + numLambda) = 1;
    // }
    fC(0, numScheduleCoefs - 2 + numLambda) = 1;
    fC(0, numScheduleCoefs - 1 + numLambda) = -1;
    bC(0, numScheduleCoefs - 2 + numLambda) = -1;
    bC(0, numScheduleCoefs - 1 + numLambda) = 1;
    // note that delta/constant coef is handled as last `s`
    return {fw, bw};
    // fw.removeExtraVariables(numVarKeep);
    // bw.removeExtraVariables(numVarKeep);
    // assert(fw.E.numRow() == fw.q.size());
    // assert(bw.E.numRow() == bw.q.size());
    // return pair;
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
  [[no_unique_address]] Simplex dependenceSatisfaction;
  [[no_unique_address]] Simplex dependenceBounding;
  [[no_unique_address]] NotNull<MemoryAccess> in;
  [[no_unique_address]] NotNull<MemoryAccess> out;
  [[no_unique_address]] bool forward;

public:
  Dependence(NotNull<DepPoly> poly, std::array<Simplex, 2> depSatBound,
             std::array<NotNull<MemoryAccess>, 2> inOut, bool fwd)
    : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
      dependenceBounding(depSatBound[1]), in(inOut[0]), out(inOut[1]),
      forward(fwd) {}

  using BitSet = MemoryAccess::BitSet;
  void pushToEdgeVector(llvm::SmallVectorImpl<NotNull<Dependence>> &vec) {
    in->addEdgeOut(vec.size());
    out->addEdgeIn(vec.size());
    vec.push_back(this);
  }
  [[nodiscard]] constexpr auto getArrayPointer() -> const llvm::SCEV * {
    return in->getArrayPointer();
  }
  /// indicates whether forward is non-empty
  [[nodiscard]] constexpr auto isForward() const -> bool { return forward; }
  [[nodiscard]] auto nodesIn() const -> const BitSet & {
    return in->getNodes();
  }
  [[nodiscard]] auto nodesOut() const -> const BitSet & {
    return out->getNodes();
  }
  [[nodiscard]] auto getDynSymDim() const -> size_t {
    return depPoly->getNumDynSym();
  }
  [[nodiscard]] auto inputIsLoad() const -> bool { return in->isLoad(); }
  [[nodiscard]] auto outputIsLoad() const -> bool { return out->isLoad(); }
  [[nodiscard]] auto inputIsStore() const -> bool { return in->isStore(); }
  [[nodiscard]] auto outputIsStore() const -> bool { return out->isStore(); }
  /// getInIndMat() -> getInNumLoops() x arrayDim()
  [[nodiscard]] auto getInIndMat() const -> PtrMatrix<int64_t> {
    return in->indexMatrix();
  }
  /// getOutIndMat() -> getOutNumLoops() x arrayDim()
  [[nodiscard]] auto getOutIndMat() const -> PtrMatrix<int64_t> {
    return out->indexMatrix();
  }
  [[nodiscard]] constexpr auto getInOutPair() const
    -> std::array<NotNull<MemoryAccess>, 2> {
    return {in, out};
  }
  // returns the memory access pair, placing the store first in the pair
  [[nodiscard]] auto getStoreAndOther() const
    -> std::array<NotNull<MemoryAccess>, 2> {
    if (in->isStore()) return {in, out};
    return {out, in};
  }
  [[nodiscard]] auto getInNumLoops() const -> size_t {
    return in->getNumLoops();
  }
  [[nodiscard]] auto getOutNumLoops() const -> size_t {
    return out->getNumLoops();
  }
  [[nodiscard]] auto isInactive(size_t depth) const -> bool {
    return (depth >= std::min(out->getNumLoops(), in->getNumLoops()));
  }
  [[nodiscard]] auto getNumLambda() const -> size_t {
    return depPoly->getNumLambda() << 1;
  }
  [[nodiscard]] auto getNumSymbols() const -> size_t {
    return depPoly->getNumSymbols();
  }
  [[nodiscard]] auto getNumPhiCoefficients() const -> size_t {
    return depPoly->getNumPhiCoefficients();
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoefficients() -> size_t {
    return DepPoly::getNumOmegaCoefficients();
  }
  [[nodiscard]] constexpr auto getNumDepSatConstraintVar() const -> size_t {
    return size_t(dependenceSatisfaction.tableau.getConstraints().numCol());
  }
  [[nodiscard]] constexpr auto getNumDepBndConstraintVar() const -> size_t {
    return size_t(dependenceBounding.tableau.getConstraints().numCol());
  }
  // returns `w`
  [[nodiscard]] constexpr auto getNumDynamicBoundingVar() const -> size_t {
    return getNumDepBndConstraintVar() - getNumDepSatConstraintVar();
  }
  void validate() {
    assert(getInNumLoops() + getOutNumLoops() == getNumPhiCoefficients());
    // 2 == 1 for const offset + 1 for w
    assert(2 + depPoly->getNumLambda() + getNumPhiCoefficients() +
             getNumOmegaCoefficients() ==
           size_t(dependenceSatisfaction.tableau.getConstraints().numCol()));
  }
  [[nodiscard]] auto getDepPoly() -> NotNull<DepPoly> { return depPoly; }
  [[nodiscard]] constexpr auto getNumConstraints() const -> size_t {
    return dependenceBounding.tableau.getNumCons() +
           dependenceSatisfaction.tableau.getNumCons();
  }
  [[nodiscard]] constexpr auto getSatConstants() const
    -> StridedVector<int64_t> {
    return dependenceSatisfaction.tableau.getConstants();
  }
  [[nodiscard]] constexpr auto getBndConstants() const
    -> StridedVector<int64_t> {
    return dependenceBounding.tableau.getConstants();
  }
  [[nodiscard]] constexpr auto getSatConstraints() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.tableau.getConstraints();
  }
  [[nodiscard]] constexpr auto getBndConstraints() const -> PtrMatrix<int64_t> {
    return dependenceBounding.tableau.getConstraints();
  }
  [[nodiscard]] auto getSatLambda() const -> PtrMatrix<int64_t> {
    return getSatConstraints()(_, _(1, 1 + depPoly->getNumLambda()));
  }
  [[nodiscard]] auto getBndLambda() const -> PtrMatrix<int64_t> {
    return getBndConstraints()(_, _(1, 1 + depPoly->getNumLambda()));
  }
  [[nodiscard]] auto getSatPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getSatConstraints()(_, _(l, l + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getSatPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getSatConstraints()(_, _(l, l + depPoly->getDim0()));
  }
  [[nodiscard]] auto getSatPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda() + depPoly->getDim0();
    return getSatConstraints()(_, _(l, l + depPoly->getDim1()));
  }
  [[nodiscard]] auto getBndPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getBndConstraints()(_, _(l, l + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getBndPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getBndConstraints()(_, _(l, l + depPoly->getDim0()));
  }
  [[nodiscard]] auto getBndPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda() + depPoly->getDim0();
    return getBndConstraints()(_, _(l, l + depPoly->getDim1()));
  }
  [[nodiscard]] auto getSatOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = depPoly->getNumLambda() + getNumPhiCoefficients();
    return getSatConstraints()(_, _(1 + l, 1 + l + getNumOmegaCoefficients()));
  }
  [[nodiscard]] auto getBndOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = depPoly->getNumLambda() + getNumPhiCoefficients();
    return getBndConstraints()(_, _(1 + l, 1 + l + getNumOmegaCoefficients()));
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
    PtrMatrix<int64_t> phiCoefsIn =
      forward ? getSatPhi0Coefs() : getSatPhi1Coefs();
    PtrMatrix<int64_t> phiCoefsOut =
      forward ? getSatPhi1Coefs() : getSatPhi0Coefs();
    return std::make_tuple(getSatConstants(), getSatLambda(), phiCoefsIn,
                           phiCoefsOut, getSatOmegaCoefs(), getSatW());
  }
  [[nodiscard]] auto splitBounding() const
    -> std::tuple<StridedVector<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>, PtrMatrix<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>> {
    PtrMatrix<int64_t> phiCoefsIn =
      forward ? getBndPhi0Coefs() : getBndPhi1Coefs();
    PtrMatrix<int64_t> phiCoefsOut =
      forward ? getBndPhi1Coefs() : getBndPhi0Coefs();
    return std::make_tuple(getBndConstants(), getBndLambda(), phiCoefsIn,
                           phiCoefsOut, getBndOmegaCoefs(), getBndCoefs());
  }
  // // order of variables from Farkas:
  // // [ lambda, Phi coefs, omega coefs, w, u ]
  // // that is thus the order of arguments here
  // // Note: we have two different sets of lambdas, so we store
  // // A = [lambda_sat, lambda_bound]
  // void copyLambda(MutPtrMatrix<int64_t> A, MutPtrMatrix<int64_t> Bp,
  //                 MutPtrMatrix<int64_t> Bm, MutPtrMatrix<int64_t> C,
  //                 MutStridedVector<int64_t> W, MutPtrMatrix<int64_t> U,
  //                 MutStridedVector<int64_t> c) const {
  //     // const size_t numBoundingConstraints =
  //     //     dependenceBounding.getNumConstraints();
  //     const auto satLambda = getSatLambda();
  //     const auto bndLambda = getBndLambda();
  //     const size_t satConstraints = size_t(satLambda.numRow());
  //     const size_t numSatLambda = size_t(satLambda.numCol());
  //     assert(numSatLambda + bndLambda.numCol() == A.numCol());

  //     c(_(begin, satConstraints)) = dependenceSatisfaction.getConstants();
  //     c(_(satConstraints, end)) = dependenceBounding.getConstants();

  //     A(_(begin, satConstraints), _(begin, numSatLambda)) = satLambda;
  //     // A(_(begin, satConstraints), _(numSatLambda, end)) = 0;
  //     // A(_(satConstraints, end), _(begin, numSatLambda)) = 0;
  //     A(_(satConstraints, end), _(numSatLambda, end)) = bndLambda;

  //     // TODO: develop and suport fusion of statements like
  //     // Bp(_(begin, satConstraints), _) = getSatPhiCoefs();
  //     // Bm(_(begin, satConstraints), _) = -getSatPhiCoefs();
  //     // Bp(_(satConstraints, end), _) = getBndPhiCoefs();
  //     // Bm(_(satConstraints, end), _) = -getBndPhiCoefs();
  //     // perhaps something like:
  //     // std::make_pair(
  //     //   Bp(_(begin, satConstraints), _),
  //     //   Bm(_(begin, satConstraints), _)) =
  //     //     elementwiseMap(
  //     //       std::make_pair(Plus{},Minus{}),
  //     //       getSatPhiCoefs()
  //     //     );
  //     auto SP{getSatPhiCoefs()};
  //     assert(Bp.numCol() == SP.numCol());
  //     assert(Bm.numCol() == SP.numCol());
  //     assert(Bp.numRow() == Bm.numRow());
  //     for (size_t i = 0; i < satConstraints; ++i) {
  //         for (size_t j = 0; j < SP.numCol(); ++j) {
  //             int64_t SOij = SP(i, j);
  //             Bp(i, j) = SOij;
  //             Bm(i, j) = -SOij;
  //         }
  //     }
  //     auto BP{getBndPhiCoefs()};
  //     assert(Bp.numCol() == BP.numCol());
  //     for (size_t i = satConstraints; i < Bp.numRow(); ++i) {
  //         for (size_t j = 0; j < BP.numCol(); ++j) {
  //             int64_t BOij = BP(i - satConstraints, j);
  //             Bp(i, j) = BOij;
  //             Bm(i, j) = -BOij;
  //         }
  //     }

  //     C(_(begin, satConstraints), _) = getSatOmegaCoefs();
  //     C(_(satConstraints, end), _) = getBndOmegaCoefs();

  //     auto BC{getBndCoefs()};
  //     W(_(satConstraints, end)) = BC(_, 0);
  //     U(_(satConstraints, end), _) = BC(_, _(1, end));
  // }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 NotNull<const AffineSchedule> schIn,
                                 NotNull<const AffineSchedule> schOut) const
    -> bool {
    size_t numLoopsIn = in->getNumLoops();
    size_t numLoopsOut = out->getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numLoopsTotal = numLoopsIn + numLoopsOut;
    Vector<int64_t> schv;
    schv.resizeForOverwrite(dependenceSatisfaction.tableau.getNumVars());
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
      schv[_(begin, numLoopsIn)] = inPhi(last - i, _);
      schv[_(numLoopsIn, numLoopsTotal)] = outPhi(last - i, _);
      int64_t inO = inOffOmega[i], outO = outOffOmega[i];
      // forward means offset is 2nd - 1st
      schv[numLoopsTotal] = outO - inO;
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction.unSatisfiable(alloc, schv, numLambda) ||
          dependenceBounding.unSatisfiable(alloc, schv, numLambda))
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 PtrVector<unsigned> inFusOmega,
                                 PtrVector<unsigned> outFusOmega) const
    -> bool {
    size_t numLoopsIn = in->getNumLoops();
    size_t numLoopsOut = out->getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numLoopsTotal = numLoopsIn + numLoopsOut;
    Vector<int64_t> schv;
    schv.resizeForOverwrite(dependenceSatisfaction.tableau.getNumVars());
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
      schv = 0;
      schv[numLoopsIn - i - 1] = 1;
      schv[numLoopsTotal - i - 1] = 1;
      // forward means offset is 2nd - 1st
      schv[numLoopsTotal] = 0;
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction.unSatisfiable(alloc, schv, numLambda) ||
          dependenceBounding.unSatisfiable(alloc, schv, numLambda))
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
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
    // const size_t numLoopsX = sx.getNumLoops();
    // const size_t numLoopsY = sy.getNumLoops();
    const size_t numLoopsTotal = nLoopX + nLoopY;
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    sch[_(begin, nLoopX)] = sx->getSchedule(d)[_(end - nLoopX, end)];
    sch[_(nLoopX, numLoopsTotal)] = sy->getSchedule(d)[_(end - nLoopY, end)];
    sch[numLoopsTotal] = sx->getOffsetOmega()[d];
    sch[numLoopsTotal + 1] = sy->getOffsetOmega()[d];
    return dependenceSatisfaction.satisfiable(alloc, sch, numLambda);
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc, size_t d) const -> bool {
    const size_t numLambda = depPoly->getNumLambda();
    const size_t numLoopsX = depPoly->getDim0();
    const size_t numLoopsY = depPoly->getDim1();
    // const size_t numLoopsX = sx.getNumLoops();
    // const size_t numLoopsY = sy.getNumLoops();
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    Vector<int64_t> sch(numLoopsTotal + 2);
    assert(sch.size() == numLoopsTotal + 2);
    sch[numLoopsX - d - 1] = 1;
    sch[numLoopsTotal - d - 1] = 1;
    // sch(numLoopsTotal) = x[d];
    // sch(numLoopsTotal + 1) = y[d];
    return dependenceSatisfaction.satisfiable(alloc, sch, numLambda);
  }
  // bool isSatisfied(size_t d) {
  //     return forward ? isSatisfied(in->getFusedOmega(),
  //     out->getFusedOmega(), d)
  //                    : isSatisfied(out->getFusedOmega(),
  //                    in->getFusedOmega(), d);
  // }
  static auto checkDirection(BumpAlloc<> &alloc,
                             const std::array<Simplex, 2> &p,
                             NotNull<const MemoryAccess> x,
                             NotNull<const MemoryAccess> y,
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
      if (int64_t o2idiff = yFusOmega[i] - xFusOmega[i]) return o2idiff > 0;
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
      sch[_(begin, numLoopsX)] = xPhi(last - i, _);
      sch[_(numLoopsX, numLoopsTotal)] = yPhi(last - i, _);
      sch[numLoopsTotal] = xOffOmega[i];
      sch[numLoopsTotal + 1] = yOffOmega[i];
      if (fxy.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        assert(
          !fyx.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim)));
        // llvm::errs()
        //     << "Dependence decided by forward violation with i = " <<
        //     i
        //     << "\n";
#endif
        return false;
      }
      if (fyx.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim)))
#ifndef NDEBUG
      // llvm::errs()
      //     << "Dependence decided by backward violation with i = "
      //     << i
      //     << "\n";
#endif
        return true;
    }
    // assert(false);
    // return false;
  }
  static auto checkDirection(BumpAlloc<> &alloc,
                             const std::array<Simplex, 2> &p,
                             NotNull<const MemoryAccess> x,
                             NotNull<const MemoryAccess> y, size_t numLambda,
                             Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    const size_t numLoopsX = x->getNumLoops();
    const size_t numLoopsY = y->getNumLoops();
#ifndef NDEBUG
    const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
#endif
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    PtrVector<int64_t> xFusOmega = x->getFusionOmega();
    PtrVector<int64_t> yFusOmega = y->getFusionOmega();
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
      sch = 0;
      sch[numLoopsX - 1 - i] = 1;
      sch[numLoopsTotal - 1 - i] = 1;
      if (fxy.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        assert(
          !fyx.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim)));
        // llvm::errs()
        //     << "Dependence decided by forward violation with i = " <<
        //     i
        //     << "\n";
#endif
        return false;
      }
      if (fyx.unSatisfiableZeroRem(alloc, sch, numLambda, size_t(nonTimeDim)))
#ifndef NDEBUG
      // llvm::errs()
      //     << "Dependence decided by backward violation with i = "
      //     << i
      //     << "\n";
#endif
        return true;
    }
    // assert(false);
    // return false;
  }
  static auto timelessCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                            NotNull<MemoryAccess> x, NotNull<MemoryAccess> y)
    -> Dependence {
    std::array<Simplex, 2> pair{dxy->farkasPair(alloc)};
    const size_t numLambda = dxy->getNumLambda();
    assert(dxy->getTimeDim() == 0);
    if (checkDirection(alloc, pair, x, y, numLambda, dxy->getA().numCol())) {
      // pair[0].truncateVars(pair[0].getNumVar() -
      //                         dxy.getNumSymbols());
      pair[0].tableau.truncateVars(2 + numLambda +
                                   dxy->getNumScheduleCoefficients());
      return Dependence{dxy, pair, {x, y}, true};
    }
    // pair[1].truncateVars(pair[1].getNumVar() -
    // dxy.getNumSymbols());
    pair[1].tableau.truncateVars(2 + numLambda +
                                 dxy->getNumScheduleCoefficients());
    std::swap(pair[0], pair[1]);
    return Dependence{dxy, pair, {y, x}, false};
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  static auto timeCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                        NotNull<MemoryAccess> x, NotNull<MemoryAccess> y)
    -> std::array<std::optional<Dependence>, 2> {
    std::array<Simplex, 2> pair(dxy->farkasPair(alloc));
    // copy backup
    std::array<Simplex, 2> farkasBackups = pair;
    const size_t numInequalityConstraintsOld =
      dxy->getNumInequalityConstraints();
    const size_t numEqualityConstraintsOld = dxy->getNumEqualityConstraints();
    const size_t ineqEnd = 1 + numInequalityConstraintsOld;
    const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
    const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
    const size_t numScheduleCoefs = dxy->getNumScheduleCoefficients();
    assert(numLambda == dxy->getNumLambda());
    NotNull<MemoryAccess> in = x, out = y;
    const bool isFwd = checkDirection(alloc, pair, x, y, numLambda,
                                      dxy->getA().numCol() - dxy->getTimeDim());
    if (isFwd) {
      std::swap(farkasBackups[0], farkasBackups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0].tableau.truncateVars(2 + numLambda + numScheduleCoefs);
    auto dep0 = Dependence{dxy->copy(alloc), pair, {in, out}, isFwd};
    assert(out->getNumLoops() + in->getNumLoops() ==
           dep0.getNumPhiCoefficients());
    // pair is invalid
    const size_t timeDim = dxy->getTimeDim();
    assert(timeDim);
    const size_t numVarOld = size_t(dxy->getA().numCol());
    const size_t numVar = numVarOld - timeDim;
    // const size_t numBoundingCoefs = numVarKeep - numLambda;
    // remove the time dims from the deps
    dep0.depPoly->truncateVars(numVar);
    dep0.depPoly->setTimeDim(0);
    assert(out->getNumLoops() + in->getNumLoops() ==
           dep0.getNumPhiCoefficients());
    // dep0->depPoly.removeExtraVariables(numVar);
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be absolutely
    // insane
    llvm::SmallVector<bool, 16> timeDirection(timeDim);
    size_t t = 0;
    auto fE{farkasBackups[0].tableau.getConstraints()(_, _(1, end))};
    auto sE{farkasBackups[1].tableau.getConstraints()(_, _(1, end))};
    do {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = dxy->getNullStep(t);
      size_t v = numVar + t;
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        if (int64_t Acv = dxy->getA(c, v)) {
          Acv *= step;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *1
        }
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy->getE(c, v) * step;
        fE(0, c + ineqEnd) -= Ecv;
        fE(0, c + posEqEnd) += Ecv;
        sE(0, c + ineqEnd) -= Ecv;
        sE(0, c + posEqEnd) += Ecv;
      }
      // pair = farkasBackups;
      // pair[0].removeExtraVariables(numVarKeep);
      // pair[1].removeExtraVariables(numVarKeep);
      // farkasBacklups is swapped with respect to
      // checkDirection(..., *in, *out);
      timeDirection[t] =
        checkDirection(alloc, farkasBackups, *out, *in, numLambda,
                       dxy->getA().numCol() - dxy->getTimeDim());
      // fix
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        int64_t Acv = dxy->getA(c, v) * step;
        fE(0, c + 1) += Acv;
        sE(0, c + 1) += Acv;
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy->getE(c, v) * step;
        fE(0, c + ineqEnd) += Ecv;
        fE(0, c + posEqEnd) -= Ecv;
        sE(0, c + ineqEnd) += Ecv;
        sE(0, c + posEqEnd) -= Ecv;
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
        if (int64_t Acv = dxy->getA(c, v)) {
          Acv *= step;
          dxy->getA(c, 0) -= Acv;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *-1
        }
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy->getE(c, v) * step;
        dxy->getE(c, 0) -= Ecv;
        fE(0, c + ineqEnd) -= Ecv;
        fE(0, c + posEqEnd) += Ecv;
        sE(0, c + ineqEnd) -= Ecv;
        sE(0, c + posEqEnd) += Ecv;
      }
    } while (++t < timeDim);
    dxy->truncateVars(numVar);
    dxy->setTimeDim(0);
    farkasBackups[0].tableau.truncateVars(2 + numLambda + numScheduleCoefs);
    auto dep1 = Dependence{dxy, farkasBackups, {out, in}, !isFwd};
    assert(out->getNumLoops() + in->getNumLoops() ==
           dep0.getNumPhiCoefficients());
    return {dep0, dep1};
  }

  static auto check(BumpAlloc<> &alloc, NotNull<MemoryAccess> x,
                    NotNull<MemoryAccess> y)
    -> std::array<std::optional<Dependence>, 2> {
    // TODO: implement gcd test
    // if (x.gcdKnownIndependent(y)) return {};
    DepPoly *dxy{DepPoly::dependence(alloc, x, y)};
    if (!dxy) return {};
    assert(x->getNumLoops() == dxy->getDim0());
    assert(y->getNumLoops() == dxy->getDim1());
    assert(x->getNumLoops() + y->getNumLoops() == dxy->getNumPhiCoefficients());
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    if (dxy->getTimeDim()) return timeCheck(alloc, dxy, x, y);
    return {timelessCheck(alloc, dxy, x, y), {}};
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const Dependence &d)
    -> llvm::raw_ostream & {
    os << "Dependence Poly ";
    if (d.forward) os << "x -> y:";
    else os << "y -> x:";
    // os << d.depPoly << "\nA = " << d.depPoly.A << "\nE = " << d.depPoly.E
    //    << "\nSchedule Constraints:" << d.dependenceSatisfaction
    //    << "\nBounding Constraints:" << d.dependenceBounding;
    if (d.in) os << "\n\tInput:\n" << *d.in;
    if (d.out) os << "\n\tOutput:\n" << *d.out;
    return os << "\n";
  }
};
