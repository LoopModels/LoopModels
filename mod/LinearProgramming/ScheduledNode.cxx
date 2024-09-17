#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/Support/Casting.h>
#include <ranges>

#ifndef USE_MODULE
#include "IR/Node.cxx"
#include "Polyhedra/Dependence.cxx"
#include "Polyhedra/Schedule.cxx"
#include "IR/Address.cxx"
#include "Utilities/Valid.cxx"
#include "Math/Simplex.cxx"
#include "Graphs/Graphs.cxx"
#include "Utilities/Optional.cxx"
#include "Utilities/ListRanges.cxx"
#include "Utilities/Invariant.cxx"
#include "Math/Constructors.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#else
export module IR:ScheduledNode;
import Arena;
import Array;
import ArrayConstructors;
import Invariant;
import ListRange;
import Optional;
import PtrGraph;
import Simplex;
import Valid;
import :Address;
import :AffineSchedule;
import :Dependence;
import :Node;
#endif

using IR::Addr, IR::Value, IR::Instruction, IR::Load, IR::Stow;
using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::MutSquarePtrMatrix,
  math::end, math::last, math::_, math::Simplex;
using poly::Dependence, poly::DepPoly;
using utils::Valid, utils::invariant, utils::Optional, alloc::Arena;
#ifdef USE_MODULE
export namespace lp {
#else
namespace lp {
#endif

/// ScheduledNode
/// Represents a set of memory accesses that are optimized together in the LP.
/// These instructions are all connected directly by through registers.
/// E.g., `A[i] = B[i] + C[i]` is a single node
/// because we load from `B[i]` and `C[i]` into registers, compute, and
/// `A[i]`;
/// When splitting LoopBlock graphs, these graphs will have edges between
/// them that we drop. This is only a problem if we merge graphs later.
///
/// Addr chains here are formed using `next`
class ScheduledNode {

  Valid<Addr> store; // linked list to loads, iterate over getNext
  Valid<poly::Loop> loopNest;
  ScheduledNode *next{nullptr};
  ScheduledNode *component{nullptr}; // SCC cycle, or last node in a chain
  // Dependence *dep{nullptr};          // input edges (points to parents)
  int64_t *offsets{nullptr};
  uint32_t phiOffset{0}, omegaOffset{0}; // used in LoopBlock
  uint16_t index_, lowLink_;
  uint8_t rank{0};
  bool visited_{false};
  bool onStack_{false};
  ScheduledNode *originalNext{nullptr};
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  int64_t mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

  [[nodiscard]] constexpr auto getNumLoopsSquared() const -> ptrdiff_t {
    ptrdiff_t L = getNumLoops();
    return L * L;
  }
  constexpr ScheduledNode(Addr *write, poly::Loop *L)
    : store(write), loopNest(L) {
    mem[0] = L->getNumLoops();
    getFusionOmega() << 0;
  }

public:
  constexpr auto index() -> uint16_t & { return index_; }
  constexpr auto lowLink() -> uint16_t & { return lowLink_; }
  [[nodiscard]] constexpr auto onStack() const -> bool { return onStack_; }
  constexpr void addToStack() { onStack_ = true; }
  constexpr void removeFromStack() { onStack_ = false; }
  [[nodiscard]] constexpr auto visited() const -> bool { return visited_; }
  constexpr void visit() { visited_ = true; }
  constexpr auto unVisit() { visited_ = false; }
  constexpr auto setNext(ScheduledNode *n) -> ScheduledNode * {
    next = n;
    return this;
  }
  constexpr auto setOrigNext(ScheduledNode *n) -> ScheduledNode * {
    originalNext = next = n;
    return this;
  }
  static auto construct(Arena<> *alloc, Addr *store,
                        poly::Loop *L) -> ScheduledNode * {
    ptrdiff_t memNeeded = poly::requiredScheduleStorage(L->getNumLoops());
    void *p =
      alloc->allocate(sizeof(ScheduledNode) + memNeeded * sizeof(int64_t));
    auto *n = new (p) ScheduledNode(store, L);
    for (IR::Node *v : store->nodes()) llvm::cast<Addr>(v)->setNode(n);
    return n;
  }
  [[nodiscard]] constexpr auto getNext() -> ScheduledNode * { return next; }
  [[nodiscard]] constexpr auto getNext() const -> const ScheduledNode * {
    return next;
  }

