#pragma once
#include "IR/Address.hpp"
#include "Math/Simplex.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include "Polyhedra/Schedule.hpp"
#include "Support/Iterators.hpp"
#include <Math/Constructors.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Invariant.hpp>
#include <cstdint>
#include <ranges>

namespace poly {
namespace poly {

/// Dependence
/// Represents a dependence relationship between two memory accesses.
/// It contains simplices representing constraints that affine schedules
/// are allowed to take.
class Dependence {
public:
  struct ID {
    int32_t id;
  };

private:
  //
  //
  NotNull<DepPoly> depPoly;
  NotNull<math::Simplex> dependenceSatisfaction;
  NotNull<math::Simplex> dependenceBounding;
  NotNull<IR::Addr> in;
  NotNull<IR::Addr> out;
  // Dependence *nextInput{nullptr}; // all share same `in`
  // Dependence *nextOutput{nullptr};
  // // all share same `out`
  // // the upper bit of satLvl indicates whether the satisfaction is
  // // because of conditional independence (value = 0), or whether it
  // // was because of offsets when solving the linear program (value =
  // // 1).
  // std::array<uint8_t, 7> satLvl{255, 255, 255, 255, 255, 255, 255};
  std::array<uint8_t, 2> satLvl;
  bool forward;

  constexpr auto getSimplexPair() -> std::array<NotNull<math::Simplex>, 2> {
    return {dependenceSatisfaction, dependenceBounding};
  }

public:
  friend class Dependencies;
  // constexpr auto getNextInput() -> Dependence * { return nextInput; }
  // [[nodiscard]] constexpr auto getNextInput() const -> const Dependence * {
  //   return nextInput;
  // }
  // [[nodiscard]] constexpr auto getNextOutput() -> Dependence * {
  //   return nextOutput;
  // }
  // [[nodiscard]] constexpr auto getNextOutput() const -> const Dependence * {
  //   return nextOutput;
  // }
  [[nodiscard]] constexpr auto input() -> NotNull<IR::Addr> { return in; }
  [[nodiscard]] constexpr auto output() -> NotNull<IR::Addr> { return out; }
  [[nodiscard]] constexpr auto input() const -> NotNull<const IR::Addr> {
    return in;
  }
  [[nodiscard]] constexpr auto output() const -> NotNull<const IR::Addr> {
    return out;
  }
  // constexpr auto setNextInput(Dependence *n) -> Dependence * {
  //   return nextInput = n;
  // }
  // constexpr auto setNextOutput(Dependence *n) -> Dependence * {
  //   return nextOutput = n;
  // }
  constexpr Dependence(NotNull<DepPoly> poly,
                       std::array<NotNull<math::Simplex>, 2> depSatBound,
                       NotNull<IR::Addr> i, NotNull<IR::Addr> o, bool fwd)
    : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
      dependenceBounding(depSatBound[1]), in(i), out(o), forward(fwd) {}
  constexpr Dependence(NotNull<DepPoly> poly,
                       std::array<NotNull<math::Simplex>, 2> depSatBound,
                       NotNull<IR::Addr> i, NotNull<IR::Addr> o,
                       std::array<uint8_t, 2> sL, bool fwd)
    : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
      dependenceBounding(depSatBound[1]), in(i), out(o), satLvl(sL),
      forward(fwd) {}

