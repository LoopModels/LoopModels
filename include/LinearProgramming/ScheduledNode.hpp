#pragma once
#include "Dependence.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include "Utilities/ListRanges.hpp"
#include <Utilities/Invariant.hpp>
#include <Utilities/Valid.hpp>
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
    : store(store), loopNest(L) {}
  struct AllEdgeIterator {
    ScheduledNode *node;
  };

public:
  using VertexType = ScheduledNode;
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
  constexpr auto getNext() -> ScheduledNode * { return next; }
  constexpr auto setNext(ScheduledNode *n) -> ScheduledNode * {
    next = n;
    return this;
  }
  constexpr auto getNextComponent() -> ScheduledNode * { return component; }
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
  [[nodiscard]] constexpr auto getStore() -> Addr * { return store; }
  [[nodiscard]] constexpr auto getStore() const -> const Addr * {
    return store;
  }
  [[nodiscard]] constexpr auto getVertices()
    -> utils::ListRange<ScheduledNode, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }

  // convention: `local` means only for this node
  // `each` for all connected nodes
  // range of `Addr` for this node
  [[nodiscard]] constexpr auto localAddr() {
    return utils::ListRange{(Addr *)store, [](Addr *a) -> Addr * {
                              return llvm::cast_or_null<Addr>(a->getChild());
                            }};
  }
  // range of all `Addr` for the list starting with this node
  [[nodiscard]] constexpr auto eachAddr() {
    return utils::NestedListRange{
      this, utils::GetNext{},
      [](Addr *a) -> Addr * { return llvm::cast_or_null<Addr>(a->getChild()); },
      [](ScheduledNode *n) -> Addr * { return n->getStore(); }};
  }
  // all nodes that are memory inputs to this one; i.e. all parents
  // NOTE: we may reach each node multiple times
  [[nodiscard]] constexpr auto inNeighbors() {
    return utils::NestedListRange{
      store,
      [](Addr *a) -> Addr * { return llvm::cast_or_null<Addr>(a->getChild()); },
      [](Dependence *d) -> Dependence * { return d->getNextInput(); },
      [](Addr *a) -> Dependence * { return a->getEdgeIn(); },
      [](Dependence *d) -> ScheduledNode * { return d->input()->getNode(); }};
  }
  // all nodes that are memory outputs of this one; i.e. all children
  // NOTE: we may reach each node multiple times
  [[nodiscard]] constexpr auto outNeighbors() {
    return utils::NestedListRange{
      store,
      [](Addr *a) -> Addr * { return llvm::cast_or_null<Addr>(a->getChild()); },
      [](Dependence *d) -> Dependence * { return d->getNextOutput(); },
      [](Addr *a) -> Dependence * { return a->getEdgeOut(); },
      [](Dependence *d) -> ScheduledNode * { return d->output()->getNode(); }};
  }
  [[nodiscard]] constexpr auto inputEdges() {
    return utils::NestedListRange{
      store,
      [](Addr *a) -> Addr * { return llvm::cast_or_null<Addr>(a->getChild()); },
      [](Dependence *d) -> Dependence * { return d->getNextInput(); },
      [](Addr *a) -> Dependence * { return a->getEdgeIn(); }};
  }
  [[nodiscard]] constexpr auto outputEdges() {
    return utils::NestedListRange{
      store,
      [](Addr *a) -> Addr * { return llvm::cast_or_null<Addr>(a->getChild()); },
      [](Dependence *d) -> Dependence * { return d->getNextOutput(); },
      [](Addr *a) -> Dependence * { return a->getEdgeOut(); }};
  }
  [[nodiscard]] constexpr auto inputEdges() const {
    return utils::NestedListRange{
      (const Addr *)store,
      [](const Addr *a) -> const Addr * {
        return llvm::cast_or_null<Addr>(a->getChild());
      },
      [](const Dependence *d) -> const Dependence * {
        return d->getNextInput();
      },
      [](const Addr *a) -> const Dependence * { return a->getEdgeIn(); }};
  }
  [[nodiscard]] constexpr auto outputEdges() const {
    return utils::NestedListRange{
      (const Addr *)store,
      [](const Addr *a) -> const Addr * {
        return llvm::cast_or_null<Addr>(a->getChild());
      },
      [](const Dependence *d) -> const Dependence * {
        return d->getNextOutput();
      },
      [](const Addr *a) -> const Dependence * { return a->getEdgeOut(); }};
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
    -> math::Range<size_t, size_t> {
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
  friend inline auto operator<<(llvm::raw_ostream &os,
                                const ScheduledNode &node)
    -> llvm::raw_ostream & {
    os << "inNeighbors = ";
    node.forEachInput([&](auto m) { os << "v_" << m << ", "; });
    return os << "\n";
  }
};
static_assert(std::is_trivially_destructible_v<ScheduledNode>);

} // namespace lp
namespace graph {
static_assert(AbstractPtrGraph<lp::ScheduledNode>);
} // namespace graph
} // namespace poly
