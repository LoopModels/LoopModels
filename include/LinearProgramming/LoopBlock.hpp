#pragma once

#include "Dependence.hpp"
#include "Dicts/BumpMapSet.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "IR/Cache.hpp"
#include "IR/Node.hpp"
#include "Polyhedra/Loops.hpp"
#include "Schedule.hpp"
#include <Containers/BitSets.hpp>
#include <Math/Array.hpp>
#include <Math/Comparisons.hpp>
#include <Math/GreatestCommonDivisor.hpp>
#include <Math/Math.hpp>
#include <Math/NormalForm.hpp>
#include <Math/Simplex.hpp>
#include <Math/StaticArrays.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Invariant.hpp>
#include <Utilities/Optional.hpp>
#include <Utilities/Valid.hpp>
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <type_traits>

namespace poly::lp {

using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::MutSquarePtrMatrix,
  math::end, math::_, utils::NotNull, utils::invariant, poly::Dependence,
  IR::Addr;

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

  NotNull<Addr> store; // linked list to loads
  NotNull<poly::Loop> loopNest;
  ScheduledNode *next{nullptr};
  ScheduledNode *component{nullptr}; // SCC cycle, or last node in a chain
  Dependence *dep{nullptr};          // input edges (points to parents)
  int64_t *offsets{nullptr};
  uint32_t phiOffset{0};             // used in LoopBlock
  uint32_t omegaOffset{0};           // used in LoopBlock
  uint8_t rank{0};
  bool visited{false};
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

public:
  constexpr auto getNext() -> ScheduledNode * { return next; }
  constexpr auto setNext(ScheduledNode *n) -> void { next = n; }
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
  constexpr void insertMem(Arena<> *alloc, PtrVector<Addr *> memAccess,
                           CostModeling::LoopTreeSchedule *L) const;
  // constexpr void
  // incrementReplicationCounts(PtrVector<MemoryAccess *> memAccess) const {
  //   for (auto i : memory)
  //     if (i != storeId && (memAccess[i]->isStore()))
  //       memAccess[i]->replicateAddr();
  // }
  // [[nodiscard]] constexpr auto getNumMem() const -> size_t {
  //   return memory.size();
  // }
  constexpr auto getStore() -> Addr * { return store; }
  [[nodiscard]] constexpr auto getStore() const -> const Addr * {
    return store;
  }
  // at this point, getNext chains memory ops, letting us loop over them
  // getPrev lets us iterate within a ScheduledNode
  // we can iterate over
  // a. nodes -> edges
  // b. nodes -> addrs
  constexpr void forEachAddr(const auto &f) {
    Addr *m = store;
    while (true) {
      f(m);
      IR::Node *v = m->getPrev();
      if (!v) break;
      m = llvm::cast<Addr>(v);
    }
  }
  // for each input node, i.e. for each where this is the output
  constexpr void forEachInput(const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext()) f(d->input()->getNode());
  }
  constexpr void forEachInput(const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
      f(d->input()->getNode());
  }
  constexpr void forEachInput(unsigned depth, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) f(d->input()->getNode());
  }
  constexpr void forEachInput(unsigned depth, const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) f(d->input()->getNode());
  }
  constexpr auto reduceEachInput(auto x, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext())
      x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
      x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, unsigned depth, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) x = f(x, d->input()->getNode());
    return x;
  }
  constexpr void reduceEachInput(auto x, unsigned depth, const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) x = f(x, d->input()->getNode());
    return x;
  }

  constexpr void forEachInputEdge(const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext()) f(d);
  }
  constexpr void forEachInputEdge(const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext()) f(d);
  }
  constexpr void forEachInputEdge(unsigned depth, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) f(d);
  }
  constexpr void forEachInputEdge(unsigned depth, const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) f(d);
  }
  constexpr auto reduceEachInputEdge(auto x, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext()) x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext()) x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, unsigned depth, const auto &f) {
    for (Dependence *d = dep; d; d = d->getNext())
      if (!d->isSat(depth)) x = f(x, d);
    return x;
  }
  constexpr void reduceEachInputEdge(auto x, unsigned depth,
                                     const auto &f) const {
    for (const Dependence *d = dep; d; d = d->getNext())
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

  [[nodiscard]] constexpr auto wasVisited() const -> bool { return visited; }
  constexpr void visit() { visited = true; }
  constexpr void unVisit() { visited = false; }
  // [[nodiscard]] constexpr auto wasVisited2() const -> bool { return visited2;
  // } constexpr void visit2() { visited2 = true; } constexpr void unVisit2() {
  // visited2 = false; }
  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return unsigned(mem[0]);
  }
  // 'phiIsScheduled()` means that `phi`'s schedule has been
  // set for the outer `rank` loops.
  [[nodiscard]] constexpr auto phiIsScheduled(size_t d) const -> bool {
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
    ;
  }
};
static_assert(std::is_trivially_destructible_v<ScheduledNode>);

/// A loop block is a block of the program that may include multiple loops.
/// These loops are either all executed (note iteration count may be 0, or
/// loops may be in rotated form and the guard prevents execution; this is okay
/// and counts as executed for our purposes here ), or none of them are.
/// That is, the LoopBlock does not contain divergent control flow, or guards
/// unrelated to loop bounds.
/// The loops within a LoopBlock are optimized together, so we can consider
/// optimizations such as reordering or fusing them together as a set.
///
///
/// Initially, the `LoopBlock` is initialized as a set of
/// `Read` and `Write`s, without any dependence polyhedra.
/// Then, it builds `DependencePolyhedra`.
/// These can be used to construct an ILP.
///
/// That is:
/// fields that must be provided/filled:
///  - refs
///  - memory
///  - userToMemory
/// fields it self-initializes:
///
///
/// NOTE: w/ respect to index linearization (e.g., going from Cartesian indexing
/// to linear indexing), the current behavior will be to fully delinearize as a
/// preprocessing step. Linear indexing may be used later as an optimization.
/// This means that not only do we want to delinearize
/// for (n = 0; n < N; ++n){
///   for (m = 0; m < M; ++m){
///      C(m + n*M)
///   }
/// }
/// we would also want to delinearize
/// for (i = 0; i < M*N; ++i){
///   C(i)
/// }
/// into
/// for (n = 0; n < N; ++n){
///   for (m = 0; m < M; ++m){
///      C(m, n)
///   }
/// }
/// and then relinearize as an optimization later.
/// Then we can compare fully delinearized loop accesses.
/// Should be in same block:
/// s = 0
/// for (i = eachindex(x)){
///   s += x[i]; // Omega = [0, _, 0]
/// }
/// m = s / length(x); // Omega = [1]
/// for (i = eachindex(y)){
///   f(m, ...); // Omega = [2, _, 0]
/// }
class LoopBlock {
  // TODO: figure out how to handle the graph's dependencies based on
  // operation/instruction chains.
  // Perhaps implicitly via the graph when using internal orthogonalization
  // and register tiling methods, and then generate associated constraints
  // or aliasing between schedules when running the ILP solver?
  // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
  // and all other other shared schedule parameters are aliases (i.e.,
  // identical)?
  // Addr *memory{nullptr};
  ScheduledNode *node{nullptr};
  // dict::map<llvm::User *, Addr *> userToMem{};
  // dict::set<llvm::User *> visited{};
  // llvm::LoopInfo *LI;
  utils::OwningArena<> allocator{};
  // we may turn off edges because we've exceeded its loop depth
  // or because the dependence has already been satisfied at an
  // earlier level.
  struct CoefCounts {
    unsigned numPhiCoefs{0};
    unsigned numOmegaCoefs{0};
    unsigned numSlack{0};
    unsigned numLambda{0};
    unsigned numBounding{0};
    unsigned numConstraints{0};
    unsigned numActiveEdges{0};
  };

public:
  LoopBlock() = default;
  void optimize(IR::Cache &cache, IR::TreeResult tr) {
    // fillEdges();
    for (Addr *stow = tr.stow; stow;) {
      Addr *next = stow->getNext();
      for (Addr *other = next; other; other = other->getNext())
        check(&allocator, stow, other);
      for (Addr *other = tr.load; other; other = other->getNext())
        check(&allocator, stow, other);
      stow = next
    }
    for (Addr *stow = tr.stow; stow;) {
      Addr *next = stow->getNext(); // addScheduledNode breaks chain
      addScheduledNode(cache, stow);
      stow = next;
    }
    // buildGraph();
    // shiftOmegas();
    // return optOrth(fullGraph());
  }
  void clear() { allocator.reset(); }

private:
  constexpr auto addScheduledNode(IR::Cache &cache, IR::Stow stow) {
    // we search the instruction graph for all directly connected loads that
    // must be scheduled together with the stow.
    stow->removeFromList();
    // how are we going to handle load duplication?
    // we also need to duplicate the instruction graph leading to the node
    // implying we need to track that tree.
    // w = a[i]
    // x = log(w)
    // y = 2*x
    // z = 3*x
    // p = z / 5
    // q = 5 / z
    // s = p - q
    // b[i] = y
    // c[i] = s
    // if adding c[i] after b[i], we must duplicate `w` and `x`
    // but duplicating `z, `p`, `q`, or `s` is unnecessary.
    // We don't need to duplicate those instructions where
    // all uses only lead to `c[i]`.
    // The trick we use is to mark each instruction with
    // the store that visited it.
    // If one has already been visited, duplicate and
    // mark the new one.
    searchOperandsForLoads(stow, stow->getStoredVal());
  }
  void searchOperandsForLoads(IR::Cache &cache, IR::Stow stow, Value *val) {
    Instruction *inst = dyn_cast<Instruction>(val);
    if (!inst) return;
    if (Load load = IR::Load(inst)) {

      // now, check if it is a load
      return;
    }
    for (Value *use : inst->getUsers()) {
      if (IR::Stow other = IR::Stow(use)) {
        if (other == stow) break; // not other
        // we reload this use, so we need to duplicate the instructions
        if (useInst->getParent() == stow->getParent())
          searchOperandsForLoads(stow, useInst);
      }
    }
    // if not, check if it is stored
    // if not, check its operands
  }
  constexpr void setLI(llvm::LoopInfo *loopInfo) { LI = loopInfo; }
  constexpr void addStow(Addr *stow) {}
  constexpr void addAddr(Addr *addr) {
    if (memory != nullptr) addr->setNext(memory);
    memory = addr;
    userToMem[addr->getInstruction()] = addr;
  }
  /// L is the inner-most loop getting dropped, i.e. it is the level at which
  /// the TreeResult rejected
  [[nodiscard]] constexpr auto getAllocator() -> Arena<> * { return allocator; }
  [[nodiscard]] constexpr auto numVerticies() const -> size_t {
    return nodes.size();
  }
  [[nodiscard]] constexpr auto getVerticies() -> MutPtrVector<ScheduledNode> {
    return nodes;
  }
  [[nodiscard]] constexpr auto getVerticies() const
    -> PtrVector<ScheduledNode> {
    return nodes;
  }
  [[nodiscard]] constexpr auto getAddr() const -> const Addr * {
    return memory;
  }
  constexpr auto getAddr() -> Addr * { return memory; }

