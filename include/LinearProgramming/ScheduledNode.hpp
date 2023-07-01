#pragma once
#include "Dependence.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include "Utilities/ListRanges.hpp"
#include <Utilities/Invariant.hpp>
#include <Utilities/Valid.hpp>
#include <bits/iterator_concepts.h>
#include <ranges>

namespace poly {
namespace lp {
using IR::Addr, IR::Value, IR::Instruction, IR::Load, IR::Stow;
using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::MutSquarePtrMatrix,
  math::end, math::last, math::_, math::Simplex;
using poly::Dependence, poly::DepPoly;
using utils::NotNull, utils::invariant, utils::Optional, utils::Arena;

/// ScheduledNode
/// Represents a set of memory accesses that are optimized together in the LP.
/// These instructions are all connected directly by through registers.
/// E.g., `A[i] = B[i] + C[i]` is a single node
/// because we load from `B[i]` and `C[i]` into registers, compute, and
/// `A[i]`;
/// When splitting LoopBlock graphs, these graphs will have edges between
/// them that we drop. This is only a problem if we merge graphs later.
///
class ScheduledNode {

  NotNull<Addr> store; // linked list to loads, iterate over getChild
  NotNull<poly::Loop> loopNest;
  ScheduledNode *next{nullptr};
  ScheduledNode *component{nullptr}; // SCC cycle, or last node in a chain
  // Dependence *dep{nullptr};          // input edges (points to parents)
  int64_t *offsets{nullptr};
  uint32_t phiOffset{0}, omegaOffset{0}; // used in LoopBlock
  unsigned index_, lowLink_;
  uint8_t rank{0};
  bool visited_{false};
  bool onStack_{false};
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

  [[nodiscard]] constexpr auto getNumLoopsSquared() const -> unsigned {
    auto L = getNumLoops();
    return L * L;
  }
  constexpr ScheduledNode(Addr *store, poly::Loop *L)
    : store(store), loopNest(L) {
    mem[0] = L->getNumLoops();
    getFusionOmega() << 0;
  }

public:
  constexpr auto index() -> unsigned & { return index_; }
  constexpr auto lowLink() -> unsigned & { return lowLink_; }
  [[nodiscard]] constexpr auto onStack() const -> bool { return onStack_; }
  constexpr void addToStack() { onStack_ = true; }
  constexpr void removeFromStack() { onStack_ = false; }
  [[nodiscard]] constexpr auto visited() const -> bool { return visited_; }
  constexpr void visit() { visited_ = true; }
  constexpr auto unVisit() { visited_ = false; }
  constexpr auto addNext(ScheduledNode *n) -> ScheduledNode * {
    next = n;
    return this;
  }

