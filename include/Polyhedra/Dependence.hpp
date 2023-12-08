#pragma once
#include "IR/Address.hpp"
#include "IR/Node.hpp"
#include "Math/Array.hpp"
#include "Math/Simplex.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include "Polyhedra/Schedule.hpp"
#include "Support/Iterators.hpp"
#include <Alloc/Arena.hpp>
#include <Containers/Tuple.hpp>
#include <Math/Constructors.hpp>
#include <Math/SOA.hpp>
#include <Utilities/Invariant.hpp>
#include <cstdint>
#include <ranges>

namespace poly {
namespace poly {

/// Dependence
/// Represents a dependence relationship between two memory accesses.
/// It contains simplices representing constraints that affine schedules
/// are allowed to take.
struct Dependence {

  // public:
  struct ID {
    int32_t id;
    [[nodiscard]] constexpr explicit operator bool() const { return id >= 0; }
  };
  enum MetaFlags : uint8_t {
    Forward = 1,
    Reassociable = 2,
    FreeOfDeeperDeps = 4,
    Peelable = 8
  };

  // private:
  //
  //
  Valid<DepPoly> depPoly;
  Valid<math::Simplex> dependenceSatisfaction;
  Valid<math::Simplex> dependenceBounding;
  Valid<IR::Addr> in;
  Valid<IR::Addr> out;
  // Dependence *nextInput{nullptr}; // all share same `in`
  // Dependence *nextOutput{nullptr};
  // // all share same `out`
  // // the upper bit of satLvl indicates whether the satisfaction is
  // // because of conditional independence (value = 0), or whether it
  // // was because of offsets when solving the linear program (value =
  // // 1).
  // std::array<uint8_t, 7> satLvl{255, 255, 255, 255, 255, 255, 255};
  ID revTimeEdge_{-1};
  std::array<uint8_t, 2> satLvl;
  uint8_t meta{0};

  // template <size_t I> [[nodiscard]] auto get() const -> const auto & {
  //   if constexpr (I == 0) return depPoly;
  //   else if constexpr (I==1) return dependenceSatisfaction;
  //   else if constexpr (I==1) return dependenceBounding;
  // }

  constexpr auto getSimplexPair() -> std::array<Valid<math::Simplex>, 2> {
    return {dependenceSatisfaction, dependenceBounding};
  }
  constexpr auto getMeta() const -> uint8_t { return meta; }