  /// fuse; difference between `setNext` is that this assumes both have `next`s
  /// Note, this is expensive: O(N) in size, because we don't keep an `end`...
  constexpr auto fuse(ScheduledNode *n) -> ScheduledNode * {
    while (true) {
      ScheduledNode *ns = n->getNext();
      if (ns == nullptr) break;
      n = ns;
    }
    return n->setNext(this);
  }

  constexpr auto getNextComponent() -> ScheduledNode * { return component; }
  [[nodiscard]] constexpr auto
  getNextComponent() const -> const ScheduledNode * {
    return component;
  }
  constexpr auto setNextComponent(ScheduledNode *n) -> ScheduledNode * {
    component = n;
    return this;
  }
  constexpr auto getLoopOffsets() -> MutPtrVector<int64_t> {
    return {offsets, math::length(getNumLoops())};
  }
  constexpr void setOffsets(int64_t *o) { offsets = o; }
  struct NextAddr {
    auto operator()(Addr *a) const -> Addr * {
      return llvm::cast_or_null<Addr>(a->getNext());
    }
    auto operator()(const Addr *a) const -> const Addr * {
      return llvm::cast_or_null<Addr>(a->getNext());
    }
  };
  struct Component {
    constexpr auto operator()(ScheduledNode *n) const -> ScheduledNode * {
      return n->getNextComponent();
    }
    constexpr auto
    operator()(const ScheduledNode *n) const -> const ScheduledNode * {
      return n->getNextComponent();
    }
  };

  [[nodiscard]] constexpr auto getStore() -> Addr * { return store; }
  [[nodiscard]] constexpr auto getStore() const -> const Addr * {
    return store;
  }
  [[nodiscard]] constexpr auto getVertices()
    -> utils::ListRange<ScheduledNode, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }
  [[nodiscard]] constexpr auto getVertices() const
    -> utils::ListRange<const ScheduledNode, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }
  constexpr auto getOrigNext() -> ScheduledNode * { return originalNext; }
  struct OrigNext {
    static constexpr auto operator()(ScheduledNode *n) -> ScheduledNode * {
      return n->getOrigNext();
    }
  };
  [[nodiscard]] constexpr auto getAllVertices() {
    return utils::ListRange{this, OrigNext{}};
    // return utils::ListRange{this, [](ScheduledNode *n) -> ScheduledNode * {
    //                           return n->getOrigNext();
    //                         }};
  }
  [[nodiscard]] constexpr auto getComponents()
    -> utils::ListRange<ScheduledNode, Component, utils::Identity> {
    return utils::ListRange{this, Component{}};
  }
  [[nodiscard]] constexpr auto getComponents() const
    -> utils::ListRange<const ScheduledNode, Component, utils::Identity> {
    return utils::ListRange{this, Component{}};
  }
  // convention: `local` means only for this node
  // `each` for all connected nodes
  // range of `Addr` for this node
  [[nodiscard]] constexpr auto localAddr() {
    return utils::ListRange{(Addr *)store, NextAddr{}};
  }
  [[nodiscard]] constexpr auto localAddr() const {
    return utils::ListRange{(const Addr *)store, NextAddr{}};
  }
  struct GetStore {
    static constexpr auto operator()(ScheduledNode *n) -> Addr * {
      return n->getStore();
    }
  };
  struct NextAddrRange {
    static constexpr auto
    operator()(Addr *A) -> utils::ListRange<Addr, NextAddr, utils::Identity> {
      return {llvm::cast<Addr>(A->getNext()), NextAddr{}};
    }
  };
  // range of all `Addr` for the list starting with this node
  [[nodiscard]] constexpr auto eachAddr() {
    return utils::NestedList{
      utils::ListRange{this, utils::GetNext{}, GetStore{}}, NextAddrRange{}};
    // [](ScheduledNode *n) -> Addr * { return n->getStore(); }},
    // [](Addr *a) -> utils::ListRange<Addr, NextAddr, utils::Identity> {
    //   return utils::ListRange{llvm::cast<Addr>(a->getNext()), NextAddr{}};
    // }};
  }
  template <bool Out> struct GetEdge {
    constexpr auto operator()(const Addr *a) const -> int32_t {
      if constexpr (Out) return a->getEdgeOut();
      else return a->getEdgeIn();
    }
  };
  template <bool Out> struct Deps {
    poly::Dependencies *dep;

    constexpr auto operator()(int32_t id) const {
      if constexpr (Out)
        return dep->outputEdgeIDs(id) | std::views::transform(OutNode{dep});
      else return dep->inputEdgeIDs(id) | std::views::transform(InNode{dep});
    }
    constexpr auto operator()(IR::Addr *a) const {
      if constexpr (Out) return (*this)(a->getEdgeOut());
      else return (*this)(a->getEdgeIn());
    }
  };
  template <bool Out> struct DepIDs {
    poly::Dependencies *dep;

    constexpr auto operator()(int32_t id) const {
      if constexpr (Out) return dep->outputEdgeIDs(id);
      else return dep->inputEdgeIDs(id);
    }
    constexpr auto operator()(IR::Addr *a) const {
      if constexpr (Out) return (*this)(a->getEdgeOut());
      else return (*this)(a->getEdgeIn());
    }
  };
  template <bool Out> struct DepFilter {
    poly::Dependencies *dep;
    unsigned depth;

    constexpr auto operator()(int32_t id) const {
      if constexpr (Out)
        return dep->outputEdgeIDs(id) | dep->activeFilter(depth) |
               std::views::transform(OutNode{dep});
      else
        return dep->inputEdgeIDs(id) | dep->activeFilter(depth) |
               std::views::transform(InNode{dep});
    }
    constexpr auto operator()(IR::Addr *a) const {
      if constexpr (Out) return (*this)(a->getEdgeOut());
      else return (*this)(a->getEdgeIn());
    }
  };