  auto getNode(size_t i) -> ScheduledNode & { return nodes[i]; }
  [[nodiscard]] auto getNode(size_t i) const -> const ScheduledNode & {
    return nodes[i];
  }
  auto getNodes() -> MutPtrVector<ScheduledNode> { return nodes; }
  auto getEdges() -> MutPtrVector<Dependence> { return edges; }
  [[nodiscard]] auto numNodes() const -> size_t { return nodes.size(); }
  [[nodiscard]] auto numEdges() const -> size_t { return edges.size(); }
  [[nodiscard]] auto numMemoryAccesses() const -> size_t {
    return memory.size();
  }
  [[nodiscard]] auto calcMaxDepth() const -> size_t {
    unsigned d = 0;
    for (const auto *mem : memory) d = std::max(d, mem->getNumLoops());
    return d;
  }

  /// NOTE: this relies on two important assumptions:
  /// 1. Code has been fully delinearized, so that axes all match
  ///    (this means that even C[i], 0<=i<M*N -> C[m*M*n])
  ///    (TODO: what if we have C[n+N*m] and C[m+M*n]???)
  ///    (this of course means we have to see other uses in
  ///     deciding whether to expand `C[i]`, and what to expand
  ///     it into.)
  /// 2. Reduction targets have been orthogonalized, so that
  ///     the number of axes reflects the number of loops they
  ///     depend on.
  /// if we have
  /// for (i = I, j = J, m = M, n = N) {
  ///   C(m,n) = foo(C(m,n), ...)
  /// }
  /// then we have dependencies that
  /// the load C(m,n) [ i = x, j = y ]
  /// happens after the store C(m,n) [ i = x-1, j = y], and
  /// happens after the store C(m,n) [ i = x, j = y-1]
  /// and that the store C(m,n) [ i = x, j = y ]
  /// happens after the load C(m,n) [ i = x-1, j = y], and
  /// happens after the load C(m,n) [ i = x, j = y-1]
  ///
  void addEdge(NotNull<Addr> mai, NotNull<Addr> maj) {
    // note, axes should be fully delinearized, so should line up
    // as a result of preprocessing.
    auto d = Dependence::check(allocator, mai, maj);
    __builtin_trap();
  }
  /// fills all the edges between memory accesses, checking for
  /// dependencies.
  void fillEdges() {
    // TODO: handle predicates
    for (size_t i = 1; i < memory.size(); ++i) {
      MemoryAccess *mai = memory[i];
      for (size_t j = 0; j < i; ++j) {
        MemoryAccess *maj = memory[j];
        if ((mai->getArrayPointer() != maj->getArrayPointer()) ||
            ((mai->isLoad()) && (maj->isLoad())))
          continue;
        addEdge(mai, maj);
      }
    }
  }
  /// used in searchOperandsForLoads
  /// if an operand is stored, we can reload it.
  /// This will insert a new store memory access.
  ///
  /// If an instruction was stored somewhere, we don't keep
  /// searching for place it was loaded, and instead add a reload.
  [[nodiscard]] auto searchValueForStores(ScheduledNode &node, llvm::User *user,
                                          unsigned nodeIdx) -> bool {
    for (llvm::User *use : user->users()) {
      if (visited.contains(use)) continue;
      if (llvm::isa<llvm::StoreInst>(use)) {
        auto ma = userToMem.find(use);
        if (ma == userToMem.end()) continue;
        // we want to reload a store
        // this store will be treated as a load
        NotNull<MemoryAccess> store = memory[ma->second];
        auto [load, d] = Dependence::reload(allocator, store);
        // for every store->store, we also want a load->store
        for (auto o : store->outputEdges()) {
          Dependence edge = edges[o];
          if (!edge.outputIsStore()) continue;
          pushToEdgeVector(edges, edge.replaceInput(load));
        }
        pushToEdgeVector(edges, d);
        unsigned memId = memory.size();
        memory.push_back(load);
        node.addMemory(memId, load, nodeIdx);
        return true;
      }
    }
    return false;
  }
  auto duplicateLoad(NotNull<Addr> load, unsigned &memId) -> NotNull<Addr> {
    NotNull<Addr> newLoad = allocator.create<Addr>(load->getArrayRef(), true);
    memId = memory.size();
    memory.push_back(load);
    for (auto l : load->inputEdges())
      pushToEdgeVector(edges, edges[l].replaceOutput(newLoad));
    for (auto o : load->outputEdges())
      pushToEdgeVector(edges, edges[o].replaceInput(newLoad));
    return newLoad;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void checkUserForLoads(ScheduledNode &node, llvm::User *user,
                         unsigned nodeIdx) {
    if (!user || visited.contains(user)) return;
    if (llvm::isa<llvm::LoadInst>(user)) {
      // check if load is a part of the LoopBlock
      auto ma = userToMem.find(user);
      if (ma == userToMem.end()) return;
      unsigned memId = ma->second;
      NotNull<MemoryAccess> load = memory[memId];
      if (load->getNode() != std::numeric_limits<unsigned>::max())
        load = duplicateLoad(load, memId);
      node.addMemory(memId, load, nodeIdx);
    } else if (!searchValueForStores(node, user, nodeIdx))
      searchOperandsForLoads(node, user, nodeIdx);
  }
  /// We search uses of user `u` for any stores so that we can assign the use
  /// and the store the same schedule. This is done because it is assumed the
  /// data is held in registers (or, if things go wrong, spilled to the stack)
  /// in between a load and a store. A complication is that LLVM IR can be
  /// messy, e.g. we may have
  /// %x = load %a
  /// %y = call foo(x)
  /// store %y, %b
  /// %z = call bar(y)
  /// store %z, %c
  /// here, we might lock all three operations
  /// together. However, this limits reordering opportunities; we thus want to
  /// insert a new load instruction so that we have:
  /// %x = load %a
  /// %y = call foo(x)
  /// store %y, %b
  /// %y.reload = load %b
  /// %z = call bar(y.reload)
  /// store %z, %c
  /// and we create a new edge from `store %y, %b` to `load %b`.
  /// We're going to also build up the `Node` graph,
  /// this is to avoid duplicating any logic or needing to traverse the object
  /// graph an extra time as we go.
  ///
  // NOLINTNEXTLINE(misc-no-recursion)
  void searchOperandsForLoads(ScheduledNode &node, llvm::User *u,
                              unsigned nodeIdx) {
    visited.insert(u);
    if (auto *s = llvm::dyn_cast<llvm::StoreInst>(u)) {
      if (auto *user = llvm::dyn_cast<llvm::User>(s->getValueOperand()))
        checkUserForLoads(node, user, nodeIdx);
      return;
    }
    for (auto &&op : u->operands())
      if (auto *user = llvm::dyn_cast<llvm::User>(op.get()))
        checkUserForLoads(node, user, nodeIdx);
  }
  void connect(unsigned inIndex, unsigned outIndex) {
    nodes[inIndex].addOutNeighbor(outIndex);
    nodes[outIndex].addInNeighbor(inIndex);
  }
  [[nodiscard]] auto calcNumStores() const -> size_t {
    return std::ranges::count_if(memory,
                                 [](const auto &m) { return m->isStore(); });
  }
  /// When connecting a graph, we draw direct connections between stores and
  /// loads loads may be duplicated across stores to allow for greater
  /// reordering flexibility (which should generally reduce the ultimate
  /// amount of loads executed in the eventual generated code)
  void connectGraph() {
    // assembles direct connections in node graph
    for (unsigned i = 0; i < memory.size(); ++i)
      userToMem.insert({memory[i]->getInstruction(), i});

    nodes.reserve(calcNumStores());
    for (unsigned i = 0; i < memory.size(); ++i) {
      MemoryAccess *mai = memory[i];
      if (mai->isLoad()) continue;
      unsigned nodeIdx = nodes.size();
      searchOperandsForLoads(nodes.emplace_back(i, mai, nodeIdx),
                             mai->getInstruction(), nodeIdx);
      visited.clear();
    }
    // destructors of amap and aset poison memory
  }
  void buildGraph() {
    connectGraph();
    // now that we've assigned each MemoryAccess to a NodeIndex, we
    // build the actual graph
    for (auto e : edges) connect(e.nodeIn(), e.nodeOut());
    for (auto &&node : nodes) node.init(allocator);
  }
  struct Graph {
    ScheduledNode *nodes;
    class Iterator {
      ScheduledNode *node;
      constexpr auto operator*() const -> ScheduledNode & { return *node; }
      constexpr auto operator->() const -> ScheduledNode * { return node; }
      constexpr auto operator++() -> Iterator & {
        node = node->getNext();
        return *this;
      }
      constexpr auto operator==(const Iterator &other) const -> bool {
        return node == other.node;
      }
      constexpr auto operator!=(const Iterator &other) const -> bool {
        return node != other.node;
      }
    };
    class ConstIterator {
      const ScheduledNode *node;
      constexpr auto operator*() const -> const ScheduledNode & {
        return *node;
      }
      constexpr auto operator->() const -> const ScheduledNode * {
        return node;
      }
      constexpr auto operator++() -> ConstIterator & {
        node = node->getNext();
        return *this;
      }
      constexpr auto operator==(const ConstIterator &other) const -> bool {
        return node == other.node;
      }
      constexpr auto operator!=(const ConstIterator &other) const -> bool {
        return node != other.node;
      }
    };
    [[nodiscard]] constexpr auto begin() -> Iterator { return {node}; }
    [[nodiscard]] constexpr auto begin() const -> ConstIterator {
      return {node};
    }
    [[nodiscard]] constexpr auto end() -> Iterator { return {nullptr}; }
    [[nodiscard]] constexpr auto end() const -> ConstIterator {
      return {nullptr};
    }
    [[nodiscard]] constexpr auto calcMaxDepth() const -> size_t {
      if (nodeIds.data.empty()) return 0;
      size_t d = 0;
      for (auto n : nodeIds) d = std::max(d, nodes[n].getNumLoops());
      return d;
    }

    constexpr void forEachEdge(const auto &f) {
      for (Iterator i : (*this)) i->forEachInputEdge(f);
    }
    constexpr void forEachEdge(const auto &f) const {
      for (ConstIterator i : (*this)) i->forEachInputEdge(f);
    }
    constexpr void forEachEdge(unsigned depth, const auto &f) {
      for (Iterator i : (*this)) i->forEachEdge(depth, f);
    }
    constexpr void forEachEdge(unsigned depth, const auto &f) const {
      for (ConstIterator i : (*this)) i->forEachInputEdge(depth, f);
    }
    constexpr auto reduceEachEdge(auto x, const auto &f) {
      for (Iterator i : (*this)) x = i->reduceEachInputEdge(x, f);
      return x;
    }
    constexpr void reduceEachEdge(auto x, const auto &f) const {
      for (ConstIterator i : (*this)) x = i->reduceEachInputEdge(x, f);
      return x;
    }
    constexpr void reduceEachEdge(auto x, unsigned depth, const auto &f) {
      for (Iterator i : (*this)) x = i->reduceEachInputEdge(x, depth, f);
      return x;
    }
    constexpr void reduceEachEdge(auto x, unsigned depth, const auto &f) const {
      for (ConstIterator i : (*this)) x = i->reduceEachInputEdge(x, depth, f);
      return x;
    }
  };
  // bool connects(const Dependence &e, Graph &g0, Graph &g1, size_t d) const
  // {
  //     return ((e.getInNumLoops() > d) && (e.getOutNumLoops() > d)) &&
  //            connects(e, g0, g1);
  // }
  static auto connects(const Dependence &e, Graph &g0, Graph &g1) -> bool {
    size_t nodeIn = e.nodeIn(), nodeOut = e.nodeOut();
    return ((g0.nodeIds.contains(nodeIn) && g1.nodeIds.contains(nodeOut)) ||
            (g1.nodeIds.contains(nodeIn) && g0.nodeIds.contains(nodeOut)));
  }
  constexpr auto fullGraph() -> Graph {
    return {BitSet::dense(nodes.size()), BitSet::dense(edges.size()), memory,
            nodes, edges};
  }
  static constexpr auto getOverlapIndex(const Dependence &edge)
    -> Optional<size_t> {
    auto [store, other] = edge.getStoreAndOther();
    size_t sindex = store->getNode(), lindex = other->getNode();
    if (sindex == lindex) return sindex;
    return {};
  }
  auto optOrth(Graph g) -> std::optional<BitSet> {
    const size_t maxDepth = calcMaxDepth();
    // check for orthogonalization opportunities
    bool tryOrth = false;
    for (auto &edge : edges) {
      if (edge.inputIsLoad() == edge.outputIsLoad()) continue;
      Optional<size_t> maybeIndex = getOverlapIndex(edge);
      if (!maybeIndex) continue;
      size_t index = *maybeIndex;
      ScheduledNode &node = nodes[index];
      DensePtrMatrix<int64_t> indMat = edge.getInIndMat();
      if (node.phiIsScheduled(0) || (indMat != edge.getOutIndMat())) continue;
      size_t r = NormalForm::rank(indMat);
      if (r == edge.getInNumLoops()) continue;
      // TODO handle linearly dependent acceses, filtering them out
      if (r != size_t(indMat.numRow())) continue;
      node.schedulePhi(indMat, r);
      tryOrth = true;
    }
    if (tryOrth) {
      if (std::optional<BitSet> opt = optimize(g, 0, maxDepth)) return opt;
      for (auto &&n : nodes) n.unschedulePhi();
    }
    return optimize(g, 0, maxDepth);
  }
  constexpr void addMemory(NotNull<ArrayIndex> m) {
    addMemory(MemoryAccess::construct(allocator, m));
  }
  constexpr void addMemory(MemoryAccess *m) {
#ifndef NDEBUG
    for (auto *o : memory) assert(o->getInstruction() != m->getInstruction());
#endif
    memory.push_back(m);
  }
  [[nodiscard]] static constexpr auto anyActive(const Graph &g, const BitSet &b)
    -> bool {
    return std::ranges::any_of(b, [&](size_t e) { return !g.isInactive(e); });
  }
  [[nodiscard]] static constexpr auto anyActive(const Graph &g, size_t d,
                                                const BitSet &b) -> bool {
    return std::ranges::any_of(b,
                               [&](size_t e) { return !g.isInactive(e, d); });
  }
  // assemble omni-simplex
  // we want to order variables to be
  // us, ws, Phi^-, Phi^+, omega, lambdas
  // this gives priority for minimization

  // bounding, scheduled coefs, lambda
  // matches lexicographical ordering of minimization
  // bounding, however, is to be favoring minimizing `u` over `w`
  [[nodiscard]] static constexpr auto hasActiveEdges(const Graph &g,
                                                     const MemoryAccess *mem)
    -> bool {
    return anyActive(g, mem->inputEdges()) || anyActive(g, mem->outputEdges());
  }
  [[nodiscard]] static constexpr auto
  hasActiveEdges(const Graph &g, const MemoryAccess *mem, size_t d) -> bool {
    return anyActive(g, d, mem->inputEdges()) ||
           anyActive(g, d, mem->outputEdges());
  }
  [[nodiscard]] constexpr auto hasActiveEdges(const Graph &g,
                                              const ScheduledNode &node,
                                              size_t d) const -> bool {
    return std::ranges::any_of(node.getMemory(), [&](auto memId) {
      return hasActiveEdges(g, memory[memId], d);
    });
  }
  [[nodiscard]] constexpr auto hasActiveEdges(const Graph &g,
                                              const ScheduledNode &node) const
    -> bool {
    return std::ranges::any_of(node.getMemory(), [&](auto memId) {
      return hasActiveEdges(g, memory[memId]);
    });
  }
  constexpr void setScheduleMemoryOffsets(const Graph &g, size_t d) {
    // C, lambdas, omegas, Phis
    numOmegaCoefs = 0;
    numPhiCoefs = 0;
    numSlack = 0;
    for (auto &&node : nodes) {
      // note, we had d > node.getNumLoops() for omegas earlier; why?
      if ((d >= node.getNumLoops()) || (!hasActiveEdges(g, node, d))) continue;
      numOmegaCoefs = node.updateOmegaOffset(numOmegaCoefs);
      if (node.phiIsScheduled(d)) continue;
      numPhiCoefs = node.updatePhiOffset(numPhiCoefs);
      ++numSlack;
    }
  }
#ifndef NDEBUG
  void validateEdges() {
    for (auto edge : edges) edge.validate();
  }
#endif
  void shiftOmega(ScheduledNode &node) {
    unsigned nLoops = node.getNumLoops();
    if (nLoops == 0) return;
    auto p0 = allocator.checkpoint();
    MutPtrVector<int64_t> offs = vector<int64_t>(allocator, nLoops);
    auto p1 = allocator.checkpoint();
    MutSquarePtrMatrix<int64_t> A = matrix<int64_t>(allocator, nLoops + 1);
    // MutPtrVector<BumpPtrVector<int64_t>> offsets{
    //   vector<BumpPtrVector<int64_t>>(allocator, nLoops)};
    // for (size_t i = 0; i < nLoops; ++i)
    //   offsets[i] = BumpPtrVector<int64_t>(allocator);
    // BumpPtrVector<std::pair<BitSet64, int64_t>> omegaOffsets{allocator};
    // // we check all memory accesses in the node, to see if applying the same
    // omega offsets can zero dependence offsets. If so, we apply the shift.
    // we look for offsets, then try and validate that the shift
    // if not valid, we drop it from the potential candidates.
    bool foundNonZeroOffset = false;
    unsigned rank = 0, L = nLoops - 1;
    for (size_t i : node.getMemory()) {
      MemoryAccess *mem = memory[i];
      unsigned nIdx = mem->getNode();
      for (size_t e : mem->inputEdges()) {
        const Dependence &dep = edges[e]; // other -> mem
        const DepPoly *depPoly = dep.getDepPoly();
        unsigned numSyms = depPoly->getNumSymbols(), dep0 = depPoly->getDim0(),
                 dep1 = depPoly->getDim1();
        PtrMatrix<int64_t> E = depPoly->getE();
        if (dep.input()->getNode() == nIdx) {
          unsigned depCommon = std::min(dep0, dep1),
                   depMax = std::max(dep0, dep1);
          invariant(nLoops >= depMax);
          // input and output, no relative shift of shared loops possible
          // but indices may of course differ.
          for (size_t d = 0; d < E.numRow(); ++d) {
            MutPtrVector<int64_t> x = A(rank, _);
            x[last] = E(d, 0);
            foundNonZeroOffset |= x[last] != 0;
            size_t j = 0;
            for (; j < depCommon; ++j)
              x[L - j] = E(d, j + numSyms) + E(d, j + numSyms + dep0);
            if (dep0 != dep1) {
              size_t offset = dep0 > dep1 ? numSyms : numSyms + dep0;
              for (; j < depMax; ++j) x[L - j] = E(d, j + offset);
            }
            for (; j < nLoops; ++j) x[L - j] = 0;
            rank = NormalForm::updateForNewRow(A(_(0, rank + 1), _));
          }
        } else {
          // is forward means other -> mem, else mem <- other
          unsigned offset = dep.isForward() ? numSyms + dep0 : numSyms,
                   numDep = dep.isForward() ? dep1 : dep0;
          for (size_t d = 0; d < E.numRow(); ++d) {
            MutPtrVector<int64_t> x = A(rank, _);
            x[last] = E(d, 0);
            foundNonZeroOffset |= x[last] != 0;
            size_t j = 0;
            for (; j < numDep; ++j) x[L - j] = E(d, j + offset);
            for (; j < nLoops; ++j) x[L - j] = 0;
            rank = NormalForm::updateForNewRow(A(_(0, rank + 1), _));
          }
        }
      }
      for (size_t e : mem->outputEdges()) {
        const Dependence &dep = edges[e];              // mem -> other
        if (dep.output()->getNode() == nIdx) continue; // handled above
        const DepPoly *depPoly = dep.getDepPoly();
        unsigned numSyms = depPoly->getNumSymbols(), dep0 = depPoly->getDim0(),
                 dep1 = depPoly->getDim1();
        PtrMatrix<int64_t> E = depPoly->getE();
        // is forward means mem -> other, else other <- mem
        unsigned offset = dep.isForward() ? numSyms : numSyms + dep0,
                 numDep = dep.isForward() ? dep0 : dep1;
        for (size_t d = 0; d < E.numRow(); ++d) {
          MutPtrVector<int64_t> x = A(rank, _);
          x[last] = E(d, 0);
          foundNonZeroOffset |= x[last] != 0;
          size_t j = 0;
          for (; j < numDep; ++j) x[L - j] = E(d, j + offset);
          for (; j < nLoops; ++j) x[L - j] = 0;
          rank = NormalForm::updateForNewRow(A(_(0, rank + 1), _));
        }
      }
    }
    if (!foundNonZeroOffset) return allocator.rollback(p0);
    bool nonZero = false;
    // matrix A is reasonably diagonalized, should indicate
    size_t c = 0;
    for (size_t r = 0; r < rank; ++r) {
      int64_t off = A(r, last);
      if (off == 0) continue;
      for (; c < nLoops; ++c) {
        if (A(r, c) != 0) break;
        offs[L - c] = 0;
      }
      if (c == nLoops) return;
      int64_t Arc = A(r, c), x = off / Arc;
      if (x * Arc != off) continue;
      offs[L - c++] = x; // decrement loop `L-c` by `x`
      nonZero = true;
    }
    if (!nonZero) return allocator.rollback(p0);
    allocator.rollback(p1);
    for (; c < nLoops; ++c) offs[L - c] = 0;
    node.setOffsets(offs.data());
    // now we iterate over the edges again
    // perhaps this should be abstracted into higher order functions that
    // iterate over the edges?
    for (size_t i : node.getMemory()) {
      MemoryAccess *mem = memory[i];
      unsigned nIdx = mem->getNode();
      for (size_t e : mem->inputEdges()) {
        Dependence &dep = edges[e]; // other -> mem
        dep.copySimplices(allocator);
        DepPoly *depPoly = dep.getDepPoly();
        unsigned numSyms = depPoly->getNumSymbols(), dep0 = depPoly->getDim0(),
                 dep1 = depPoly->getDim1();
        MutPtrMatrix<int64_t> satL = dep.getSatLambda();
        MutPtrMatrix<int64_t> bndL = dep.getBndLambda();
        bool pick = dep.isForward(), repeat = dep.input()->getNode() == nIdx;
        while (true) {
          unsigned offset = pick ? numSyms + dep0 : numSyms,
                   numDep = pick ? dep1 : dep0;
          for (size_t l = 0; l < numDep; ++l) {
            int64_t mlt = offs[l];
            if (mlt == 0) continue;
            satL(0, _) -= mlt * satL(offset + l, _);
            bndL(0, _) -= mlt * bndL(offset + l, _);
          }
          if (!repeat) break;
          repeat = false;
          pick = !pick;
        }
      }
      for (size_t e : mem->outputEdges()) {
        Dependence &dep = edges[e];                    // mem -> other
        if (dep.output()->getNode() == nIdx) continue; // handled above
        dep.copySimplices(allocator); // we don't want to copy twice
        DepPoly *depPoly = dep.getDepPoly();
        unsigned numSyms = depPoly->getNumSymbols(), dep0 = depPoly->getDim0(),
                 dep1 = depPoly->getDim1();
        MutPtrMatrix<int64_t> satL = dep.getSatLambda();
        MutPtrMatrix<int64_t> bndL = dep.getBndLambda();
        unsigned offset = dep.isForward() ? numSyms : numSyms + dep0,
                 numDep = dep.isForward() ? dep0 : dep1;
        for (size_t l = 0; l < numDep; ++l) {
          int64_t mlt = offs[l];
          if (mlt == 0) continue;
          satL(0, _) -= mlt * satL(offset + l, _);
          bndL(0, _) -= mlt * bndL(offset + l, _);
        }
      }
    }
  }
  void shiftOmegas() {
    for (auto &&node : nodes) shiftOmega(node);
  }
  /// For now, we instantiate a dense simplex specifying the full problem.
  ///
  /// Eventually, the plan is to generally avoid instantiating the
  /// omni-simplex first, we solve individual problems
  ///
  /// The order of variables in the simplex is:
  /// C, lambdas, slack, omegas, Phis, w, u
  /// where
  /// C: constraints, rest of matrix * variables == C
  /// lambdas: farkas multipliers
  /// slack: slack variables from independent phi solution constraints
  /// omegas: scheduling offsets
  /// Phis: scheduling rotations
  /// w: bounding offsets, independent of symbolic variables
  /// u: bounding offsets, dependent on symbolic variables
  auto instantiateOmniSimplex(const Graph &g, size_t d, bool satisfyDeps)
    -> std::unique_ptr<Simplex> {
    auto omniSimplex = Simplex::create(
      numConstraints + numSlack, numBounding + numActiveEdges + numPhiCoefs +
                                   numOmegaCoefs + numSlack + numLambda);
    auto C{omniSimplex->getConstraints()};
    C << 0;
    // layout of omniSimplex:
    // Order: C, then rev-priority to minimize
    // C, lambdas, slack, omegas, Phis, w, u
    // rows give constraints; each edge gets its own
    // numBounding = num u
    // numActiveEdges = num w
    Row c = 0;
    Col l = 1, o = 1 + numLambda + numSlack, p = o + numOmegaCoefs,
        w = p + numPhiCoefs, u = w + numActiveEdges;
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (g.isInactive(e, d)) continue;
      size_t outNodeIndex = edge.nodeOut(), inNodeIndex = edge.nodeIn();
      const auto [satC, satL, satPp, satPc, satO, satW] =
        edge.splitSatisfaction();
      const auto [bndC, bndL, bndPp, bndPc, bndO, bndWU] = edge.splitBounding();
      const size_t numSatConstraints = satC.size(),
                   numBndConstraints = bndC.size();
      const Col nPc = satPc.numCol(), nPp = satPp.numCol();
      invariant(nPc, bndPc.numCol());
      invariant(nPp, bndPp.numCol());
      const ScheduledNode &outNode = nodes[outNodeIndex];
      const ScheduledNode &inNode = nodes[inNodeIndex];

      Row cc = c + numSatConstraints;
      Row ccc = cc + numBndConstraints;

      Col ll = l + satL.numCol();
      Col lll = ll + bndL.numCol();
      C(_(c, cc), _(l, ll)) << satL;
      C(_(cc, ccc), _(ll, lll)) << bndL;
      l = lll;
      // bounding
      C(_(cc, ccc), w++) << bndWU(_, 0);
      Col uu = u + bndWU.numCol() - 1;
      C(_(cc, ccc), _(u, uu)) << bndWU(_, _(1, end));
      u = uu;
      if (satisfyDeps) C(_(c, cc), 0) << satC + satW;
      else C(_(c, cc), 0) << satC;
      C(_(cc, ccc), 0) << bndC;
      // now, handle Phi and Omega
      // phis are not constrained to be 0
      if (outNodeIndex == inNodeIndex) {
        if (d < outNode.getNumLoops()) {
          if (nPc == nPp) {
            if (outNode.phiIsScheduled(d)) {
              // add it constants
              auto sch = outNode.getSchedule(d);
              C(_(c, cc), 0) -= satPc * sch[_(0, nPc)] + satPp * sch[_(0, nPp)];
              C(_(cc, ccc), 0) -=
                bndPc * sch[_(0, nPc)] + bndPp * sch[_(0, nPp)];
            } else {
              // FIXME: phiChild = [14:18), 4 cols
              // while Dependence seems to indicate 2
              // loops why the disagreement?
              auto po = outNode.getPhiOffset() + p;
              C(_(c, cc), _(po, po + nPc)) << satPc + satPp;
              C(_(cc, ccc), _(po, po + nPc)) << bndPc + bndPp;
            }
          } else if (outNode.phiIsScheduled(d)) {
            // add it constants
            // note that loop order in schedule goes
            // inner -> outer
            // so we need to drop inner most if one has less
            auto sch = outNode.getSchedule(d);
            auto schP = sch[_(0, nPp)];
            auto schC = sch[_(0, nPc)];
            C(_(c, cc), 0) -= satPc * schC + satPp * schP;
            C(_(cc, ccc), 0) -= bndPc * schC + bndPp * schP;
          } else if (nPc < nPp) {
            // Pp has more cols, so outer/leftmost overlap
            auto po = outNode.getPhiOffset() + p, poc = po + nPc,
                 pop = po + nPp;
            C(_(c, cc), _(po, poc)) << satPc + satPp(_, _(0, nPc));
            C(_(cc, ccc), _(po, poc)) << bndPc + bndPp(_, _(0, nPc));
            C(_(c, cc), _(poc, pop)) << satPp(_, _(nPc, end));
            C(_(cc, ccc), _(poc, pop)) << bndPp(_, _(nPc, end));
          } else /* if (nPc > nPp) */ {
            auto po = outNode.getPhiOffset() + p, poc = po + nPc,
                 pop = po + nPp;
            C(_(c, cc), _(po, pop)) << satPc(_, _(0, nPp)) + satPp;
            C(_(cc, ccc), _(po, pop)) << bndPc(_, _(0, nPp)) + bndPp;
            C(_(c, cc), _(pop, poc)) << satPc(_, _(nPp, end));
            C(_(cc, ccc), _(pop, poc)) << bndPc(_, _(nPp, end));
          }
          C(_(c, cc), outNode.getOmegaOffset() + o) << satO(_, 0) + satO(_, 1);
          C(_(cc, ccc), outNode.getOmegaOffset() + o)
            << bndO(_, 0) + bndO(_, 1);
        }
      } else {
        if (d < edge.getOutNumLoops())
          updateConstraints(C, outNode, satPc, bndPc, d, c, cc, ccc, p);
        if (d < edge.getInNumLoops()) {
          if (d < edge.getOutNumLoops() && !inNode.phiIsScheduled(d) &&
              !outNode.phiIsScheduled(d)) {
            invariant(inNode.getPhiOffset() != outNode.getPhiOffset());
          }
          updateConstraints(C, inNode, satPp, bndPp, d, c, cc, ccc, p);
        }
        // Omegas are included regardless of rotation
        if (d < edge.getOutNumLoops()) {
          if (d < edge.getInNumLoops())
            invariant(inNode.getOmegaOffset() != outNode.getOmegaOffset());
          C(_(c, cc), outNode.getOmegaOffset() + o)
            << satO(_, edge.isForward());
          C(_(cc, ccc), outNode.getOmegaOffset() + o)
            << bndO(_, edge.isForward());
        }
        if (d < edge.getInNumLoops()) {
          C(_(c, cc), inNode.getOmegaOffset() + o)
            << satO(_, !edge.isForward());
          C(_(cc, ccc), inNode.getOmegaOffset() + o)
            << bndO(_, !edge.isForward());
        }
      }
      c = ccc;
    }
    invariant(size_t(l), size_t(1 + numLambda));
    invariant(size_t(c), size_t(numConstraints));
    addIndependentSolutionConstraints(omniSimplex.get(), g, d);
    return omniSimplex;
  }
  static void updateConstraints(MutPtrMatrix<int64_t> C,
                                const ScheduledNode &node,
                                PtrMatrix<int64_t> sat, PtrMatrix<int64_t> bnd,
                                size_t d, Row c, Row cc, Row ccc, Col p) {
    invariant(sat.numCol(), bnd.numCol());
    if (node.phiIsScheduled(d)) {
      // add it constants
      auto sch = node.getSchedule(d)[_(0, sat.numCol())];
      // order is inner <-> outer
      // so we need the end of schedule if it is larger
      C(_(c, cc), 0) -= sat * sch;
      C(_(cc, ccc), 0) -= bnd * sch;
    } else {
      // add it to C
      auto po = node.getPhiOffset() + p;
      C(_(c, cc), _(po, po + sat.numCol())) << sat;
      C(_(cc, ccc), _(po, po + bnd.numCol())) << bnd;
    }
  }
  [[nodiscard]] auto solveGraph(Graph &g, size_t depth, bool satisfyDeps)
    -> std::optional<BitSet> {
    if (numLambda == 0) {
      setSchedulesIndependent(g, depth);
      return checkEmptySatEdges(g, depth);
    }
    auto omniSimplex = instantiateOmniSimplex(g, depth, satisfyDeps);
    if (omniSimplex->initiateFeasible()) return std::nullopt;
    auto sol = omniSimplex->rLexMinStop(numLambda + numSlack);
    assert(sol.size() ==
           numBounding + numActiveEdges + numPhiCoefs + numOmegaCoefs);
    updateSchedules(g, depth, sol);
    return deactivateSatisfiedEdges(g, depth,
                                    sol[_(numPhiCoefs + numOmegaCoefs, end)]);
  }
  auto checkEmptySatEdges(Graph &g, size_t depth) -> BitSet {
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (g.isInactive(e, depth)) continue;
      size_t inIndex = edge.nodeIn(), outIndex = edge.nodeOut();
      const ScheduledNode &inNode = nodes[inIndex], &outNode = nodes[outIndex];
      DensePtrMatrix<int64_t> inPhi = inNode.getPhi()(_(0, depth + 1), _),
                              outPhi = outNode.getPhi()(_(0, depth + 1), _);
      if (edge.checkEmptySat(allocator, inNode.getLoopNest(),
                             inNode.getOffset(), inPhi, outNode.getLoopNest(),
                             outNode.getOffset(), outPhi)) {
        g.activeEdges.remove(e);
      }
    }
    return BitSet{};
  }
  [[nodiscard]] auto deactivateSatisfiedEdges(Graph &g, size_t depth,
                                              Simplex::Solution sol) -> BitSet {
    if (allZero(sol[_(begin, numBounding + numActiveEdges)]))
      return checkEmptySatEdges(g, depth);
    size_t w = 0, u = numActiveEdges;
    BitSet deactivated{};
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (g.isInactive(e, depth)) continue;
      size_t inIndex = edge.nodeIn(), outIndex = edge.nodeOut();
      Col uu = u + edge.getNumDynamicBoundingVar();
      if ((sol[w++] != 0) || (anyNEZero(sol[_(u, uu)]))) {
        g.activeEdges.remove(e);
        deactivated.insert(e);
        edge.setSatLevelLP(depth);
      } else {
        const ScheduledNode &inNode = nodes[inIndex],
                            &outNode = nodes[outIndex];
        DensePtrMatrix<int64_t> inPhi = inNode.getPhi()(_(0, depth + 1), _),
                                outPhi = outNode.getPhi()(_(0, depth + 1), _);
        if (edge.checkEmptySat(allocator, inNode.getLoopNest(),
                               inNode.getOffset(), inPhi, outNode.getLoopNest(),
                               outNode.getOffset(), outPhi)) {
          g.activeEdges.remove(e);
        }
      }
      u = size_t(uu);
    }
    return deactivated;
  }
  static void setDepFreeSchedule(PtrVector<MemoryAccess *> mem,
                                 ScheduledNode &node, size_t depth) {
    node.getOffsetOmega(depth) = 0;
    if (node.phiIsScheduled(depth)) return;
    // we'll check the null space of the phi's so far
    // and then search for array indices
    if (depth == 0) {
      // for now, if depth == 0, we just set last active
      MutPtrVector<int64_t> phiv{node.getSchedule(0)};
      phiv[_(0, last)] << 0;
      phiv[last] = 1;
      return;
    }
    DenseMatrix<int64_t> nullSpace; // d x lfull
    DenseMatrix<int64_t> A{node.getPhi()(_(0, depth), _).transpose()};
    NormalForm::nullSpace11(nullSpace, A);
    invariant(size_t(nullSpace.numRow()), node.getNumLoops() - depth);
    // now, we search index matrices for schedules not in the null space of
    // existing phi.
    // Here, we collect candidates for the next schedule
    DenseMatrix<int64_t> candidates{DenseDims{0, node.getNumLoops() + 1}};
    Vector<int64_t> indv;
    indv.resizeForOverwrite(node.getNumLoops());
    for (size_t ind : node.getMemory()) {
      PtrMatrix<int64_t> indMat = mem[ind]->indexMatrix(); // lsub x d
      A.resizeForOverwrite(DenseDims{nullSpace.numRow(), indMat.numCol()});
      A = nullSpace(_, _(0, indMat.numRow())) * indMat;
      // we search A for rows that aren't all zero
      for (size_t d = 0; d < A.numCol(); ++d) {
        if (allZero(A(_, d))) continue;
        indv << indMat(_, d);
        bool found = false;
        for (size_t j = 0; j < candidates.numRow(); ++j) {
          if (candidates(j, _(0, last)) != indv) continue;
          found = true;
          ++candidates(j, 0);
          break;
        }
        if (!found) {
          candidates.resize(candidates.numRow() + 1);
          assert(candidates(last, 0) == 0);
          candidates(last, _(1, end)) << indv;
        }
      }
    }
    if (Row R = candidates.numRow()) {
      // >= 1 candidates, pick the one with with greatest lex, favoring
      // number of repetitions (which were placed in first index)
      size_t i = 0;
      for (size_t j = 1; j < candidates.numRow(); ++j)
        if (candidates(j, _) > candidates(i, _)) i = j;
      node.getSchedule(depth) << candidates(i, _(1, end));
      return;
    }
    // do we want to pick the outermost original loop,
    // or do we want to pick the outermost lex null space?
    node.getSchedule(depth) << 0;
    for (size_t c = 0; c < nullSpace.numCol(); ++c) {
      if (allZero(nullSpace(_, c))) continue;
      node.getSchedule(depth)[c] = 1;
      return;
    }
    invariant(false);
  }
  void updateSchedules(const Graph &g, size_t depth, Simplex::Solution sol) {
#ifndef NDEBUG
    if (numPhiCoefs > 0)
      assert(
        std::ranges::any_of(sol, [](Rational s) -> bool { return s != 0; }));
#endif
    size_t o = numOmegaCoefs;
    for (auto &&node : nodes) {
      if (depth >= node.getNumLoops()) continue;
      if (!hasActiveEdges(g, node)) {
        setDepFreeSchedule(memory, node, depth);
        continue;
      }
      Rational sOmega = sol[node.getOmegaOffset()];
      // TODO: handle s.denominator != 1
      if (!node.phiIsScheduled(depth)) {
        auto phi = node.getSchedule(depth);
        auto s = sol[node.getPhiOffsetRange() + o];
        int64_t baseDenom = sOmega.denominator;
        int64_t l = lcm(s.denomLCM(), baseDenom);
#ifndef NDEBUG
        for (size_t i = 0; i < phi.size(); ++i)
          assert(((s[i].numerator * l) / (s[i].denominator)) >= 0);
#endif
        if (l == 1) {
          node.getOffsetOmega(depth) = sOmega.numerator;
          for (size_t i = 0; i < phi.size(); ++i) phi[i] = s[i].numerator;
        } else {
          node.getOffsetOmega(depth) = (sOmega.numerator * l) / baseDenom;
          for (size_t i = 0; i < phi.size(); ++i)
            phi[i] = (s[i].numerator * l) / (s[i].denominator);
        }
        assert(!(allZero(phi)));
      } else {
        node.getOffsetOmega(depth) = sOmega.numerator;
      }
#ifndef NDEBUG
      if (!node.phiIsScheduled(depth)) {
        int64_t l = sol[node.getPhiOffsetRange() + o].denomLCM();
        for (size_t i = 0; i < node.getPhi().numCol(); ++i)
          assert(node.getPhi()(depth, i) ==
                 sol[node.getPhiOffsetRange() + o][i] * l);
      }
#endif
    }
  }
  // Note this is based on the assumption that original loops are in
  // outer<->inner order. With that assumption, using lexSign on the null
  // space will tend to preserve the original traversal order.
  [[nodiscard]] static constexpr auto lexSign(PtrVector<int64_t> x) -> int64_t {
    for (auto a : x)
      if (a) return 2 * (a > 0) - 1;
    invariant(false);
    return 0;
  }
  void addIndependentSolutionConstraints(NotNull<Simplex> omniSimplex,
                                         const Graph &g, size_t d) {
    // omniSimplex->setNumCons(omniSimplex->getNumCons() +
    //                                memory.size());
    // omniSimplex->reserveExtraRows(memory.size());
    auto C{omniSimplex->getConstraints()};
    size_t i = size_t{C.numRow()} - numSlack, s = numLambda,
           o = 1 + numSlack + numLambda + numOmegaCoefs;
    if (d == 0) {
      // add ones >= 0
      for (auto &&node : nodes) {
        if (node.phiIsScheduled(d) || (!hasActiveEdges(g, node, d))) continue;
        C(i, 0) = 1;
        C(i, node.getPhiOffsetRange() + o) << 1;
        C(i++, ++s) = -1; // for >=
      }
    } else {
      DenseMatrix<int64_t> A, N;
      for (auto &&node : nodes) {
        if (node.phiIsScheduled(d) || (d >= node.getNumLoops()) ||
            (!hasActiveEdges(g, node, d)))
          continue;
        A.resizeForOverwrite(Row{size_t(node.getPhi().numCol())}, Col{d});
        A << node.getPhi()(_(0, d), _).transpose();
        NormalForm::nullSpace11(N, A);
        // we add sum(NullSpace,dims=1) >= 1
        // via 1 = sum(NullSpace,dims=1) - s, s >= 0
        C(i, 0) = 1;
        MutPtrVector<int64_t> cc{C(i, node.getPhiOffsetRange() + o)};
        // sum(N,dims=1) >= 1 after flipping row signs to be lex > 0
        for (size_t m = 0; m < N.numRow(); ++m)
          cc += N(m, _) * lexSign(N(m, _));
        C(i++, ++s) = -1; // for >=
      }
    }
    invariant(size_t(omniSimplex->getNumCons()), i);
    assert(!allZero(omniSimplex->getConstraints()(last, _)));
  }
  [[nodiscard]] static auto nonZeroMask(const AbstractVector auto &x)
    -> uint64_t {
    assert(x.size() <= 64);
    uint64_t m = 0;
    for (auto y : x) m = ((m << 1) | (y != 0));
    return m;
  }
  static void nonZeroMasks(Vector<uint64_t> &masks,
                           const AbstractMatrix auto &A) {
    const auto [M, N] = A.size();
    assert(N <= 64);
    masks.resizeForOverwrite(M);
    for (size_t m = 0; m < M; ++m) masks[m] = nonZeroMask(A(m, _));
  }
  [[nodiscard]] static auto nonZeroMasks(const AbstractMatrix auto &A)
    -> Vector<uint64_t> {
    Vector<uint64_t> masks;
    nonZeroMasks(masks, A);
    return masks;
  }
  [[nodiscard]] static auto nonZeroMask(const AbstractMatrix auto A)
    -> uint64_t {
    const auto [M, N] = A.size();
    assert(N <= 64);
    uint64_t mask = 0;
    for (size_t m = 0; m < M; ++m) mask |= nonZeroMask(A(m, _));
    return mask;
  }
  void setSchedulesIndependent(const Graph &g, size_t depth) {
    // IntMatrix A, N;
    for (auto &&node : nodes) {
      if ((depth >= node.getNumLoops()) || node.phiIsScheduled(depth)) continue;
      // we should only be here if numLambda==0
      assert(!hasActiveEdges(g, node));
      setDepFreeSchedule(memory, node, depth);
    }
  }
  void resetPhiOffsets() {
    for (auto &&node : nodes) node.resetPhiOffset();
  }
  [[nodiscard]] auto isSatisfied(Dependence &e, size_t d) -> bool {
    size_t inIndex = e.nodeIn();
    size_t outIndex = e.nodeOut();
    AffineSchedule first = nodes[inIndex].getSchedule();
    AffineSchedule second = nodes[outIndex].getSchedule();
    if (!e.isForward()) std::swap(first, second);
    if (!e.isSatisfied(allocator, first, second, d)) return false;
    return true;
  }
  [[nodiscard]] auto canFuse(Graph &g0, Graph &g1, size_t d) -> bool {
    for (auto e : edges) {
      if ((e.getInNumLoops() <= d) || (e.getOutNumLoops() <= d)) return false;
      if (connects(e, g0, g1))
        if (!isSatisfied(e, d)) return false;
    }
    return true;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto breakGraph(Graph g, size_t d) -> std::optional<BitSet> {
    llvm::SmallVector<BitSet> components;
    Graphs::stronglyConnectedComponents(components, g);
    if (components.size() <= 1) return {};
    // components are sorted in topological order.
    // We split all of them, solve independently,
    // and then try to fuse again after if/where optimal schedules
    // allow it.
    auto graphs = g.split(components);
    assert(graphs.size() == components.size());
    BitSet satDeps{};
    for (auto &sg : graphs) {
      if (d >= sg.calcMaxDepth()) continue;
      countAuxParamsAndConstraints(sg, d);
      setScheduleMemoryOffsets(sg, d);
      if (std::optional<BitSet> sat = solveGraph(sg, d, false)) satDeps |= *sat;
      else return {}; // give up
    }
    int64_t unfusedOffset = 0;
    // For now, just greedily try and fuse from top down
    // we do this by setting the Omegas in a loop.
    // If fusion is legal, we don't increment the Omega offset.
    // else, we do.
    Graph *gp = graphs.data();
    Vector<unsigned> baseGraphs;
    baseGraphs.push_back(0);
    for (size_t i = 1; i < components.size(); ++i) {
      Graph &gi = graphs[i];
      if (!canFuse(*gp, gi, d)) {
        // do not fuse
        for (auto &&v : *gp) v.getFusionOmega(d) = unfusedOffset;
        ++unfusedOffset;
        // gi is the new base graph
        gp = &gi;
        baseGraphs.push_back(i);
      } else // fuse
        (*gp) |= gi;
    }
    // set omegas for gp
    for (auto &&v : *gp) v.getFusionOmega(d) = unfusedOffset;
    ++d;
    // size_t numSat = satDeps.size();
    for (auto i : baseGraphs)
      if (std::optional<BitSet> sat =
            optimize(graphs[i], d, graphs[i].calcMaxDepth())) {
        // TODO: try and satisfy extra dependences
        // if ((numSat > 0) && (sat->size()>0)){}
        satDeps |= *sat;
      } else {
        return {};
      }
    // remove
    return satDeps;
  }
  static constexpr auto numParams(const Dependence &edge)
    -> math::SVector<size_t, 4> {
    return {edge.getNumLambda(), edge.getDynSymDim(), edge.getNumConstraints(),
            1};
  }
  template <typename F> void for_each_edge(const Graph &g, size_t d, F &&f) {
    for (size_t e = 0; e < edges.size(); ++e) {
      if (g.isInactive(e, d)) continue;
      f(edges[e]);
    }
  }
  constexpr void countAuxParamsAndConstraints(const Graph &g, size_t d) {
    math::SVector<size_t, 4> params{};
    assert(allZero(params));
    for (auto &&e : g.getEdges(d)) params += numParams(e);
    numLambda = params[0];
    numBounding = params[1];
    numConstraints = params[2];
    numActiveEdges = params[3];
  }
  constexpr void countAuxAndStash(const Graph &g, size_t d) {
    math::SVector<size_t, 4> params{};
    assert(allZero(params));
    for (auto &&e : g.getEdges(d)) params += numParams(e.stashSatLevel());
    numLambda = params[0];
    numBounding = params[1];
    numConstraints = params[2];
    numActiveEdges = params[3];
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto optimizeSatDep(Graph g, size_t d, size_t maxDepth,
                                    BitSet depSatLevel, BitSet activeEdges)
    -> BitSet {
    // if we're here, there are satisfied deps in both
    // depSatLevel and depSatNest
    // what we want to know is, can we satisfy all the deps
    // in depSatNest?
    // backup in case we fail
    // activeEdges was the old original; swap it in
    BitSet oldEdges = g.activeEdges, nodeIds = g.nodeIds;
    g.activeEdges = activeEdges;
    auto chckpt = allocator.checkpoint();
    auto oldSchedules = vector<AffineSchedule>(allocator, g.nodeIds.size());
    // oldSchedules.reserve(g.nodeIds.size()); // is this worth it?
    size_t i = 0;
    for (auto &n : g) oldSchedules[i++] = n.getSchedule().copy(allocator);
    countAuxAndStash(g, d);
    setScheduleMemoryOffsets(g, d);
    if (std::optional<BitSet> depSat = solveGraph(g, d, true))
      if (std::optional<BitSet> depSatN = optimize(g, d + 1, maxDepth))
        return *depSat |= *depSatN;
    // we failed, so reset solved schedules
    std::swap(g.nodeIds, nodeIds);
    g.activeEdges = activeEdges; // undo such that g.getEdges(d) is correct
    for (auto &&e : g.getEdges(d)) e.popSatLevel();
    g.activeEdges = oldEdges;    // restore backup
    auto *oldNodeIter = oldSchedules.begin();
    for (auto &&n : g) n.getSchedule() = *(oldNodeIter++);
    allocator.rollback(chckpt);
    return depSatLevel;
  }
  /// optimize at depth `d`
  /// receives graph by value, so that it is not invalidated when
  /// recursing
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto optimize(Graph g, size_t d, size_t maxDepth)
    -> std::optional<BitSet> {
    if (d >= maxDepth) return BitSet{};
    countAuxParamsAndConstraints(g, d);
    setScheduleMemoryOffsets(g, d);
    // if we fail on this level, break the graph
    BitSet activeEdgesBackup = g.activeEdges;
    if (std::optional<BitSet> depSat = solveGraph(g, d, false)) {
      const size_t dp1 = d + 1;
      if (dp1 == maxDepth) return *depSat;
      if (std::optional<BitSet> depSatNest = optimize(g, dp1, maxDepth)) {
        bool depSatEmpty = depSat->empty();
        *depSat |= *depSatNest;
        if (!(depSatEmpty || depSatNest->empty())) // try and sat all this level
          return optimizeSatDep(g, d, maxDepth, *depSat, activeEdgesBackup);
        return *depSat;
      }
    }
    return breakGraph(g, d);
  }
  // returns a BitSet indicating satisfied dependencies
  [[nodiscard]] auto optimize() -> std::optional<BitSet> {
    fillEdges();
    buildGraph();
    shiftOmegas();
#ifndef NDEBUG
    validateEdges();
#endif
    return optOrth(fullGraph());
  }
  auto summarizeMemoryAccesses(llvm::raw_ostream &os) const
    -> llvm::raw_ostream & {
    os << "MemoryAccesses:\n";
    for (const auto *m : memory) {
      os << "Inst: " << *m->getInstruction()
         << "\nOrder: " << m->getFusionOmega() << "\nLoop:" << *m->getLoop()
         << "\n";
    }
    return os;
  }
  friend inline auto operator<<(llvm::raw_ostream &os, const LoopBlock &lblock)
    -> llvm::raw_ostream & {
    os << "\nLoopBlock graph (#nodes = " << lblock.nodes.size() << "):\n";
    for (size_t i = 0; i < lblock.nodes.size(); ++i) {
      const auto &v = lblock.getNode(i);
      os << "v_" << i << ":\nmem =\n";
      for (auto m : v.getMemory())
        os << *lblock.memory[m]->getInstruction() << "\n";
      os << v << "\n";
    }
    // BitSet
    os << "\nLoopBlock Edges (#edges = " << lblock.edges.size() << "):";
    for (const auto &edge : lblock.edges) {
      os << "\n\n\tEdge = " << edge;
      size_t inIndex = edge.nodeIn();
      const AffineSchedule sin = lblock.getNode(inIndex).getSchedule();
      os << "Schedule In: nodeIndex = " << edge.nodeIn()
         << "\ns.getPhi() =" << sin.getPhi()
         << "\ns.getFusionOmega() = " << sin.getFusionOmega()
         << "\ns.getOffsetOmega() = " << sin.getOffsetOmega();

      size_t outIndex = edge.nodeOut();
      const AffineSchedule sout = lblock.getNode(outIndex).getSchedule();
      os << "\n\nSchedule Out: nodeIndex = " << edge.nodeOut()
         << "\ns.getPhi() =" << sout.getPhi()
         << "\ns.getFusionOmega() = " << sout.getFusionOmega()
         << "\ns.getOffsetOmega() = " << sout.getOffsetOmega();

      os << "\n\n";
    }
    os << "\nLoopBlock schedule (#mem accesses = " << lblock.memory.size()
       << "):\n\n";
    for (const auto &mem : lblock.memory) {
      os << "Ref = " << *mem->getArrayRef();
      size_t nodeIndex = mem->getNode();
      const AffineSchedule s = lblock.getNode(nodeIndex).getSchedule();
      os << "\nnodeIndex = " << nodeIndex << "\ns.getPhi()" << s.getPhi()
         << "\ns.getFusionOmega() = " << s.getFusionOmega()
         << "\ns.getOffsetOmega() = " << s.getOffsetOmega() << "\n";
    }
    return os << "\n";
  }
};

template <> struct std::iterator_traits<LoopBlock::Graph> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = ScheduledNode;
  using reference_type = ScheduledNode &;
  using pointer_type = ScheduledNode *;
};
// static_assert(std::ranges::range<LoopBlock::Graph>);
static_assert(Graphs::AbstractIndexGraph<LoopBlock::Graph>);
static_assert(std::is_trivially_destructible_v<LoopBlock::Graph>);
} // namespace poly::lp
