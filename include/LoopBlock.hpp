#pragma once

#include "./Address.hpp"
#include "./BitSets.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Graphs.hpp"
#include "./Loops.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "Containers/BumpMapSet.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Math/NormalForm.hpp"
#include "Math/Simplex.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Optional.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
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

template <std::integral I>
constexpr void insertSortedUnique(Vector<I> &v, const I &x) {
  for (auto it = v.begin(), ite = v.end(); it != ite; ++it) {
    if (*it < x) continue;
    if (*it > x) v.insert(it, x);
    return;
  }
  v.push_back(x);
}

/// ScheduledNode
/// Represents a set of memory accesses that are optimized together in the LP.
/// These instructions are all connected directly by through registers.
/// E.g., `A[i] = B[i] + C[i]` is a single node
/// because we load from `B[i]` and `C[i]` into registers, compute, and
/// `A[i]`;
struct ScheduledNode {
  using BitSet = ::MemoryAccess::BitSet;

private:
  [[no_unique_address]] BitSet memory{};
  [[no_unique_address]] BitSet inNeighbors{};
  [[no_unique_address]] BitSet outNeighbors{};
  [[no_unique_address]] AffineSchedule schedule{};
  [[no_unique_address]] uint32_t storeId;
  [[no_unique_address]] uint32_t phiOffset{0};   // used in LoopBlock
  [[no_unique_address]] uint32_t omegaOffset{0}; // used in LoopBlock
  [[no_unique_address]] uint8_t numLoops{0};
  [[no_unique_address]] uint8_t rank{0};
  [[no_unique_address]] bool visited{false};

public:
  constexpr ScheduledNode(unsigned int sId, MemoryAccess *store,
                          unsigned int nodeIndex)
    : storeId(sId) {
    addMemory(sId, store, nodeIndex);
  }
  [[nodiscard]] constexpr auto
  getMemAccesses(BumpAlloc<> &alloc,
                 llvm::ArrayRef<MemoryAccess *> memAccess) const
    -> Vector<Address *> {
    // First, we invert the schedule matrix.
    SquarePtrMatrix<int64_t> Phi = schedule.getPhi();
    auto [Pinv, s] = NormalForm::scaledInv(Phi);
    // if (s == 1) {}
    Vector<Address *> accesses;
    accesses.reserve(memory.size());
    for (auto i : memory) {
      // TODO: cache!
      NotNull<AffineLoopNest<false>> loop =
        memAccess[i]->getLoop()->rotate(alloc, Pinv);
      accesses.push_back(Address::construct(alloc, loop, memAccess[i],
                                            i == storeId, Pinv, s,
                                            schedule.getFusionOmega()));
    }
    return accesses;
  }
  constexpr auto getMemory() -> BitSet & { return memory; }
  constexpr auto getInNeighbors() -> BitSet & { return inNeighbors; }
  constexpr auto getOutNeighbors() -> BitSet & { return outNeighbors; }
  constexpr auto getSchedule() -> AffineSchedule { return schedule; }
  [[nodiscard]] constexpr auto getMemory() const -> const BitSet & {
    return memory;
  }
  [[nodiscard]] constexpr auto getInNeighbors() const -> const BitSet & {
    return inNeighbors;
  }
  [[nodiscard]] constexpr auto getOutNeighbors() const -> const BitSet & {
    return outNeighbors;
  }
  [[nodiscard]] constexpr auto getSchedule() const -> AffineSchedule {
    return schedule;
  }
  constexpr void addOutNeighbor(unsigned int i) { outNeighbors.insert(i); }
  constexpr void addInNeighbor(unsigned int i) { inNeighbors.insert(i); }
  constexpr void init(BumpAlloc<> &alloc) {
    schedule = AffineSchedule(alloc, getNumLoops());
  }
  constexpr void addMemory(unsigned memId, MemoryAccess *mem,
                           unsigned nodeIndex) {
    mem->addNodeIndex(nodeIndex);
    memory.insert(memId);
    numLoops = std::max(numLoops, uint8_t(mem->getNumLoops()));
  }
  [[nodiscard]] constexpr auto wasVisited() const -> bool { return visited; }
  constexpr void visit() { visited = true; }
  constexpr void unVisit() { visited = false; }
  [[nodiscard]] constexpr auto getNumLoops() const -> size_t {
    return numLoops;
  }
  // 'phiIsScheduled()` means that `phi`'s schedule has been
  // set for the outer `rank` loops.
  [[nodiscard]] constexpr auto phiIsScheduled(size_t d) const -> bool {
    return d < rank;
  }

