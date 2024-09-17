#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ostream>
#include <ranges>
#include <type_traits>
#include <utility>

#ifndef NDEBUG
#define DEBUGUSED [[gnu::used]]
#else
#define DEBUGUSED
#endif

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Tuple.cxx"
#include "IR/Address.cxx"
#include "IR/Node.cxx"
#include "Math/Array.cxx"
#include "Math/Comparisons.cxx"
#include "Math/Constructors.cxx"
#include "Math/SOA.cxx"
#include "Math/Simplex.cxx"
#include "Polyhedra/DependencyPolyhedra.cxx"
#include "Polyhedra/Schedule.cxx"
#include "Support/Iterators.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Optional.cxx"
#else
export module IR:Dependence;
import Arena;
import Array;
import ArrayConstructors;
import Comparisons;
import Invariant;
import ListIterator;
import Optional;
import Simplex;
import SOA;
import Tuple;
export import :DepPoly;
import :Address;
import :AffineSchedule;
import :Node;
#endif

using math::MutPtrMatrix;

#ifdef USE_MODULE
export namespace poly {
#else
namespace poly {
#endif

/// Dependence
/// Represents a dependence relationship between two memory accesses.
/// It contains simplices representing constraints that affine schedules
/// are allowed to take.
struct Dependence {
  using Tuple =
    containers::Tuple<IR::Addr *, IR::Addr *, std::array<math::Simplex *, 2>,
                      DepPoly *, int32_t, int32_t, int32_t, int32_t, int32_t,
                      std::array<uint8_t, 2>, uint8_t, uint8_t>;
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
  static constexpr size_t GetPeelI = 11;

  math::ManagedSOA<Tuple> &datadeps_;
  int32_t id_;

  // TODO: revert to `bool` flag for `Forward`?
  enum MetaFlags : uint8_t {
    Forward = 1,                   // 0
    ReverseTime = 2,               // 1
    FreeOfDeeperDeps = 4,          // 2
    Reassociable = 8,              // 3
    NotReassociable = 16,          // 4
    ConditionallyIndependent = 32, // 5
    RegisterEligible = 64          // 6
  };

  [[nodiscard]] constexpr auto output() -> IR::Addr *& {
    return datadeps_.template get<OutI>(id_);
  }
  [[nodiscard]] constexpr auto output() const -> IR::Addr * {
    return datadeps_.template get<OutI>(id_);
  }
  [[nodiscard]] constexpr auto input() -> IR::Addr *& {
    return datadeps_.template get<InI>(id_);
  }
  [[nodiscard]] constexpr auto input() const -> IR::Addr * {
    return datadeps_.template get<InI>(id_);
  }
  constexpr auto nextOut() -> int32_t & {
    return datadeps_.template get<NextEdgeOutI>(id_);
  }
  constexpr auto prevOut() -> int32_t & {
    return datadeps_.template get<PrevEdgeOutI>(id_);
  }
  constexpr auto nextIn() -> int32_t & {
    return datadeps_.template get<NextEdgeInI>(id_);
  }
  constexpr auto prevIn() -> int32_t & {
    return datadeps_.template get<PrevEdgeInI>(id_);
  }
  constexpr auto depSatBnd() -> std::array<math::Simplex *, 2> & {
    return datadeps_.template get<SimplexPairI>(id_);
  }
  constexpr auto revTimeEdge() -> int32_t & {
    return datadeps_.template get<RevTimeEdgeI>(id_);
  }
  [[nodiscard]] constexpr auto revTimeEdge() const -> int32_t {
    return datadeps_.template get<RevTimeEdgeI>(id_);
  }
  constexpr auto depPoly() -> DepPoly *& {
    return datadeps_.template get<DepPolyI>(id_);
  }
  [[nodiscard]] constexpr auto depSatBnd() const
    -> std::array<math::Simplex *, 2> {
    return datadeps_.template get<SimplexPairI>(id_);
  }
  [[nodiscard]] constexpr auto depSat() const -> math::Simplex * {
    return datadeps_.template get<SimplexPairI>(id_)[0];
  }
  [[nodiscard]] constexpr auto depBnd() const -> math::Simplex * {
    return datadeps_.template get<SimplexPairI>(id_)[1];
  }
  [[nodiscard]] constexpr auto depPoly() const -> DepPoly * {
    return datadeps_.template get<DepPolyI>(id_);
  }
  constexpr auto satLevelPair() -> std::array<uint8_t, 2> & {
    return datadeps_.template get<SatLevelI>(id_);
  }
  [[nodiscard]] constexpr auto satLevelPair() const -> std::array<uint8_t, 2> {
    return datadeps_.template get<SatLevelI>(id_);
  }
  // note that sat levels start at `0`, `0` meaning the outer most loop
  // satisfies it. Thus, `satLevel() == 0` means the `depth == 1` loop satisfied
  // it.
  [[nodiscard]] constexpr auto satLevel() const -> uint8_t {
    return satLevelMask(satLevelPair()[0]);
  }
#ifndef NDEBUG
  [[nodiscard, gnu::used]] constexpr auto getMeta() noexcept -> uint8_t & {
    return datadeps_.template get<GetMetaI>(id_);
  }
  [[nodiscard, gnu::used]] constexpr auto getMeta() const noexcept -> uint8_t {
    return datadeps_.template get<GetMetaI>(id_);
  }
#else
  [[nodiscard]] constexpr auto getMeta() noexcept -> uint8_t & {
    return datadeps.template get<GetMetaI>(id);
  }
  [[nodiscard]] constexpr auto getMeta() const noexcept -> uint8_t {
    return datadeps.template get<GetMetaI>(id);
  }
#endif
  [[nodiscard]] constexpr auto getPeel() noexcept -> uint8_t & {
    return datadeps_.template get<GetPeelI>(id_);
  }
  [[nodiscard]] constexpr auto getPeel() const noexcept -> uint8_t {
    return datadeps_.template get<GetPeelI>(id_);
  }
  // is this the reverse time direction?
  [[nodiscard]] constexpr auto isReverseTimeDep() const noexcept -> bool {
    return getMeta() & MetaFlags::ReverseTime;
  }
  /// indicates whether forward is non-empty
  /// Direction in simplex [x,y]: Forward ? x -> y : y -> x
  /// i.e., is the simplex `[in, out]` (forward) or `[out, in]` (!forward)
  [[nodiscard]] constexpr auto isForward() const noexcept -> bool {
    return getMeta() & MetaFlags::Forward;
  }
  [[nodiscard]] constexpr auto isRegisterEligible() const noexcept -> bool {
    return getMeta() & MetaFlags::RegisterEligible;
  }
  constexpr auto checkRegisterEligible() noexcept -> bool {
    if (revTimeEdge() < 0) return false;
    IR::Addr *x = input(), *y = output();
    /// If no repeated accesses across time, it can't be hoisted out
    DensePtrMatrix<int64_t> x_mat{x->indexMatrix()}, y_mat{y->indexMatrix()};
    ptrdiff_t num_loops_x = ptrdiff_t(x_mat.numCol()),
              num_loops_y = ptrdiff_t(y_mat.numCol()),
              num_loops = std::min(num_loops_x, num_loops_y);

    if (((num_loops_x != num_loops_y) &&
         math::anyNEZero(num_loops_x > num_loops_y
                           ? x_mat[_, _(num_loops_y, num_loops_x)]
                           : y_mat[_, _(num_loops_x, num_loops_y)])) ||
        (x_mat[_, _(0, num_loops)] != y_mat[_, _(0, num_loops)]))
      return false;
    getMeta() |= MetaFlags::RegisterEligible;
    return true;
  }
  // FIXME: does not currently get set
  [[nodiscard]] constexpr auto conditionallyIndependent() const noexcept
    -> bool {
    return getMeta() & MetaFlags::ConditionallyIndependent;
  }