  // public:
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
  [[nodiscard]] constexpr auto input() const -> Valid<IR::Addr> { return in; }
  [[nodiscard]] constexpr auto output() const -> Valid<IR::Addr> { return out; }
  [[nodiscard]] constexpr auto revTimeEdge() const -> ID {
    return revTimeEdge_;
  }
  // constexpr auto setNextInput(Dependence *n) -> Dependence * {
  //   return nextInput = n;
  // }
  // constexpr auto setNextOutput(Dependence *n) -> Dependence * {
  //   return nextOutput = n;
  // }
  // constexpr Dependence(Valid<DepPoly> poly,
  //                      std::array<Valid<math::Simplex>, 2> depSatBound,
  //                      Valid<IR::Addr> i, Valid<IR::Addr> o, bool fwd)
  //   : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
  //     dependenceBounding(depSatBound[1]), in(i), out(o), forward(fwd) {}
  // constexpr Dependence(Valid<DepPoly> poly,
  //                      std::array<Valid<math::Simplex>, 2> depSatBound,
  //                      Valid<IR::Addr> i, Valid<IR::Addr> o,
  //                      std::array<uint8_t, 2> sL, bool fwd)
  //   : depPoly(poly), dependenceSatisfaction(depSatBound[0]),
  //     dependenceBounding(depSatBound[1]), in(i), out(o), satLvl(sL),
  //     forward(fwd) {}

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
  // note that sat levels start at `0`, `0` meaning the outer most loop
  // satisfies it. Thus, `satLevel() == 0` means the `depth == 1` loop satisfied
  // it.
  [[nodiscard]] constexpr auto satLevel() const -> uint8_t {
    return satLevelMask(satLvl[0]);
  }
  /// `isSat` returns `true` on the level that satisfies it
  [[nodiscard]] constexpr auto isSat(int depth) const -> bool {
    invariant(depth <= 127);
    return satLevel() <= depth;
  }
  /// `isActive` returns `false` on the level that satisfies it
  [[nodiscard]] constexpr auto isActive(int depth) const -> bool {
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
  [[nodiscard]] constexpr auto isForward() const -> bool { return meta & 1; }
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
  void checkEmptySat(Arena<> *alloc, Valid<const poly::Loop> inLoop,
                     const int64_t *inOff, DensePtrMatrix<int64_t> inPhi,
                     Valid<const poly::Loop> outLoop, const int64_t *outOff,
                     DensePtrMatrix<int64_t> outPhi) {
    if (!isForward()) {
      std::swap(inLoop, outLoop);
      std::swap(inOff, outOff);
      std::swap(inPhi, outPhi);
    }
    invariant(inPhi.numRow(), outPhi.numRow());
    if (depPoly->checkSat(*alloc, inLoop, inOff, inPhi, outLoop, outOff,
                          outPhi))
      satLvl[0] = uint8_t(ptrdiff_t(inPhi.numRow()) - 1);
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
    return (depth >=
            size_t(std::min(out->getCurrentDepth(), in->getCurrentDepth())));
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
           ptrdiff_t(dependenceSatisfaction->getConstraints().numCol()));
  }
  [[nodiscard]] constexpr auto getDepPoly() const -> Valid<DepPoly> {
    return depPoly;
  }
  // [[nodiscard]] constexpr auto getDepPoly() const -> Valid<const DepPoly> {
  //   return depPoly;
  // }
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
    return getSatConstraints()[_, _(1, 1 + depPoly->getNumLambda())];
  }
  [[nodiscard]] auto getBndLambda() const -> PtrMatrix<int64_t> {
    return getBndConstraints()[_, _(1, 1 + depPoly->getNumLambda())];
  }
  [[nodiscard]] auto getSatPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getSatConstraints()[_, _(l, l + getNumPhiCoefficients())];
  }
  [[nodiscard]] auto getSatPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getSatConstraints()[_, _(l, l + depPoly->getDim0())];
  }
  [[nodiscard]] auto getSatPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda() + depPoly->getDim0();
    return getSatConstraints()[_, _(l, l + depPoly->getDim1())];
  }
  [[nodiscard]] auto getBndPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getBndConstraints()[_, _(l, l + getNumPhiCoefficients())];
  }
  [[nodiscard]] auto getBndPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda();
    return getBndConstraints()[_, _(l, l + depPoly->getDim0())];
  }
  [[nodiscard]] auto getBndPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly->getNumLambda() + depPoly->getDim0();
    return getBndConstraints()[_, _(l, l + depPoly->getDim1())];
  }
  [[nodiscard]] auto getSatOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getSatConstraints()[_, _(l, l + getNumOmegaCoefficients())];
  }
  [[nodiscard]] auto getBndOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly->getNumLambda();
    return getBndConstraints()[_, _(l, l + getNumOmegaCoefficients())];
  }
  [[nodiscard]] auto getSatW() const -> math::StridedVector<int64_t> {
    return getSatConstraints()[_, 1 + depPoly->getNumLambda() +
                                    getNumPhiCoefficients() +
                                    getNumOmegaCoefficients()];
  }
  [[nodiscard]] auto getBndCoefs() const -> PtrMatrix<int64_t> {
    size_t lb = 1 + depPoly->getNumLambda() + getNumPhiCoefficients() +
                getNumOmegaCoefficients();
    return getBndConstraints()[_, _(lb, end)];
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
                                 Valid<const AffineSchedule> schIn,
                                 Valid<const AffineSchedule> schOut) const
    -> bool {
    ptrdiff_t numLoopsIn = in->getCurrentDepth(),
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
      schv[_(2, 2 + numLoopsIn)] << inPhi[last - i, _];
      schv[_(2 + numLoopsIn, 2 + numLoopsTotal)] << outPhi[last - i, _];
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
    ptrdiff_t numLoopsIn = in->getCurrentDepth(),
              numLoopsOut = out->getCurrentDepth(),
              numLoopsCommon = std::min(numLoopsIn, numLoopsOut),
              numVar = numLoopsIn + numLoopsOut + 2;
    invariant(dependenceSatisfaction->getNumVars(), numVar);
    auto schv = vector(&alloc, numVar, int64_t(0));
    // Vector<int64_t> schv(dependenceSatisfaction->getNumVars(),int64_t(0));
    const unsigned numLambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (ptrdiff_t i = 0; i <= numLoopsCommon; ++i) {
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
  [[nodiscard]] auto isSatisfied(Arena<> alloc, Valid<const AffineSchedule> sx,
                                 Valid<const AffineSchedule> sy, size_t d) const
    -> bool {
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
    ptrdiff_t numLambda = depPoly->getNumLambda(),
              numLoopsX = depPoly->getDim0(),
              numLoopsTotal = numLoopsX + depPoly->getDim1();
    MutPtrVector<int64_t> sch{math::vector<int64_t>(&alloc, numLoopsTotal + 2)};
    sch << 0;
    invariant(sch.size(), numLoopsTotal + 2);
    sch[2 + d] = 1;
    sch[2 + d + numLoopsX] = 1;
    return dependenceSatisfaction->satisfiable(alloc, sch, numLambda);
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
  using Tuple = containers::Tuple<IR::Addr *, IR::Addr *,
                                  std::array<Valid<math::Simplex>, 2>,
                                  DepPoly *, int32_t, int32_t, int32_t, int32_t,
                                  int32_t, std::array<uint8_t, 2>, uint8_t>;

  math::ManagedSOA<Tuple> datadeps;

  static constexpr size_t OutI = 0;
  static constexpr size_t InI = 1;
  static constexpr size_t SimplexPairI = 2;
  static constexpr size_t DepPolyI = 3;
  static constexpr size_t NextEdgeOutI = 4;
  static constexpr size_t PrevEdgeOutI = 5;
  static constexpr size_t NextEdgeInI = 6;
  static constexpr size_t PrevEdgeInI = 7;
  static constexpr size_t RevTimeEdgeI = 8;
  static constexpr size_t SatLevelI = 9;
  static constexpr size_t GetMetaI = 10;

public:
  using ID = Dependence::ID;
  Dependencies(ptrdiff_t len) : datadeps(len) {}
  Dependencies(const Dependencies &) noexcept = delete;
  constexpr Dependencies(Dependencies &&) noexcept = default;
  constexpr auto operator=(Dependencies &&other) noexcept -> Dependencies & {
    datadeps = std::move(other.datadeps);
    return *this;
  };

  [[nodiscard]] constexpr auto size() const noexcept -> ptrdiff_t {
    return datadeps.size();
  }

  constexpr auto tup(Dependence d, int32_t i) -> Tuple {
    IR::Addr *out = d.output(), *in = d.input();
    if (out->getEdgeOut() >= 0) prevOut(ID{out->getEdgeOut()}) = i;
    if (in->getEdgeIn() >= 0) prevIn(ID{in->getEdgeIn()}) = i;
    in->setEdgeOut(i);
    out->setEdgeIn(i);
    return Tuple{out,
                 in,
                 d.getSimplexPair(),
                 d.getDepPoly(),
                 out->getEdgeOut(),
                 -1,
                 in->getEdgeIn(),
                 -1,
                 d.revTimeEdge().id,
                 d.satLvl,
                 d.getMeta()};
  }

private:
  /// set(ID i, Dependence d)
  /// stores `d` at index `i`
  /// Dependence `d` is pushed to the fronts of the edgeOut and edgeIn chains.
  constexpr void set(int32_t i, Dependence d) { datadeps[i] = tup(d, i); }
  constexpr void set(ID i, Dependence d) { set(i.id, d); }
  auto addEdge(Dependence d) -> ID {
    int32_t id{int32_t(datadeps.size())};
    invariant(id >= 0);
    datadeps.push_back(tup(d, id));
    return {int32_t(id)};
  }

  void addOrdered(Valid<poly::DepPoly> dxy, Valid<IR::Addr> x,
                  Valid<IR::Addr> y, std::array<Valid<math::Simplex>, 2> pair,
                  bool isFwd) {
    ptrdiff_t numLambda = dxy->getNumLambda();
    if (!isFwd) {
      std::swap(pair[0], pair[1]);
      std::swap(x, y);
    }
    pair[0]->truncateVars(1 + numLambda + dxy->getNumScheduleCoef());
    addEdge(Dependence{.depPoly = dxy,
                       .dependenceSatisfaction = pair[0],
                       .dependenceBounding = pair[1],
                       .in = x,
                       .out = y,
                       .meta = isFwd});
  }
  void timelessCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                     Valid<IR::Addr> y,
                     std::array<Valid<math::Simplex>, 2> pair) {
    invariant(dxy->getTimeDim(), unsigned(0));
    return addOrdered(dxy, x, y, pair,
                      checkDirection(*alloc, pair, x, y, dxy->getNumLambda(),
                                     Col<>{dxy->getNumVar() + 1}));
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  void timeCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                 Valid<IR::Addr> y, std::array<Valid<math::Simplex>, 2> pair) {
    bool isFwd = checkDirection(
      *alloc, pair, x, y, dxy->getNumLambda(),
      Col<>{ptrdiff_t(dxy->getA().numCol()) - dxy->getTimeDim()});
    timeCheck(alloc, dxy, x, y, pair, isFwd);
  }
  static void timeStep(Valid<DepPoly> dxy, MutPtrMatrix<int64_t> fE,
                       MutPtrMatrix<int64_t> sE,
                       ptrdiff_t numInequalityConstraintsOld,
                       ptrdiff_t numEqualityConstraintsOld, ptrdiff_t ineqEnd,
                       ptrdiff_t posEqEnd, ptrdiff_t v, ptrdiff_t step) {
    for (ptrdiff_t c = 0; c < numInequalityConstraintsOld; ++c) {
      int64_t Acv = dxy->getA(Row<>{c}, Col<>{v});
      if (!Acv) continue;
      Acv *= step;
      fE[0, c + 1] -= Acv; // *1
      sE[0, c + 1] -= Acv; // *1
    }
    for (ptrdiff_t c = 0; c < numEqualityConstraintsOld; ++c) {
      // each of these actually represents 2 inds
      int64_t Ecv = dxy->getE(Row<>{c}, Col<>{v});
      if (!Ecv) continue;
      Ecv *= step;
      fE[0, c + ineqEnd] -= Ecv;
      fE[0, c + posEqEnd] += Ecv;
      sE[0, c + ineqEnd] -= Ecv;
      sE[0, c + posEqEnd] += Ecv;
    }
  }
  void timeCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                 Valid<IR::Addr> y, std::array<Valid<math::Simplex>, 2> pair,
                 bool isFwd) {
    const unsigned numInequalityConstraintsOld =
                     dxy->getNumInequalityConstraints(),
                   numEqualityConstraintsOld = dxy->getNumEqualityConstraints(),
                   ineqEnd = 1 + numInequalityConstraintsOld,
                   posEqEnd = ineqEnd + numEqualityConstraintsOld,
                   numLambda = posEqEnd + numEqualityConstraintsOld,
                   numScheduleCoefs = dxy->getNumScheduleCoef();
    invariant(numLambda, dxy->getNumLambda());
    // copy backup
    std::array<Valid<math::Simplex>, 2> farkasBackups{pair[0]->copy(alloc),
                                                      pair[1]->copy(alloc)};
    Valid<IR::Addr> in = x, out = y;
    if (isFwd) {
      std::swap(farkasBackups[0], farkasBackups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0]->truncateVars(1 + numLambda + numScheduleCoefs);
    Dependence dep0{.depPoly = dxy->copy(alloc),
                    .dependenceSatisfaction = pair[0],
                    .dependenceBounding = pair[1],
                    .in = in,
                    .out = out,
                    .meta = isFwd};
    invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
              ptrdiff_t(dep0.getNumPhiCoefficients()));
    ID d0ID{addEdge(dep0)}, prevID = d0ID;
    // pair is invalid
    const ptrdiff_t timeDim = dxy->getTimeDim(),
                    numVar = 1 + dxy->getNumVar() - timeDim;
    invariant(timeDim > 0);
    // 1 + because we're indexing into A and E, ignoring the constants
    // remove the time dims from the deps
    // dep0.depPoly->truncateVars(numVar);

    // dep0.depPoly->setTimeDim(0);
    invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
              ptrdiff_t(dep0.getNumPhiCoefficients()));
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be insane
    for (ptrdiff_t t = 0;;) {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = dxy->getNullStep(t);
      ptrdiff_t v = numVar + t;
      bool repeat = (++t < timeDim);
      std::array<Valid<math::Simplex>, 2> fp{farkasBackups};
      if (repeat) {
        fp[0] = fp[0]->copy(alloc);
        fp[1] = fp[1]->copy(alloc);
      }
      // set (or unset) for this timedim
      auto fE{fp[0]->getConstraints()[_, _(1, end)]};
      auto sE{fp[1]->getConstraints()[_, _(1, end)]};
      timeStep(dxy, fE, sE, numInequalityConstraintsOld,
               numEqualityConstraintsOld, ineqEnd, posEqEnd, v, step);
      // checkDirection should be `true`, so if `false` we flip the sign
      // this is because `isFwd = checkDirection` of the original
      // `if (isFwd)`, we swapped farkasBackups args, making the result
      // `false`; for our timeDim to capture the opposite movement
      // through time, we thus need to flip it back to `true`.
      // `if (!isFwd)`, i.e. the `else` branch above, we don't flip the
      // args, so it'd still return `false` and a flip would still mean `true`.
      if (!checkDirection(
            *alloc, fp, *out, *in, numLambda,
            Col<>{ptrdiff_t(dxy->getA().numCol()) - dxy->getTimeDim()}))
        timeStep(dxy, fE, sE, numInequalityConstraintsOld,
                 numEqualityConstraintsOld, ineqEnd, posEqEnd, v, -2 * step);

      fp[0]->truncateVars(1 + numLambda + numScheduleCoefs);
      Dependence dep1{.depPoly = dxy,
                      .dependenceSatisfaction = farkasBackups[0],
                      .dependenceBounding = farkasBackups[1],
                      .in = out,
                      .out = in,
                      .revTimeEdge_ = prevID,
                      .meta = !isFwd};
      invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
                ptrdiff_t(dep1.getNumPhiCoefficients()));
      prevID = addEdge(dep1);
      if (!repeat) break;
    }
    revTimeEdge(d0ID) = prevID.id;
  }
  static auto checkDirection(Arena<> alloc,
                             const std::array<Valid<math::Simplex>, 2> &p,
                             Valid<const IR::Addr> x, Valid<const IR::Addr> y,
                             Valid<const AffineSchedule> xSchedule,
                             Valid<const AffineSchedule> ySchedule,
                             ptrdiff_t numLambda, Col<> nonTimeDim) -> bool {
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
      sch[_(2, 2 + numLoopsX)] << xPhi[last - i, _];
      sch[_(2 + numLoopsX, 2 + numLoopsTotal)] << yPhi[last - i, _];
      if (fxy->unSatisfiableZeroRem(alloc, sch, numLambda,
                                    ptrdiff_t(nonTimeDim))) {
        assert(!fyx->unSatisfiableZeroRem(alloc, sch, numLambda,
                                          ptrdiff_t(nonTimeDim)));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, sch, numLambda,
                                    ptrdiff_t(nonTimeDim)))
        return true;
    }
    // assert(false);
    // return false;
  }
  // returns `true` if forward, x->y
  static auto checkDirection(Arena<> alloc,
                             const std::array<Valid<math::Simplex>, 2> &p,
                             Valid<const IR::Addr> x, Valid<const IR::Addr> y,
                             ptrdiff_t numLambda, Col<> nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    unsigned numLoopsX = x->getCurrentDepth(), nTD = ptrdiff_t(nonTimeDim);
#ifndef NDEBUG
    ptrdiff_t numLoopsCommon =
      std::min(ptrdiff_t(numLoopsX), ptrdiff_t(y->getCurrentDepth()));
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
    auto [depSat, depBnd] = depSatBnd(i);
    return Dependence{.depPoly = depPoly(i),
                      .dependenceSatisfaction = depSat,
                      .dependenceBounding = depBnd,
                      .in = in,
                      .out = out,
                      .satLvl = satLevelPair(i),
                      .meta = getMeta(i)

    };
  }

