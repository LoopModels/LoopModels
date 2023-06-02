#pragma once
#include "DependencyPolyhedra.hpp"
#include "Schedule.hpp"
#include <Loops.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Invariant.hpp>
#include <cstdint>
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
  [[no_unique_address]] NotNull<Addr> in;
  [[no_unique_address]] NotNull<Addr> out;
  [[no_unique_address]] Dependence *next{nullptr};
  // the upper bit of satLvl indicates whether the satisfaction is because of
  // conditional independence (value = 0), or whether it was because of offsets
  // when solving the linear program (value = 1).
  [[no_unique_address]] std::array<uint8_t, 7> satLvl{255, 255, 255, 255,
                                                      255, 255, 255};
  [[no_unique_address]] bool forward;

  static auto timelessCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                            NotNull<Addr> x, NotNull<Addr> y,
                            std::array<NotNull<Simplex>, 2> pair, bool isFwd)
    -> Dependence * {
    const size_t numLambda = dxy->getNumLambda();
    invariant(dxy->getTimeDim(), unsigned(0));
    if (isFwd) {
      pair[0]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
      return alloc.create<Dependence>(dxy, pair, x, y, true);
    }
    pair[1]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
    std::swap(pair[0], pair[1]);
    return alloc.create<Dependence>(dxy, pair, y, x, false);
  }
  static auto timelessCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                            NotNull<Addr> x, NotNull<Addr> y,
                            std::array<NotNull<Simplex>, 2> pair)
    -> Dependence * {
    return timelessCheck(alloc, dxy, x, y, pair,
                         checkDirection(alloc, pair, x, y, dxy->getNumLambda(),
                                        dxy->getNumVar() + 1));
    ;
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  static auto timeCheck(BumpAlloc<> &alloc, NotNull<DepPoly> dxy,
                        NotNull<Addr> x, NotNull<Addr> y,
                        std::array<NotNull<Simplex>, 2> pair) -> Dependence * {
    // copy backup
    std::array<NotNull<Simplex>, 2> farkasBackups{pair[0]->copy(alloc),
                                                  pair[1]->copy(alloc)};
    const size_t numInequalityConstraintsOld =
                   dxy->getNumInequalityConstraints(),
                 numEqualityConstraintsOld = dxy->getNumEqualityConstraints(),
                 ineqEnd = 1 + numInequalityConstraintsOld,
                 posEqEnd = ineqEnd + numEqualityConstraintsOld,
                 numLambda = posEqEnd + numEqualityConstraintsOld,
                 numScheduleCoefs = dxy->getNumScheduleCoef();
    invariant(numLambda, size_t(dxy->getNumLambda()));
    const bool isFwd = checkDirection(alloc, pair, x, y, numLambda,
                                      dxy->getA().numCol() - dxy->getTimeDim());
    NotNull<Addr> in = x, out = y;
    if (isFwd) {
      std::swap(farkasBackups[0], farkasBackups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0]->truncateVars(1 + numLambda + numScheduleCoefs);
    auto *dep0 =
      alloc.create<Dependence>(dxy->copy(alloc), pair, in, out, isFwd);
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0->getNumPhiCoefficients());
    // pair is invalid
    const size_t timeDim = dxy->getTimeDim(),
                 numVar = 1 + dxy->getNumVar() - timeDim;
    invariant(timeDim > 0);
    // 1 + because we're indexing into A and E, ignoring the constants
    // remove the time dims from the deps
    // dep0.depPoly->truncateVars(numVar);

    // dep0.depPoly->setTimeDim(0);
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0->getNumPhiCoefficients());
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
    auto *dep1 = alloc.create<Dependence>(dxy, farkasBackups, out, in, !isFwd);
    invariant(out->getNumLoops() + in->getNumLoops(),
              dep0->getNumPhiCoefficients());
    dep0->setNext(dep1);
    return dep0;
  }