  // // private:
  // //
  // //
  // Valid<DepPoly> depPoly;
  // math::Simplex* dependenceSatisfaction;
  // math::Simplex* dependenceBounding;
  // Valid<IR::Addr> in;
  // Valid<IR::Addr> out;
  // // Dependence *nextInput{nullptr}; // all share same `in`
  // // Dependence *nextOutput{nullptr};
  // // // all share same `out`
  // // // the upper bit of satLvl indicates whether the satisfaction is
  // // // because of conditional independence (value = 0), or whether it
  // // // was because of offsets when solving the linear program (value =
  // // // 1).
  // // std::array<uint8_t, 7> satLvl{255, 255, 255, 255, 255, 255, 255};
  // ID revTimeEdge_{-1};
  // std::array<uint8_t, 2> satLvl{255, 255}; // isSat must return `false`
  // uint8_t meta{0};
  // uint8_t peel{255}; // sentinal value for cannot peel

  // public:
  friend class Dependencies;
  [[nodiscard]] constexpr auto peelable() const -> bool {
    return getPeel() != 255;
  }
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
  constexpr void setSatLevelLP(uint8_t depth0) {
    satLevelPair()[0] = uint8_t(128) | (2 * depth0);
  }
  // Set sat level, but allow parallelizing this loop
  constexpr void setSatLevelParallel(uint8_t depth0) {
    satLevelPair()[0] = 2 * depth0;
  }
  static constexpr auto satLevelMask(uint8_t slvl) -> uint8_t {
    return slvl & uint8_t(127); // NOTE: deduces to `int`
  }
  /// SatLevels are given as `2*depth0`.
  /// The number of loops is given as `2*depth0+1`.
  ///
  /// For topological sorting at depth `d=depth0`, we
  /// filter out all sat levels `<= 2d`.
  ///
  /// For choosing which edges to skip because they don't matter at a given
  /// level in LP, we filter out all sat levels `<= 2d+1`.
  ///
  /// Note that during the LP solve, we haven't marked sat levels
  /// yet, and definitely not of the greater depth!
  /// Thus, for LP solve, we can filter on `satLevel() <= 2(d+1)`
  /// vs `satLevel() <= 2d` for top-sort.
  ///
  /// `isSat` returns `true` on the level that satisfies it
  /// Opposite of `isActive`.
  [[nodiscard]] constexpr auto isSat(int depth0) const -> bool {
    invariant(depth0 <= 127);
    return satLevel() <= (2 * depth0);
  }
  /// `isActive` returns `false` on the level that satisfies it
  /// Opposite of `isSat`.
  [[nodiscard]] constexpr auto isActive(int depth0) const -> bool {
    invariant(depth0 <= 127);
    return satLevel() > (2 * depth0);
  }
  /// if true, then it's independent conditioned on the phis...
  /// We don't actually use this for anything.
  /// Also, bit flag seems conflated with `preventsReodering`
  /// Which we also don't use?
  [[nodiscard]] constexpr auto isCondIndep() const -> bool {
    return (satLevelPair()[0] & uint8_t(128)) == uint8_t(0);
  }
  [[nodiscard]] static constexpr auto preventsReordering(uint8_t depth0)
    -> bool {
    return depth0 & uint8_t(128);
  }
  // prevents reordering satisfied level if `true`
  // Conflated with `isCondIndep`, but is used?
  [[nodiscard]] constexpr auto preventsReordering() const -> bool {
    return preventsReordering(satLevelPair()[0]);
  }
  /// checks the stash is active at `depth`, and that the stash
  /// does prevent reordering.
  [[nodiscard]] constexpr auto stashedPreventsReordering(int depth0) const
    -> bool {
    invariant(depth0 <= 127);
    auto s = satLevelPair()[1];
    return preventsReordering(s) && s > depth0;
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> IR::Value * {
    return input()->getArrayPointer();
  }
  [[nodiscard]] constexpr auto nodeIn() const -> const lp::ScheduledNode * {
    return input()->getNode();
  }
  // [[nodiscard]] constexpr auto nodeOut() const -> unsigned {
  //   return out->getNode();
  // }
  [[nodiscard]] constexpr auto getDynSymDim() const -> int {
    return depPoly()->getNumDynSym();
  }
  [[nodiscard]] auto inputIsLoad() const -> bool { return input()->isLoad(); }
  [[nodiscard]] auto outputIsLoad() const -> bool { return output()->isLoad(); }
  [[nodiscard]] auto inputIsStore() const -> bool { return input()->isStore(); }
  [[nodiscard]] auto outputIsStore() const -> bool {
    return output()->isStore();
  }
  /// getInIndMat() -> getInNumLoops() x arrayDim()
  [[nodiscard]] auto getInIndMat() const -> DensePtrMatrix<int64_t> {
    return input()->indexMatrix();
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
    if (!depPoly()->checkSat(*alloc, inLoop, inOff, inPhi, outLoop, outOff,
                             outPhi))
      return;
    satLevelPair()[0] = uint8_t(ptrdiff_t(inPhi.numRow()) - 1); // 0-based
    // getMeta() |= MetaFlags::ConditionallyIndependent;
  }
  constexpr void copySimplices(Arena<> *alloc) {
    auto &&[depSat, depBnd] = depSatBnd();
    depSat = depSat->copy(alloc);
    depBnd = depBnd->copy(alloc);
  }
  /// getOutIndMat() -> getOutNumLoops() x arrayDim()
  [[nodiscard]] constexpr auto getOutIndMat() const -> PtrMatrix<int64_t> {
    return output()->indexMatrix();
  }
  [[nodiscard]] constexpr auto getInOutPair() const
    -> std::array<IR::Addr *, 2> {
    return {input(), output()};
  }
  // returns the memory access pair, placing the store first in the pair
  [[nodiscard]] constexpr auto getStoreAndOther() const
    -> std::array<IR::Addr *, 2> {
    auto [in, out] = getInOutPair();
    if (in->isStore()) return {in, out};
    return {out, in};
  }
  [[nodiscard]] constexpr auto getInCurrentDepth() const -> int {
    return input()->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getOutCurrentDepth() const -> int {
    return output()->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getInNaturalDepth() const -> int {
    return input()->getNaturalDepth();
  }
  [[nodiscard]] constexpr auto getOutNatrualDepth() const -> int {
    return output()->getNaturalDepth();
  }
  [[nodiscard]] constexpr auto getNumLambda() const -> int {
    return depPoly()->getNumLambda() << 1;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> int {
    return depPoly()->getNumSymbols();
  }
  [[nodiscard]] constexpr auto getNumPhiCoefficients() const -> int {
    return depPoly()->getNumPhiCoef();
  }
  [[nodiscard]] static constexpr auto getNumOmegaCoefficients() -> int {
    return DepPoly::getNumOmegaCoef();
  }
  [[nodiscard]] constexpr auto getNumDepSatConstraintVar() const -> int {
    auto ret = depSatBnd()[0]->getNumVars();
    invariant(ret <= std::numeric_limits<int>::max());
    return int(ret);
  }
  [[nodiscard]] constexpr auto getNumDepBndConstraintVar() const -> int {
    auto ret = depSatBnd()[1]->getNumVars();
    invariant(ret <= std::numeric_limits<int>::max());
    return int(ret);
  }
  // returns `w`
  [[nodiscard]] constexpr auto getNumDynamicBoundingVar() const -> int {
    return getNumDepBndConstraintVar() - getNumDepSatConstraintVar();
  }
  constexpr void validate() {
    assert(getInCurrentDepth() + getOutCurrentDepth() ==
           getNumPhiCoefficients());
    // 2 == 1 for const offset + 1 for w
    assert(2 + depPoly()->getNumLambda() + getNumPhiCoefficients() +
             getNumOmegaCoefficients() ==
           ptrdiff_t(depSat()->getConstraints().numCol()));
  }
  [[nodiscard]] constexpr auto getNumConstraints() const -> int {
    auto [sat, bnd] = depSatBnd();
    auto ret = bnd->getNumCons() + sat->getNumCons();
    invariant(ret <= std::numeric_limits<int>::max());
    return int(ret);
  }
  [[nodiscard]] auto getSatConstants() const -> math::StridedVector<int64_t> {
    return depSat()->getConstants();
  }
  [[nodiscard]] auto getBndConstants() const -> math::StridedVector<int64_t> {
    return depBnd()->getConstants();
  }
  [[nodiscard]] auto getSatConstraints() const -> PtrMatrix<int64_t> {
    return depSat()->getConstraints();
  }
  [[nodiscard]] auto getBndConstraints() const -> PtrMatrix<int64_t> {
    return depBnd()->getConstraints();
  }
  [[nodiscard]] auto getSatLambda() const -> PtrMatrix<int64_t> {
    return getSatConstraints()[_, _(1, 1 + depPoly()->getNumLambda())];
  }
  [[nodiscard]] auto getBndLambda() const -> PtrMatrix<int64_t> {
    return getBndConstraints()[_, _(1, 1 + depPoly()->getNumLambda())];
  }
  [[nodiscard]] auto getSatPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly()->getNumLambda();
    return getSatConstraints()[_, _(l, l + getNumPhiCoefficients())];
  }
  [[nodiscard]] auto getSatPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly()->getNumLambda();
    return getSatConstraints()[_, _(l, l + depPoly()->getDim0())];
  }
  [[nodiscard]] auto getSatPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto *dep = depPoly();
    auto l = 3 + dep->getNumLambda() + dep->getDim0();
    return getSatConstraints()[_, _(l, l + dep->getDim1())];
  }
  [[nodiscard]] auto getBndPhiCoefs() const -> PtrMatrix<int64_t> {
    auto l = 3 + depPoly()->getNumLambda();
    return getBndConstraints()[_, _(l, l + getNumPhiCoefficients())];
  }
  [[nodiscard]] auto getBndPhi0Coefs() const -> PtrMatrix<int64_t> {
    auto *dep = depPoly();
    auto l = 3 + dep->getNumLambda();
    return getBndConstraints()[_, _(l, l + dep->getDim0())];
  }
  [[nodiscard]] auto getBndPhi1Coefs() const -> PtrMatrix<int64_t> {
    auto *dep = depPoly();
    auto l = 3 + dep->getNumLambda() + dep->getDim0();
    return getBndConstraints()[_, _(l, l + dep->getDim1())];
  }
  [[nodiscard]] auto getSatOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto *dep = depPoly();
    auto l = 1 + dep->getNumLambda();
    return getSatConstraints()[_, _(l, l + getNumOmegaCoefficients())];
  }
  [[nodiscard]] auto getBndOmegaCoefs() const -> PtrMatrix<int64_t> {
    auto l = 1 + depPoly()->getNumLambda();
    return getBndConstraints()[_, _(l, l + getNumOmegaCoefficients())];
  }
  [[nodiscard]] auto getSatW() const -> math::StridedVector<int64_t> {
    return getSatConstraints()[_, 1 + depPoly()->getNumLambda() +
                                    getNumPhiCoefficients() +
                                    getNumOmegaCoefficients()];
  }
  [[nodiscard]] auto getBndCoefs() const -> PtrMatrix<int64_t> {
    size_t lb = 1 + depPoly()->getNumLambda() + getNumPhiCoefficients() +
                getNumOmegaCoefficients();
    return getBndConstraints()[_, _(lb, end)];
  }
  [[nodiscard]] auto satPhiCoefs() const -> std::array<PtrMatrix<int64_t>, 2> {
    PtrMatrix<int64_t> phi_coefs_in = getSatPhi1Coefs(),
                       phi_coefs_out = getSatPhi0Coefs();
    if (isForward()) std::swap(phi_coefs_in, phi_coefs_out);
    return {phi_coefs_in, phi_coefs_out};
  }
  [[nodiscard]] auto bndPhiCoefs() const -> std::array<PtrMatrix<int64_t>, 2> {
    PtrMatrix<int64_t> phi_coefs_in = getBndPhi1Coefs(),
                       phi_coefs_out = getBndPhi0Coefs();
    if (isForward()) std::swap(phi_coefs_in, phi_coefs_out);
    return {phi_coefs_in, phi_coefs_out};
  }
  [[nodiscard]] auto isSatisfied(Arena<> alloc,
                                 Valid<const AffineSchedule> schIn,
                                 Valid<const AffineSchedule> schOut) const
    -> bool {
    ptrdiff_t num_loops_in = input()->getCurrentDepth(),
              num_loops_out = output()->getCurrentDepth(),
              num_loops_common = std::min(num_loops_in, num_loops_out),
              num_loops_total = num_loops_in + num_loops_out,
              num_var = num_loops_in + num_loops_out + 2;
    auto [sat, bnd] = depSatBnd();
    invariant(sat->getNumVars(), num_var);
    auto schv = vector(&alloc, num_var, 0z);
    const SquarePtrMatrix<int64_t> in_phi = schIn->getPhi();
    const SquarePtrMatrix<int64_t> out_phi = schOut->getPhi();
    auto in_fus_omega = schIn->getFusionOmega();
    auto out_fus_omega = schOut->getFusionOmega();
    auto in_off_omega = schIn->getOffsetOmega();
    auto out_off_omega = schOut->getOffsetOmega();
    const unsigned num_lambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (ptrdiff_t i = 0; i <= num_loops_common; ++i) {
      if (ptrdiff_t o2idiff = out_fus_omega[i] - in_fus_omega[i])
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
      invariant(i != num_loops_common);
      // forward means offset is 2nd - 1st
      schv[0] = out_off_omega[i];
      schv[1] = in_off_omega[i];
      schv[_(2, 2 + num_loops_in)] << in_phi[last - i, _];
      schv[_(2 + num_loops_in, 2 + num_loops_total)] << out_phi[last - i, _];
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (sat->unSatisfiable(alloc, schv, num_lambda) ||
          bnd->unSatisfiable(alloc, schv, num_lambda)) {
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
    ptrdiff_t num_loops_in = input()->getCurrentDepth(),
              num_loops_out = output()->getCurrentDepth(),
              num_loops_common = std::min(num_loops_in, num_loops_out),
              num_var = num_loops_in + num_loops_out + 2;
    auto [sat, bnd] = depSatBnd();
    invariant(sat->getNumVars(), num_var);
    auto schv = vector(&alloc, num_var, 0z);
    // Vector<int64_t> schv(dependenceSatisfaction->getNumVars(),int64_t(0));
    const unsigned num_lambda = getNumLambda();
    // when i == numLoopsCommon, we've passed the last loop
    for (ptrdiff_t i = 0; i <= num_loops_common; ++i) {
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
      invariant(i != num_loops_common);
      schv[2 + i] = 1;
      schv[2 + num_loops_in + i] = 1;
      // forward means offset is 2nd - 1st
      // dependenceSatisfaction is phi_t - phi_s >= 0
      // dependenceBounding is w + u'N - (phi_t - phi_s) >= 0
      // we implicitly 0-out `w` and `u` here,
      if (sat->unSatisfiable(alloc, schv, num_lambda) ||
          bnd->unSatisfiable(alloc, schv, num_lambda)) {
        // if zerod-out bounding not >= 0, then that means
        // phi_t - phi_s > 0, so the dependence is satisfied
        return false;
      }
      schv[2 + i] = 0;
      schv[2 + num_loops_in + i] = 0;
    }
    return true;
  }
  [[nodiscard]] auto isSatisfied(Arena<> alloc, Valid<const AffineSchedule> sx,
                                 Valid<const AffineSchedule> sy, size_t d) const
    -> bool {
    auto *dep = depPoly();
    unsigned num_lambda = dep->getNumLambda(), n_loop_x = dep->getDim0(),
             n_loop_y = dep->getDim1(), num_loops_total = n_loop_x + n_loop_y;
    MutPtrVector<int64_t> sch{
      math::vector<int64_t>(&alloc, num_loops_total + 2)};
    sch[0] = sx->getOffsetOmega()[d];
    sch[1] = sy->getOffsetOmega()[d];
    sch[_(2, n_loop_x + 2)] << sx->getSchedule(d)[_(end - n_loop_x, end)];
    sch[_(n_loop_x + 2, num_loops_total + 2)]
      << sy->getSchedule(d)[_(end - n_loop_y, end)];
    return depSat()->satisfiable(alloc, sch, num_lambda);
  }
  [[nodiscard]] auto isSatisfied(Arena<> alloc, size_t d) const -> bool {
    auto *dep = depPoly();
    ptrdiff_t num_lambda = dep->getNumLambda(), num_loops_x = dep->getDim0(),
              num_loops_total = num_loops_x + dep->getDim1();
    MutPtrVector<int64_t> sch{
      math::vector<int64_t>(&alloc, num_loops_total + 2)};
    sch << 0;
    invariant(sch.size(), num_loops_total + 2);
    sch[2 + d] = 1;
    sch[2 + d + num_loops_x] = 1;
    return depSat()->satisfiable(alloc, sch, num_lambda);
  }

  DEBUGUSED void dump() const {
    std::cout << input() << " -> " << output()
              << "; SatLevel: " << int(satLevel()) << "\n";
  }

private:
  friend auto operator<<(std::ostream &os, const Dependence &d)
    -> std::ostream & {
    os << "Dependence Poly ";
    if (d.isForward()) os << "x -> y:";
    else os << "y -> x:";
    auto *dep = d.depPoly();
    auto [sat, bnd] = d.depSatBnd();
    os << "\n\tInput:\n" << *d.input();
    os << "\n\tOutput:\n" << *d.output();
    os << "\nA = " << dep->getA() << "\nE = " << dep->getE()
       << "\nSchedule Constraints:" << sat->getConstraints()
       << "\nBounding Constraints:" << bnd->getConstraints();
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
//         A[i,j] = f(A[i+1,j], A[i,j-1], A[j,j], A[j,i], A[i,j-k])
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
  using Tuple = Dependence::Tuple;

  math::ManagedSOA<Tuple> datadeps_{math::length(0)};

public:
  Dependencies() = default;
  Dependencies(ptrdiff_t len) : datadeps_(math::length(len)) {}
  Dependencies(const Dependencies &) noexcept = delete;
  constexpr Dependencies(Dependencies &&) noexcept = default;
  constexpr auto operator=(Dependencies &&other) noexcept -> Dependencies & {
    datadeps_ = std::move(other.datadeps_);
    return *this;
  };

  [[nodiscard]] constexpr auto size() const noexcept -> ptrdiff_t {
    return datadeps_.size();
  }
  constexpr void clear() { datadeps_.clear(); }

private:
  using ID = int32_t;
  struct Dep {
    DepPoly *dep_poly_;
    std::array<math::Simplex *, 2> dep_sat_bnd_;
    IR::Addr *in_;
    IR::Addr *out_;
    ID rev_time_edge_{-1};
    bool is_fwd_;
    bool is_reverse_{false};
  };
  // This is only used for `copyDependencies
  constexpr auto get(ID id, IR::Addr *in, IR::Addr *out) -> Dep {
    Dependence d{get(id)};
    return {.dep_poly_ = d.depPoly(),
            .dep_sat_bnd_ = d.depSatBnd(),
            .in_ = in,
            .out_ = out,
            .rev_time_edge_ = d.revTimeEdge(),
            .is_fwd_ = d.isForward()};
  }

  constexpr auto tup(Dep d, ID i) -> Tuple {
    IR::Addr *out = d.out_, *in = d.in_;
    auto [p_in, p_out] = insertDependencies(in, out, i);
    auto satlevel =
      uint8_t(2 * std::min(in->getCurrentDepth(), out->getCurrentDepth()) - 1);
    uint8_t meta = (Dependence::MetaFlags::Forward * d.is_fwd_) |
                   (Dependence::MetaFlags::ReverseTime * d.is_reverse_);
    return Tuple{out,              // Output
                 in,               // Input
                 d.dep_sat_bnd_,   // Simplex Pair
                 d.dep_poly_,      // DepPoly
                 p_out,            // NextEdgeOut
                 -1,               // PrevEdgeOut
                 p_in,             // NextEdgeIn
                 -1,               // PrevEdgeIn
                 d.rev_time_edge_, // RevTimeEdge
                 {satlevel, 255},  // SatLevel
                 meta,             // Meta
                 255};             // Peel
  }

  /// set(ID i, Dependence d)
  /// stores `d` at index `i`
  /// Dependence `d` is pushed to the fronts of the edgeOut and edgeIn chains.
  // constexpr void set(int32_t i, Dependence d) { datadeps[i] = tup(d, i); }
  // constexpr void set(ID i, Dependence d) { set(i.id, d); }
  auto addEdge(Dep d) -> ID {
    int32_t id{int32_t(datadeps_.size())};
    invariant(id >= 0);

    datadeps_.push_back(tup(d, id));
    return id;
  }

  void addOrdered(Valid<poly::DepPoly> dxy, Valid<IR::Addr> x,
                  Valid<IR::Addr> y, std::array<math::Simplex *, 2> pair,
                  bool isFwd) {
    ptrdiff_t num_lambda = dxy->getNumLambda();
    if (!isFwd) {
      std::swap(pair[0], pair[1]);
      std::swap(x, y);
    }
    pair[0]->truncateVars(1 + num_lambda + dxy->getNumScheduleCoef());
    addEdge(Dep{.dep_poly_ = dxy,
                .dep_sat_bnd_ = pair,
                .in_ = x,
                .out_ = y,
                .is_fwd_ = isFwd});
  }
  void timelessCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                     Valid<IR::Addr> y, std::array<math::Simplex *, 2> pair) {
    invariant(dxy->getTimeDim(), 0);
    addOrdered(dxy, x, y, pair,
               checkDirection(*alloc, pair, x, y, dxy->getNumLambda(),
                              math::col(dxy->getNumVar() + 1)));
  }

  // emplaces dependencies with repeat accesses to the same memory across
  // time
  void timeCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                 Valid<IR::Addr> y, std::array<math::Simplex *, 2> pair) {
    bool is_fwd = checkDirection(
      *alloc, pair, x, y, dxy->getNumLambda(),
      math::col(ptrdiff_t(dxy->getA().numCol()) - dxy->getTimeDim()));
    timeCheck(alloc, dxy, x, y, pair, is_fwd);
  }
  static void timeStep(Valid<DepPoly> dxy, MutPtrMatrix<int64_t> fE,
                       MutPtrMatrix<int64_t> sE,
                       ptrdiff_t numInequalityConstraintsOld,
                       ptrdiff_t numEqualityConstraintsOld, ptrdiff_t ineqEnd,
                       ptrdiff_t posEqEnd, ptrdiff_t v, ptrdiff_t step) {
    for (ptrdiff_t c = 0; c < numInequalityConstraintsOld; ++c) {
      int64_t Acv = dxy->getA(math::row(c), math::col(v));
      if (!Acv) continue;
      Acv *= step;
      dxy->getA(math::row(c), Col<>{}) -= Acv;
      fE[0, c + 1] -= Acv; // *1
      sE[0, c + 1] -= Acv; // *1
    }
    for (ptrdiff_t c = 0; c < numEqualityConstraintsOld; ++c) {
      // each of these actually represents 2 inds
      int64_t Ecv = dxy->getE(math::row(c), math::col(v));
      if (!Ecv) continue;
      Ecv *= step;
      dxy->getE(math::row(c), Col<>{}) -= Ecv;
      fE[0, c + ineqEnd] -= Ecv;
      fE[0, c + posEqEnd] += Ecv;
      sE[0, c + ineqEnd] -= Ecv;
      sE[0, c + posEqEnd] += Ecv;
    }
  }
  void timeCheck(Arena<> *alloc, Valid<DepPoly> dxy, Valid<IR::Addr> x,
                 Valid<IR::Addr> y, std::array<math::Simplex *, 2> pair,
                 bool isFwd) {
    const int num_inequality_constraints_old =
                dxy->getNumInequalityConstraints(),
              num_equality_constraints_old = dxy->getNumEqualityConstraints(),
              ineq_end = 1 + num_inequality_constraints_old,
              pos_eq_end = ineq_end + num_equality_constraints_old,
              num_lambda = pos_eq_end + num_equality_constraints_old,
              num_schedule_coefs = dxy->getNumScheduleCoef();
    invariant(num_lambda, dxy->getNumLambda());
    // copy backup
    std::array<math::Simplex *, 2> farkas_backups{pair[0]->copy(alloc),
                                                  pair[1]->copy(alloc)};
    Valid<IR::Addr> in = x, out = y;
    if (isFwd) {
      std::swap(farkas_backups[0], farkas_backups[1]);
    } else {
      std::swap(in, out);
      std::swap(pair[0], pair[1]);
    }
    pair[0]->truncateVars(1 + num_lambda + num_schedule_coefs);
    Dep dep0{.dep_poly_ = dxy,
             .dep_sat_bnd_ = pair,
             .in_ = in,
             .out_ = out,
             .is_fwd_ = isFwd};
    ID d0_id{addEdge(dep0)}, prev_id = d0_id;
    invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
              ptrdiff_t(get(d0_id).getNumPhiCoefficients()));
    // pair is invalid
    const ptrdiff_t time_dim = dxy->getTimeDim(),
                    num_var = 1 + dxy->getNumVar() - time_dim;
    invariant(time_dim > 0);
    // 1 + because we're indexing into A and E, ignoring the constants
    // remove the time dims from the deps
    // dep0.depPoly->truncateVars(numVar);

    // dep0.depPoly->setTimeDim(0);
    invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
              ptrdiff_t(get(d0_id).getNumPhiCoefficients()));
    DepPoly *olddp = dxy;
    // now we need to check the time direction for all times
    // anything approaching 16 time dimensions would be insane
    for (ptrdiff_t t = 0;;) {
      // set `t`th timeDim to +1/-1
      // basically, what we do here is set it to `step` and pretend it was
      // a constant. so a value of c = a'x + t*step -> c - t*step = a'x so
      // we update the constant `c` via `c -= t*step`.
      // we have the problem that.
      int64_t step = olddp->getNullStep(t);
      ptrdiff_t v = num_var + t;
      bool repeat = (++t < time_dim);
      std::array<math::Simplex *, 2> fp{farkas_backups};
      dxy = olddp->copy(alloc);
      if (repeat) {
        fp[0] = fp[0]->copy(alloc);
        fp[1] = fp[1]->copy(alloc);
      }
      // set (or unset) for this timedim
      auto fE{fp[0]->getConstraints()[_, _(1, end)]};
      auto sE{fp[1]->getConstraints()[_, _(1, end)]};
      timeStep(dxy, fE, sE, num_inequality_constraints_old,
               num_equality_constraints_old, ineq_end, pos_eq_end, v, step);
      // checkDirection should be `true`, so if `false` we flip the sign
      // this is because `isFwd = checkDirection` of the original
      // `if (isFwd)`, we swapped farkasBackups args, making the result
      // `false`; for our timeDim to capture the opposite movement
      // through time, we thus need to flip it back to `true`.
      // `if (!isFwd)`, i.e. the `else` branch above, we don't flip the
      // args, so it'd still return `false` and a flip would still mean `true`.
      if (!checkDirection(
            *alloc, fp, out, in, num_lambda,
            math::col(ptrdiff_t(dxy->getA().numCol()) - dxy->getTimeDim())))
        timeStep(dxy, fE, sE, num_inequality_constraints_old,
                 num_equality_constraints_old, ineq_end, pos_eq_end, v,
                 -2 * step);

      fp[0]->truncateVars(1 + num_lambda + num_schedule_coefs);
      Dep dep1{.dep_poly_ = dxy,
               .dep_sat_bnd_ = farkas_backups,
               .in_ = out,
               .out_ = in,
               .rev_time_edge_ = prev_id,
               .is_fwd_ = !isFwd,
               .is_reverse_ = true};
      prev_id = addEdge(dep1);
      invariant(ptrdiff_t(out->getCurrentDepth()) + in->getCurrentDepth(),
                ptrdiff_t(get(prev_id).getNumPhiCoefficients()));
      if (!repeat) break;
    }
    get(d0_id).revTimeEdge() = prev_id;
    invariant(olddp == get(d0_id).depPoly());
  }
  static auto checkDirection(Arena<> alloc,
                             const std::array<math::Simplex *, 2> &p,
                             Valid<const IR::Addr> x, Valid<const IR::Addr> y,
                             Valid<const AffineSchedule> xSchedule,
                             Valid<const AffineSchedule> ySchedule,
                             ptrdiff_t numLambda, Col<> nonTimeDim) -> bool {
    const auto &[fxy, fyx] = p;
    unsigned num_loops_x = x->getCurrentDepth(),
             num_loops_y = y->getCurrentDepth(),
             num_loops_total = num_loops_x + num_loops_y;
#ifndef NDEBUG
    unsigned num_loops_common = std::min(num_loops_x, num_loops_y);
#endif
    SquarePtrMatrix<int64_t> x_phi = xSchedule->getPhi();
    SquarePtrMatrix<int64_t> y_phi = ySchedule->getPhi();
    PtrVector<int64_t> x_off_omega = xSchedule->getOffsetOmega();
    PtrVector<int64_t> y_off_omega = ySchedule->getOffsetOmega();
    PtrVector<int64_t> x_fus_omega = xSchedule->getFusionOmega();
    PtrVector<int64_t> y_fus_omega = ySchedule->getFusionOmega();
    MutPtrVector<int64_t> sch{
      math::vector<int64_t>(&alloc, num_loops_total + 2)};
    // i iterates from outer-most to inner most common loop
    for (ptrdiff_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (y_fus_omega[i] != x_fus_omega[i])
        return y_fus_omega[i] > x_fus_omega[i];
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
      assert(i != num_loops_common);
      sch[0] = x_off_omega[i];
      sch[1] = y_off_omega[i];
      sch[_(2, 2 + num_loops_x)] << x_phi[last - i, _];
      sch[_(2 + num_loops_x, 2 + num_loops_total)] << y_phi[last - i, _];
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
                             const std::array<math::Simplex *, 2> &p,
                             Valid<const IR::Addr> x, Valid<const IR::Addr> y,
                             ptrdiff_t numLambda, Col<> nonTimeDim) -> bool {
    const auto [fxy, fyx] = p;
    unsigned num_loops_x = x->getCurrentDepth(), nTD = ptrdiff_t(nonTimeDim);
#ifndef NDEBUG
    ptrdiff_t num_loops_common =
      std::min(ptrdiff_t(num_loops_x), ptrdiff_t(y->getCurrentDepth()));
#endif
    PtrVector<int64_t> x_fus_omega = x->getFusionOmega();
    PtrVector<int64_t> y_fus_omega = y->getFusionOmega();
    // i iterates from outer-most to inner most common loop
    for (ptrdiff_t i = 0; /*i <= numLoopsCommon*/; ++i) {
      if (y_fus_omega[i] != x_fus_omega[i])
        return y_fus_omega[i] > x_fus_omega[i];
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
      invariant(i < num_loops_common);
      std::array<ptrdiff_t, 2> inds{2 + i, 2 + i + num_loops_x};
      if (fxy->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) {
        assert(!fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD));
        return false;
      }
      if (fyx->unSatisfiableZeroRem(alloc, numLambda, inds, nTD)) return true;
    }
    invariant(false);
    return false;
  }
  // constexpr auto get(ID i, IR::Addr *in, IR::Addr *out) const -> Dependence {
  //   auto [depSat, depBnd] = depSatBnd(i);
  //   return Dependence{.depPoly = depPoly(i),
  //                     .dependenceSatisfaction = depSat,
  //                     .dependenceBounding = depBnd,
  //                     .in = in,
  //                     .out = out,
  //                     .satLvl = satLevelPair(i),
  //                     .meta = getMeta(i)