  [[nodiscard]] constexpr auto updatePhiOffset(size_t p) -> size_t {
    phiOffset = p;
    return p + numLoops;
  }
  [[nodiscard]] constexpr auto updateOmegaOffset(size_t o) -> size_t {
    omegaOffset = o;
    return ++o;
  }
  [[nodiscard]] constexpr auto getPhiOffset() const -> size_t {
    return phiOffset;
  }
  [[nodiscard]] constexpr auto getPhiOffsetRange() const
    -> Range<size_t, size_t> {
    return _(phiOffset, phiOffset + numLoops);
  }
  [[nodiscard]] constexpr auto getPhi() -> MutSquarePtrMatrix<int64_t> {
    return schedule.getPhi();
  }
  [[nodiscard]] constexpr auto getPhi() const -> SquarePtrMatrix<int64_t> {
    return schedule.getPhi();
  }
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) -> int64_t & {
    return schedule.getOffsetOmega()[i];
  }
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) const -> int64_t {
    return schedule.getOffsetOmega()[i];
  }
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) -> int64_t & {
    return schedule.getFusionOmega()[i];
  }
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) const -> int64_t {
    return schedule.getFusionOmega()[i];
  }
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return schedule.getOffsetOmega();
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return schedule.getOffsetOmega();
  }
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return schedule.getFusionOmega();
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return schedule.getFusionOmega();
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) const
    -> PtrVector<int64_t> {
    return schedule.getSchedule(d);
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) -> MutPtrVector<int64_t> {
    return schedule.getSchedule(d);
  }
  constexpr void schedulePhi(PtrMatrix<int64_t> indMat, size_t r) {
    // indMat indvars are indexed from outer<->inner
    // phi indvars are indexed from outer<->inner
    // so, indMat is indvars[outer<->inner] x array dim
    // phi is loop[outer<->inner] x indvars[outer<->inner]
    MutSquarePtrMatrix<int64_t> phi = getPhi();
    size_t indR = size_t(indMat.numRow());
    for (size_t i = 0; i < r; ++i) {
      phi(i, _(0, indR)) << indMat(_, i);
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
    for (auto m : node.getInNeighbors()) os << "v_" << m << ", ";
    os << "\noutNeighbors = ";
    for (auto m : node.getOutNeighbors()) os << "v_" << m << ", ";
    return os << "\n";
    ;
  }
};
static_assert(std::is_trivially_destructible_v<ScheduledNode>);