public:
  [[nodiscard]] constexpr auto getNext() -> Dependence * { return next; }
  [[nodiscard]] constexpr auto getNext() const -> const Dependence * {
    return next;
  }
  [[nodiscard]] constexpr auto input() -> NotNull<Addr> { return in; }
  [[nodiscard]] constexpr auto output() -> NotNull<Addr> { return out; }
  [[nodiscard]] constexpr auto input() const -> NotNull<const Addr> {
    return in;
  }
  [[nodiscard]] constexpr auto output() const -> NotNull<const Addr> {
    return out;
  }
  constexpr void setNext(Dependence *n) { next = n; }
  constexpr Dependence(NotNull<DepPoly> poly,
                       std::array<NotNull<Simplex>, 2> depSatBound,
                       NotNull<Addr> i, NotNull<Addr> o, bool fwd)
    : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
      dependenceBounding(depSatBound[1]), in(i), out(o), forward(fwd) {}
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
  constexpr void setSatLevelLP(uint8_t d) { satLvl.front() = uint8_t(128) | d; }
  [[nodiscard]] constexpr auto satLevel() const -> uint8_t {
    return satLvl.front() & uint8_t(127);
  }
  [[nodiscard]] constexpr auto isSat(unsigned d) const -> bool {
    invariant(d <= 127);
    return satLevel() <= d;
  }
  /// if true, then conditioned on the sat level,
  [[nodiscard]] constexpr auto isCondIndep() const -> bool {
    return (satLvl.front() & uint8_t(128)) == uint8_t(0);
  }
  [[nodiscard]] constexpr auto getArrayPointer() -> const llvm::SCEV * {
    return in->getArrayPointer();
  }
  /// indicates whether forward is non-empty
  [[nodiscard]] constexpr auto isForward() const -> bool { return forward; }
  [[nodiscard]] constexpr auto nodeIn() const -> const ScheduledNode * {
    return in->getNode();
  }
  // [[nodiscard]] constexpr auto nodeOut() const -> unsigned {
  //   return out->getNode();
  // }
  [[nodiscard]] constexpr auto getDynSymDim() const -> size_t {
    return depPoly->getNumDynSym();
  }
  [[nodiscard]] auto inputIsLoad() const -> bool { return in->isLoad(); }
  [[nodiscard]] auto outputIsLoad() const -> bool { return out->isLoad(); }
  [[nodiscard]] auto inputIsStore() const -> bool { return in->isStore(); }
  [[nodiscard]] auto outputIsStore() const -> bool { return out->isStore(); }
  /// getInIndMat() -> getInNumLoops() x arrayDim()
  [[nodiscard]] auto getInIndMat() const -> DensePtrMatrix<int64_t> {
    return in->indexMatrix();
  }
  [[nodiscard]] auto
  checkEmptySat(BumpAlloc<> &alloc, NotNull<const AffineLoopNest> inLoop,
                const int64_t *inOff, DensePtrMatrix<int64_t> inPhi,
                NotNull<const AffineLoopNest> outLoop, const int64_t *outOff,
                DensePtrMatrix<int64_t> outPhi) -> bool {
    if (!isForward()) {
      std::swap(inLoop, outLoop);
      std::swap(inOff, outOff);
      std::swap(inPhi, outPhi);
    }
    invariant(inPhi.numRow(), outPhi.numRow());
    if (!depPoly->checkSat(alloc, inLoop, inOff, inPhi, outLoop, outOff,
                           outPhi))
      return false;
    satLvl.front() = uint8_t(inPhi.numRow() - 1);
    return true;
  }
  // constexpr auto addEdge(size_t i) -> Dependence & {
  //   in->addEdgeOut(i);
  //   out->addEdgeIn(i);
  //   return *this;
  // }
  constexpr void copySimplices(BumpAlloc<> &alloc) {
    dependenceSatisfaction = dependenceSatisfaction->copy(alloc);
    dependenceBounding = dependenceBounding->copy(alloc);
  }
  /// getOutIndMat() -> getOutNumLoops() x arrayDim()
  [[nodiscard]] constexpr auto getOutIndMat() const -> PtrMatrix<int64_t> {
    return out->indexMatrix();
  }
  [[nodiscard]] constexpr auto getInOutPair() const -> std::array<Addr *, 2> {
    return {in, out};
  }
  // returns the memory access pair, placing the store first in the pair
  [[nodiscard]] constexpr auto getStoreAndOther() const
    -> std::array<Addr *, 2> {
    if (in->isStore()) return {in, out};
    return {out, in};
  }
  [[nodiscard]] constexpr auto getInNumLoops() const -> unsigned {
    return in->getNumLoops();
  }
  [[nodiscard]] constexpr auto getOutNumLoops() const -> unsigned {
    return out->getNumLoops();
  }
  [[nodiscard]] constexpr auto isInactive(size_t depth) const -> bool {
    return (depth >= std::min(out->getNumLoops(), in->getNumLoops()));
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> unsigned {
    return depPoly->getNumLambda() << 1;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> unsigned {
    return depPoly->getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumPhiCoefficients() const -> unsigned {
    return depPoly->getNumPhiCoef();
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoefficients() -> unsigned {
    return DepPoly::getNumOmegaCoef();
  }
  [[nodiscard]] constexpr auto getNumDepSatConstraintVar() const -> unsigned {
    return dependenceSatisfaction->getNumVars();
  }
  [[nodiscard]] constexpr auto getNumDepBndConstraintVar() const -> unsigned {
    return dependenceBounding->getNumVars();
  }
  // returns `w`
  [[nodiscard]] constexpr auto getNumDynamicBoundingVar() const -> unsigned {
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
  [[nodiscard]] constexpr auto getDepPoly() const -> NotNull<const DepPoly> {
    return depPoly;
  }
  [[nodiscard]] constexpr auto getNumConstraints() const -> unsigned {
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
    if (isForward()) std::swap(phiCoefsIn, phiCoefsOut);
    return {getSatConstants(), getSatLambda(),     phiCoefsIn,
            phiCoefsOut,       getSatOmegaCoefs(), getSatW()};
  }
  [[nodiscard]] auto splitBounding() const
    -> std::tuple<StridedVector<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>, PtrMatrix<int64_t>, PtrMatrix<int64_t>,
                  PtrMatrix<int64_t>> {
    PtrMatrix<int64_t> phiCoefsIn = getBndPhi1Coefs(),
                       phiCoefsOut = getBndPhi0Coefs();
    if (isForward()) std::swap(phiCoefsIn, phiCoefsOut);
    return {getBndConstants(), getBndLambda(),     phiCoefsIn,
            phiCoefsOut,       getBndOmegaCoefs(), getBndCoefs()};
  }
  [[nodiscard]] auto isSatisfied(BumpAlloc<> &alloc,
                                 NotNull<const AffineSchedule> schIn,
                                 NotNull<const AffineSchedule> schOut) const
    -> bool {
    size_t numLoopsIn = in->getNumLoops();
    size_t numLoopsOut = out->getNumLoops();
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
    size_t numLoopsIn = in->getNumLoops();
    size_t numLoopsOut = out->getNumLoops();
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
                             NotNull<const Addr> x, NotNull<const Addr> y,
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
                             NotNull<const Addr> x, NotNull<const Addr> y,
                             size_t numLambda, Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    unsigned numLoopsX = x->getNumLoops(), nTD = unsigned(nonTimeDim);
#ifndef NDEBUG
    const unsigned numLoopsCommon = std::min(numLoopsX, y->getNumLoops());
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

  static auto check(BumpAlloc<> &alloc, NotNull<Addr> x, NotNull<Addr> y)
    -> Dependence * {
    // TODO: implement gcd test
    // if (x.gcdKnownIndependent(y)) return {};
    DepPoly *dxy{DepPoly::dependence(alloc, x, y)};
    if (!dxy) return {};
    invariant(x->getNumLoops(), dxy->getDim0());
    invariant(y->getNumLoops(), dxy->getDim1());
    invariant(x->getNumLoops() + y->getNumLoops(), dxy->getNumPhiCoef());
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    std::array<NotNull<Simplex>, 2> pair(dxy->farkasPair(alloc));
    if (dxy->getTimeDim()) return timeCheck(alloc, dxy, x, y, pair);
    return timelessCheck(alloc, dxy, x, y, pair);
  }
  // reload store `x`
  static auto reload(BumpAlloc<> &alloc, NotNull<Addr> store)
    -> std::pair<NotNull<Addr>, Dependence *> {
    NotNull<DepPoly> dxy{DepPoly::self(alloc, store)};
    std::array<NotNull<Simplex>, 2> pair(dxy->farkasPair(alloc));
    NotNull<Addr> load = store->reload(alloc);
    // no need for a timeCheck, because if there is a time-dim, we have a
    // store -> store dependence.
    // when we add new load -> store edges for each store->store,
    // that will cover the time-dependence
    return {load, timelessCheck(alloc, dxy, store, load, pair, true)};
  }
  constexpr auto replaceInput(NotNull<Addr> newIn) -> Dependence {
    Dependence edge = *this;
    edge.in = newIn;
    return edge;
  }
  constexpr auto replaceOutput(NotNull<Addr> newOut) -> Dependence {
    Dependence edge = *this;
    edge.out = newOut;
    return edge;
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const Dependence &d)
    -> llvm::raw_ostream & {
    os << "Dependence Poly ";
    if (d.isForward()) os << "x -> y:";
    else os << "y -> x:";
    os << "\n\tInput:\n" << *d.in;
    os << "\n\tOutput:\n" << *d.out;
    os << "\nA = " << d.depPoly->getA() << "\nE = " << d.depPoly->getE()
       << "\nSchedule Constraints:"
       << d.dependenceSatisfaction->getConstraints()
       << "\nBounding Constraints:" << d.dependenceBounding->getConstraints();
    return os << "\nSatisfied (isCondIndep() == " << d.isCondIndep()
              << ") = " << int(d.satLevel()) << "\n";
  }
};

constexpr void Addr::forEachInput(const auto &f) {
  Dependence *d = edgeIn;
  while (d) {
    f(d->input());
    d = d->getNext();
  }
}