  /// stashSatLevel() -> Dependence &
  /// This is used to track sat levels in the LP recursion.
  /// Recursion works from outer -> inner most loop.
  /// On each level of the recursion, we
  /// 1. evaluate the level.
  /// 2. if we succeed w/out deps, update sat levels and go a level deeper.
  /// 3.
  //   constexpr auto stashSatLevel(unsigned depth) -> Dependence * {
  //     invariant(depth <= 127);
  //     assert(satLvl.back() == 255 || "satLevel overflow");
  //     std::copy_backward(satLvl.begin(), satLvl.end() - 1, satLvl.end());
  //     // we clear `d` level as well; we're pretending we're a level deeper
  //     if ((satLevel() + 1) > depth) satLvl.front() = 255;
  //     return this;
  //   }
  //   constexpr void popSatLevel() {
  //     std::copy(satLvl.begin() + 1, satLvl.end(), satLvl.begin());
  // #ifndef NDEBUG
  //     satLvl.back() = 255;
  // #endif
  //   }
  // Set sat level and flag as indicating that this loop cannot be parallelized
  constexpr void setSatLevelLP(uint8_t d) { satLvl[0] = uint8_t(128) | d; }
  // Set sat level, but allow parallelizing this loop
  constexpr void setSatLevelParallel(uint8_t d) { satLvl[0] = d; }
  static constexpr auto satLevelMask(uint8_t slvl) -> uint8_t {
    return slvl & uint8_t(127); // NOTE: deduces to `int`
  }
  [[nodiscard]] constexpr auto satLevel() const -> uint8_t {
    return satLevelMask(satLvl[0]);
  }
  [[nodiscard]] constexpr auto isSat(unsigned depth) const -> bool {
    invariant(depth <= 127);
    return satLevel() <= depth;
  }
  [[nodiscard]] constexpr auto isActive(unsigned depth) const -> bool {
    invariant(depth <= 127);
    return satLevel() > depth;
  }
  /// if true, then it's independent conditioned on the phis...
  [[nodiscard]] constexpr auto isCondIndep() const -> bool {
    return (satLvl[0] & uint8_t(128)) == uint8_t(0);
  }
  [[nodiscard]] static constexpr auto preventsReordering(uint8_t depth)
    -> bool {
    return depth & uint8_t(128);
  }
  // prevents reordering satisfied level if `true`
  [[nodiscard]] constexpr auto preventsReordering() const -> bool {
    return preventsReordering(satLvl[0]);
  }
  /// checks the stash is active at `depth`, and that the stash
  /// does prevent reordering.
  [[nodiscard]] constexpr auto stashedPreventsReordering(unsigned depth) const
    -> bool {
    invariant(depth <= 127);
    return preventsReordering(satLvl[1]) && satLvl[1] > depth;
  }
  [[nodiscard]] constexpr auto getArrayPointer() -> const llvm::SCEV * {
    return in->getArrayPointer();
  }
  /// indicates whether forward is non-empty
  [[nodiscard]] constexpr auto isForward() const -> bool { return forward; }
  [[nodiscard]] constexpr auto nodeIn() const -> const lp::ScheduledNode * {
    return in->getNode();
  }
  // [[nodiscard]] constexpr auto nodeOut() const -> unsigned {
  //   return out->getNode();
  // }
  [[nodiscard]] constexpr auto getDynSymDim() const -> unsigned {
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
  // satisfies dep if it is empty when conditioning on inPhi and outPhi
  void checkEmptySat(Arena<> *alloc, NotNull<const poly::Loop> inLoop,
                     const int64_t *inOff, DensePtrMatrix<int64_t> inPhi,
                     NotNull<const poly::Loop> outLoop, const int64_t *outOff,
                     DensePtrMatrix<int64_t> outPhi) {
    if (!isForward()) {
      std::swap(inLoop, outLoop);
      std::swap(inOff, outOff);
      std::swap(inPhi, outPhi);
    }
    invariant(inPhi.numRow(), outPhi.numRow());
    if (depPoly->checkSat(*alloc, inLoop, inOff, inPhi, outLoop, outOff,
                          outPhi))
      satLvl[0] = uint8_t(inPhi.numRow() - 1);
  }
  constexpr void copySimplices(Arena<> *alloc) {
    dependenceSatisfaction = dependenceSatisfaction->copy(alloc);
    dependenceBounding = dependenceBounding->copy(alloc);
  }
  /// getOutIndMat() -> getOutNumLoops() x arrayDim()
  [[nodiscard]] constexpr auto getOutIndMat() const -> PtrMatrix<int64_t> {
    return out->indexMatrix();
  }
  [[nodiscard]] constexpr auto getInOutPair() const
    -> std::array<IR::Addr *, 2> {
    return {in, out};
  }
  // returns the memory access pair, placing the store first in the pair
  [[nodiscard]] constexpr auto getStoreAndOther() const
    -> std::array<IR::Addr *, 2> {
    if (in->isStore()) return {in, out};
    return {out, in};
  }
  [[nodiscard]] constexpr auto getInCurrentDepth() const -> unsigned {
    return in->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getOutCurrentDepth() const -> unsigned {
    return out->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getInNaturalDepth() const -> unsigned {
    return in->getNaturalDepth();
  }
  [[nodiscard]] constexpr auto getOutNatrualDepth() const -> unsigned {
    return out->getNaturalDepth();
  }
  [[nodiscard]] constexpr auto isInactive(size_t depth) const -> bool {
    return (depth >= std::min(out->getCurrentDepth(), in->getCurrentDepth()));
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
    assert(getInCurrentDepth() + getOutCurrentDepth() ==
           getNumPhiCoefficients());
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
  [[nodiscard]] auto getSatConstants() const -> math::StridedVector<int64_t> {
    return dependenceSatisfaction->getConstants();
  }
  [[nodiscard]] auto getBndConstants() const -> math::StridedVector<int64_t> {
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
  [[nodiscard]] auto getSatW() const -> math::StridedVector<int64_t> {
    return getSatConstraints()(_, 1 + depPoly->getNumLambda() +
                                    getNumPhiCoefficients() +
                                    getNumOmegaCoefficients());
  }
  [[nodiscard]] auto getBndCoefs() const -> PtrMatrix<int64_t> {
    size_t lb = 1 + depPoly->getNumLambda() + getNumPhiCoefficients() +
                getNumOmegaCoefficients();
    return getBndConstraints()(_, _(lb, end));
  }
  [[nodiscard]] auto satPhiCoefs() const -> std::array<PtrMatrix<int64_t>, 2> {
    PtrMatrix<int64_t> phiCoefsIn = getSatPhi1Coefs(),
                       phiCoefsOut = getSatPhi0Coefs();
    if (isForward()) std::swap(phiCoefsIn, phiCoefsOut);
    return {phiCoefsIn, phiCoefsOut};
  }
  [[nodiscard]] auto bndPhiCoefs() const -> std::array<PtrMatrix<int64_t>, 2> {
    PtrMatrix<int64_t> phiCoefsIn = getBndPhi1Coefs(),
                       phiCoefsOut = getBndPhi0Coefs();
    if (isForward()) std::swap(phiCoefsIn, phiCoefsOut);
    return {phiCoefsIn, phiCoefsOut};
  }
  [[nodiscard]] auto isSatisfied(Arena<> alloc,
                                 NotNull<const AffineSchedule> schIn,
                                 NotNull<const AffineSchedule> schOut) const
    -> bool {
    unsigned numLoopsIn = in->getCurrentDepth(),
             numLoopsOut = out->getCurrentDepth(),
             numLoopsCommon = std::min(numLoopsIn, numLoopsOut),
             numLoopsTotal = numLoopsIn + numLoopsOut,
             numVar = numLoopsIn + numLoopsOut + 2;
    invariant(dependenceSatisfaction->getNumVars(), numVar);
    auto schv = vector(&alloc, numVar, int64_t(0));
    const SquarePtrMatrix<int64_t> inPhi = schIn->getPhi();
    const SquarePtrMatrix<int64_t> outPhi = schOut->getPhi();
    auto inFusOmega = schIn->getFusionOmega();
    auto outFusOmega = schOut->getFusionOmega();
    auto inOffOmega = schIn->getOffsetOmega();
    auto outOffOmega = schOut->getOffsetOmega();
    const unsigned numLambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (ptrdiff_t i = 0; i <= numLoopsCommon; ++i) {
      if (ptrdiff_t o2idiff = outFusOmega[i] - inFusOmega[i])
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
      invariant(i != numLoopsCommon);
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
  [[nodiscard]] auto isSatisfied(Arena<> alloc, PtrVector<unsigned> inFusOmega,
                                 PtrVector<unsigned> outFusOmega) const
    -> bool {
    unsigned numLoopsIn = in->getCurrentDepth(),
             numLoopsOut = out->getCurrentDepth(),
             numLoopsCommon = std::min(numLoopsIn, numLoopsOut),
             numVar = numLoopsIn + numLoopsOut + 2;
    invariant(dependenceSatisfaction->getNumVars(), numVar);
    auto schv = vector(&alloc, numVar, int64_t(0));
    // Vector<int64_t> schv(dependenceSatisfaction->getNumVars(),int64_t(0));
    const unsigned numLambda = getNumLambda();
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
      invariant(i != numLoopsCommon);
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
  [[nodiscard]] auto isSatisfied(Arena<> alloc,
                                 NotNull<const AffineSchedule> sx,
                                 NotNull<const AffineSchedule> sy,
                                 size_t d) const -> bool {
    unsigned numLambda = depPoly->getNumLambda(), nLoopX = depPoly->getDim0(),
             nLoopY = depPoly->getDim1(), numLoopsTotal = nLoopX + nLoopY;
    MutPtrVector<int64_t> sch{math::vector<int64_t>(&alloc, numLoopsTotal + 2)};
    sch[0] = sx->getOffsetOmega()[d];
    sch[1] = sy->getOffsetOmega()[d];
    sch[_(2, nLoopX + 2)] << sx->getSchedule(d)[_(end - nLoopX, end)];
    sch[_(nLoopX + 2, numLoopsTotal + 2)]
      << sy->getSchedule(d)[_(end - nLoopY, end)];
    return dependenceSatisfaction->satisfiable(alloc, sch, numLambda);
  }
  [[nodiscard]] auto isSatisfied(Arena<> alloc, size_t d) const -> bool {
    unsigned numLambda = depPoly->getNumLambda(),
             numLoopsX = depPoly->getDim0(),
             numLoopsTotal = numLoopsX + depPoly->getDim1();
    MutPtrVector<int64_t> sch{math::vector<int64_t>(&alloc, numLoopsTotal + 2)};
    sch << 0;
    invariant(sch.size(), numLoopsTotal + 2);
    sch[2 + d] = 1;
    sch[2 + d + numLoopsX] = 1;
    return dependenceSatisfaction->satisfiable(alloc, sch, numLambda);
  }

  struct Active {
    unsigned depth;
    constexpr Active(const Active &) noexcept = default;
    constexpr Active(Active &&) noexcept = default;
    constexpr Active() noexcept = default;
    constexpr auto operator=(const Active &) noexcept -> Active & = default;
    constexpr Active(unsigned depth) : depth(depth) {}
    constexpr auto operator()(const Dependence *d) const -> bool {
      return d->isActive(depth);
    }
  };

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
static_assert(sizeof(Dependence) <= 64);

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
//         A(i,j) = f(A(i+1,j), A(i,j-1), A(j,j), A(j,i), A(i,j-k))
// label:     0           1        2         3       4       5
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
class Dependencies {
  char *data{nullptr};
  int32_t numData{0};
  // int32_t tombstone{-1};

public:
  using ID = Dependence::ID;
  constexpr Dependencies() noexcept = default;
  constexpr Dependencies(Arena<> *alloc)
    : data(alloc->allocate<char>(memNeeded(64))) {}
  constexpr Dependencies(const Dependencies &) noexcept = default; // or delete?
  constexpr Dependencies(Dependencies &&) noexcept = default;      // or delete?
  constexpr auto operator=(Dependencies &&other) noexcept
    -> Dependencies & = default;
  constexpr auto operator=(const Dependencies &other) noexcept
    -> Dependencies & = default;

  [[nodiscard]] constexpr auto size() const noexcept -> int32_t {
    return numData;
  }

private:
  constexpr void set(ID i, Dependence d) {
    auto out = d.output();
    auto in = d.input();
    output(i) = out;
    input(i) = in;
    nextOut(i) = out->getEdgeOut();
    if (out->getEdgeOut() >= 0) prevOut(ID{out->getEdgeOut()}) = i.id;
    prevOut(i) = -1;
    nextIn(i) = in->getEdgeIn();
    if (in->getEdgeIn() >= 0) prevIn(ID{in->getEdgeIn()}) = i.id;
    prevIn(i) = -1;
    depSatBnd(i) = d.getSimplexPair();
    depPoly(i) = d.getDepPoly();
    satLevelPair(i) = d.satLvl;
    isForward(i) = d.isForward();
    in->setEdgeOut(i.id);
    out->setEdgeIn(i.id);
  }
  auto addEdge(Arena<> *alloc, Dependence d) -> void * {
    void *ret = nullptr;
    if (numData == getCapacity()) {
      auto newCapacity = getCapacity() * 2;
      auto *newData = alloc->allocate<char>(memNeeded(newCapacity));
      std::memcpy(newData, data, memNeeded(numData));
      ret = std::exchange(data, newData);
    }
    set(ID{numData++}, d);
    return ret;
  }
  static constexpr auto memNeeded(size_t N) -> size_t {
    constexpr size_t memPer = sizeof(int32_t) * 2 + sizeof(DepPoly *) +
                              sizeof(NotNull<math::Simplex>) * 2 +
                              sizeof(bool) + sizeof(uint8_t);
    return N * memPer;
  }

  void timelessCheck(Arena<> *alloc, NotNull<DepPoly> dxy, NotNull<IR::Addr> x,
                     NotNull<IR::Addr> y,
                     std::array<NotNull<math::Simplex>, 2> pair, bool isFwd) {
    const size_t numLambda = dxy->getNumLambda();
    invariant(dxy->getTimeDim(), unsigned(0));
    if (!isFwd) {
      std::swap(pair[0], pair[1]);
      std::swap(x, y);
    }
    pair[0]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
    addEdge(alloc, Dependence{dxy, pair, x, y, isFwd});
  }
  void timelessCheck(Arena<> *alloc, NotNull<DepPoly> dxy, NotNull<IR::Addr> x,
                     NotNull<IR::Addr> y,
                     std::array<NotNull<math::Simplex>, 2> pair) {
    return timelessCheck(alloc, dxy, x, y, pair,
                         checkDirection(*alloc, pair, x, y, dxy->getNumLambda(),
                                        dxy->getNumVar() + 1));
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  void timeCheck(Arena<> *alloc, NotNull<DepPoly> dxy, NotNull<IR::Addr> x,
                 NotNull<IR::Addr> y,
                 std::array<NotNull<math::Simplex>, 2> pair) {
    bool isFwd = checkDirection(*alloc, pair, x, y, dxy->getNumLambda(),
                                dxy->getA().numCol() - dxy->getTimeDim());
    timeCheck(alloc, dxy, x, y, pair, isFwd);
  }
  void timeCheck(Arena<> *alloc, NotNull<DepPoly> dxy, NotNull<IR::Addr> x,
                 NotNull<IR::Addr> y,
                 std::array<NotNull<math::Simplex>, 2> pair, bool isFwd) {
    const unsigned numInequalityConstraintsOld =
                     dxy->getNumInequalityConstraints(),
                   numEqualityConstraintsOld = dxy->getNumEqualityConstraints(),
                   ineqEnd = 1 + numInequalityConstraintsOld,
                   posEqEnd = ineqEnd + numEqualityConstraintsOld,
                   numLambda = posEqEnd + numEqualityConstraintsOld,
                   numScheduleCoefs = dxy->getNumScheduleCoef();
    invariant(numLambda, dxy->getNumLambda());
    // copy backup
    std::array<NotNull<math::Simplex>, 2> farkasBackups{pair[0]->copy(alloc),
                                                        pair[1]->copy(alloc)};
    NotNull<IR::Addr> in = x, out = y;
    if (isFwd) {
      std::swap(farkasBackups[0], farkasBackups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0]->truncateVars(1 + numLambda + numScheduleCoefs);
    auto dep0 = Dependence{dxy->copy(alloc), pair, in, out, isFwd};
    invariant(out->getCurrentDepth() + in->getCurrentDepth(),
              dep0.getNumPhiCoefficients());
    addEdge(alloc, dep0);
    // pair is invalid
    const ptrdiff_t timeDim = dxy->getTimeDim(),
                    numVar = 1 + dxy->getNumVar() - timeDim;
    invariant(timeDim > 0);
    // 1 + because we're indexing into A and E, ignoring the constants
    // remove the time dims from the deps
    // dep0.depPoly->truncateVars(numVar);

    // dep0.depPoly->setTimeDim(0);
    invariant(out->getCurrentDepth() + in->getCurrentDepth(),
              dep0.getNumPhiCoefficients());
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be absolutely
    // insane
    math::Vector<bool, 16> timeDirection(timeDim);
    ptrdiff_t t = 0;
    auto fE{farkasBackups[0]->getConstraints()(_, _(1, end))};
    auto sE{farkasBackups[1]->getConstraints()(_, _(1, end))};
    do {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = dxy->getNullStep(t);
      ptrdiff_t v = numVar + t, i = 0;
      while (true) {
        for (ptrdiff_t c = 0; c < numInequalityConstraintsOld; ++c) {
          int64_t Acv = dxy->getA(c, v);
          if (!Acv) continue;
          Acv *= step;
          fE(0, c + 1) -= Acv; // *1
          sE(0, c + 1) -= Acv; // *1
        }
        for (ptrdiff_t c = 0; c < numEqualityConstraintsOld; ++c) {
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
          checkDirection(*alloc, farkasBackups, *out, *in, numLambda,
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
      ptrdiff_t v = numVar + t;
      for (ptrdiff_t c = 0; c < numInequalityConstraintsOld; ++c) {
        int64_t Acv = dxy->getA(c, v);
        if (!Acv) continue;
        Acv *= step;
        dxy->getA(c, 0) -= Acv;
        fE(0, c + 1) -= Acv; // *1
        sE(0, c + 1) -= Acv; // *-1
      }
      for (ptrdiff_t c = 0; c < numEqualityConstraintsOld; ++c) {
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
    auto dep1 = Dependence{dxy, farkasBackups, out, in, !isFwd};
    invariant(out->getCurrentDepth() + in->getCurrentDepth(),
              dep1.getNumPhiCoefficients());
    addEdge(alloc, dep1);
  }
  static auto checkDirection(Arena<> alloc,
                             const std::array<NotNull<math::Simplex>, 2> &p,
                             NotNull<const IR::Addr> x,
                             NotNull<const IR::Addr> y,
                             NotNull<const AffineSchedule> xSchedule,
                             NotNull<const AffineSchedule> ySchedule,
                             unsigned numLambda, Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    unsigned numLoopsX = x->getCurrentDepth(), numLoopsY = y->getCurrentDepth(),
             numLoopsTotal = numLoopsX + numLoopsY;
#ifndef NDEBUG
    unsigned numLoopsCommon = std::min(numLoopsX, numLoopsY);
#endif
    SquarePtrMatrix<int64_t> xPhi = xSchedule->getPhi();
    SquarePtrMatrix<int64_t> yPhi = ySchedule->getPhi();
    PtrVector<int64_t> xOffOmega = xSchedule->getOffsetOmega();
    PtrVector<int64_t> yOffOmega = ySchedule->getOffsetOmega();
    PtrVector<int64_t> xFusOmega = xSchedule->getFusionOmega();
    PtrVector<int64_t> yFusOmega = ySchedule->getFusionOmega();
    MutPtrVector<int64_t> sch{math::vector<int64_t>(&alloc, numLoopsTotal + 2)};
    // i iterates from outer-most to inner most common loop
    for (ptrdiff_t i = 0; /*i <= numLoopsCommon*/; ++i) {
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
                                    unsigned(nonTimeDim))) {
        assert(!fyx->unSatisfiableZeroRem(alloc, sch, numLambda,
                                          unsigned(nonTimeDim)));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, sch, numLambda,
                                    unsigned(nonTimeDim)))
        return true;
    }
    // assert(false);
    // return false;
  }
  // returns `true` if forward, x->y
  static auto checkDirection(Arena<> alloc,
                             const std::array<NotNull<math::Simplex>, 2> &p,
                             NotNull<const IR::Addr> x,
                             NotNull<const IR::Addr> y, unsigned numLambda,
                             Col nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    unsigned numLoopsX = x->getCurrentDepth(), nTD = unsigned(nonTimeDim);
#ifndef NDEBUG
    const unsigned numLoopsCommon = std::min(numLoopsX, y->getCurrentDepth());
#endif
    PtrVector<int64_t> xFusOmega = x->getFusionOmega();
    PtrVector<int64_t> yFusOmega = y->getFusionOmega();
    // i iterates from outer-most to inner most common loop
    for (ptrdiff_t i = 0; /*i <= numLoopsCommon*/; ++i) {
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
      std::array<ptrdiff_t, 2> inds{2 + i, 2 + i + numLoopsX};
      if (fxy->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) {
        assert(!fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) return true;
    }
    invariant(false);
    return false;
  }
  constexpr auto get(ID i, IR::Addr *in, IR::Addr *out) const -> Dependence {
    return Dependence{depPoly(i),      depSatBnd(i), in, out,
                      satLevelPair(i), isForward(i)

    };
  }

  [[nodiscard]] constexpr auto getCapacity() const noexcept -> int32_t {
    return int32_t(std::bit_ceil(uint32_t(numData)));
  }

  // field order:
  // AddrOut
  // AddrIn
  // nextOut
  // prevOut
  // nextIn
  // prevIn
  // dependenceSatisfaction
  // dependenceBounding
  // depPoly
  // satLevel
  // isForward
  [[nodiscard]] static constexpr auto inAddrOffset() -> size_t {
    return sizeof(IR::Addr *);
  }
  [[nodiscard]] static constexpr auto nextEdgeOutOffset() -> size_t {
    return inAddrOffset() + sizeof(IR::Addr *);
  }
  [[nodiscard]] static constexpr auto prevEdgeOutOffset() -> size_t {
    return nextEdgeOutOffset() + sizeof(int32_t);
  }
  [[nodiscard]] static constexpr auto nextEdgeInOffset() -> size_t {
    return prevEdgeOutOffset() + sizeof(int32_t);
  }
  [[nodiscard]] static constexpr auto prevEdgeInOffset() -> size_t {
    return nextEdgeInOffset() + sizeof(int32_t);
  }
  [[nodiscard]] static constexpr auto depSatBndOffset() -> size_t {
    return prevEdgeInOffset() + sizeof(int32_t);
  }
  [[nodiscard]] static constexpr auto depPolyOffset() -> size_t {
    return depSatBndOffset() + sizeof(std::array<NotNull<math::Simplex>, 2>);
  }
  [[nodiscard]] static constexpr auto satLevelsOffset() -> size_t {
    return depPolyOffset() + sizeof(DepPoly *);
  }
  [[nodiscard]] static constexpr auto isForwardOffset() -> size_t {
    return satLevelsOffset() + sizeof(std::array<uint8_t, 2>);
  }

  constexpr auto outAddrPtr() -> IR::Addr ** {
    void *p = data;
    return static_cast<IR::Addr **>(p);
  }
  [[nodiscard]] constexpr auto outAddrPtr() const -> IR::Addr *const * {
    const void *p = data;
    return static_cast<IR::Addr *const *>(p);
  }
  constexpr auto inAddrPtr() -> IR::Addr ** {
    void *p = data + inAddrOffset() * getCapacity();
    return static_cast<IR::Addr **>(p);
  }
  [[nodiscard]] constexpr auto inAddrPtr() const -> IR::Addr *const * {
    const void *p = data + inAddrOffset() * getCapacity();
    return static_cast<IR::Addr *const *>(p);
  }
  constexpr auto outEdgePtr() -> int32_t * {
    void *p = data + nextEdgeOutOffset() * getCapacity();
    return static_cast<int32_t *>(p);
  }
  [[nodiscard]] constexpr auto outEdgePtr() const -> const int32_t * {
    const void *p = data + nextEdgeOutOffset() * getCapacity();
    return static_cast<const int32_t *>(p);
  }
  constexpr auto prevOutEdgePtr() -> int32_t * {
    void *p = data + prevEdgeOutOffset() * getCapacity();
    return static_cast<int32_t *>(p);
  }
  [[nodiscard]] constexpr auto prevOutEdgePtr() const -> const int32_t * {
    const void *p = data + prevEdgeOutOffset() * getCapacity();
    return static_cast<const int32_t *>(p);
  }
  constexpr auto inEdgePtr() -> int32_t * {
    void *p = data + nextEdgeInOffset() * getCapacity();
    return static_cast<int32_t *>(p);
  }
  [[nodiscard]] constexpr auto inEdgePtr() const -> const int32_t * {
    const void *p = data + nextEdgeInOffset() * getCapacity();
    return static_cast<const int32_t *>(p);
  }
  constexpr auto prevInEdgePtr() -> int32_t * {
    void *p = data + prevEdgeInOffset() * getCapacity();
    return static_cast<int32_t *>(p);
  }
  [[nodiscard]] constexpr auto prevEdgePtr() const -> const int32_t * {
    const void *p = data + prevEdgeInOffset() * getCapacity();
    return static_cast<const int32_t *>(p);
  }
  constexpr auto depSatBndPtr() -> std::array<NotNull<math::Simplex>, 2> * {
    void *p = data + depSatBndOffset() * getCapacity();
    return static_cast<std::array<NotNull<math::Simplex>, 2> *>(p);
  }
  [[nodiscard]] constexpr auto depSatBndPtr() const
    -> const std::array<NotNull<math::Simplex>, 2> * {
    const void *p = data + depSatBndOffset() * getCapacity();
    return static_cast<const std::array<NotNull<math::Simplex>, 2> *>(p);
  }
  constexpr auto depPolyPtr() -> DepPoly ** {
    void *p = data + depPolyOffset() * getCapacity();
    return static_cast<DepPoly **>(p);
  }
  [[nodiscard]] constexpr auto depPolyPtr() const -> DepPoly *const * {
    const void *p = data + depPolyOffset() * getCapacity();
    return static_cast<DepPoly *const *>(p);
  }
  constexpr auto satLevelsPtr() -> std::array<uint8_t, 2> * {
    void *p = data + satLevelsOffset() * getCapacity();
    return static_cast<std::array<uint8_t, 2> *>(p);
  }
  [[nodiscard]] constexpr auto satLevelsPtr() const
    -> const std::array<uint8_t, 2> * {
    const void *p = data + satLevelsOffset() * getCapacity();
    return static_cast<const std::array<uint8_t, 2> *>(p);
  }
  constexpr auto isForwardPtr() -> bool * {
    void *p = data + isForwardOffset() * getCapacity();
    return static_cast<bool *>(p);
  }
  [[nodiscard]] constexpr auto isForwardPtr() const -> const bool * {
    const void *p = data + isForwardOffset() * getCapacity();
    return static_cast<const bool *>(p);
  }

public:
  constexpr void removeEdge(ID id) {
    removeOutEdge(id.id);
    removeInEdge(id.id);
  }
  constexpr void removeOutEdge(int32_t id) {
    int32_t prev = prevOut(poly::Dependence::ID{id});
    int32_t next = nextOut(poly::Dependence::ID{id});
    if (prev >= 0) nextOut(poly::Dependence::ID{prev}) = next;
    if (next >= 0) prevOut(poly::Dependence::ID{next}) = prev;
  }
  constexpr void removeInEdge(int32_t id) {
    int32_t prev = prevIn(poly::Dependence::ID{id});
    int32_t next = nextIn(poly::Dependence::ID{id});
    if (prev >= 0) nextIn(poly::Dependence::ID{prev}) = next;
    if (next >= 0) prevIn(poly::Dependence::ID{next}) = prev;
  }
  [[nodiscard]] constexpr auto get(ID i) const -> Dependence {
    return get(i, input(i), output(i));
  }
  constexpr auto outAddrs() -> MutPtrVector<IR::Addr *> {
    return {outAddrPtr(), numData};
  }
  constexpr auto inAddrs() -> MutPtrVector<IR::Addr *> {
    return {inAddrPtr(), numData};
  }
  constexpr auto outEdges() -> MutPtrVector<int32_t> {
    return {outEdgePtr(), numData};
  }
  constexpr auto inEdges() -> MutPtrVector<int32_t> {
    return {inEdgePtr(), numData};
  }
  [[nodiscard]] constexpr auto outEdges() const -> PtrVector<int32_t> {
    return {outEdgePtr(), unsigned(numData)};
  }
  [[nodiscard]] constexpr auto inEdges() const -> PtrVector<int32_t> {
    return {inEdgePtr(), unsigned(numData)};
  }
  constexpr auto satLevels() -> MutPtrVector<std::array<uint8_t, 2>> {
    return {satLevelsPtr(), numData};
  }

  [[nodiscard]] constexpr auto output(ID i) -> IR::Addr *& {
    return outAddrPtr()[i.id];
  }
  [[nodiscard]] constexpr auto output(ID i) const -> IR::Addr * {
    return outAddrPtr()[i.id];
  }
  [[nodiscard]] constexpr auto input(ID i) -> IR::Addr *& {
    return inAddrPtr()[i.id];
  }
  [[nodiscard]] constexpr auto input(ID i) const -> IR::Addr * {
    return inAddrPtr()[i.id];
  }
  constexpr auto nextOut(ID i) -> int32_t & { return outEdgePtr()[i.id]; }
  constexpr auto prevOut(ID i) -> int32_t & { return prevOutEdgePtr()[i.id]; }
  constexpr auto nextIn(ID i) -> int32_t & { return inEdgePtr()[i.id]; }
  constexpr auto prevIn(ID i) -> int32_t & { return prevInEdgePtr()[i.id]; }
  constexpr auto depSatBnd(ID i) -> std::array<NotNull<math::Simplex>, 2> & {
    return depSatBndPtr()[i.id];
  }
  constexpr auto depPoly(ID i) -> DepPoly *& { return depPolyPtr()[i.id]; }
  [[nodiscard]] constexpr auto depSatBnd(ID i) const
    -> std::array<NotNull<math::Simplex>, 2> {
    return depSatBndPtr()[i.id];
  }
  [[nodiscard]] constexpr auto depPoly(ID i) const -> DepPoly * {
    return depPolyPtr()[i.id];
  }
  constexpr auto satLevelPair(ID i) -> std::array<uint8_t, 2> & {
    return satLevelsPtr()[i.id];
  }
  [[nodiscard]] constexpr auto satLevelPair(ID i) const
    -> std::array<uint8_t, 2> {
    return satLevelsPtr()[i.id];
  }
  [[nodiscard]] constexpr auto satLevel(ID i) const -> uint8_t {
    return Dependence::satLevelMask(satLevelPair(i)[0]);
  }
  [[nodiscard]] constexpr auto isSat(ID i, unsigned depth) const -> uint8_t {
    return Dependence::satLevelMask(satLevelPair(i)[0]) <= depth;
  }

  [[nodiscard]] constexpr auto isForward(ID i) noexcept -> bool & {
    return isForwardPtr()[i.id];
  }
  [[nodiscard]] constexpr auto isForward(ID i) const noexcept -> bool {
    return isForwardPtr()[i.id];
  }

  class Ref {
    Dependencies *deps;
    ID i;

  public:
    Ref(Dependencies *deps, ID i) : deps(deps), i(i) {}
    operator Dependence() const { return deps->get(i); }
    auto operator=(Dependence d) -> Ref & {
      deps->set(i, d);
      return *this;
    }
  };

  void check(Arena<> *alloc, NotNull<IR::Addr> x, NotNull<IR::Addr> y) {
    // TODO: implement gcd test
    // if (x.gcdKnownIndependent(y)) return {};
    DepPoly *dxy{DepPoly::dependence(alloc, x, y)};
    if (!dxy) return;
    invariant(x->getCurrentDepth(), dxy->getDim0());
    invariant(y->getCurrentDepth(), dxy->getDim1());
    invariant(x->getCurrentDepth() + y->getCurrentDepth(),
              dxy->getNumPhiCoef());
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    std::array<NotNull<math::Simplex>, 2> pair(dxy->farkasPair(alloc));
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, x, y, pair);
    else timelessCheck(alloc, dxy, x, y, pair);
  }
  inline void copyDependencies(Arena<> *alloc, IR::Addr *src, IR::Addr *dst);
  // reload store `x`
  auto reload(Arena<> *alloc, NotNull<IR::Addr> store) -> NotNull<IR::Addr> {
    NotNull<DepPoly> dxy{DepPoly::self(alloc, store)};
    std::array<NotNull<math::Simplex>, 2> pair(dxy->farkasPair(alloc));
    NotNull<IR::Addr> load = store->reload(alloc);
    copyDependencies(alloc, store, load);
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, store, load, pair, true);
    else timelessCheck(alloc, dxy, store, load, pair, true);
    return load;
  }
  [[nodiscard]] constexpr auto inputEdgeIDs(int32_t id) const {
    return utils::VForwardRange{inEdges(), id};
  }
  [[nodiscard]] constexpr auto outputEdgeIDs(int32_t id) const {
    return utils::VForwardRange{outEdges(), id};
  }
  [[nodiscard]] constexpr auto getEdgeTransform() const {
    auto f = [=](int32_t id) { return get(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto inputEdges(int32_t id) const {
    return inputEdgeIDs(id) | getEdgeTransform();
  }
  [[nodiscard]] constexpr auto outputEdges(int32_t id) const {
    return outputEdgeIDs(id) | getEdgeTransform();
  }

  [[nodiscard]] constexpr auto activeFilter(unsigned depth) const {
    auto f = [=](int32_t id) -> bool {
      return !isSat(Dependence::ID{id}, depth);
    };
    return std::views::filter(f);
  }
  [[nodiscard]] constexpr auto inputAddrTransform() {
    auto f = [=](int32_t id) { return input(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto outputAddrTransform() {
    auto f = [=](int32_t id) { return output(Dependence::ID{id}); };
    return std::views::transform(f);
  }
};

static_assert(std::is_trivially_copyable_v<Dependencies>);
static_assert(std::is_trivially_destructible_v<Dependencies>);
} // namespace poly
namespace IR {
using poly::Dependencies;

inline auto Addr::inputEdges(Dependencies deps) const {
  return deps.inputEdges(getEdgeIn());
}
inline auto Addr::outputEdges(Dependencies deps) const {
  return deps.outputEdges(getEdgeOut());
}
inline auto Addr::inputEdgeIDs(Dependencies deps) const {
  return deps.inputEdgeIDs(getEdgeIn());
}
inline auto Addr::outputEdgeIDs(Dependencies deps) const {
  return deps.outputEdgeIDs(getEdgeOut());
}
inline auto Addr::inputEdgeIDs(Dependencies deps, unsigned depth) const {
  return inputEdgeIDs(deps) | deps.activeFilter(depth);
}
inline auto Addr::outputEdgeIDs(Dependencies deps, unsigned depth) const {
  return outputEdgeIDs(deps) | deps.activeFilter(depth);
}

inline auto IR::Addr::inputAddrs(Dependencies deps) const {
  return inputEdgeIDs(deps) | deps.inputAddrTransform();
}
inline auto IR::Addr::outputAddrs(Dependencies deps) const {
  return outputEdgeIDs(deps) | deps.outputAddrTransform();
}

inline auto Addr::inputEdges(Dependencies deps, unsigned depth) const {
  return inputEdgeIDs(deps) | deps.activeFilter(depth) |
         deps.getEdgeTransform();
}
inline auto Addr::outputEdges(Dependencies deps, unsigned depth) const {
  return outputEdgeIDs(deps) | deps.activeFilter(depth) |
         deps.getEdgeTransform();
}

inline auto IR::Addr::inputAddrs(Dependencies deps, unsigned depth) const {
  return inputEdgeIDs(deps, depth) | deps.inputAddrTransform();
}
inline auto IR::Addr::outputAddrs(Dependencies deps, unsigned depth) const {
  return outputEdgeIDs(deps, depth) | deps.outputAddrTransform();
}

inline void IR::Addr::drop(Dependencies deps) {
  // NOTE: this doesn't get removed from the `origAddr` list/the addrChain
  if (IR::Loop *L = getLoop(); L->getChild() == this) L->setChild(getNext());
  removeFromList();
  for (int32_t id : inputEdgeIDs(deps)) deps.removeEdge(Dependence::ID{id});
  for (int32_t id : outputEdgeIDs(deps)) deps.removeEdge(Dependence::ID{id});
}

inline auto Loop::getLegality(poly::Dependencies deps,
                              math::PtrVector<int32_t> loopDeps)
  -> LegalTransforms {
  if (legal != Unknown) return legal;
  // TODO: determine legality...
}
} // namespace IR

namespace poly {
inline void Dependencies::copyDependencies(Arena<> *alloc, IR::Addr *src,
                                           IR::Addr *dst) {
  for (int32_t id : src->inputEdgeIDs(*this)) {
    IR::Addr *input = this->input(Dependence::ID{id});
    if (input->isLoad()) continue;
    Dependence d = get(Dependence::ID{id}, input, dst);
    addEdge(alloc, d);
  }
  for (int32_t id : src->outputEdgeIDs(*this)) {
    IR::Addr *output = this->output(Dependence::ID{id});
    if (output->isLoad()) continue;
    Dependence d = get(Dependence::ID{id}, dst, output);
    addEdge(alloc, d);
  }
}

} // namespace poly

} // namespace poly