struct CarriedDependencyFlag {
  [[no_unique_address]] uint32_t flag{0};
  [[nodiscard]] constexpr auto carriesDependency(size_t d) const -> bool {
    return (flag >> d) & 1;
  }
  constexpr void setCarriedDependency(size_t d) {
    flag |= (uint32_t(1) << uint32_t(d));
  }
  [[nodiscard]] static constexpr auto resetMaskFlag(size_t d) -> uint32_t {
    return ((uint32_t(1) << uint32_t(d)) - uint32_t(1));
  }
  // resets all but `d` deps
  constexpr void resetDeepDeps(size_t d) { flag &= resetMaskFlag(d); }
};
constexpr void resetDeepDeps(MutPtrVector<CarriedDependencyFlag> v, size_t d) {
  uint32_t mask = CarriedDependencyFlag::resetMaskFlag(d);
  for (auto &&x : v) x.flag &= mask;
}
static_assert(!LinAlg::Printable<CarriedDependencyFlag>);

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
class LinearProgramLoopBlock {
  // TODO: figure out how to handle the graph's dependencies based on
  // operation/instruction chains.
  // Perhaps implicitly via the graph when using internal orthogonalization
  // and register tiling methods, and then generate associated constraints
  // or aliasing between schedules when running the ILP solver?
  // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
  // and all other other shared schedule parameters are aliases (i.e.,
  // identical)?
  // using VertexType = ScheduledNode;
  [[no_unique_address]] Vector<MemoryAccess *> memory;
  [[no_unique_address]] Vector<ScheduledNode, 0> nodes;
  // Vector<unsigned> memoryToNodeMap;
  [[no_unique_address]] Vector<Dependence> edges;
  /// Flag indicating which depths carries dependencies
  /// One per node; held separately so we can copy/etc
  [[no_unique_address]] Vector<CarriedDependencyFlag, 16> carriedDeps;
  // Vector<bool> visited; // visited, for traversing graph
  [[no_unique_address]] BumpAlloc<> allocator;
  // Vector<llvm::Value *> symbols;
  // we may turn off edges because we've exceeded its loop depth
  // or because the dependence has already been satisfied at an
  // earlier level.
  // Vector<bool, 256> doNotAddEdge;
  // Vector<bool, 256> scheduled;
  [[no_unique_address]] unsigned numPhiCoefs{0};
  [[no_unique_address]] unsigned numOmegaCoefs{0};
  [[no_unique_address]] unsigned numSlack{0};
  [[no_unique_address]] unsigned numLambda{0};
  [[no_unique_address]] unsigned numBounding{0};
  [[no_unique_address]] unsigned numConstraints{0};
  [[no_unique_address]] unsigned numActiveEdges{0};

public:
  using BitSet = ::MemoryAccess::BitSet;
  void clear() {
    // TODO: maybe we shouldn't have to manually call destructors?
    // That would require handling more memory allocations via
    // `allocator`, though.
    // Some objects may need to reallocate/resize.
    memory.clear();
    nodes.clear();
    edges.clear();
    carriedDeps.clear();
    allocator.reset();
  }
  [[nodiscard]] constexpr auto numVerticies() const -> size_t {
    return nodes.size();
  }
  [[nodiscard]] constexpr auto getVerticies() -> MutPtrVector<ScheduledNode> {
    return nodes;
  }
  [[nodiscard]] auto getVerticies() const -> PtrVector<ScheduledNode> {
    return nodes;
  }
  [[nodiscard]] auto getMemoryAccesses() const -> PtrVector<MemoryAccess *> {
    return memory;
  }
  auto getMemoryAccesses() -> MutPtrVector<MemoryAccess *> { return memory; }
  auto getMemoryAccess(size_t i) -> MemoryAccess * { return memory[i]; }
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
  struct OutNeighbors {
    LinearProgramLoopBlock &loopBlock;
    ScheduledNode &node;
  };
  // TODO: `constexpr` once `llvm::SmallVector` supports it
  [[nodiscard]] auto outNeighbors(size_t idx) -> OutNeighbors {
    return OutNeighbors{*this, nodes[idx]};
  }
  [[nodiscard]] auto calcMaxDepth() const -> size_t {
    size_t d = 0;
    for (const auto &mem : memory) d = std::max(d, mem->getNumLoops());
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
  static constexpr void pushToEdgeVector(Vector<Dependence> &vec,
                                         Dependence dep) {
    dep.addEdge(vec.size());
    vec.push_back(dep);
  }
  void addEdge(MemoryAccess &mai, MemoryAccess &maj) {
    // note, axes should be fully delinearized, so should line up
    // as a result of preprocessing.
    auto d = Dependence::check(allocator, mai, maj);
    for (auto &i : d) pushToEdgeVector(edges, i);
  }
  /// fills all the edges between memory accesses, checking for
  /// dependencies.
  void fillEdges() {
    // TODO: handle predicates
    for (size_t i = 1; i < memory.size(); ++i) {
      MemoryAccess &mai = *memory[i];
      for (size_t j = 0; j < i; ++j) {
        MemoryAccess &maj = *memory[j];
        if ((mai.getArrayPointer() != maj.getArrayPointer()) ||
            ((mai.isLoad()) && (maj.isLoad())))
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
  /// searching for placed it was loaded, and instead add a reload.
  [[nodiscard]] auto
  searchValueForStores(aset<llvm::User *> &visited, ScheduledNode &node,
                       amap<llvm::User *, unsigned> &userToMemory,
                       llvm::User *user, unsigned nodeIndex) -> bool {
    for (llvm::User *use : user->users()) {
      if (visited.contains(use)) continue;
      if (llvm::isa<llvm::StoreInst>(use)) {
        auto *memAccess = userToMemory.find(use);
        if (memAccess == userToMemory.end())
          continue; // load is not a part of the LoopBlock
        unsigned memId = memAccess->second;
        MemoryAccess *store = memory[memId];
        // this store will be treated as a load
        node.addMemory(memId, store, nodeIndex);
        return true;
      }
    }
    return false;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void checkUserForLoads(aset<llvm::User *> &visited, ScheduledNode &node,
                         amap<llvm::User *, unsigned> &userToMemory,
                         llvm::User *user, unsigned nodeIndex) {
    if (!user || visited.contains(user)) return;
    if (llvm::isa<llvm::LoadInst>(user)) {
      auto *memAccess = userToMemory.find(user);
      if (memAccess == userToMemory.end())
        return; // load is not a part of the LoopBlock
      unsigned memId = memAccess->second;
      node.addMemory(memId, memory[memId], nodeIndex);
    } else if (!searchValueForStores(visited, node, userToMemory, user,
                                     nodeIndex))
      searchOperandsForLoads(visited, node, userToMemory, user, nodeIndex);
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
  // NOLINTNEXTLINE(misc-no-recursion)
  void searchOperandsForLoads(aset<llvm::User *> &visited, ScheduledNode &node,
                              amap<llvm::User *, unsigned> &userToMemory,
                              llvm::User *u, unsigned nodeIndex) {
    visited.insert(u);
    if (auto *s = llvm::dyn_cast<llvm::StoreInst>(u)) {
      if (auto *user = llvm::dyn_cast<llvm::User>(s->getValueOperand()))
        checkUserForLoads(visited, node, userToMemory, user, nodeIndex);
      return;
    }
    for (auto &&op : u->operands())
      if (auto *user = llvm::dyn_cast<llvm::User>(op.get()))
        checkUserForLoads(visited, node, userToMemory, user, nodeIndex);
  }
  void connect(unsigned inIndex, unsigned outIndex) {
    nodes[inIndex].addOutNeighbor(outIndex);
    nodes[outIndex].addInNeighbor(inIndex);
  }
  // the order of parameters is irrelevant, so swapping is irrelevant
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void connect(const BitSet &inIndexSet, const BitSet &outIndexSet) {
    for (auto inIndex : inIndexSet)
      for (auto outIndex : outIndexSet) connect(inIndex, outIndex);
  }
  [[nodiscard]] auto calcNumStores() const -> size_t {
    size_t numStores = 0;
    for (const auto &m : memory) numStores += !(m->isLoad());
    return numStores;
  }
  /// When connecting a graph, we draw direct connections between stores and
  /// loads loads may be duplicated across stores to allow for greater
  /// reordering flexibility (which should generally reduce the ultimate
  /// amount of loads executed in the eventual generated code)
  void connectGraph() {
    // assembles direct connections in node graph
    auto p = allocator.scope();
    amap<llvm::User *, unsigned> userToMemory{allocator};
    for (unsigned i = 0; i < memory.size(); ++i)
      userToMemory.insert(std::make_pair(memory[i]->getInstruction(), i));

    aset<llvm::User *> visited{allocator};
    nodes.reserve(calcNumStores());
    for (unsigned i = 0; i < memory.size(); ++i) {
      MemoryAccess *mai = memory[i];
      if (mai->isLoad()) continue;
      unsigned nodeIndex = nodes.size();
      ScheduledNode &node = nodes.emplace_back(i, mai, nodeIndex);
      searchOperandsForLoads(visited, node, userToMemory, mai->getInstruction(),
                             nodeIndex);
      visited.clear();
    }
    // destructors of amap and aset poison memory
  }
  void buildGraph() {
    connectGraph();
    // now that we've assigned each MemoryAccess to a NodeIndex, we
    // build the actual graph
    for (auto e : edges) connect(e.nodesIn(), e.nodesOut());
    for (auto &&node : nodes) node.init(allocator);
  }
  struct Graph {
    // a subset of Nodes
    BitSet nodeIds{};
    BitSet activeEdges{};
    MutPtrVector<MemoryAccess *> mem;
    MutPtrVector<ScheduledNode> nodes;
    PtrVector<Dependence> edges;
    // llvm::SmallVector<bool> visited;
    // BitSet visited;
    constexpr auto operator&(const Graph &g) -> Graph {
      return Graph{nodeIds & g.nodeIds, activeEdges & g.activeEdges, mem, nodes,
                   edges};
    }
    constexpr auto operator|(const Graph &g) -> Graph {
      return Graph{nodeIds | g.nodeIds, activeEdges | g.activeEdges, mem, nodes,
                   edges};
    }
    constexpr auto operator&=(const Graph &g) -> Graph & {
      nodeIds &= g.nodeIds;
      activeEdges &= g.activeEdges;
      return *this;
    }
    constexpr auto operator|=(const Graph &g) -> Graph & {
      nodeIds |= g.nodeIds;
      activeEdges |= g.activeEdges;
      return *this;
    }
    [[nodiscard]] constexpr auto inNeighbors(size_t i) -> BitSet & {
      return nodes[i].getInNeighbors();
    }
    [[nodiscard]] constexpr auto outNeighbors(size_t i) -> BitSet & {
      return nodes[i].getOutNeighbors();
    }
    [[nodiscard]] constexpr auto inNeighbors(size_t i) const -> const BitSet & {
      return nodes[i].getInNeighbors();
    }
    [[nodiscard]] constexpr auto outNeighbors(size_t i) const
      -> const BitSet & {
      return nodes[i].getOutNeighbors();
    }
    [[nodiscard]] constexpr auto containsNode(size_t i) const -> bool {
      return nodeIds.contains(i);
    }
    [[nodiscard]] constexpr auto containsNode(BitSet &b) const -> bool {
      return std::ranges::any_of(b, nodeIds.contains());
    }
    [[nodiscard]] constexpr auto missingNode(size_t i) const -> bool {
      return !containsNode(i);
    }
    [[nodiscard]] constexpr auto missingNode(size_t i, size_t j) const -> bool {
      return !(containsNode(i) && containsNode(j));
    }
    /// returns false iff e.in and e.out are both in graph
    /// that is, to be missing, both `e.in` and `e.out` must be missing
    /// in case of multiple instances of the edge, we check all of them
    /// if any are not missing, returns false
    /// only returns true if every one of them is missing.
    [[nodiscard]] constexpr auto missingNode(const Dependence &e) const
      -> bool {
      for (auto inIndex : e.nodesIn())
        for (auto outIndex : e.nodesOut())
          if (!missingNode(inIndex, outIndex)) return false;
      return true;
    }

    [[nodiscard]] constexpr auto isInactive(const Dependence &edge,
                                            size_t d) const -> bool {
      return edge.isInactive(d) || missingNode(edge);
    }
    [[nodiscard]] constexpr auto isInactive(const Dependence &edge) const
      -> bool {
      return missingNode(edge);
    }
    [[nodiscard]] constexpr auto isInactive(size_t e, size_t d) const -> bool {
      return !(activeEdges[e]) || isInactive(edges[e], d);
    }
    [[nodiscard]] constexpr auto isInactive(size_t e) const -> bool {
      return !(activeEdges[e]) || isInactive(edges[e]);
    }
    [[nodiscard]] constexpr auto isActive(size_t e, size_t d) const -> bool {
      return (activeEdges[e]) && (!isInactive(edges[e], d));
    }
    [[nodiscard]] constexpr auto isActive(size_t e) const -> bool {
      return (activeEdges[e]) && (!isInactive(edges[e]));
    }
    [[nodiscard]] constexpr auto begin()
      -> BitSliceView<ScheduledNode, BitSet>::Iterator {
      return BitSliceView{nodes, nodeIds}.begin();
    }
    [[nodiscard]] constexpr auto begin() const
      -> BitSliceView<ScheduledNode, BitSet>::ConstIterator {
      const BitSliceView bsv{nodes, nodeIds};
      return bsv.begin();
    }
    [[nodiscard]] static constexpr auto end() -> EndSentinel { return {}; }
    [[nodiscard]] constexpr auto wasVisited(size_t i) const -> bool {
      return nodes[i].wasVisited();
    }
    constexpr void visit(size_t i) { nodes[i].visit(); }
    constexpr void unVisit(size_t i) { nodes[i].unVisit(); }
    [[nodiscard]] constexpr auto getNumVertices() const -> size_t {
      return nodeIds.size();
    }
    [[nodiscard]] constexpr auto maxVertexId() const -> size_t {
      return nodeIds.maxValue();
    }
    [[nodiscard]] constexpr auto vertexIds() -> BitSet & { return nodeIds; }
    [[nodiscard]] constexpr auto vertexIds() const -> const BitSet & {
      return nodeIds;
    }
    [[nodiscard]] constexpr auto subGraph(const BitSet &components) -> Graph {
      return {components, activeEdges, mem, nodes, edges};
    }
    [[nodiscard]] auto split(const llvm::SmallVector<BitSet> &components)
      -> Vector<Graph, 0> {
      Vector<Graph, 0> graphs;
      graphs.reserve(components.size());
      for (const auto &c : components) graphs.push_back(subGraph(c));
      return graphs;
    }
    [[nodiscard]] constexpr auto calcMaxDepth() const -> size_t {
      if (nodeIds.data.empty()) return 0;
      size_t d = 0;
      for (auto n : nodeIds) d = std::max(d, nodes[n].getNumLoops());
      return d;
    }
  };
  // bool connects(const Dependence &e, Graph &g0, Graph &g1, size_t d) const
  // {
  //     return ((e.getInNumLoops() > d) && (e.getOutNumLoops() > d)) &&
  //            connects(e, g0, g1);
  // }
  static auto connects(const Dependence &e, Graph &g0, Graph &g1) -> bool {
    if (!e.inputIsLoad()) {
      // e.in is a store
      size_t nodeIn = *e.nodesIn().begin();
      bool g0ContainsNodeIn = g0.nodeIds.contains(nodeIn);
      bool g1ContainsNodeIn = g1.nodeIds.contains(nodeIn);
      if (!(g0ContainsNodeIn || g1ContainsNodeIn)) return false;
      for (size_t nodeOut : e.nodesOut())
        if ((g0ContainsNodeIn && g1.nodeIds.contains(nodeOut)) ||
            (g1ContainsNodeIn && g0.nodeIds.contains(nodeOut)))
          return true;
    } else {
      // e.out must be a store
      size_t nodeOut = *e.nodesOut().begin();
      bool g0ContainsNodeOut = g0.nodeIds.contains(nodeOut);
      bool g1ContainsNodeOut = g1.nodeIds.contains(nodeOut);
      if (!(g0ContainsNodeOut || g1ContainsNodeOut)) return false;
      for (auto nodeIn : e.nodesIn())
        if ((g0ContainsNodeOut && g1.nodeIds.contains(nodeIn)) ||
            (g1ContainsNodeOut && g0.nodeIds.contains(nodeIn)))
          return true;
    }
    return false;
  }
  constexpr auto fullGraph() -> Graph {
    return {BitSet::dense(nodes.size()), BitSet::dense(edges.size()), memory,
            nodes, edges};
  }
  static auto getOverlapIndex(const Dependence &edge) -> Optional<size_t> {
    auto [store, other] = edge.getStoreAndOther();
    size_t index = *store->getNodeIndex().begin();
    if (other->getNodeIndex().contains(index)) return index;
    return {};
  }
  auto optOrth(Graph g) -> std::optional<BitSet> {

    const size_t maxDepth = calcMaxDepth();
    // check for orthogonalization opportunities
    bool tryOrth = false;
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (edge.inputIsLoad() == edge.outputIsLoad()) continue;
      Optional<size_t> maybeIndex = getOverlapIndex(edge);
      if (!maybeIndex) continue;
      size_t index = *maybeIndex;
      ScheduledNode &node = nodes[index];
      PtrMatrix<int64_t> indMat = edge.getInIndMat();
      if (node.phiIsScheduled(0) || (indMat != edge.getOutIndMat())) continue;
      size_t r = NormalForm::rank(indMat);
      if (r == edge.getInNumLoops()) continue;
      // TODO handle linearly dependent acceses, filtering them out
      if (r == size_t(indMat.numCol())) {
        node.schedulePhi(indMat, r);
        tryOrth = true;
      }
    }
    if (tryOrth) {
      if (std::optional<BitSet> opt = optimize(g, 0, maxDepth)) return opt;
      for (auto &&n : nodes) n.unschedulePhi();
    }
    return optimize(g, 0, maxDepth);
  }
  [[nodiscard]] constexpr auto countNumLambdas(const Graph &g, size_t d) const
    -> size_t {
    size_t c = 0;
    for (size_t e = 0; e < edges.size(); ++e)
      c += ((g.isInactive(e, d)) ? 0 : edges[e].getNumLambda());
    return c;
  }
  [[nodiscard]] constexpr auto countNumBoundingCoefs(const Graph &g,
                                                     size_t d) const -> size_t {
    size_t c = 0;
    for (size_t e = 0; e < edges.size(); ++e)
      c += (g.isInactive(e, d) ? 0 : edges[e].getNumSymbols());
    return c;
  }
  constexpr void countAuxParamsAndConstraints(const Graph &g, size_t d) {
    size_t a = 0, b = 0, c = 0, ae = 0;
    for (size_t e = 0; e < edges.size(); ++e) {
      if (g.isInactive(e, d)) continue;
      const Dependence &edge = edges[e];
      size_t mlt = edge.nodesIn().size() * edge.nodesOut().size();
      a += mlt * edge.getNumLambda();
      b += mlt * edge.getDynSymDim();
      c += mlt * edge.getNumConstraints();
      ae += mlt;
    }
    numLambda = a;
    numBounding = b;
    numConstraints = c;
    numActiveEdges = ae;
  }
  constexpr void addMemory(MemoryAccess *m) {
#ifndef NDEBUG
    for (auto *o : memory) assert(o->getInstruction() != m->getInstruction());
#endif
    memory.push_back(m);
  }
  // assemble omni-simplex
  // we want to order variables to be
  // us, ws, Phi^-, Phi^+, omega, lambdas
  // this gives priority for minimization

  // bounding, scheduled coefs, lambda
  // matches lexicographical ordering of minimization
  // bounding, however, is to be favoring minimizing `u` over `w`
  [[nodiscard]] static constexpr auto hasActiveEdges(const Graph &g,
                                                     const MemoryAccess &mem)
    -> bool {
    for (auto e : mem.inputEdges())
      if (!g.isInactive(e)) return true;
    for (auto e : mem.outputEdges())
      if (!g.isInactive(e)) return true;
    return false;
  }
  [[nodiscard]] static constexpr auto
  hasActiveEdges(const Graph &g, const MemoryAccess &mem, size_t d) -> bool {
    for (auto e : mem.inputEdges())
      if (!g.isInactive(e, d)) return true;
    for (auto e : mem.outputEdges())
      if (!g.isInactive(e, d)) return true;
    return false;
  }
  [[nodiscard]] constexpr auto hasActiveEdges(const Graph &g,
                                              const ScheduledNode &node,
                                              size_t d) const -> bool {
    // std::ranges::any_of(node.getMemory(), [&](auto memId) {
    //   return hasActiveEdges(g, *memory[memId], d);
    // });
    for (auto memId : node.getMemory())
      if (hasActiveEdges(g, *memory[memId], d)) return true;
    return false;
  }
  [[nodiscard]] constexpr auto hasActiveEdges(const Graph &g,
                                              const ScheduledNode &node) const
    -> bool {
    for (auto memId : node.getMemory())
      if (hasActiveEdges(g, *memory[memId])) return true;
    return false;
  }
  constexpr void setScheduleMemoryOffsets(const Graph &g, size_t d) {
    // C, lambdas, omegas, Phis
    numOmegaCoefs = 0;
    numPhiCoefs = 0;
    numSlack = 0;
    for (auto &&node : nodes) {
      // note, we had d > node.getNumLoops() for omegas earlier; why?
      if ((d >= node.getNumLoops()) || (!hasActiveEdges(g, node, d))) continue;
      if (!node.phiIsScheduled(d)) {
        numPhiCoefs = node.updatePhiOffset(numPhiCoefs);
        ++numSlack;
      }
      numOmegaCoefs = node.updateOmegaOffset(numOmegaCoefs);
    }
  }
#ifndef NDEBUG
  void validateEdges() {
    for (auto edge : edges) edge.validate();
  }
#endif
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
    -> Optional<Simplex *> {
    auto omniSimplex =
      Simplex::create(allocator, numConstraints + numSlack,
                      numBounding + numActiveEdges + numPhiCoefs +
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
      const BitSet &outNodeIndexSet = edge.nodesOut();
      const BitSet &inNodeIndexSet = edge.nodesIn();
      const auto [satC, satL, satPp, satPc, satO, satW] =
        edge.splitSatisfaction();
      const auto [bndC, bndL, bndPp, bndPc, bndO, bndWU] = edge.splitBounding();
      const size_t numSatConstraints = satC.size(),
                   numBndConstraints = bndC.size();
      const Col nPc = satPc.numCol(), nPp = satPp.numCol();
      invariant(nPc, bndPc.numCol());
      invariant(nPp, bndPp.numCol());
      for (auto outNodeIndex : outNodeIndexSet) {
        const ScheduledNode &outNode = nodes[outNodeIndex];
        for (auto inNodeIndex : inNodeIndexSet) {
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
                  C(_(c, cc), 0) -=
                    satPc * sch[_(0, nPc)] + satPp * sch[_(0, nPp)];
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
              C(_(c, cc), outNode.getOmegaOffset() + o)
                << satO(_, 0) + satO(_, 1);
              C(_(cc, ccc), outNode.getOmegaOffset() + o)
                << bndO(_, 0) + bndO(_, 1);
            }
          } else {
            if (d < edge.getOutNumLoops())
              updateConstraints(C, outNode, satPc, bndPc, d, c, cc, ccc, p);
            if (d < edge.getInNumLoops())
              updateConstraints(C, inNode, satPp, bndPp, d, c, cc, ccc, p);
            // Omegas are included regardless of rotation
            if (d < edge.getOutNumLoops()) {
              C(_(c, cc), outNode.getOmegaOffset() + o)
                << satO(_, !edge.isForward());
              C(_(cc, ccc), outNode.getOmegaOffset() + o)
                << bndO(_, !edge.isForward());
            }
            if (d < edge.getInNumLoops()) {
              C(_(c, cc), inNode.getOmegaOffset() + o)
                << satO(_, edge.isForward());
              C(_(cc, ccc), inNode.getOmegaOffset() + o)
                << bndO(_, edge.isForward());
            }
          }
          c = ccc;
        }
      }
    }
    invariant(size_t(c), size_t(numConstraints));
    addIndependentSolutionConstraints(omniSimplex, g, d);
    return omniSimplex->initiateFeasible() ? nullptr : (Simplex *)omniSimplex;
  }
  static void updateConstraints(MutPtrMatrix<int64_t> C,
                                const ScheduledNode &node,
                                PtrMatrix<int64_t> sat, PtrMatrix<int64_t> bnd,
                                size_t d, Row c, Row cc, Row ccc, Col p) {
    invariant(sat.numCol(), bnd.numCol());
    if (node.phiIsScheduled(d)) {
      // add it constants
      auto sch = node.getSchedule(d);
      // order is inner <-> outer
      // so we need the end of schedule if it is larger
      C(_(c, cc), 0) -= sat * sch[_(0, sat.numCol())];
      C(_(cc, ccc), 0) -= bnd * sch[_(0, bnd.numCol())];
    } else {
      // add it to C
      auto po = node.getPhiOffset() + p;
      C(_(c, cc), _(po, po + sat.numCol())) << sat;
      C(_(cc, ccc), _(po, po + bnd.numCol())) << bnd;
    }
  }
  [[nodiscard]] auto solveGraphCore(Graph &g, size_t depth, bool satisfyDeps)
    -> std::optional<BitSet> {
    auto omniSimplex = instantiateOmniSimplex(g, depth, satisfyDeps);
    if (!omniSimplex) return {};
    auto sol = omniSimplex->rLexMinStop(numLambda + numSlack);
    updateSchedules(g, depth, sol);
    return deactivateSatisfiedEdges(g, depth,
                                    sol[_(numPhiCoefs + numOmegaCoefs, end)]);
  }
  [[nodiscard]] auto solveGraph(Graph &g, size_t depth, bool satisfyDeps)
    -> std::optional<BitSet> {
    auto p = allocator.scope();
    return solveGraphCore(g, depth, satisfyDeps);
  }
  [[nodiscard]] auto deactivateSatisfiedEdges(Graph &g, size_t depth,
                                              Simplex::Solution sol) -> BitSet {
    if (allZero(sol[_(begin, numBounding + numActiveEdges)])) return {};
    // size_t u = 0, w = numBounding;
    size_t w = 0, u = numActiveEdges;
    BitSet deactivated{};
    for (size_t e = 0; e < edges.size(); ++e) {
      if (g.isInactive(e, depth)) continue;
      const Dependence &edge = edges[e];
      Col uu = u + edge.getNumDynamicBoundingVar();
      if ((sol[w++] != 0) || (!(allZero(sol[_(u, uu)])))) {
        g.activeEdges.remove(e);
        deactivated.insert(e);
        for (size_t inIndex : edge.nodesIn())
          carriedDeps[inIndex].setCarriedDependency(depth);
        for (size_t outIndex : edge.nodesOut())
          carriedDeps[outIndex].setCarriedDependency(depth);
      }
      u = size_t(uu);
    }
    return deactivated;
  }
  void updateSchedules(const Graph &g, size_t depth, Simplex::Solution sol) {
#ifndef NDEBUG
    if (depth & 1) {
      bool allZero = true;
      for (auto s : sol) allZero &= (s == 0);
      // if (allZero) llvm::errs() << "omniSimplex = " << omniSimplex << "\n";
      assert(!allZero);
    }
#endif
    size_t o = numOmegaCoefs;
    for (auto &&node : nodes) {
      if (depth >= node.getNumLoops()) continue;
      if (!hasActiveEdges(g, node)) {
        node.getOffsetOmega()[depth] = std::numeric_limits<int64_t>::min();
        if (!node.phiIsScheduled(depth))
          node.getSchedule(depth) << std::numeric_limits<int64_t>::min();
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
      if (!hasActiveEdges(g, node)) {
        node.getOffsetOmega(depth) = std::numeric_limits<int64_t>::min();
        if (!node.phiIsScheduled(depth))
          node.getSchedule(depth) << std::numeric_limits<int64_t>::min();
        continue;
      }
      node.getOffsetOmega(depth) = 0;
      node.getSchedule(depth) << std::numeric_limits<int64_t>::min();
    }
  }
  void resetPhiOffsets() {
    for (auto &&node : nodes) node.resetPhiOffset();
  }
  [[nodiscard]] auto isSatisfied(Dependence &e, size_t d) -> bool {
    for (size_t inIndex : e.nodesIn()) {
      for (size_t outIndex : e.nodesOut()) {
        AffineSchedule first = nodes[inIndex].getSchedule();
        AffineSchedule second = nodes[outIndex].getSchedule();
        if (!e.isForward()) std::swap(first, second);
        if (!e.isSatisfied(allocator, first, second, d)) return false;
      }
    }
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
      if (std::optional<BitSet> sat = optimizeLevel(sg, d)) satDeps |= *sat;
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
        for (auto &&v : *gp) v.getFusionOmega()[d] = unfusedOffset;
        ++unfusedOffset;
        // gi is the new base graph
        gp = &gi;
        baseGraphs.push_back(i);
      } else // fuse
        (*gp) |= gi;
    }
    // set omegas for gp
    for (auto &&v : *gp) v.getFusionOmega()[d] = unfusedOffset;
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
  [[nodiscard]] auto optimizeLevel(Graph &g, size_t d)
    -> std::optional<BitSet> {
    if (numPhiCoefs == 0) {
      // is this valid? what about omegas, or checking satisfiability?
      // if it is, then why not do this for all solveGraph calls?
      setSchedulesIndependent(g, d);
      return BitSet{};
    }
    return solveGraph(g, d, false);
  }
  // NOTE: the NOLINTS, maybe we should come up with a way
  // to avoid easily swappable params. For now, we just
  // double check that the single callsite is correct.
  // This is an internal function, so that should be okay.
  // Maybe it'd make sense to define some sort of API
  // around the ideas of dependency satisfaction at a level,
  // or active edges, so these BitSets can be given types.
  // But that sort of seems like abstraction for the sake of
  // abstraction, rather than actually a good idea?
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto optimizeSatDep(Graph g, size_t d, size_t maxDepth,
                                    BitSet depSatLevel,
                                    const BitSet &depSatNest,
                                    BitSet activeEdges) -> BitSet {
    // if we're here, there are satisfied deps in both
    // depSatLevel and depSatNest
    // what we want to know is, can we satisfy all the deps
    // in depSatNest?
    depSatLevel |= depSatNest;
    if (!depSatLevel.empty()) {
      // backup in case we fail
      // activeEdges was the old original; swap it in
      std::swap(g.activeEdges, activeEdges);
      BitSet nodeIds = g.nodeIds;
      Vector<AffineSchedule, 0> oldSchedules;
      for (auto &n : g) oldSchedules.push_back(n.getSchedule());
      Vector<CarriedDependencyFlag, 16> oldCarriedDeps = carriedDeps;
      resetDeepDeps(carriedDeps, d);

      countAuxParamsAndConstraints(g, d);
      setScheduleMemoryOffsets(g, d);
      if (auto depSat = solveGraph(g, d, true))
        if (std::optional<BitSet> depSatN = optimize(g, d + 1, maxDepth))
          return *depSat |= *depSatN;
      // we failed, so reset solved schedules
      std::swap(g.activeEdges, activeEdges);
      std::swap(g.nodeIds, nodeIds);
      auto *oldNodeIter = oldSchedules.begin();
      for (auto &&n : g) n.getSchedule() = *(oldNodeIter++);
      std::swap(carriedDeps, oldCarriedDeps);
    }
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
    if (std::optional<BitSet> depSat = optimizeLevel(g, d)) {
      const size_t numSat = depSat->size();
      if (std::optional<BitSet> depSatNest = optimize(g, d + 1, maxDepth)) {
        if (numSat && (!depSatNest->empty()))
          return optimizeSatDep(g, d, maxDepth, *depSat, *depSatNest,
                                activeEdgesBackup);
        return *depSat |= *depSatNest;
      }
    }
    return breakGraph(g, d);
  }
  // returns true on failure
  [[nodiscard]] auto optimize() -> std::optional<BitSet> {
    fillEdges();
    buildGraph();
    carriedDeps.resize(nodes.size());
#ifndef NDEBUG
    validateEdges();
#endif
    return optOrth(fullGraph());
  }

  friend inline auto operator<<(llvm::raw_ostream &os,
                                const LinearProgramLoopBlock &lblock)
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
    for (const auto edge : lblock.edges) {
      os << "\n\tEdge = " << edge;
      for (size_t inIndex : edge.nodesIn()) {
        const AffineSchedule sin = lblock.getNode(inIndex).getSchedule();
        os << "Schedule In: nodeIndex = " << edge.nodesIn() << "\ns.getPhi()"
           << sin.getPhi() << "\ns.getFusionOmega() = " << sin.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sin.getOffsetOmega();
      }
      for (size_t outIndex : edge.nodesOut()) {
        const AffineSchedule sout = lblock.getNode(outIndex).getSchedule();
        os << "\n\nSchedule Out:\nnodeIndex = " << edge.nodesOut()
           << "\ns.getPhi()" << sout.getPhi()
           << "\ns.getFusionOmega() = " << sout.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sout.getOffsetOmega();
      }
      llvm::errs() << "\n\n";
    }
    os << "\nLoopBlock schedule (#mem accesses = " << lblock.memory.size()
       << "):\n\n";
    for (auto *mem : lblock.memory) {
      os << "Ref = " << *mem;
      for (size_t nodeIndex : mem->getNodeIndex()) {
        const AffineSchedule s = lblock.getNode(nodeIndex).getSchedule();
        os << "\nnodeIndex = " << nodeIndex << "\ns.getPhi()" << s.getPhi()
           << "\ns.getFusionOmega() = " << s.getFusionOmega()
           << "\ns.getOffsetOmega() = " << s.getOffsetOmega() << "\n";
      }
    }
    return os << "\n";
  }
};

template <> struct std::iterator_traits<LinearProgramLoopBlock::Graph> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = ScheduledNode;
  using reference_type = ScheduledNode &;
  using pointer_type = ScheduledNode *;
};
// static_assert(std::ranges::range<LinearProgramLoopBlock::Graph>);
static_assert(Graphs::AbstractGraph<LinearProgramLoopBlock::Graph>);
static_assert(std::is_trivially_destructible_v<LinearProgramLoopBlock::Graph>);