  // all nodes that are memory inputs to this one; i.e. all parents
  // NOTE: we may reach each node multiple times
  [[nodiscard]] inline auto inNeighbors(poly::Dependencies &dep) {
    return utils::NestedList{utils::ListRange{store, NextAddr{}},
                             Deps<false>{&dep}};
  }
  // all nodes that are memory inputs to this one; i.e. all parents
  // NOTE: we may reach each node multiple times

  // all nodes that are memory outputs of this one; i.e. all children
  // NOTE: we may reach each node multiple times
  [[nodiscard]] inline auto outNeighbors(poly::Dependencies &dep) {
    return utils::NestedList{utils::ListRange{store, NextAddr{}},
                             Deps<true>{&dep}};
  }
  [[nodiscard]] inline auto inputEdgeIds(poly::Dependencies &dep) const {
    return utils::NestedList{utils::ListRange{store, NextAddr{}},
                             DepIDs<false>{&dep}};
  }
  [[nodiscard]] inline auto outputEdgeIds(poly::Dependencies &dep) const {
    return utils::NestedList{utils::ListRange{store, NextAddr{}},
                             DepIDs<true>{&dep}};
  }
  [[nodiscard]] inline auto inputEdgeIds(poly::Dependencies &dep,
                                         int depth) const {
    static_assert(std::forward_iterator<
                  decltype(DepIDs<false>{&dep}((IR::Addr *)nullptr).begin())>);
    static_assert(std::forward_iterator<decltype(utils::ListRange{
                    store, NextAddr{}}.begin())>);
    static_assert(std::forward_iterator<decltype(inputEdgeIds(dep).begin())>);
    return inputEdgeIds(dep) | dep.activeFilter(depth);
  }
  [[nodiscard]] inline auto outputEdgeIds(poly::Dependencies dep,
                                          int depth) const {
    static_assert(std::forward_iterator<decltype(outputEdgeIds(dep).begin())>);

    static_assert(std::ranges::range<decltype(outputEdgeIds(dep))>);
    return outputEdgeIds(dep) | dep.activeFilter(depth);
  }