  static auto construct(Arena<> *alloc, Addr *store, poly::Loop *L)
    -> ScheduledNode * {
    size_t memNeeded = poly::requiredScheduleStorage(L->getNumLoops());
    void *p =
      alloc->allocate(sizeof(ScheduledNode) + memNeeded * sizeof(int64_t));
    return new (p) ScheduledNode(store, L);
  }
  [[nodiscard]] constexpr auto getNext() -> ScheduledNode * { return next; }
  [[nodiscard]] constexpr auto getNext() const -> const ScheduledNode * {
    return next;
  }
  constexpr auto setNext(ScheduledNode *n) -> ScheduledNode * {
    next = n;
    return this;
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
  [[nodiscard]] constexpr auto getNextComponent() const
    -> const ScheduledNode * {
    return component;
  }
  constexpr auto setNextComponent(ScheduledNode *n) -> ScheduledNode * {
    component = n;
    return this;
  }
  constexpr auto getLoopOffsets() -> MutPtrVector<int64_t> {
    return {offsets, getNumLoops()};
  }
  constexpr void setOffsets(int64_t *o) { offsets = o; }
  // MemAccess addrCapacity field gives the replication count
  // so for each memory access, we can count the number of edges in
  // and the number of edges out through iterating edges in and summing
  // repCounts
  //
  // we use these to
  // 1. alloc enough memory for each Addresses*
  // 2. add each created address to the MemoryAddress's remap
  // TODO:
  // 1. the above
  // 2. add the direct Addr connections corresponding to the node
  // constexpr void insertMem(Arena<> *alloc, PtrVector<Addr *> memAccess,
  //                          CostModeling::LoopTreeSchedule *L) const;
  // constexpr void
  // incrementReplicationCounts(PtrVector<MemoryAccess *> memAccess) const {
  //   for (auto i : memory)
  //     if (i != storeId && (memAccess[i]->isStore()))
  //       memAccess[i]->replicateAddr();
  // }
  // [[nodiscard]] constexpr auto getNumMem() const -> size_t {
  //   return memory.size();
  // }
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
  struct NextAddr {
    constexpr auto operator()(Addr *a) const -> Addr * {
      return llvm::cast_or_null<Addr>(a->getChild());
    }
    constexpr auto operator()(const Addr *a) const -> const Addr * {
      return llvm::cast_or_null<Addr>(a->getChild());
    }
  };
  struct NextInput {
    constexpr auto operator()(Dependence *d) const -> Dependence * {
      return d->getNextInput();
    }
    constexpr auto operator()(const Dependence *d) const -> const Dependence * {
      return d->getNextInput();
    }
  };
  struct NextOutput {
    constexpr auto operator()(Dependence *d) const -> Dependence * {
      return d->getNextOutput();
    }
    constexpr auto operator()(const Dependence *d) const -> const Dependence * {
      return d->getNextOutput();
    }
  };
  struct Component {
    constexpr auto operator()(ScheduledNode *n) const -> ScheduledNode * {
      return n->getNextComponent();
    }
    constexpr auto operator()(const ScheduledNode *n) const
      -> const ScheduledNode * {
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
  // range of all `Addr` for the list starting with this node
  [[nodiscard]] constexpr auto eachAddr() {
    return utils::NestedList{
      utils::ListRange{
        this, utils::GetNext{},
        [](ScheduledNode *n) -> Addr * { return n->getStore(); }},

      [](Addr *a) -> utils::ListRange<Addr, utils::GetNext, utils::Identity> {
        return utils::ListRange{llvm::cast<Addr>(a->getChild()),
                                utils::GetNext{}};
      }};
  }
  // all nodes that are memory inputs to this one; i.e. all parents
  // NOTE: we may reach each node multiple times
  [[nodiscard]] constexpr auto inNeighbors() {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeIn(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextInput{},
                                [](Dependence *d) -> ScheduledNode * {
                                  return d->input()->getNode();
                                }};
      }};
  }
  // all nodes that are memory inputs to this one; i.e. all parents
  // NOTE: we may reach each node multiple times

  // all nodes that are memory outputs of this one; i.e. all children
  // NOTE: we may reach each node multiple times
  [[nodiscard]] constexpr auto outNeighbors() {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeOut(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextOutput{},
                                [](Dependence *d) -> ScheduledNode * {
                                  return d->output()->getNode();
                                }};
      }};
  }
  [[nodiscard]] constexpr auto inputEdges() {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeIn(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextInput{}};
      }};
  }
  [[nodiscard]] constexpr auto outputEdges() {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeOut(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextOutput{}};
      }};
  }
  [[nodiscard]] constexpr auto inputEdges() const {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeIn(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextInput{}};
      }};
  }
  [[nodiscard]] constexpr auto outputEdges() const {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeOut(); }},
      [](Dependence *d) {
        return utils::ListRange{d, NextOutput{}};
      }};
  }
  [[nodiscard]] constexpr auto inputEdges(unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeIn(); }},
      [depth](Dependence *d) {
        return utils::ListRange{d, NextInput{}} |
               std::views::filter(Active{depth});
      }};
  }
  [[nodiscard]] constexpr auto outputEdges(unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeOut(); }},
      [depth](Dependence *d) {
        return utils::ListRange{d, NextOutput{}} |
               std::views::filter(Active{depth});
      }};
  }
  [[nodiscard]] constexpr auto inputEdges(unsigned depth) const {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeIn(); }},
      [depth](Dependence *d) {
        return utils::ListRange{d, NextInput{}} |
               std::views::filter(Active{depth});
      }};
  }
  [[nodiscard]] constexpr auto outputEdges(unsigned depth) const {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{},
                       [](Addr *a) -> Dependence * { return a->getEdgeOut(); }},
      [depth](Dependence *d) {
        return utils::ListRange{d, NextOutput{}} |
               std::views::filter(Active{depth});
      }};
  }
  struct InNode {
    constexpr auto operator()(Dependence *d) const -> ScheduledNode * {
      return d->input()->getNode();
    }
  };
  struct OutNode {
    constexpr auto operator()(Dependence *d) const -> ScheduledNode * {
      return d->output()->getNode();
    }
  };
  template <bool Out> struct DepFilter {
    unsigned depth;

    constexpr auto operator()(Dependence *d) const {
      if constexpr (Out)
        return utils::ListRange{d, NextOutput{}} |
               std::views::filter(Active{depth}) |
               std::views::transform(OutNode{});
      else
        return utils::ListRange{d, NextInput{}} |
               std::views::filter(Active{depth}) |
               std::views::transform(InNode{});
    }
  };
  template <bool Out> struct GetEdge {
    constexpr auto operator()(Addr *a) const -> Dependence * {
      if constexpr (Out) return a->getEdgeOut();
      else return a->getEdgeIn();
    }
  };
  [[nodiscard]] constexpr auto outNeighbors(unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{}, GetEdge<true>{}},
      DepFilter<true>{depth}};
  }
  [[nodiscard]] constexpr auto inNeighbors(unsigned depth) {
    return utils::NestedList{
      utils::ListRange{store, NextAddr{}, GetEdge<false>{}},
      DepFilter<false>{depth}};
  }
  [[nodiscard]] constexpr auto hasActiveEdges(unsigned depth) const -> bool {
    const auto f = [depth](const Dependence *d) { return d->isActive(depth); };
    return std::ranges::any_of(inputEdges(), f) ||
           std::ranges::any_of(outputEdges(), f);
  }

  // constexpr auto eachInputNodes() {
  //   return utils::NestedListRange{
  //     this, utils::GetNext{},
  //     [](ScheduledNode *n) -> ScheduledNode * { return n->getNext(); },
  //     [](ScheduledNode *n) -> ScheduledNode * { return n->getNext(); }};
  // }

  // for each input node, i.e. for each where this is the output
  constexpr void forEachInput(const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        f(d->input()->getNode());
  }
  constexpr void forEachInput(const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        f(d->input()->getNode());
  }
  constexpr void forEachInput(unsigned depth, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) f(d->input()->getNode());
  }
  constexpr void forEachInput(unsigned depth, const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) f(d->input()->getNode());
  }
  constexpr auto reduceEachInput(auto x, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, unsigned depth, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, unsigned depth, const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void forEachInputEdge(const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput()) f(d);
  }
  constexpr void forEachInputEdge(const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput()) f(d);
  }
  constexpr void forEachInputEdge(unsigned depth, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) f(d);
  }
  constexpr void forEachInputEdge(unsigned depth, const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) f(d);
  }
  constexpr auto reduceEachInputEdge(auto x, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, unsigned depth, const auto &f) {
    for (Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, unsigned depth,
                                     const auto &f) const {
    for (const Addr *a = store; a; a = llvm::cast_or_null<Addr>(a->getChild()))
      for (const Dependence *d = a->getEdgeIn(); d; d = d->getNextInput())
        if (!d->isSat(depth)) x = f(x, d);
    return x;
  }

  [[nodiscard]] constexpr auto getSchedule() -> poly::AffineSchedule {
    return {mem};
  }
  [[nodiscard]] constexpr auto getLoopNest() const
    -> NotNull<const poly::Loop> {
    return loopNest;
  }

  [[nodiscard]] constexpr auto getOffset() const -> const int64_t * {
    return offsets;
  }

  // [[nodiscard]] constexpr auto wasVisited2() const -> bool { return visited2;
  // } constexpr void visit2() { visited2 = true; } constexpr void unVisit2() {
  // visited2 = false; }
  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return unsigned(mem[0]);
  }
  // 'phiIsScheduled()` means that `phi`'s schedule has been
  // set for the outer `rank` loops.
  [[nodiscard]] constexpr auto phiIsScheduled(unsigned d) const -> bool {
    return d < rank;
  }

  [[nodiscard]] constexpr auto updatePhiOffset(size_t p) -> size_t {
    phiOffset = p;
    return p + getNumLoops();
  }
  [[nodiscard]] constexpr auto updateOmegaOffset(size_t o) -> size_t {
    omegaOffset = o;
    return ++o;
  }
  [[nodiscard]] constexpr auto getPhiOffset() const -> size_t {
    return phiOffset;
  }
  [[nodiscard]] constexpr auto getPhiOffsetRange() const
    -> math::Range<ptrdiff_t, ptrdiff_t> {
    return _(phiOffset, phiOffset + getNumLoops());
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getPhi() -> MutSquarePtrMatrix<int64_t> {
    return {mem + 1, math::SquareDims{unsigned(getNumLoops())}};
  }
  [[nodiscard]] constexpr auto getPhi() const -> SquarePtrMatrix<int64_t> {
    return {const_cast<int64_t *>(mem) + 1, math::SquareDims{getNumLoops()}};
  }
  /// getSchedule, loops are always indexed from outer to inner
  [[nodiscard]] constexpr auto getSchedule(size_t d) const
    -> PtrVector<int64_t> {
    return getPhi()(d, _);
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) -> MutPtrVector<int64_t> {
    return getPhi()(d, _);
  }
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) const -> int64_t {
    return (mem + 1)[getNumLoopsSquared() + i];
  }
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) const -> int64_t {
    return (mem + 2)[getNumLoopsSquared() + getNumLoops() + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) -> int64_t & {
    return (mem + 1)[getNumLoopsSquared() + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) -> int64_t & {
    return (mem + 2)[getNumLoopsSquared() + getNumLoops() + i];
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return {const_cast<int64_t *>(mem + 1) + getNumLoopsSquared(),
            getNumLoops() + 1};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {const_cast<int64_t *>(mem) + 2 + getNumLoopsSquared() +
              getNumLoops(),
            getNumLoops()};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return {mem + 1 + getNumLoopsSquared(), getNumLoops() + 1};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {mem + 2 + getNumLoopsSquared() + getNumLoops(), getNumLoops()};
  }

  constexpr void schedulePhi(DensePtrMatrix<int64_t> indMat, size_t r) {
    // indMat indvars are indexed from outer<->inner
    // phi indvars are indexed from outer<->inner
    // so, indMat is indvars[outer<->inner] x array dim
    // phi is loop[outer<->inner] x indvars[outer<->inner]
    MutSquarePtrMatrix<int64_t> phi = getPhi();
    size_t indR = size_t(indMat.numCol());
    for (size_t i = 0; i < r; ++i) {
      phi(i, _(0, indR)) << indMat(i, _);
      phi(i, _(indR, end)) << 0;
    }
    rank = r;
  }
  constexpr void unschedulePhi() { rank = 0; }
  [[nodiscard]] constexpr auto getOmegaOffset() const -> size_t {
    return omegaOffset;
  }
  void resetPhiOffset() { phiOffset = std::numeric_limits<unsigned>::max(); }
  [[nodiscard]] constexpr auto calcGraphMaxDepth() const -> unsigned {
    unsigned maxDepth = 0;
    for (const ScheduledNode *n : getVertices())
      maxDepth = std::max(maxDepth, n->getNumLoops());
    return maxDepth;
  }
  friend inline auto operator<<(llvm::raw_ostream &os,
                                const ScheduledNode &node)
    -> llvm::raw_ostream & {
    os << "inNeighbors = ";
    node.forEachInput([&](auto m) { os << "v_" << m << ", "; });
    return os << "\n";
  }
};
static_assert(std::is_trivially_destructible_v<ScheduledNode>);

class ScheduleGraph {
  unsigned depth;

public:
  using VertexType = ScheduledNode;
  constexpr ScheduleGraph(unsigned depth) : depth(depth) {}

  [[nodiscard]] static constexpr auto getVertices(ScheduledNode *nodes)
    -> utils::ListRange<ScheduledNode, utils::GetNext, utils::Identity> {
    return nodes->getVertices();
  }
  [[nodiscard]] static constexpr auto getVertices(const ScheduledNode *nodes)
    -> utils::ListRange<const ScheduledNode, utils::GetNext, utils::Identity> {
    return static_cast<const ScheduledNode *>(nodes)->getVertices();
  }
  [[nodiscard]] constexpr auto outNeighbors(ScheduledNode *v) const {
    return v->outNeighbors(depth);
  }
  [[nodiscard]] constexpr auto inNeighbors(ScheduledNode *v) const {
    return v->inNeighbors(depth);
  }
};

} // namespace lp

namespace graph {
// static_assert(AbstractPtrGraph<lp::ScheduledNode>);
static_assert(std::forward_iterator<
              decltype(lp::ScheduleGraph{0}.outNeighbors(nullptr).begin())>);
static_assert(std::forward_iterator<
              decltype(lp::ScheduleGraph{0}.inNeighbors(nullptr).begin())>);
static_assert(AbstractPtrGraph<lp::ScheduleGraph>);
} // namespace graph
} // namespace poly