  //   };
  // }
  /// returns the innermost loop with a non-zero index
  static auto innermostNonZero(PtrMatrix<int64_t> A, ptrdiff_t skip)
    -> ptrdiff_t {
    for (ptrdiff_t i = ptrdiff_t(A.numCol()); --i;) {
      if (i == skip) continue;
      if (!math::allZero(A[_, i])) return i;
    }
    return -1;
  }
  /// this function essentially indicates that this dependency does not
  /// prevent the hoisting of a memory access out of a loop, because a
  /// memory->register transform is possible. The requirements are that the
  /// `indexMatrix` match
  struct Getter {
    Dependencies *d_;
    constexpr auto operator()(int32_t id) const -> Dependence {
      return d_->get(id);
    }
  };
  struct ActiveCheck {
    Dependencies *d_;
    int depth0_;
    constexpr auto operator()(int32_t id) const -> bool {
      return d_->get(id).isActive(depth0_);
    }
  };

public:
  constexpr void removeEdge(ID id) {
    removeOutEdge(id);
    removeInEdge(id);
    /// TODO: remove revTimeEdge?
  }
  constexpr void removeEdge(ID id, IR::Addr *in, IR::Addr *out) {
    // in -id-> out
    if (in && in->getEdgeOut() == id) in->setEdgeOut(outEdges()[id]);
    if (out && out->getEdgeIn() == id) out->setEdgeIn(inEdges()[id]);
    removeEdge(id);
  }
  constexpr void removeOutEdge(int32_t id) {
    int32_t prev = get(id).prevOut();
    int32_t next = get(id).nextOut();
    if (prev >= 0) get(prev).nextOut() = next;
    if (next >= 0) get(next).prevOut() = prev;
  }
  constexpr void removeInEdge(int32_t id) {
    int32_t prev = get(id).prevIn();
    int32_t next = get(id).nextIn();
    if (prev >= 0) get(prev).nextIn() = next;
    if (next >= 0) get(next).prevIn() = prev;
  }
  [[nodiscard]] constexpr auto operator[](ID i) -> Dependence {
    return {datadeps_, i};
  }
  /// Like `operator[]`, but maybe nicer to use with pointers?
  [[nodiscard]] constexpr auto get(ID i) -> Dependence {
    return {datadeps_, i};
    // return get(i, input(i), output(i));
  }
  void check(Valid<Arena<>> alloc, Valid<IR::Addr> x, Valid<IR::Addr> y) {
    if (x->getArrayPointer() != y->getArrayPointer()) return;
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
    std::array<math::Simplex *, 2> pair(dxy->farkasPair(alloc));
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, x, y, pair);
    else timelessCheck(alloc, dxy, x, y, pair);
  }
  // reload store `x`
  auto reload(Arena<> *alloc, Valid<IR::Addr> store) -> Valid<IR::Addr> {
    Valid<DepPoly> dxy{DepPoly::self(alloc, store)};
    std::array<math::Simplex *, 2> pair(dxy->farkasPair(alloc));
    Valid<IR::Addr> load = store->reload(alloc);
    copyDependencies(store, load);
    if (dxy->getTimeDim()) timeCheck(alloc, dxy, store, load, pair, true);
    else addOrdered(dxy, store, load, pair, true);
    return load;
  }
  constexpr auto outEdges() -> MutPtrVector<int32_t> {
    return datadeps_.template get<Dependence::NextEdgeOutI>();
  }
  constexpr auto inEdges() -> MutPtrVector<int32_t> {
    return datadeps_.template get<Dependence::NextEdgeInI>();
  }
  [[nodiscard]] constexpr auto outEdges() const -> PtrVector<int32_t> {
    return datadeps_.template get<Dependence::NextEdgeOutI>();
  }
  [[nodiscard]] constexpr auto inEdges() const -> PtrVector<int32_t> {
    return datadeps_.template get<Dependence::NextEdgeInI>();
  }
  [[nodiscard]] constexpr auto output(ptrdiff_t id) -> IR::Addr *& {
    return datadeps_.template get<Dependence::OutI>()[id];
  }
  [[nodiscard]] constexpr auto input(ptrdiff_t id) -> IR::Addr *& {
    return datadeps_.template get<Dependence::InI>()[id];
  }
  [[nodiscard]] constexpr auto output(ptrdiff_t id) const -> IR::Addr * {
    return datadeps_.template get<Dependence::OutI>()[id];
  }
  [[nodiscard]] constexpr auto input(ptrdiff_t id) const -> IR::Addr * {
    return datadeps_.template get<Dependence::InI>()[id];
  }
  [[nodiscard]] constexpr auto inputEdgeIDs(int32_t id) const
    -> utils::VForwardRange {
    return utils::VForwardRange{inEdges(), id};
  }
  [[nodiscard]] constexpr auto outputEdgeIDs(int32_t id) const
    -> utils::VForwardRange {
    return utils::VForwardRange{outEdges(), id};
  }
  [[nodiscard]] constexpr auto getEdgeTransform() {
    // auto f = [=, this](int32_t id) -> Dependence {
    //   return get(Dependence::ID{id});
    // };
    auto f = Getter{this};
    static_assert(sizeof(decltype(f)) == sizeof(intptr_t));

    static_assert(std::is_trivially_copyable_v<decltype(f)>);
    // static_assert(
    //   std::is_trivially_copyable_v<decltype(std::views::transform(f))>);
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto inputEdges(int32_t id) {
    return inputEdgeIDs(id) | getEdgeTransform();
  }
  [[nodiscard]] constexpr auto outputEdges(int32_t id) {
    return outputEdgeIDs(id) | getEdgeTransform();
  }

  [[nodiscard]] constexpr auto activeFilter(int depth0) {
    return std::views::filter(ActiveCheck{this, depth0});
    // auto f = [=, this](int32_t id) -> bool {
    //   return isActive(Dependence::ID{id}, depth);
    // };
    // return std::views::filter(f);
  }
  [[nodiscard]] constexpr auto inputAddrTransform() {
    auto f = [=, this](int32_t id) -> IR::Addr * { return get(id).input(); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto outputAddrTransform() {
    auto f = [=, this](int32_t id) -> IR::Addr * { return get(id).output(); };
    return std::views::transform(f);
  }
  [[nodiscard]] constexpr auto getMeta(ID id) const -> uint8_t {
    return datadeps_.template get<Dependence::GetMetaI>(id);
  }
  [[nodiscard]] constexpr auto registerEligible(ID id) const -> bool {
    return getMeta(id) & Dependence::MetaFlags::RegisterEligible;
  }
  [[nodiscard]] constexpr auto registerEligibleFilter() const {
    auto f = [=, this](int32_t id) -> bool { return !registerEligible(id); };
    return std::views::filter(f);
  }
  constexpr auto insertDependencies(IR::Addr *in, IR::Addr *out, int32_t idx)
    -> std::array<ID, 2> {
    ID p_out = in->getEdgeOut(), p_in = out->getEdgeIn();
    if (p_out >= 0) Dependence{datadeps_, p_out}.prevOut() = idx;
    if (p_in >= 0) Dependence{datadeps_, p_in}.prevIn() = idx;
    in->setEdgeOut(idx);
    out->setEdgeIn(idx);
    return {p_in, p_out};
  }
  // assumes `insertids` are already present within deps, but that we have
  // called `removeEdge(id)`
  auto insertDependencies(MutPtrVector<int32_t> insertids) -> int {
    int inserted = 0;
    for (int32_t idx : insertids) {
      IR::Addr *in = input(idx), *out = output(idx);
      // FIXME: these dependencies should have been updated!
      // if (in->wasDropped() || out->wasDropped()) continue;
      invariant(!in->wasDropped() && !out->wasDropped());
      insertDependencies(in, out, idx);
      ++inserted;
    }
    return inserted;
  }
  // returns an `Optional`.
  // The optional is empty if the dependence cannot be reordered due to peeling,
  // otherwise, it contains the index of the loop to peel.
  // How would we capture
  // dependencies/uses like
  //
  //     int64_t x = 0;
  //     for (ptrdiff_t m = 0; m < M; ++m){
  //       x += a[m];
  //       b[m] = x;
  //     }
  //
  // we have `x +=` as a reassociable self-dependence, but the fact it is stored
  // into `b[m]` means that we can't really reassociate, as each nominal
  // intermediate value of `x` must be realized!
  // We must check that there are no other reads. Note that this is represented
  // as
  //
  //     int64_t x[1]{};
  //     for (ptrdiff_t m = 0; m < M; ++m){
  //       x[0] = x[0] + a[m];
  //       b[m] = x[0];
  //     }
  //
  // So we have write->read dependence for the store `x[0] =` to the read in
  // `b[m] = x[0]`. The key observation here is that `x[0]` has a time
  // component; the violation occurs because we store in another location,
  // providing a non-reassociable component.
  auto determinePeelDepth(IR::Loop *L, ID id) -> utils::Optional<size_t> {
    Dependence dep{get(id)};
    IR::Addr *in = dep.input(), *out = dep.output();
    // clang-format off
    // If we have a dependency nested inside `L`, we won't be able to reorder if
    //    either:
    // a) that dependency's output is `in`
    // b) that dependency's input is `out`
    //    as we'd then have to maintain the order of this loop level's
    //    evaluations with respect to the subloop.
    // Otherwise, we check
    // 1. If this dependency may be peeled. For this, it must
    //   a) be indexed by both `L` and a subloop of `L`.
    //   b) have an equality relation, so that it occurs for a single iteration
    //      of the subloop. Then, we can split the subloop across this value,
    //      scalarizing around it.
    // 2. Is this dependency reassociable? E.g., if it's connected by
    //    reassociable adds (such as integer adds, or floating point with the
    //    reassociable FMF), then mark it as such.
    // clang-format on
    //
    // if (anyInteriorDependencies(L, in) || anyInteriorDependents(L, out))
    //   return false;
    // no inner dependence
    // FIXME: handle force-scalarization in cost-modeling; how to clearly
    //        forward instructions to codegen?
    //        Basically, if `dep.getPeel() == i`, that means we need to peel
    //        loop `i` when it equals `L`.
    //        These get stored in `L->getLegality()`, as a flag of all loops
    //        that must be peeled when equal to this loop.
    PtrMatrix<int64_t> iIdx = in->indexMatrix(), oIdx = out->indexMatrix();
    invariant(iIdx.numRow(), oIdx.numRow());
    ptrdiff_t d0 = L->getCurrentDepth() - 1;
    // invariant(iIdx.numCol() >= d0);
    bool noInIndAtDepth = d0 >= iIdx.numCol() || math::allZero(iIdx[_, d0]),
         noOutIndAtDepth = d0 >= oIdx.numCol() || math::allZero(oIdx[_, d0]);
    if (noInIndAtDepth == noOutIndAtDepth) return -1;
    // now, we want to find a loop that `in` depends on but `out` does not
    // so that we can split over this loop.
    // For now, to simplify codegen, we only accept the innermost non-zero
    ptrdiff_t i = innermostNonZero(noInIndAtDepth ? iIdx : oIdx, d0);
    if (i >= 0) dep.getPeel() = i;
    return i >= 0 ? utils::Optional<size_t>{size_t(i)}
                  : utils::Optional<size_t>{};
  }
  DEBUGUSED void dump() {
    for (int i = 0; i < size(); ++i) get(i).dump();
  }
  // inputAddr -> inputEdge -> this
  inline auto inputEdges(const IR::Addr *A) {
    return inputEdges(A->getEdgeIn());
  }
  // this -> outputEdge -> outputAddr

  inline auto outputEdges(const IR::Addr *A) {
    return outputEdges(A->getEdgeOut());
  }
  // inputAddr -> inputEdge -> this
  inline auto inputEdgeIDs(const IR::Addr *A) -> utils::VForwardRange {
    return inputEdgeIDs(A->getEdgeIn());
  }
  // this -> outputEdge -> outputAddr
  inline auto outputEdgeIDs(const IR::Addr *A) -> utils::VForwardRange {
    return outputEdgeIDs(A->getEdgeOut());
  }
  // inputAddr -> inputEdge -> this
  inline auto inputEdgeIDs(const IR::Addr *A, int depth0) {
    return inputEdgeIDs(A) | activeFilter(depth0);
  }
  // this -> outputEdge -> outputAddr
  inline auto outputEdgeIDs(const IR::Addr *A, int depth0) {
    return outputEdgeIDs(A) | activeFilter(depth0);
  }

  // inputAddr -> inputEdge -> this
  inline auto inputEdges(const IR::Addr *A, int depth0) {
    return inputEdgeIDs(A, depth0) | getEdgeTransform();
  }
  // this -> outputEdge -> outputAddr
  inline auto outputEdges(const IR::Addr *A, int depth0) {
    return outputEdgeIDs(A, depth0) | getEdgeTransform();
  }
  // inputAddr -> inputEdge -> this
  inline auto inputAddrs(const IR::Addr *A) {
    return inputEdgeIDs(A) | inputAddrTransform();
  }
  // inputAddr -> inputEdge -> this
  inline auto inputAddrs(const IR::Addr *A, int depth0) {
    return inputEdgeIDs(A, depth0) | inputAddrTransform();
  }
  // this -> outputEdge -> outputAddr
  inline auto outputAddrs(const IR::Addr *A) {
    return outputEdgeIDs(A) | outputAddrTransform();
  }
  // this -> outputEdge -> outputAddr
  inline auto outputAddrs(const IR::Addr *A, int depth0) {
    return outputEdgeIDs(A, depth0) | outputAddrTransform();
  }
  inline auto unhoistableOutputs(const IR::Addr *A, int depth0) {
    return outputEdgeIDs(A, depth0) | registerEligibleFilter() |
           outputAddrTransform();
  }
  inline void copyDependencies(IR::Addr *src, IR::Addr *dst) {
    for (int32_t id : inputEdgeIDs(src)) {
      Dependence old{get(id)};
      IR::Addr *input = old.input();
      if (input->isLoad()) continue;
      int32_t nid = addEdge(get(id, input, dst));
      if (int32_t &rt = old.revTimeEdge(); rt >= 0) rt = nid;
    }
    for (int32_t id : outputEdgeIDs(src)) {
      Dependence old{get(id)};
      IR::Addr *output = old.output();
      if (output->isLoad()) continue;
      int32_t nid = addEdge(get(id, dst, output));
      if (int32_t &rt = old.revTimeEdge(); rt >= 0) rt = nid;
    }
  }
}; // class Dependencies

} // namespace poly
#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using poly::Dependencies;

// assumes `id_set` is an id-based partition, e.g.
//                       0,  1, 2, 3,  4, 5, 6,  7, 8,  9,10, 11, 12
// Vector of size: 13 = {5, -1, 1, 7, 12, 6, 8, 11, 2, -1, 9, -1, 0}
// we have the following lists:
// 10, 9
// 4, 12, 0, 5, 6, 8, 2, 1
// 3, 7, 11
// If we were to `rmidx = 5`, then we would need to update the second list
// 4, 12, 0, 6, 8, 2, 1
// i.e., `0` would have to point to 6 instead of 5
// If we were to `rmidx = 4`, we need to update the loop.
// We do that later in `dropDroppedDependencies`.
// For example, with `rmidx=4`, we fail to find it.
// The loop can check that the 4th was dropped, and then update
// its own edge to 12.
// Alternatively, with `rmidx=5`, `5` is left pointing to `6`,
// while `0` is updated to point to `6`, skipping over `5`.
// If from here, we also `rmidx=0`
//                       0,  1, 2, 3,  4, 5, 6,  7, 8,  9,10, 11, 12
// Vector of size: 13 = {6, -1, 1, 7, 12, 6, 8, 11, 2, -1, 9, -1, 6}
// `rmidx=4`, no update, as it is not present. And `rmidx=12`...
//                       0,  1, 2, 3, 4, 5, 6,  7, 8,  9,10, 11, 12
// Vector of size: 13 = {5, -1, 1, 7, 6, 6, 8, 11, 2, -1, 9, -1, 6}
// So now, loop->getEdge() returns `4`, which was dropped. We
// thus immediately follow it to `6`.
constexpr void removeEdge(MutPtrVector<int32_t> id_set, int32_t rmidx) {
  int32_t *f = std::ranges::find_if(
    id_set, [=](int32_t idx) -> bool { return idx == rmidx; });
  // int32_t next = id_set[rmidx];
  // if (L->getEdge() == rmidx) L->setEdge(next);
  if (f != id_set.end()) *f = id_set[rmidx];
}

using math::StridedVector;
} // namespace IR