public:
  constexpr void removeEdge(ID id) {
    removeOutEdge(id.id);
    removeInEdge(id.id);
    /// TODO: remove revTimeEdge?
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
  // constexpr auto outAddrs() -> MutPtrVector<IR::Addr *> {
  //   return {outAddrPtr(), numData};
  // }
  // constexpr auto inAddrs() -> MutPtrVector<IR::Addr *> {
  //   return {inAddrPtr(), numData};
  // }
  constexpr auto outEdges() -> MutPtrVector<int32_t> {
    return datadeps.template get<NextEdgeOutI>();
  }
  constexpr auto inEdges() -> MutPtrVector<int32_t> {
    return datadeps.template get<NextEdgeInI>();
  }
  [[nodiscard]] constexpr auto outEdges() const -> PtrVector<int32_t> {
    return datadeps.template get<NextEdgeOutI>();
  }
  [[nodiscard]] constexpr auto inEdges() const -> PtrVector<int32_t> {
    return datadeps.template get<NextEdgeInI>();
  }
  // [[nodiscard]] constexpr auto outEdges() const -> PtrVector<int32_t> {
  //   return {outEdgePtr(), unsigned(numData)};
  // }
  // [[nodiscard]] constexpr auto inEdges() const -> PtrVector<int32_t> {
  //   return {inEdgePtr(), unsigned(numData)};
  // }
  // constexpr auto satLevels() -> MutPtrVector<std::array<uint8_t, 2>> {
  //   return {satLevelsPtr(), numData};
  // }

  [[nodiscard]] constexpr auto output(ID i) -> IR::Addr *& {
    return datadeps.template get<OutI>(i.id);
  }
  [[nodiscard]] constexpr auto output(ID i) const -> IR::Addr * {
    return datadeps.template get<OutI>(i.id);
  }
  [[nodiscard]] constexpr auto input(ID i) -> IR::Addr *& {
    return datadeps.template get<InI>(i.id);
  }
  [[nodiscard]] constexpr auto input(ID i) const -> IR::Addr * {
    return datadeps.template get<InI>(i.id);
  }
  constexpr auto nextOut(ID i) -> int32_t & {
    return datadeps.template get<NextEdgeOutI>(i.id);
  }
  constexpr auto prevOut(ID i) -> int32_t & {
    return datadeps.template get<PrevEdgeOutI>(i.id);
  }
  constexpr auto nextIn(ID i) -> int32_t & {
    return datadeps.template get<NextEdgeInI>(i.id);
  }
  constexpr auto prevIn(ID i) -> int32_t & {
    return datadeps.template get<PrevEdgeInI>(i.id);
  }
  constexpr auto depSatBnd(ID i) -> std::array<Valid<math::Simplex>, 2> & {
    return datadeps.template get<SimplexPairI>(i.id);
  }
  constexpr auto revTimeEdge(ID i) -> int32_t & {
    return datadeps.template get<RevTimeEdgeI>(i.id);
  }
  [[nodiscard]] constexpr auto revTimeEdge(ID i) const -> int32_t {
    return datadeps.template get<RevTimeEdgeI>(i.id);
  }
  constexpr auto depPoly(ID i) -> DepPoly *& {
    return datadeps.template get<DepPolyI>(i.id);
  }
  [[nodiscard]] constexpr auto depSatBnd(ID i) const
    -> std::array<Valid<math::Simplex>, 2> {
    return datadeps.template get<SimplexPairI>(i.id);
  }
  [[nodiscard]] constexpr auto depPoly(ID i) const -> DepPoly * {
    return datadeps.template get<DepPolyI>(i.id);
  }
  constexpr auto satLevelPair(ID i) -> std::array<uint8_t, 2> & {
    return datadeps.template get<SatLevelI>(i.id);
  }
  [[nodiscard]] constexpr auto satLevelPair(ID i) const
    -> std::array<uint8_t, 2> {
    return datadeps.template get<SatLevelI>(i.id);
  }
  [[nodiscard]] constexpr auto satLevel(ID i) const -> uint8_t {
    return Dependence::satLevelMask(satLevelPair(i)[0]);
  }
  [[nodiscard]] constexpr auto isSat(ID i, unsigned depth) const -> uint8_t {
    return Dependence::satLevelMask(satLevelPair(i)[0]) <= depth;
  }
  [[nodiscard]] constexpr auto isActive(ID i, unsigned depth) const -> uint8_t {
    return Dependence::satLevelMask(satLevelPair(i)[0]) > depth;
  }

  [[nodiscard]] constexpr auto getMeta(ID i) noexcept -> uint8_t & {
    return datadeps.template get<GetMetaI>(i.id);
  }
  [[nodiscard]] constexpr auto getMeta(ID i) const noexcept -> uint8_t {
    return datadeps.template get<GetMetaI>(i.id);
  }
  [[nodiscard]] constexpr auto isForward(ID i) const noexcept -> bool {
    return getMeta(i);
  }

  class Ref {
    Dependencies *deps_;
    ID i_;

  public:
    constexpr Ref(Dependencies *deps, ID i) : deps_(deps), i_(i) {}
    operator Dependence() const { return deps_->get(i_); }
    auto operator=(Dependence d) -> Ref & {
      deps_->set(i_, d);
      return *this;
    }
  };

  void check(Arena<> *alloc, Valid<IR::Addr> x, Valid<IR::Addr> y) {
    // TODO: implement gcd test
    // if (x.gcdKnownIndependent(y)) return {};
    DepPoly *dxy{DepPoly::dependence(alloc, x, y)};
    if (!dxy) return;
    invariant(x->getCurrentDepth() == ptrdiff_t(dxy->getDim0()));
    invariant(y->getCurrentDepth() == ptrdiff_t(dxy->getDim1()));
    invariant(x->getCurrentDepth() + y->getCurrentDepth() ==
              ptrdiff_t(dxy->getNumPhiCoef()));
    // note that we set boundAbove=true, so we reverse the
    // dependence direction for the dependency we week, we'll
    // discard the program variables x then y
    std::array<Valid<math::Simplex>, 2> pair(dxy->farkasPair(alloc));
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, x, y, pair);
    else timelessCheck(alloc, dxy, x, y, pair);
  }
  inline void copyDependencies(IR::Addr *src, IR::Addr *dst);
  // reload store `x`
  auto reload(Arena<> *alloc, Valid<IR::Addr> store) -> Valid<IR::Addr> {
    Valid<DepPoly> dxy{DepPoly::self(alloc, store)};
    std::array<Valid<math::Simplex>, 2> pair(dxy->farkasPair(alloc));
    Valid<IR::Addr> load = store->reload(alloc);
    copyDependencies(store, load);
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, store, load, pair, true);
    else addOrdered(dxy, store, load, pair, true);
    return load;
  }
  [[nodiscard]] constexpr auto inputEdgeIDs(int32_t id) const {
    return utils::VForwardRange{inEdges(), id};
  }
  [[nodiscard]] constexpr auto outputEdgeIDs(int32_t id) const {
    return utils::VForwardRange{outEdges(), id};
  }
  [[nodiscard]] constexpr auto getEdgeTransform() const {
    auto f = [=, this](int32_t id) { return get(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto inputEdges(int32_t id) const {
    return inputEdgeIDs(id) | getEdgeTransform();
  }
  [[nodiscard]] constexpr auto outputEdges(int32_t id) const {
    return outputEdgeIDs(id) | getEdgeTransform();
  }

  [[nodiscard]] constexpr auto activeFilter(int depth) const {
    auto f = [=, this](int32_t id) -> bool {
      return isActive(Dependence::ID{id}, depth);
    };
    return std::views::filter(f);
  }
  [[nodiscard]] constexpr auto inputAddrTransform() {
    auto f = [=, this](int32_t id) { return input(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto outputAddrTransform() {
    auto f = [=, this](int32_t id) { return output(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto inputAddrTransform() const {
    auto f = [=, this](int32_t id) { return input(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto outputAddrTransform() const {
    auto f = [=, this](int32_t id) { return output(Dependence::ID{id}); };
    return std::views::transform(f);
  }
  /// this function essentially indicates that this dependency does not prevent
  /// the hoisting of a memory access out of a loop, because a memory->register
  /// transform is possible.
  /// The requirements are that the `indexMatrix` match
  [[nodiscard]] constexpr auto registerEligible(ID id) const -> bool {
    /// If no repeated accesses across time, it can't be hoisted out
    if (revTimeEdge(id) < 0) return false;
    DensePtrMatrix<int64_t> inMat{input(id)->indexMatrix()},
      outMat{output(id)->indexMatrix()};
    ptrdiff_t numLoopsIn = ptrdiff_t(inMat.numCol()),
              numLoopsOut = ptrdiff_t(outMat.numCol()),
              numLoops = std::min(numLoopsIn, numLoopsOut);
    if ((numLoopsIn != numLoopsOut) &&
        math::anyNEZero(numLoopsIn > numLoopsOut
                          ? inMat[_, _(numLoopsOut, numLoopsIn)]
                          : outMat[_, _(numLoopsIn, numLoopsOut)]))
      return false;
    return inMat[_, _(0, numLoops)] == outMat[_, _(0, numLoops)];
  }
  [[nodiscard]] constexpr auto registerEligibleFilter() const {
    auto f = [=, this](int32_t id) -> bool {
      return registerEligible(Dependence::ID{id});
    };
    return std::views::filter(f);
  }
};

} // namespace poly
namespace IR {
using poly::Dependencies;

inline auto Addr::inputEdges(const Dependencies &deps) const {
  return deps.inputEdges(getEdgeIn());
}
inline auto Addr::outputEdges(const Dependencies &deps) const {
  return deps.outputEdges(getEdgeOut());
}
inline auto Addr::inputEdgeIDs(const Dependencies &deps) const {
  return deps.inputEdgeIDs(getEdgeIn());
}
inline auto Addr::outputEdgeIDs(const Dependencies &deps) const {
  return deps.outputEdgeIDs(getEdgeOut());
}
inline auto Addr::inputEdgeIDs(const Dependencies &deps, int depth) const {
  return inputEdgeIDs(deps) | deps.activeFilter(depth);
}
inline auto Addr::outputEdgeIDs(const Dependencies &deps, int depth) const {
  return outputEdgeIDs(deps) | deps.activeFilter(depth);
}

inline auto IR::Addr::inputAddrs(const Dependencies &deps) const {
  return inputEdgeIDs(deps) | deps.inputAddrTransform();
}
inline auto IR::Addr::outputAddrs(const Dependencies &deps) const {
  return outputEdgeIDs(deps) | deps.outputAddrTransform();
}
inline auto Addr::inputEdges(const Dependencies &deps, int depth) const {
  return inputEdgeIDs(deps) | deps.activeFilter(depth) |
         deps.getEdgeTransform();
}
inline auto Addr::outputEdges(const Dependencies &deps, int depth) const {
  return outputEdgeIDs(deps) | deps.activeFilter(depth) |
         deps.getEdgeTransform();
}
inline auto IR::Addr::inputAddrs(const Dependencies &deps, int depth) const {
  return inputEdgeIDs(deps, depth) | deps.inputAddrTransform();
}
inline auto IR::Addr::outputAddrs(const Dependencies &deps, int depth) const {
  return outputEdgeIDs(deps, depth) | deps.outputAddrTransform();
}
inline auto IR::Addr::unhoistableOutputs(const Dependencies &deps,
                                         int depth) const {
  return outputEdgeIDs(deps, depth) | deps.registerEligibleFilter() |
         deps.outputAddrTransform();
}

/// Addr::operator->(const Dependencies& deps)
/// drop `this` from the graph, and remove it from `deps`
inline void IR::Addr::drop(Dependencies &deps) {
  // NOTE: this doesn't get removed from the `origAddr` list/the addrChain
  if (IR::Loop *L = getLoop(); L->getChild() == this) L->setChild(getNext());
  removeFromList();
  for (int32_t id : inputEdgeIDs(deps)) deps.removeEdge(Dependence::ID{id});
  for (int32_t id : outputEdgeIDs(deps)) deps.removeEdge(Dependence::ID{id});
}

using math::StridedVector;
inline auto Loop::getLegality(const poly::Dependencies &deps,
                              math::PtrVector<int32_t> loopDeps)
  -> LegalTransforms {
  const auto legal = getLegal();
  if (legal != Unknown) return legal;
  if (edgeId < 0) return setLegal(DependenceFree);
  if (this->currentDepth == 0) return setLegal(None);
  ptrdiff_t loop = this->currentDepth - 1;
  for (int32_t id : edges(loopDeps)) {
    Dependence::ID i{id};
    StridedVector<int64_t> in = deps.input(i)->indexMatrix()[_, loop],
                           out = deps.output(i)->indexMatrix()[_, loop];
    invariant(in.size(), out.size());
    if (in != out) return setLegal(IndexMismatch);
    // ptrdiff_t common = std::min(in.size(), out.size());
    // if ((in[_(0, common)] != out[_(0, common)]) ||
    //     math::anyNEZero(((in.size() > out.size() ? in : out)[_(common,
    //     end)])))
    //   return legal = IndexMismatch;
  }
  return setLegal(None);
}
//
} // namespace IR

namespace poly {
inline void Dependencies::copyDependencies(IR::Addr *src, IR::Addr *dst) {
  for (int32_t id : src->inputEdgeIDs(*this)) {
    IR::Addr *input = this->input(Dependence::ID{id});
    if (input->isLoad()) continue;
    Dependence d = get(Dependence::ID{id}, input, dst);
    addEdge(d);
  }
  for (int32_t id : src->outputEdgeIDs(*this)) {
    IR::Addr *output = this->output(Dependence::ID{id});
    if (output->isLoad()) continue;
    Dependence d = get(Dependence::ID{id}, dst, output);
    addEdge(d);
  }
}

} // namespace poly

} // namespace poly
