#pragma once

#include "IR/Address.hpp"
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Math.hpp"
#include "Math/NormalForm.hpp"
#include "Math/Orthogonalize.hpp"
#include "Math/Polyhedra.hpp"
#include "Math/Simplex.hpp"
#include "Polyhedra/Loops.hpp"
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

/// prints in current permutation order.
/// TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
/// in which case, we have to remove `currentToOriginalPerm`,
/// which menas either change printing, or move prints `<<` into
/// the derived classes.
template <OStream OS>
inline auto printConstraints(OS &os, DensePtrMatrix<int64_t> A,
                             llvm::ArrayRef<const llvm::SCEV *> syms,
                             bool inequality = true) -> OS & {
  const Row numConstraints = A.numRow();
  const unsigned numSyms = syms.size() + 1;
  for (Row c = 0; c < numConstraints; ++c) {
    printConstraint(os, A(c, _), numSyms, inequality);
    for (size_t v = 1; v < numSyms; ++v) {
      if (int64_t Acv = A(c, v)) {
        os << (Acv > 0 ? " + " : " - ");
        Acv = constexpr_abs(Acv);
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
  unsigned numDep0Var;    // i0.size()
  unsigned numDep1Var;    // i1.size()
  unsigned numCon;        // initially: ineqConCapacity
  unsigned numEqCon;      // initially: eqConCapacity
  unsigned numDynSym;     // s.size()
  unsigned timeDim;       // null space of memory accesses
  unsigned conCapacity;   // A0.numRow() + A1.numRow()
  unsigned eqConCapacity; // C0.numRow()
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

  // [[nodiscard]] static auto allocate(BumpAlloc<> &alloc, unsigned
  // numDep0Var, unsigned numDep1Var, unsigned numCon, unsigned
  // numEqCon, unsigned numDynSym, unsigned timeDim, unsigned
  // conCapacity,
  //                                    unsigned eqConCapacity)->DepPoly * {

  // }

public:
  constexpr explicit DepPoly(unsigned nd0, unsigned nd1, unsigned nds,
                             unsigned td, unsigned conCap, unsigned eqConCap)
    : numDep0Var(nd0), numDep1Var(nd1), numCon(conCap), numEqCon(eqConCap),
      numDynSym(nds), timeDim(td), conCapacity(conCap),
      eqConCapacity(eqConCap) {}
  [[nodiscard]] constexpr auto getTimeDim() const -> unsigned {
    return timeDim;
  }
  constexpr void setTimeDim(unsigned dim) { timeDim = dim; }
  [[nodiscard]] constexpr auto getDim0() const -> unsigned {
    return numDep0Var;
  }
  [[nodiscard]] constexpr auto getDim1() const -> unsigned {
    return numDep1Var;
  }
  [[nodiscard]] constexpr auto getNumDynSym() const -> unsigned {
    return numDynSym;
  }
  [[nodiscard]] constexpr auto getNumCon() const -> unsigned { return numCon; }
  [[nodiscard]] constexpr auto getNumEqCon() const -> unsigned {
    return numEqCon;
  }
  [[nodiscard]] constexpr auto getNumVar() const -> unsigned {
    return numDep0Var + numDep1Var + timeDim + numDynSym;
  }
  [[nodiscard]] constexpr auto getNumPhiCoef() const -> unsigned {
    return numDep0Var + numDep1Var;
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoef() -> unsigned {
    return 2;
  }
  [[nodiscard]] constexpr auto getNumScheduleCoef() const -> unsigned {
    return getNumPhiCoef() + 2;
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> unsigned {
    return 1 + numCon + 2 * numEqCon;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> unsigned {
    return numDynSym + 1;
  }
  constexpr void setNumConstraints(unsigned con) { numCon = con; }
  constexpr void setNumEqConstraints(unsigned con) { numEqCon = con; }
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
  static auto nullSpace(NotNull<const Addr> x, NotNull<const Addr> y)
    -> DenseMatrix<int64_t> {
    const size_t numLoopsCommon =
                   findFirstNonEqual(x->getFusionOmega(), y->getFusionOmega()),
                 xDim = x->getArrayDim(), yDim = y->getArrayDim();
    DenseMatrix<int64_t> A(DenseDims{numLoopsCommon, xDim + yDim});
    if (!numLoopsCommon) return A;
    // indMats cols are [outerMostLoop,...,innerMostLoop]
    PtrMatrix<int64_t> indMatX = x->indexMatrix(), indMatY = y->indexMatrix();
    for (size_t i = 0; i < numLoopsCommon; ++i) {
      A(i, _(begin, xDim)) << indMatX(_, i);
      A(i, _(xDim, end)) << indMatY(_, i);
    }
    // returns rank x num loops
    return orthogonalNullSpace(std::move(A));
  }
  static auto nullSpace(NotNull<const Addr> x) -> DenseMatrix<int64_t> {
    const size_t numLoopsCommon = x->getNumLoops(), dim = x->getArrayDim();
    DenseMatrix<int64_t> A(DenseDims{numLoopsCommon, dim});
    if (!numLoopsCommon) return A;
    // indMats cols are [outerMostLoop,...,innerMostLoop]
    A << x->indexMatrix().transpose();
    // returns rank x num loops
    return orthogonalNullSpace(std::move(A));
  }
  static auto symbolIndex(llvm::ArrayRef<const llvm::SCEV *> s,
                          const llvm::SCEV *v) -> Optional<unsigned> {
    auto b = s.begin(), e = s.end();
    const auto *it = std::find(b, e, v);
    if (it == e) return {};
    return it - b;
  }
  auto symbolIndex(const llvm::SCEV *v) -> Optional<unsigned> {
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
      Optional<unsigned> j = symbolIndex(s0, s1[i]);
      map[i] = j ? *j : n++;
    }
    return n;
  }
  static void fillSyms(llvm::MutableArrayRef<const llvm::SCEV *> s,
                       std::array<llvm::ArrayRef<const llvm::SCEV *>, 2> sa,
                       Vector<unsigned> &map) {
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
  static auto dependence(BumpAlloc<> &alloc, NotNull<const Addr> aix,
                         NotNull<const Addr> aiy) -> DepPoly * {
    assert(aix->sizesMatch(aiy));
    NotNull<const AffineLoopNest> loopx = aix->getLoop();
    NotNull<const AffineLoopNest> loopy = aiy->getLoop();
    DensePtrMatrix<int64_t> Ax{loopx->getA()}, Ay{loopy->getA()};
    auto Sx{loopx->getSyms()}, Sy{loopy->getSyms()};
    // numLoops x numDim
    PtrMatrix<int64_t> Cx{aix->indexMatrix()}, Cy{aiy->indexMatrix()},
      Ox{aix->offsetMatrix()}, Oy{aiy->offsetMatrix()};
    invariant(Cx.numRow(), Cy.numRow());

    auto [nc0, nv0] = Ax.size();
    auto [nc1, nv1] = Ay.size();
    unsigned numDep0Var = loopx->getNumLoops(),
             numDep1Var = loopy->getNumLoops(),
             numVar = numDep0Var + numDep1Var;

    Vector<unsigned> map;
    unsigned numDynSym = mergeMap(map, Sx, Sy);
    invariant(size_t(map.size()), size_t(Sy.size()));
    unsigned numSym = numDynSym + 1;
    DenseMatrix<int64_t> NS{nullSpace(aix, aiy)};
    unsigned timeDim = unsigned{NS.numRow()},
             numCols = numVar + timeDim + numDynSym + 1,
             conCapacity = unsigned(Ax.numRow() + Ay.numRow()) + numVar,
             eqConCapacity = unsigned(Cx.numRow()) + timeDim;

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
    // indMats are [outerMostLoop, ..., innerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (size_t i = 0; i < indexDim; ++i) {
      E(i, _(0, Ox.numCol())) << Ox(i, _);
      E(i, _(numSym, numDep0Var + numSym)) << Cx(i, _(0, numDep0Var));
      E(i, 0) -= Oy(i, 0);
      for (size_t j = 0; j < Oy.numCol() - 1; ++j)
        E(i, 1 + map[j]) -= Oy(i, 1 + j);
      E(i, _(0, numDep1Var) + numSym + numDep0Var) << -Cy(i, _(0, numDep1Var));
    }
    for (size_t i = 0; i < timeDim; ++i) {
      for (size_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS(i, j);
        E(indexDim + i, j + numSym) = nsij;
        E(indexDim + i, j + numSym + numDep0Var) = -nsij;
      }
      E(indexDim + i, numSym + numVar + i) = 1;
    }
    dp->pruneBounds(alloc);
    if (dp->getNumCon()) return dp;
    alloc.rollback(p);
    return nullptr;
  }
  static auto self(BumpAlloc<> &alloc, NotNull<const Addr> ai)
    -> NotNull<DepPoly> {
    NotNull<const AffineLoopNest> loop = ai->getLoop();
    DensePtrMatrix<int64_t> B{loop->getA()};
    auto S{loop->getSyms()};
    // numLoops x numDim
    PtrMatrix<int64_t> C{ai->indexMatrix()}, O{ai->offsetMatrix()};

    auto [nco, nv] = B.size();
    unsigned numDepVar = loop->getNumLoops(), numVar = numDepVar + numDepVar,
             numDynSym = S.size(), numSym = numDynSym + 1;
    DenseMatrix<int64_t> NS{nullSpace(ai)};
    unsigned timeDim = unsigned{NS.numRow()},
             numCols = numVar + timeDim + numDynSym + 1,
             conCapacity = unsigned(2 * B.numRow()) + numVar,
             eqConCapacity = unsigned(C.numCol()) + timeDim;

    size_t memNeeded =
      sizeof(int64_t) * ((conCapacity + eqConCapacity) * numCols + timeDim) +
      sizeof(const llvm::SCEV *) * numDynSym;

    auto *mem =
      (DepPoly *)alloc.allocate(sizeof(DepPoly) + memNeeded, alignof(DepPoly));
    auto *dp = std::construct_at(mem, numDepVar, numDepVar, numDynSym, timeDim,
                                 conCapacity, eqConCapacity);

    // numDep1Var = nv1;
    const Row nc = nco + nco;
    const size_t indexDim{ai->getArrayDim()};
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
    for (size_t i = 0; i < nco; ++i) {
      for (size_t j = 0; j < numSym; ++j) A(i + nco, j) = A(i, j) = B(i, j);
      for (size_t j = 0; j < numDepVar; ++j)
        A(i + nco, j + numSym + numDepVar) = A(i, j + numSym) =
          B(i, j + numSym);
    }
    A(_(nc, end), _(numSym, numSym + numVar)).diag() << 1;
    // L254: Assertion `col < numCol()` failed
    // indMats are [innerMostLoop, ..., outerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (size_t i = 0; i < indexDim; ++i) {
      for (size_t j = 0; j < numDepVar; ++j) {
        int64_t Cji = C(j, i);
        E(i, j + numSym) = Cji;
        E(i, j + numSym + numDepVar) = -Cji;
      }
    }
    for (size_t i = 0; i < timeDim; ++i) {
      for (size_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS(i, j);
        E(indexDim + i, j + numSym) = nsij;
        E(indexDim + i, j + numSym + numDepVar) = -nsij;
      }
      E(indexDim + i, numSym + numVar + i) = 1;
    }
    dp->pruneBounds(alloc);
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

  /// returns `true` if the array accesses are guaranteed independent
  /// conditioning on partial schedules xPhi and yPhi
  [[nodiscard]] auto checkSat(BumpAlloc<> &alloc,
                              NotNull<const AffineLoopNest> xLoop,
                              const int64_t *xOff, DensePtrMatrix<int64_t> xPhi,
                              NotNull<const AffineLoopNest> yLoop,
                              const int64_t *yOff, DensePtrMatrix<int64_t> yPhi)
    -> bool {
    // we take in loops because we might be moving deeper inside the loopnest
    // we take in offsets, because we might be offsetting the loops
    auto p = alloc.scope();
    Row numPhi = xPhi.numRow();
    invariant(yPhi.numRow(), numPhi);
    DensePtrMatrix<int64_t> E{getE()};
    unsigned xNumLoops = unsigned(xPhi.numCol()),
             yNumLoops = unsigned(yPhi.numCol());
    if ((numDep0Var == xNumLoops) || allZero(xPhi(_, _(numDep0Var, end))))
      xNumLoops = numDep0Var;
    else invariant(numDep0Var < xNumLoops);
    if ((numDep1Var == yNumLoops) || allZero(yPhi(_, _(numDep1Var, end))))
      yNumLoops = numDep1Var;
    else invariant(numDep1Var < yNumLoops);
    unsigned numSym = getNumSymbols(), numSymX = numSym + xNumLoops,
             numSymD0 = numSym + numDep0Var, nCol = numSymX + yNumLoops;
    MutDensePtrMatrix<int64_t> B{
      matrix<int64_t>(alloc, numEqCon + numPhi, nCol)};
    bool extend = (numDep0Var != xNumLoops) || (numDep1Var != yNumLoops);
    // we truncate time dim
    if (extend || timeDim) {
      for (size_t r = 0; r < numEqCon; ++r) {
        B(r, _(0, numSymD0)) << E(r, _(0, numSymD0));
        B(r, _(numDep0Var, xNumLoops) + numSym) << 0;
        B(r, _(0, numDep1Var) + numSymX) << E(r, _(0, numDep1Var) + numSymD0);
        B(r, _(numDep1Var, yNumLoops) + numSymX) << 0;
      }
    } else std::copy_n(E.begin(), E.numRow() * E.numCol(), B.begin());
    if (xOff)
      for (size_t c = 0; c < numDep0Var; ++c)
        if (int64_t mlt = xOff[c])
          B(_(0, numEqCon), 0) -= mlt * B(_(0, numEqCon), numSym + c);
    if (yOff)
      for (size_t c = 0; c < numDep1Var; ++c)
        if (int64_t mlt = yOff[c])
          B(_(0, numEqCon), 0) -= mlt * B(_(0, numEqCon), numSymX + c);
    for (size_t r = 0; r < numPhi; ++r) {
      B(r + numEqCon, _(0, numSym)) << 0;
      B(r + numEqCon, _(0, xNumLoops) + numSym) << xPhi(r, _(0, xNumLoops));
      B(r + numEqCon, _(0, yNumLoops) + numSymX) << -yPhi(r, _(0, yNumLoops));
    }
    unsigned rank = unsigned(NormalForm::simplifySystemImpl(B));
    if (rank <= numEqCon) return false;
    unsigned numConstraints =
      extend ? (xLoop->getNumCon() + xNumLoops + yLoop->getNumCon() + yNumLoops)
             : numCon;
    size_t memNeeded =
      sizeof(int64_t) * (size_t(numConstraints + rank) * nCol) +
      sizeof(const llvm::SCEV *) * numDynSym;
    auto *mem =
      (DepPoly *)alloc.allocate(sizeof(DepPoly) + memNeeded, alignof(DepPoly));
    auto *dp = std::construct_at(mem, xNumLoops, yNumLoops, numDynSym, 0,
                                 numConstraints, rank);
    MutDensePtrMatrix<int64_t> A{dp->getA()};
    if (extend) {
      MutDensePtrMatrix<int64_t> Ax{xLoop->getA()}, Ay{yLoop->getA()};
      auto xS{xLoop->getSyms()}, yS{yLoop->getSyms()};
      Vector<unsigned> map;
      unsigned xNumSym = xS.size() + 1, xCon = xLoop->getNumCon(),
               yNumSym = yS.size() + 1, yCon = yLoop->getNumCon(),
               nDS = mergeMap(map, xS, yS), nLoop = xNumLoops + yNumLoops;
      // numSyms should be the same; we aren't pruning symbols
      invariant(numSym, 1 + nDS);
      for (size_t r = 0; r < xCon; ++r) {
        A(r, _(0, xNumSym)) << Ax(r, _(0, xNumSym));
        A(r, _(xNumSym, numSym)) << 0;
        A(r, _(0, xNumLoops) + numSym) << Ax(r, _(0, xNumLoops) + xNumSym);
        A(r, _(0, yNumLoops) + numSymX) << 0;
      }
      for (size_t r = 0; r < yCon; ++r) {
        A(r + xCon, _(0, numSym)) << 0;
        for (size_t j = 0; j < map.size(); ++j)
          A(r + xCon, 1 + map[j]) = Ay(r, 1 + j);
        A(r + xCon, _(0, xNumLoops) + numSym) << 0;
        A(r + xCon, _(0, yNumLoops) + numSymX)
          << Ay(r, _(0, yNumLoops) + yNumSym);
      }
      std::fill(A.begin() + size_t(xCon + yCon) * nCol, A.end(), 0);
      A(_(0, nLoop) + (xCon + yCon), _(0, nLoop) + numSym).diag() << 1;
    } else dp->getA() << getA()(_, _(0, nCol)); // truncate time
    if (xOff)
      for (size_t c = 0; c < xNumLoops; ++c)
        if (int64_t mlt = xOff[c]) A(_, 0) -= mlt * A(_, numSym + c);
    if (yOff)
      for (size_t c = 0; c < yNumLoops; ++c)
        if (int64_t mlt = yOff[c]) A(_, 0) -= mlt * A(_, numSymX + c);
    dp->getE() << B(_(0, rank), _);
    dp->pruneBounds(alloc);
    return dp->getNumCon() == 0;
  }
  friend inline auto operator<<(llvm::raw_ostream &os, const DepPoly &p)
    -> llvm::raw_ostream & {
    return printConstraints(
      printPositive(printConstraints(os << "\n", p.getA(), p.getSyms()),
                    p.getNumDynamic()),
      p.getE(), p.getSyms(), false);
  }

}; // class DepPoly
