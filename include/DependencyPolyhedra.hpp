#pragma once

#include "./ArrayReference.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Orthogonalize.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Simplex.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <tuple>
#include <utility>

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
struct DependencePolyhedra : SymbolicEqPolyhedra {
  // size_t numLoops;
  [[no_unique_address]] size_t numDep0Var; // loops dep 0
  // size_t numDep1Var; // loops dep 1
  [[no_unique_address]] llvm::SmallVector<int64_t, 2> nullStep;

  // TODO: `constexpr` once `llvm::SmallVector` supports it
  [[nodiscard]] auto getTimeDim() const -> size_t { return nullStep.size(); }
  [[nodiscard]] constexpr auto getDim0() const -> size_t { return numDep0Var; }
  [[nodiscard]] auto getDim1() const -> size_t {
    return getNumVar() - numDep0Var - nullStep.size() - S.size();
  }
  [[nodiscard]] auto getNumPhiCoefficients() const -> size_t {
    return getNumVar() - nullStep.size() - S.size();
  }
  static constexpr auto getNumOmegaCoefficients() -> size_t { return 2; }
  [[nodiscard]] auto getNumScheduleCoefficients() const -> size_t {
    return getNumPhiCoefficients() + getNumOmegaCoefficients();
  }
  auto getSymbols(size_t i) -> MutPtrVector<int64_t> {
    return A(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getInEqSymbols(size_t i) const -> PtrVector<int64_t> {
    return A(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getEqSymbols(size_t i) const -> PtrVector<int64_t> {
    return E(i, _(begin, getNumSymbols()));
  }
  [[nodiscard]] auto getCompTimeInEqOffset(size_t i) const
    -> std::optional<int64_t> {
    for (size_t j = 1; j < getNumSymbols(); ++j)
      if (A(i, j))
        return {};
    return A(i, 0);
  }
  [[nodiscard]] auto getCompTimeEqOffset(size_t i) const
    -> std::optional<int64_t> {
    for (size_t j = 1; j < getNumSymbols(); ++j)
      if (E(i, j))
        return {};
    return E(i, 0);
  }

  // static std::optional<llvm::SmallVector<std::pair<int, int>, 4>>
  // matchingStrideConstraintPairs(const ArrayReference &ar0,
  //                               const ArrayReference &ar1) {
  //     // fast path; most common case
  //     if (ar0.sizesMatch(ar1)) {
  //         llvm::SmallVector<std::pair<int, int>, 4> dims;
  //         size_t numDims = ar0.arrayDim();
  //         dims.reserve(numDims);
  //         for (size_t i = 0; i < numDims; ++i)
  //             dims.emplace_back(i, i);
  //         return dims;
  //     }
  //     llvm::errs() << "Sizes don't match!\n";
  //     // Farkas: psi(x) >= 0 iff
  //     // psi(x) = l_0 + lambda' * (b - A'*x) for some l_0, lambda >= 0
  //     // psi(x) is an affine function.
  //     // Here, we assume that function is either...
  //     // if (boundAbove) {
  //     //   w + u'N + alpha_delta + alpha_t'i_t - alpha_s'i_s
  //     // else {
  //     //   alpha_delta + alpha_t'i_t - alpha_s'i_s
  //     // }
  //     // N are the symbolic variables, like loop bounds.
  //     // u and w are introduced variables.
  //     //
  //     // x = [i_s..., i_t...]
  //     //
  //     // or swap alpha signs if subInd < 0
  //     //
  //     // Returns an IntegerEqPolyhedra C'*y <= d
  //     // where
  //     // y = [alpha_delta, alpha_s..., alpha_t..., w, u...]
  //     // for our cost function, we want to set `sum(u)` to zero
  //     // Note y >= 0
  //     //
  //     // This is useful for eliminating indVars as well as for eliminating
  //     `N`
  //     // We have, for example...
  //     // b = [I-1, 0, J-1, 0]
  //     // A = [ 1  -1   0   0
  //     //       0   0   1  -1 ]
  //     // N = [I, J]
  //     // x = [i_s, j_s, i_t, j_t]
  //     //
  //     // w + u'N + alpha_delta + alpha_t'i_t - alpha_s'i_s =
  //     // l_0 + lambda' * (b - A'*x)
  //     // w + alpha_delta + u_1 * I + u_2 * J + alpha_t_i * i_t + alpha_t_j
  //     *
  //     // j_t - alpha_s_i * i_s - alpha_s_j * j_s = l_0 + lambda_0 * (I - 1
  //     -
  //     // i_s) + lambda_1
  //     // * (j_s) + lambda_2 * (J-1 - i_t) + lambda_3 * j_t
  //     //
  //     // (w + alpha_delta - l_0 + lambda_0 + lambda_2) + I*(u_1 - lambda_0)
  //     +
  //     // J*(u_2 - lambda_2) + i_t*(alpha_t_i + lambda_2) + j_t *
  //     // (alpha_t_j-lambda_3) + i_s * (lambda_0 -alpha_s_i) + j_s *
  //     // (-alpha_s_j-lambda_1) = 0
  //     //
  //     // Now...we assume that it is valid to transform this into a system
  //     of
  //     // equations 0 = w + alpha_delta - l_0 + lambda_0 + lambda_2 0 = u_1
  //     -
  //     // lambda_0 0 = u_2 - lambda_2 0 = alpha_t_i + lambda_2 0 = alpha_t_j
  //     -
  //     // lambda_3 0 = lambda_0 - alpha_s_i 0 = -alpha_s_j - lambda_1
  //     //
  //     // A[w*i + x*j]
  //     // w*(i...)
  //     // x*(j...)
  //     // Delinearization seems like the weakest conditions...
  //     //
  //     // what about
  //     // x is symbol, i and j are indvars
  //     // A[i,j]
  //     // A[i,x]
  //     //
  //     // if (!ar0.allConstantStrides())
  //     //     return {};
  //     // if (!ar1.allConstantStrides())
  //     //     return {};
  //     // if (ar0.stridesMatch(ar1)) {
  //     //     return ar0.dim();
  //     // }
  //     // TODO: handle these examples that fail above but can be matched:
  //     // A[0, i, 0, j], A[k, 0, l, 0]
  //     // B[i, k], B[i, K] // k = 0:K-1
  //     // B[i, k], B[i, J] // J's relation to k??? -- split loop?
  //     // size_t dim = 0;
  //     // auto axesix = ar0.axes.begin();
  //     // auto axesiy = ar1.axes.begin();

  //     return {};
  // }

  // static bool check(const ArrayReference &ar0, const ArrayReference &ar1) {
  static auto findFirstNonEqual(PtrVector<unsigned> x, PtrVector<unsigned> y)
    -> size_t {
    const size_t M = std::min(x.size(), y.size());
    for (size_t i = 0; i < M; ++i)
      if (x[i] != y[i])
        return i;
    return M;
  }
  static auto nullSpace(const MemoryAccess &x, const MemoryAccess &y)
    -> IntMatrix {
    const size_t numLoopsCommon =
      findFirstNonEqual(x.getFusionOmega(), y.getFusionOmega());
    const size_t xDim = x.ref.getArrayDim();
    const size_t yDim = y.ref.getArrayDim();
    IntMatrix A(numLoopsCommon, xDim + yDim);
    if (!numLoopsCommon)
      return A;
    // indMats cols are [innerMostLoop, ..., outerMostLoop]
    PtrMatrix<int64_t> indMatX = x.ref.indexMatrix();
    PtrMatrix<int64_t> indMatY = y.ref.indexMatrix();
    for (size_t i = 0; i < numLoopsCommon; ++i) {
      A(i, _(begin, xDim)) = indMatX(i, _);
      A(i, _(xDim, end)) = indMatY(i, _);
    }
    // returns rank x num loops
    return orthogonalNullSpace(std::move(A));
  }
  //     // TODO: two steps:
  //     // 1: gcd test
  //     // 2: check polyhedra volume
  //     // step 1

  //     // step 2
  //     const std::optional<llvm::SmallVector<std::pair<int, int>, 4>>
  //         maybeDims = matchingStrideConstraintPairs(ar0, ar1);

  //     return true;
  // }

  //   DependencePolyhedra(aln0, aln1, ar0, ar1)
  //
  // dependence from between ma0 and ma1
  // Produces
  // A*x <= b
  // Where x = [inds0..., inds1..., time..]
  auto symbolIndex(const llvm::SCEV *v) -> unsigned int {
    for (unsigned int i = 0; i < S.size(); ++i)
      if (S[i] == v)
        return i;
    return std::numeric_limits<unsigned int>::max();
  }
  auto merge(llvm::ArrayRef<const llvm::SCEV *> s0,
             llvm::ArrayRef<const llvm::SCEV *> s1)
    -> std::pair<llvm::SmallVector<unsigned int>,
                 llvm::SmallVector<unsigned int>> {
    S.reserve(s0.size() + s1.size());
    std::pair<llvm::SmallVector<unsigned int>, llvm::SmallVector<unsigned int>>
      ret;
    ret.first.reserve(s0.size());
    ret.second.reserve(s1.size());
    for (size_t i = 0; i < s0.size(); ++i) {
      ret.first.push_back(i);
      S.push_back(s0[i]);
    }
    for (auto i : s1) {
      unsigned int j = symbolIndex(i);
      if (j == std::numeric_limits<unsigned int>::max()) {
        ret.second.push_back(S.size());
        S.push_back(i);
      } else {
        ret.second.push_back(j);
      }
    }
    return ret;
  }
  // static fillA
  DependencePolyhedra(const MemoryAccess &ma0, const MemoryAccess &ma1)
    : SymbolicEqPolyhedra{} {

    const ArrayReference &ar0 = ma0.ref;
    const ArrayReference &ar1 = ma1.ref;
    assert(ar0.sizesMatch(ar1));
    auto [nc0, nv0] = ar0.loop->A.size();
    auto [nc1, nv1] = ar1.loop->A.size();
    numDep0Var = ar0.loop->getNumLoops();
    size_t numDep1Var = ar1.loop->getNumLoops();
    size_t numVar = numDep0Var + numDep1Var;
    std::pair<llvm::SmallVector<unsigned int>, llvm::SmallVector<unsigned int>>
      oldToNewMaps{merge(ar0.loop->S, ar1.loop->S)};
    auto &oldToNewMap0 = oldToNewMaps.first;
    auto &oldToNewMap1 = oldToNewMaps.second;
    assert(oldToNewMap0.size() == ar0.loop->S.size());
    assert(oldToNewMap1.size() == ar1.loop->S.size());

    // numDep1Var = nv1;
    const Row nc = nc0 + nc1;
    IntMatrix NS{nullSpace(ma0, ma1)};
    const size_t nullDim{NS.numRow()};
    const size_t indexDim{ar0.getArrayDim()};
    nullStep.resize_for_overwrite(nullDim);
    for (size_t i = 0; i < nullDim; ++i) {
      int64_t s = 0;
      for (size_t j = 0; j < NS.numCol(); ++j)
        s += NS(i, j) * NS(i, j);
      nullStep[i] = s;
    }
    //           column meansing in in order
    const size_t numSymbols = getNumSymbols();
    A.resize(nc + numVar, numSymbols + numVar + nullDim);
    E.resize(indexDim + nullDim, A.numCol());
    // ar0 loop
    for (size_t i = 0; i < nc0; ++i) {
      A(i, 0) = ar0.loop->A(i, 0);
      for (size_t j = 0; j < oldToNewMap0.size(); ++j)
        A(i, 1 + oldToNewMap0[j]) = ar0.loop->A(i, 1 + j);
      for (size_t j = 0; j < numDep0Var; ++j)
        A(i, j + numSymbols) = ar0.loop->A(i, j + ar0.loop->getNumSymbols());
    }
    for (size_t i = 0; i < nc1; ++i) {
      A(nc0 + i, 0) = ar1.loop->A(i, 0);
      for (size_t j = 0; j < oldToNewMap1.size(); ++j)
        A(nc0 + i, 1 + oldToNewMap1[j]) = ar1.loop->A(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        A(nc0 + i, j + numSymbols + numDep0Var) =
          ar1.loop->A(i, j + ar1.loop->getNumSymbols());
    }
    A(_(nc, end), _(numSymbols, numSymbols + numVar)).diag() = 1;
    // L254: Assertion `col < numCol()` failed
    // indMats are [innerMostLoop, ..., outerMostLoop] x arrayDim
    // offsetMats are arrayDim x numSymbols
    PtrMatrix<int64_t> A0 = ar0.indexMatrix();
    PtrMatrix<int64_t> A1 = ar1.indexMatrix();
    PtrMatrix<int64_t> O0 = ar0.offsetMatrix();
    PtrMatrix<int64_t> O1 = ar1.offsetMatrix();
    // E(i,:)* indVars = q[i]
    // e.g. i_0 + j_0 + off_0 = i_1 + j_1 + off_1
    // i_0 + j_0 - i_1 - j_1 = off_1 - off_0
    for (size_t i = 0; i < indexDim; ++i) {
      E(i, 0) = O0(i, 0);
      for (size_t j = 0; j < O0.numCol() - 1; ++j)
        E(i, 1 + oldToNewMap0[j]) = O0(i, 1 + j);
      for (size_t j = 0; j < numDep0Var; ++j)
        E(i, j + numSymbols) = A0(j, i);
      E(i, 0) -= O1(i, 0);
      for (size_t j = 0; j < O1.numCol() - 1; ++j)
        E(i, 1 + oldToNewMap1[j]) -= O1(i, 1 + j);
      for (size_t j = 0; j < numDep1Var; ++j)
        E(i, j + numSymbols + numDep0Var) = -A1(j, i);
    }
    for (size_t i = 0; i < nullDim; ++i) {
      for (size_t j = 0; j < NS.numCol(); ++j) {
        int64_t nsij = NS(i, j);
        E(indexDim + i, j + numSymbols) = nsij;
        E(indexDim + i, j + numSymbols + numDep0Var) = -nsij;
      }
      E(indexDim + i, numSymbols + numDep0Var + numDep1Var + i) = 1;
    }
    initializeComparator();
    pruneBounds();
  }
  static constexpr auto getNumLambda(Row numIneq, Row numEq) -> size_t {
    return 1 + size_t(numIneq) + 2 * size_t(numEq);
  }
  [[nodiscard]] auto getNumLambda() const -> size_t {
    return getNumLambda(A.numRow(), E.numRow());
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
  [[nodiscard]] auto farkasPair() const -> std::pair<Simplex, Simplex> {

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
    assert(getNumLambda() == numLambda);
    std::pair<Simplex, Simplex> pair;
    Simplex &fw(pair.first);
    fw.resize(numConstraintsNew, numVarNew + 1);
    MutPtrMatrix<int64_t> fC{fw.getConstraints()(_, _(1, end))};
    fC(_, 0) = 0;
    fC(0, 0) = 1; // lambda_0
    fC(_, _(1, 1 + numInequalityConstraintsOld)) =
      A(_, _(begin, numConstraintsNew)).transpose();
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
    Simplex &bw(pair.second);
    bw.resize(numConstraintsNew, numVarNew + 1);
    MutPtrMatrix<int64_t> bC{bw.getConstraints()(_, _(1, end))};

    bC(_, _(begin, numVarNew)) = PtrMatrix<int64_t>(fC(_, _(begin, numVarNew)));
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
    return pair;
    // fw.removeExtraVariables(numVarKeep);
    // bw.removeExtraVariables(numVarKeep);
    // assert(fw.E.numRow() == fw.q.size());
    // assert(bw.E.numRow() == bw.q.size());
    // return pair;
  }
  friend auto operator<<(llvm::raw_ostream &os, const DependencePolyhedra &p)
    -> llvm::raw_ostream & {
    return printConstraints(
      printPositive(printConstraints(os << "\n", p.A, p.S), p.getNumDynamic()),
      p.E, p.S, false);
  }

}; // namespace DependencePolyhedra

struct Dependence {
  // Plan here is...
  // depPoly gives the constraints
  // dependenceFwd gives forward constraints
  // dependenceBwd gives forward constraints
  // isForward() indicates whether forward is non-empty
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
  [[no_unique_address]] DependencePolyhedra depPoly;
  [[no_unique_address]] Simplex dependenceSatisfaction;
  [[no_unique_address]] Simplex dependenceBounding;
  [[no_unique_address]] MemoryAccess *in;
  [[no_unique_address]] MemoryAccess *out;
  [[no_unique_address]] bool forward;
  // Dependence(DependencePolyhedra depPoly,
  //            IntegerEqPolyhedra dependenceSatisfaction,
  //            IntegerEqPolyhedra dependenceBounding, MemoryAccess *in,
  //            MemoryAccess *out, const bool forward)
  //     : depPoly(std::move(depPoly)),
  //       dependenceSatisfaction(std::move(dependenceSatisfaction)),
  //       dependenceBounding(std::move(dependenceBounding)), in(in),
  //       out(out), forward(forward){};
  // if there is no time dimension, it returns a 0xdim matrix and `R == 0`
  // else, it returns a square matrix, where the first `R` rows correspond
  // to time-axis.
  // static std::pair<IntMatrix, int64_t>
  // transformationMatrix(const ArrayReference &xRef, const ArrayReference
  // &yRef,
  //                      const size_t numLoopsCommon) {
  //     const size_t xDim = xRef.arrayDim();
  //     const size_t yDim = yRef.arrayDim();
  //     PtrMatrix<const int64_t> indMatX = xRef.indexMatrix();
  //     PtrMatrix<const int64_t> indMatY = yRef.indexMatrix();
  //     IntMatrix A(numLoopsCommon, xDim + yDim);
  //     for (size_t i = 0; i < numLoopsCommon; ++i) {
  //         for (size_t j = 0; j < xDim; ++j) {
  //             A(i, j) = indMatX(i, j);
  //         }
  //         for (size_t j = 0; j < yDim; ++j) {
  //             A(i, j + xDim) = indMatY(i, j);
  //         }
  //     }
  //     IntMatrix N = NormalForm::nullSpace(A);
  //     const auto [R, D] = N.size();
  //     if (R) {
  //         N.resizeRows(D);
  //         A = NormalForm::removeRedundantRows(A.transpose());
  //         assert(D - R == A.numRow());
  //         for (size_t r = R; r < D; ++r) {
  //             for (size_t d = 0; d < D; ++d) {
  //                 N(r, d) = A(r - R, d);
  //             }
  //         }
  //     }
  //     return std::make_pair(N, R);
  //     // IntMatrix B = NormalForm::removeRedundantRows(A.transpose());
  //     // const auto [R, D] = B.size();
  //     // if (R < D) {
  //     //     IntMatrix N = NormalForm::nullSpace(A.transpose());
  //     //     assert(N.numRow() == D - R);
  //     //     A.resizeRows(D);
  //     //     for (size_t r = R; r < D; ++r) {
  //     //         for (size_t d = 0; d < D; ++d) {
  //     //             A(r, d) = N(r - R, d);
  //     //         }
  //     //     }
  //     // }
  //     // return std::make_pair(A, R);
  // }

  // static std::pair<IntMatrix, int64_t>
  // transformationMatrix(const MemoryAccess &x, const MemoryAccess &y) {
  //     return transformationMatrix(
  //         x.ref, y.ref,
  //         findFirstNonEqualEven(x.schedule.getOmega(),
  //                               y.schedule.getOmega()) >>
  //             1);
  // }
  // emplaces dependencies without any repeat accesses to the same memory
  // returns
  [[nodiscard]] auto isInactive(size_t depth) const -> bool {
    return (depth >= std::min(out->getNumLoops(), in->getNumLoops()));
  }
  [[nodiscard]] auto getNumLambda() const -> size_t {
    return depPoly.getNumLambda() << 1;
  }
  [[nodiscard]] auto getNumSymbols() const -> size_t {
    return depPoly.getNumSymbols();
  }
  [[nodiscard]] auto getNumPhiCoefficients() const -> size_t {
    return depPoly.getNumPhiCoefficients();
  }
  static constexpr auto getNumOmegaCoefficients() -> size_t {
    return DependencePolyhedra::getNumOmegaCoefficients();
  }
  [[nodiscard]] auto getNumConstraints() const -> size_t {
    return dependenceBounding.getNumConstraints() +
           dependenceSatisfaction.getNumConstraints();
  }
  [[nodiscard]] auto getSatConstants() const -> StridedVector<int64_t> {
    return dependenceSatisfaction.getConstants();
  }
  [[nodiscard]] auto getBndConstants() const -> StridedVector<int64_t> {
    return dependenceBounding.getConstants();
  }
  [[nodiscard]] auto getSatLambda() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, _(1, 1 + depPoly.getNumLambda()));
  }
  [[nodiscard]] auto getBndLambda() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(
      _, _(1, 1 + depPoly.getNumLambda()));
  }
  [[nodiscard]] auto getSatPhiCoefs() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, _(1 + depPoly.getNumLambda(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getSatPhi0Coefs() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, _(1 + depPoly.getNumLambda(),
           1 + depPoly.getNumLambda() + depPoly.getDim0()));
  }
  [[nodiscard]] auto getSatPhi1Coefs() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, _(1 + depPoly.getNumLambda() + depPoly.getDim0(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getBndPhiCoefs() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(
      _, _(1 + depPoly.getNumLambda(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getBndPhi0Coefs() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(
      _, _(1 + depPoly.getNumLambda(),
           1 + depPoly.getNumLambda() + depPoly.getDim0()));
  }
  [[nodiscard]] auto getBndPhi1Coefs() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(
      _, _(1 + depPoly.getNumLambda() + depPoly.getDim0(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients()));
  }
  [[nodiscard]] auto getSatOmegaCoefs() const -> PtrMatrix<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, _(1 + depPoly.getNumLambda() + getNumPhiCoefficients(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients() +
             getNumOmegaCoefficients()));
  }
  [[nodiscard]] auto getBndOmegaCoefs() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(
      _, _(1 + depPoly.getNumLambda() + getNumPhiCoefficients(),
           1 + depPoly.getNumLambda() + getNumPhiCoefficients() +
             getNumOmegaCoefficients()));
  }
  [[nodiscard]] auto getSatW() const -> StridedVector<int64_t> {
    return dependenceSatisfaction.getConstraints()(
      _, 1 + depPoly.getNumLambda() + getNumPhiCoefficients() +
           getNumOmegaCoefficients());
  }
  [[nodiscard]] auto getBndCoefs() const -> PtrMatrix<int64_t> {
    return dependenceBounding.getConstraints()(_, _(1 + depPoly.getNumLambda() +
                                                      getNumPhiCoefficients() +
                                                      getNumOmegaCoefficients(),
                                                    end));
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
  [[nodiscard]] auto isSatisfied(const Schedule &schIn,
                                 const Schedule &schOut) const -> bool {
    const ArrayReference &refIn = in->ref;
    const ArrayReference &refOut = out->ref;
    size_t numLoopsIn = refIn.getNumLoops();
    size_t numLoopsOut = refOut.getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numLoopsTotal = numLoopsIn + numLoopsOut;
    Vector<int64_t> schv;
    schv.resizeForOverwrite(dependenceSatisfaction.getNumVar());
    const SquarePtrMatrix<int64_t> inPhi = schIn.getPhi();
    const SquarePtrMatrix<int64_t> outPhi = schOut.getPhi();
    llvm::ArrayRef<int64_t> inFusOmega = schIn.getFusionOmega();
    llvm::ArrayRef<int64_t> outFusOmega = schOut.getFusionOmega();
    llvm::ArrayRef<int64_t> inOffOmega = schIn.getOffsetOmega();
    llvm::ArrayRef<int64_t> outOffOmega = schOut.getOffsetOmega();
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
      schv(_(begin, numLoopsIn)) = inPhi(i, _);
      schv(_(numLoopsIn, numLoopsTotal)) = outPhi(i, _);
      int64_t inO = inOffOmega[i], outO = outOffOmega[i];
      // forward means offset is 2nd - 1st
      schv[numLoopsTotal] = outO - inO;
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction.unSatisfiable(schv, numLambda) ||
          dependenceBounding.unSatisfiable(schv, numLambda))
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(llvm::ArrayRef<unsigned> inFusOmega,
                                 llvm::ArrayRef<unsigned> outFusOmega) const
    -> bool {
    const ArrayReference &refIn = in->ref;
    const ArrayReference &refOut = out->ref;
    size_t numLoopsIn = refIn.getNumLoops();
    size_t numLoopsOut = refOut.getNumLoops();
    size_t numLoopsCommon = std::min(numLoopsIn, numLoopsOut);
    size_t numLoopsTotal = numLoopsIn + numLoopsOut;
    Vector<int64_t> schv;
    schv.resizeForOverwrite(dependenceSatisfaction.getNumVar());
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
      schv(numLoopsIn - i - 1) = 1;
      schv(numLoopsTotal - i - 1) = 1;
      // forward means offset is 2nd - 1st
      schv[numLoopsTotal] = 0;
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (dependenceSatisfaction.unSatisfiable(schv, numLambda) ||
          dependenceBounding.unSatisfiable(schv, numLambda))
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(const Schedule &sx, const Schedule &sy,
                                 size_t d) const -> bool {
    const size_t numLambda = depPoly.getNumLambda();
    const size_t numLoopsX = depPoly.getDim0();
    const size_t numLoopsY = depPoly.getDim1();
    // const size_t numLoopsX = sx.getNumLoops();
    // const size_t numLoopsY = sy.getNumLoops();
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    sch(_(begin, numLoopsX)) = sx.getPhi()(d, _(end - numLoopsX, end));
    sch(_(numLoopsX, numLoopsTotal)) = sy.getPhi()(d, _(end - numLoopsY, end));
    sch(numLoopsTotal) = sx.getOffsetOmega()[d];
    sch(numLoopsTotal + 1) = sy.getOffsetOmega()[d];
    return dependenceSatisfaction.satisfiable(sch, numLambda);
  }
  [[nodiscard]] auto isSatisfied(size_t d) const -> bool {
    const size_t numLambda = depPoly.getNumLambda();
    const size_t numLoopsX = depPoly.getDim0();
    const size_t numLoopsY = depPoly.getDim1();
    // const size_t numLoopsX = sx.getNumLoops();
    // const size_t numLoopsY = sy.getNumLoops();
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    Vector<int64_t> sch(numLoopsTotal + 2);
    assert(sch.size() == numLoopsTotal + 2);
    sch(numLoopsX - d - 1) = 1;
    sch(numLoopsTotal - d - 1) = 1;
    // sch(numLoopsTotal) = x[d];
    // sch(numLoopsTotal + 1) = y[d];
    return dependenceSatisfaction.satisfiable(sch, numLambda);
  }
  // bool isSatisfied(size_t d) {
  //     return forward ? isSatisfied(in->getFusedOmega(),
  //     out->getFusedOmega(), d)
  //                    : isSatisfied(out->getFusedOmega(),
  //                    in->getFusedOmega(), d);
  // }
  static auto checkDirection(const std::pair<Simplex, Simplex> &p,
                             const MemoryAccess &x, const MemoryAccess &y,
                             const Schedule &xSchedule,
                             const Schedule &ySchedule, size_t numLambda,
                             Col nonTimeDim) -> bool {
    const Simplex &fxy = p.first;
    const Simplex &fyx = p.second;
    const size_t numLoopsX = x.ref.getNumLoops();
    const size_t numLoopsY = y.ref.getNumLoops();
#ifndef NDEBUG
    const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
#endif
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    SquarePtrMatrix<int64_t> xPhi = xSchedule.getPhi();
    SquarePtrMatrix<int64_t> yPhi = ySchedule.getPhi();
    PtrVector<int64_t> xOffOmega = xSchedule.getOffsetOmega();
    PtrVector<int64_t> yOffOmega = ySchedule.getOffsetOmega();
    PtrVector<int64_t> xFusOmega = xSchedule.getFusionOmega();
    PtrVector<int64_t> yFusOmega = ySchedule.getFusionOmega();
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    // i iterates from outer-most to inner most common loop
    for (size_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (int64_t o2idiff = yFusOmega[i] - xFusOmega[i])
        return o2idiff > 0;
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
      sch(_(begin, numLoopsX)) = xPhi(i, _);
      sch(_(numLoopsX, numLoopsTotal)) = yPhi(i, _);
      sch(numLoopsTotal) = xOffOmega[i];
      sch(numLoopsTotal + 1) = yOffOmega[i];
      if (fxy.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        assert(!fyx.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim)));
        // llvm::errs()
        //     << "Dependence decided by forward violation with i = " <<
        //     i
        //     << "\n";
#endif
        return false;
      }
      if (fyx.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        // llvm::errs()
        //     << "Dependence decided by backward violation with i = "
        //     << i
        //     << "\n";
#endif
        return true;
      }
    }
    // assert(false);
    // return false;
  }
  static auto checkDirection(const std::pair<Simplex, Simplex> &p,
                             const MemoryAccess &x, const MemoryAccess &y,
                             size_t numLambda, Col nonTimeDim) -> bool {
    const Simplex &fxy = p.first;
    const Simplex &fyx = p.second;
    const size_t numLoopsX = x.ref.getNumLoops();
    const size_t numLoopsY = y.ref.getNumLoops();
#ifndef NDEBUG
    const size_t numLoopsCommon = std::min(numLoopsX, numLoopsY);
#endif
    const size_t numLoopsTotal = numLoopsX + numLoopsY;
    PtrVector<unsigned> xFusOmega = x.getFusionOmega();
    PtrVector<unsigned> yFusOmega = y.getFusionOmega();
    Vector<int64_t> sch;
    sch.resizeForOverwrite(numLoopsTotal + 2);
    // i iterates from outer-most to inner most common loop
    for (size_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (yFusOmega[i] != xFusOmega[i])
        return yFusOmega[i] > xFusOmega[i];
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
      sch(numLoopsX - 1 - i) = 1;
      sch(numLoopsTotal - 1 - i) = 1;
      if (fxy.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        assert(!fyx.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim)));
        // llvm::errs()
        //     << "Dependence decided by forward violation with i = " <<
        //     i
        //     << "\n";
#endif
        return false;
      }
      if (fyx.unSatisfiableZeroRem(sch, numLambda, size_t(nonTimeDim))) {
#ifndef NDEBUG
        // llvm::errs()
        //     << "Dependence decided by backward violation with i = "
        //     << i
        //     << "\n";
#endif
        return true;
      }
    }
    // assert(false);
    // return false;
  }
  static void timelessCheck(llvm::SmallVectorImpl<Dependence> &deps,
                            DependencePolyhedra dxy, MemoryAccess &x,
                            MemoryAccess &y) {
    std::pair<Simplex, Simplex> pair(dxy.farkasPair());
    const size_t numLambda = dxy.getNumLambda();
    assert(dxy.getTimeDim() == 0);
    if (checkDirection(pair, x, y, numLambda, dxy.A.numCol())) {
      // pair.first.truncateVars(pair.first.getNumVar() -
      //                         dxy.getNumSymbols());
      pair.first.truncateVars(2 + numLambda + dxy.getNumScheduleCoefficients());
      deps.emplace_back(Dependence{std::move(dxy), std::move(pair.first),
                                   std::move(pair.second), &x, &y, true});
    } else {
      // pair.second.truncateVars(pair.second.getNumVar() -
      // dxy.getNumSymbols());
      pair.second.truncateVars(2 + numLambda +
                               dxy.getNumScheduleCoefficients());
      deps.emplace_back(Dependence{std::move(dxy), std::move(pair.second),
                                   std::move(pair.first), &y, &x, false});
    }
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  static void timeCheck(llvm::SmallVectorImpl<Dependence> &deps,
                        DependencePolyhedra dxy, MemoryAccess &x,
                        MemoryAccess &y) {
    std::pair<Simplex, Simplex> pair(dxy.farkasPair());
    // copy backup
    std::pair<Simplex, Simplex> farkasBackups = pair;
    const size_t numInequalityConstraintsOld =
      dxy.getNumInequalityConstraints();
    const size_t numEqualityConstraintsOld = dxy.getNumEqualityConstraints();
    const size_t ineqEnd = 1 + numInequalityConstraintsOld;
    const size_t posEqEnd = ineqEnd + numEqualityConstraintsOld;
    const size_t numLambda = posEqEnd + numEqualityConstraintsOld;
    const size_t numScheduleCoefs = dxy.getNumScheduleCoefficients();
    assert(numLambda == dxy.getNumLambda());
    MemoryAccess *in = &x, *out = &y;
    const bool isFwd =
      checkDirection(pair, x, y, numLambda, dxy.A.numCol() - dxy.getTimeDim());
    if (isFwd) {
      std::swap(farkasBackups.first, farkasBackups.second);
    } else {
      std::swap(in, out);
      std::swap(pair.first, pair.second);
    }
    pair.first.truncateVars(2 + numLambda + numScheduleCoefs);
    deps.emplace_back(Dependence{dxy, std::move(pair.first),
                                 std::move(pair.second), in, out, isFwd});
    assert(out->getNumLoops() + in->getNumLoops() ==
           deps.back().getNumPhiCoefficients());
    // pair is invalid
    const size_t timeDim = dxy.getTimeDim();
    assert(timeDim);
    const size_t numVarOld = size_t(dxy.A.numCol());
    const size_t numVar = numVarOld - timeDim;
    // const size_t numBoundingCoefs = numVarKeep - numLambda;
    // remove the time dims from the deps
    deps.back().depPoly.truncateVars(numVar);
    deps.back().depPoly.nullStep.clear();
    assert(out->getNumLoops() + in->getNumLoops() ==
           deps.back().getNumPhiCoefficients());
    // deps.back().depPoly.removeExtraVariables(numVar);
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be absolutely
    // insane
    llvm::SmallVector<bool, 16> timeDirection(timeDim);
    size_t t = 0;
    auto fE{farkasBackups.first.getConstraints()(_, _(1, end))};
    auto sE{farkasBackups.second.getConstraints()(_, _(1, end))};
    do {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = dxy.nullStep[t];
      size_t v = numVar + t;
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        if (int64_t Acv = dxy.A(c, v)) {
          Acv *= step;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *1
        }
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy.E(c, v) * step;
        fE(0, c + ineqEnd) -= Ecv;
        fE(0, c + posEqEnd) += Ecv;
        sE(0, c + ineqEnd) -= Ecv;
        sE(0, c + posEqEnd) += Ecv;
      }
      // pair = farkasBackups;
      // pair.first.removeExtraVariables(numVarKeep);
      // pair.second.removeExtraVariables(numVarKeep);
      // farkasBacklups is swapped with respect to
      // checkDirection(..., *in, *out);
      timeDirection[t] = checkDirection(farkasBackups, *out, *in, numLambda,
                                        dxy.A.numCol() - dxy.getTimeDim());
      // fix
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        int64_t Acv = dxy.A(c, v) * step;
        fE(0, c + 1) += Acv;
        sE(0, c + 1) += Acv;
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy.E(c, v) * step;
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
      int64_t step = (2 * timeDirection[t] - 1) * dxy.nullStep[t];
      size_t v = numVar + t;
      for (size_t c = 0; c < numInequalityConstraintsOld; ++c) {
        if (int64_t Acv = dxy.A(c, v)) {
          Acv *= step;
          dxy.A(c, 0) -= Acv;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *-1
        }
      }
      for (size_t c = 0; c < numEqualityConstraintsOld; ++c) {
        // each of these actually represents 2 inds
        int64_t Ecv = dxy.E(c, v) * step;
        dxy.E(c, 0) -= Ecv;
        fE(0, c + ineqEnd) -= Ecv;
        fE(0, c + posEqEnd) += Ecv;
        sE(0, c + ineqEnd) -= Ecv;
        sE(0, c + posEqEnd) += Ecv;
      }
    } while (++t < timeDim);
    dxy.truncateVars(numVar);
    dxy.nullStep.clear();
    farkasBackups.first.truncateVars(2 + numLambda + numScheduleCoefs);
    deps.emplace_back(Dependence{std::move(dxy), std::move(farkasBackups.first),
                                 std::move(farkasBackups.second), out, in,
                                 !isFwd});
    assert(out->getNumLoops() + in->getNumLoops() ==
           deps.back().getNumPhiCoefficients());
  }

  static auto check(llvm::SmallVectorImpl<Dependence> &deps, MemoryAccess &x,
                    MemoryAccess &y) -> size_t {
    if (x.ref.gcdKnownIndependent(y.ref))
      return 0;
    DependencePolyhedra dxy(x, y);
    assert(x.getNumLoops() == dxy.getDim0());
    assert(y.getNumLoops() == dxy.getDim1());
    assert(x.getNumLoops() + y.getNumLoops() == dxy.getNumPhiCoefficients());
    if (dxy.isEmpty())
      return 0;
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    if (dxy.getTimeDim()) {
      timeCheck(deps, std::move(dxy), x, y);
      return 2;
    } else {
      timelessCheck(deps, std::move(dxy), x, y);
      return 1;
    }
  }

  friend auto operator<<(llvm::raw_ostream &os, const Dependence &d)
    -> llvm::raw_ostream & {
    os << "Dependence Poly ";
    if (d.forward)
      os << "x -> y:";
    else
      os << "y -> x:";
    // os << d.depPoly << "\nA = " << d.depPoly.A << "\nE = " << d.depPoly.E
    //    << "\nSchedule Constraints:" << d.dependenceSatisfaction
    //    << "\nBounding Constraints:" << d.dependenceBounding;
    if (d.in)
      os << "\n\tInput:\n" << *d.in;
    if (d.out)
      os << "\n\tOutput:\n" << *d.out;
    return os << "\n";
  }
};