  [[nodiscard]] inline auto inputEdges(poly::Dependencies &dep) {
    poly::Dependencies *d = &dep;
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> int32_t { return a->getEdgeIn(); }},
      [=](int32_t id) { return d->inputEdgeIDs(id) | d->getEdgeTransform(); }};
  }
  [[nodiscard]] inline auto outputEdges(poly::Dependencies &dep) {
    poly::Dependencies *d = &dep;
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> int32_t { return a->getEdgeOut(); }},
      [=](int32_t id) { return d->outputEdgeIDs(id) | d->getEdgeTransform(); }};
  }

  [[nodiscard]] inline auto inputEdges(poly::Dependencies &dep, int depth0) {
    poly::Dependencies *d = &dep;
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> int32_t { return a->getEdgeIn(); }},
      [=](int32_t id) {
        return d->inputEdgeIDs(id) | d->activeFilter(depth0) |
               std::views::transform(
                 [=](int32_t i) -> Dependence { return d->get(i); });
      }};
  }
  [[nodiscard]] inline auto outputEdges(poly::Dependencies &dep, int depth0) {
    poly::Dependencies *d = &dep;
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> int32_t { return a->getEdgeOut(); }},
      [=](int32_t id) {
        return d->outputEdgeIDs(id) | d->activeFilter(depth0) |
               std::views::transform(
                 [=](int32_t i) -> Dependence { return d->get(i); });
      }};
  }

  struct InNode {
    poly::Dependencies *dep;
    constexpr auto operator()(int32_t id) const -> ScheduledNode * {
      return dep->get(id).input()->getNode();
    }
  };
  struct OutNode {
    poly::Dependencies *dep;
    constexpr auto operator()(int32_t id) const -> ScheduledNode * {
      return dep->get(id).output()->getNode();
    }
  };
  [[nodiscard]] inline auto outNeighbors(poly::Dependencies &dep,
                                         unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{}, GetEdge<true>{}},
      DepFilter<true>{&dep, depth}};
  }
  [[nodiscard]] inline auto inNeighbors(poly::Dependencies &dep,
                                        unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{}, GetEdge<false>{}},
      DepFilter<false>{&dep, depth}};
  }
  struct IsIdActive {
    poly::Dependencies &dep;
    int depth0;
    auto operator()(int32_t id) const -> bool { return !dep[id].isSat(depth0); }
  };
  [[nodiscard]] inline auto hasActiveInEdges(poly::Dependencies &dep,
                                             int depth0) const -> bool {
    return std::ranges::any_of(inputEdgeIds(dep), IsIdActive{dep, depth0});
  }

  [[nodiscard]] inline auto hasActiveOutEdges(poly::Dependencies &dep,
                                              int depth0) const -> bool {
    return std::ranges::any_of(outputEdgeIds(dep), IsIdActive{dep, depth0});
  }
  [[nodiscard]] inline auto hasActiveEdges(poly::Dependencies &dep,
                                           int depth0) const -> bool {
    return hasActiveInEdges(dep, depth0) || hasActiveOutEdges(dep, depth0);
  }

  [[nodiscard]] constexpr auto getSchedule() -> poly::AffineSchedule {
    return {mem};
  }
  [[nodiscard]] constexpr auto getLoopNest() const -> poly::Loop * {
    return loopNest;
  }

  [[nodiscard]] constexpr auto getOffset() const -> int64_t * {
    return offsets;
  }

  [[nodiscard]] constexpr auto getNumLoops() const -> ptrdiff_t {
    ptrdiff_t nl = ptrdiff_t(mem[0]);
    invariant(nl >= 0);
    return nl;
  }
  // 'phiIsScheduled()` means that `phi`'s schedule has been
  // set for the outer `rank` loops.
  [[nodiscard]] constexpr auto phiIsScheduled(int d) const -> bool {
    return d < rank;
  }

  [[nodiscard]] constexpr auto updatePhiOffset(int p) -> int {
    invariant(p >= 0);
    phiOffset = p;
    auto nl = getNumLoops();
    invariant(nl <= std::numeric_limits<int>::max());
    return p + int(nl);
  }
  [[nodiscard]] constexpr auto updateOmegaOffset(int o) -> int {
    invariant(o >= 0);
    omegaOffset = o;
    return ++o;
  }
  [[nodiscard]] constexpr auto getPhiOffset() const -> ptrdiff_t {
    // invariant(phiOffset >= 0);
    return phiOffset;
  }
  [[nodiscard]] constexpr auto
  getPhiOffsetRange() const -> math::Range<ptrdiff_t, ptrdiff_t> {
    return _(phiOffset, phiOffset + getNumLoops());
  }
  /// numLoops x numLoops
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getPhi() -> MutSquarePtrMatrix<int64_t> {
    return {mem + 1, math::SquareDims<>{math::row(getNumLoops())}};
  }
  /// numLoops x numLoops
  [[nodiscard]] constexpr auto getPhi() const -> SquarePtrMatrix<int64_t> {
    return {const_cast<int64_t *>(mem) + 1,
            math::SquareDims<>{math::row(getNumLoops())}};
  }
  /// getSchedule, loops are always indexed from outer to inner
  [[nodiscard]] constexpr auto
  getSchedule(ptrdiff_t d) const -> PtrVector<int64_t> {
    return getPhi()[d, _];
  }
  [[nodiscard]] constexpr auto
  getSchedule(ptrdiff_t d) -> MutPtrVector<int64_t> {
    return getPhi()[d, _];
  }
  [[nodiscard]] constexpr auto getFusionOmega(ptrdiff_t i) const -> int64_t {
    return (mem + 1)[getNumLoopsSquared() + i];
  }
  [[nodiscard]] constexpr auto getOffsetOmega(ptrdiff_t i) const -> int64_t {
    return (mem + 2)[getNumLoopsSquared() + getNumLoops() + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega(ptrdiff_t i) -> int64_t & {
    return (mem + 1)[getNumLoopsSquared() + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega(ptrdiff_t i) -> int64_t & {
    return (mem + 2)[getNumLoopsSquared() + getNumLoops() + i];
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return {const_cast<int64_t *>(mem + 1) + getNumLoopsSquared(),
            math::length(getNumLoops() + 1)};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {const_cast<int64_t *>(mem) + 2 + getNumLoopsSquared() +
              getNumLoops(),
            math::length(getNumLoops())};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return {mem + 1 + getNumLoopsSquared(), math::length(getNumLoops() + 1)};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {mem + 2 + getNumLoopsSquared() + getNumLoops(),
            math::length(getNumLoops())};
  }

  constexpr void schedulePhi(DensePtrMatrix<int64_t> indMat, ptrdiff_t r) {
    // indMat indvars are indexed from outer<->inner
    // phi indvars are indexed from outer<->inner
    // so, indMat is indvars[outer<->inner] x array dim
    // phi is loop[outer<->inner] x indvars[outer<->inner]
    MutSquarePtrMatrix<int64_t> phi = getPhi();
    ptrdiff_t indR = ptrdiff_t(indMat.numCol());
    for (ptrdiff_t i = 0; i < r; ++i) {
      phi[i, _(0, indR)] << indMat[i, _];
      phi[i, _(indR, end)] << 0;
    }
    rank = r;
  }
  constexpr void unschedulePhi() { rank = 0; }
  [[nodiscard]] constexpr auto getOmegaOffset() const -> ptrdiff_t {
    return omegaOffset;
  }
  void resetPhiOffset() { phiOffset = std::numeric_limits<unsigned>::max(); }
  [[nodiscard]] constexpr auto calcGraphMaxDepth() const -> int {
    int maxDepth = 0;
    for (const ScheduledNode *n : getVertices())
      maxDepth = std::max(maxDepth, int(n->getNumLoops()));
    return maxDepth;
  }
  friend inline auto operator<<(std::ostream &os,
                                const ScheduledNode &node) -> std::ostream & {
    os << "inNeighbors = ";
    for (const Addr *m : node.localAddr()) os << "v_" << m << ", ";
    return os << "\n";
  }
};

static_assert(std::is_trivially_destructible_v<ScheduledNode>);
static_assert(sizeof(ScheduledNode) <= 64); // fits in cache line

class ScheduleGraph {
  poly::Dependencies &deps;
  unsigned depth_;

public:
  using VertexType = ScheduledNode;
  constexpr ScheduleGraph(poly::Dependencies &deps_, unsigned depth)
    : deps(deps_), depth_(depth) {}

  [[nodiscard]] static constexpr auto getVertices(ScheduledNode *nodes)
    -> utils::ListRange<ScheduledNode, utils::GetNext, utils::Identity> {
    return nodes->getVertices();
  }
  [[nodiscard]] static constexpr auto getVertices(const ScheduledNode *nodes)
    -> utils::ListRange<const ScheduledNode, utils::GetNext, utils::Identity> {
    return static_cast<const ScheduledNode *>(nodes)->getVertices();
  }
  [[nodiscard]] auto outNeighbors(ScheduledNode *v) const {
    return v->outNeighbors(deps, depth_);
  }
  [[nodiscard]] auto inNeighbors(ScheduledNode *v) const {
    return v->inNeighbors(deps, depth_);
  }
};
} // namespace lp

namespace graph {
// static_assert(AbstractPtrGraph<lp::ScheduledNode>);
static_assert(std::forward_iterator<decltype(lp::ScheduleGraph{
                std::declval<poly::Dependencies &>(), 0}
                                               .outNeighbors(nullptr)
                                               .begin())>);
static_assert(std::forward_iterator<decltype(lp::ScheduleGraph{
                std::declval<poly::Dependencies &>(), 0}
                                               .inNeighbors(nullptr)
                                               .begin())>);
static_assert(AbstractPtrGraph<lp::ScheduleGraph>);
} // namespace graph