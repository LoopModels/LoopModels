#pragma once

#include "./BitSets.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Graphs.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./NormalForm.hpp"
#include "./Schedule.hpp"
#include "./Simplex.hpp"
#include "./Utilities.hpp"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

template <std::integral I>
inline void insertSortedUnique(llvm::SmallVectorImpl<I> &v, const I &x) {
  for (auto it = v.begin(), ite = v.end(); it != ite; ++it) {
    if (*it < x) continue;
    if (*it > x) v.insert(it, x);
    return;
  }
  v.push_back(x);
}
/// Represents a memory access that has been rotated according to some affine
/// transform.
struct ScheduledMemoryAccess {
  MemoryAccess *access;
  // may be `false` while `access->isStore()==true`
  // which indicates a reload from this address.
  bool isStore;
  ScheduledMemoryAccess(MemoryAccess *access, const Schedule &schedule,
                        bool isStore)
    : access(access), isStore(isStore) {}
};

/// ScheduledNode
/// Represents a set of memory accesses that are optimized together in the LP.
/// These instructions are all connected directly by through registers.
/// E.g., `A[i] = B[i] + C[i]` is a single node
/// because we load from `B[i]` and `C[i]` into registers, compute, and
/// `A[i]`;
struct ScheduledNode {
private:
  [[no_unique_address]] BitSet<> memory{};
  [[no_unique_address]] BitSet<> inNeighbors{};
  [[no_unique_address]] BitSet<> outNeighbors{};
  [[no_unique_address]] Schedule schedule{};
  [[no_unique_address]] uint32_t storeId;
  [[no_unique_address]] uint32_t phiOffset{0};   // used in LoopBlock
  [[no_unique_address]] uint32_t omegaOffset{0}; // used in LoopBlock
  [[no_unique_address]] uint8_t numLoops{0};
  [[no_unique_address]] uint8_t rank{0};
  [[no_unique_address]] bool visited{false};

public:
  ScheduledNode(unsigned int sId, MemoryAccess *store, unsigned int nodeIndex)
    : storeId(sId) {
    addMemory(sId, store, nodeIndex);
  }
  // clang-format off
  /// Return the memory accesses after applying the Schedule.
  /// Let
  /// \f{eqnarray*}{
  /// D &=& \text{the dimension of the array}\\
  /// N &=& \text{depth of the loop nest}\\
  /// V &=& \text{runtime variables}\\
  /// \textbf{i}\in\mathbb{R}^N &=& \text{the old index vector}\\
  /// \textbf{j}\in\mathbb{R}^N &=& \text{the new index vector}\\
  /// \textbf{x}\in\mathbb{R}^D &=& \text{the indices into the array}\\
  /// \textbf{M}\in\mathbb{R}^{N \times D} &=& \text{map from loop ind vars to array indices}\\
  /// \boldsymbol{\Phi}\in\mathbb{R}^{N \times N} &=& \text{the schedule matrix}\\
  /// \boldsymbol{\Phi}_*\in\mathbb{R}^{N \times N} &=& \textbf{E}\boldsymbol{\Phi}\\
  /// \boldsymbol{\omega}\in\mathbb{R}^N &=& \text{the offset vector}\\
  /// \textbf{c}\in\mathbb{R}^{N} &=& \text{the constant offset vector}\\
  /// \textbf{C}\in\mathbb{R}^{N \times V} &=& \text{runtime variable coefficient matrix}\\
  /// \textbf{s}\in\mathbb{R}^V &=& \text{the symbolic runtime variables}\\
  /// \f}
  /// 
  /// Where \f$\textbf{E}\f$ is an [exchange matrix](https://en.wikipedia.org/wiki/Exchange_matrix).
  /// The rows of \f$\boldsymbol{\Phi}\f$ are sorted from the outermost loop to
  /// the innermost loop, the opposite ordering used elsewhere. \f$\boldsymbol{\Phi}_*\f$
  /// corrects this.
  /// We have
  /// \f{eqnarray*}{
  /// \textbf{j} &=& \boldsymbol{\Phi}_*\textbf{i} + \boldsymbol{\omega}\\
  /// \textbf{i} &=& \boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right)\\
  /// \textbf{x} &=& \textbf{M}'\textbf{i} + \textbf{c} + \textbf{Cs} \\
  /// \textbf{x} &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\
  /// \textbf{M}'_* &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\\
  /// \textbf{x} &=& \textbf{M}'_*\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\
  /// \textbf{x} &=& \textbf{M}'_*j - \textbf{M}'_*\boldsymbol{\omega} + \textbf{c} + \textbf{Cs} \\
  /// \textbf{c}_* &=& \textbf{c} - \textbf{M}'_*\boldsymbol{\omega} \\
  /// \textbf{x} &=& \textbf{M}'_*j + \textbf{c}_* + \textbf{Cs} \\
  /// \f}
  /// Therefore, to update the memory accesses, we must simply compute the updated
  /// \f$\textbf{c}_*\f$ and \f$\textbf{M}'_*\f$.
  /// We can also test for the case where \f$\boldsymbol{\Phi} = \textbf{E}\f$, or equivalently that $\textbf{E}\boldsymbol{\Phi} = \boldsymbol{\Phi}_* = \textbf{I}$.
  // clang-format on
  [[nodiscard]] auto
  getMemAccesses(llvm::ArrayRef<MemoryAccess *> memAccess) const
    -> llvm::SmallVector<ScheduledMemoryAccess> {
    // First, we invert the schedule matrix.
    auto [Pinv, s] = NormalForm::scaledInv(schedule.getPhi());

    llvm::SmallVector<ScheduledMemoryAccess> accesses;
    accesses.reserve(memory.size());
    for (auto i : memory)
      accesses.emplace_back(memAccess[i], schedule, i == storeId);
    return accesses;
  }
  constexpr auto getMemory() -> BitSet<> & { return memory; }
  constexpr auto getInNeighbors() -> BitSet<> & { return inNeighbors; }
  constexpr auto getOutNeighbors() -> BitSet<> & { return outNeighbors; }
  constexpr auto getSchedule() -> Schedule & { return schedule; }
  [[nodiscard]] constexpr auto getMemory() const -> const BitSet<> & {
    return memory;
  }
  [[nodiscard]] constexpr auto getInNeighbors() const -> const BitSet<> & {
    return inNeighbors;
  }
  [[nodiscard]] constexpr auto getOutNeighbors() const -> const BitSet<> & {
    return outNeighbors;
  }
  [[nodiscard]] constexpr auto getSchedule() const -> const Schedule & {
    return schedule;
  }
  void addOutNeighbor(unsigned int i) { outNeighbors.insert(i); }
  void addInNeighbor(unsigned int i) { inNeighbors.insert(i); }
  void init() { schedule.init(getNumLoops()); }
  void addMemory(unsigned memId, MemoryAccess *mem, unsigned nodeIndex) {
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
    return phiOffset = p + numLoops;
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
    return _(phiOffset - numLoops, phiOffset);
  }
  [[nodiscard]] auto getPhi() -> MutSquarePtrMatrix<int64_t> {
    return schedule.getPhi();
  }
  [[nodiscard]] auto getPhi() const -> SquarePtrMatrix<int64_t> {
    return schedule.getPhi();
  }
  [[nodiscard]] auto getOffsetOmega(size_t i) -> int64_t & {
    return schedule.getOffsetOmega()[i];
  }
  [[nodiscard]] auto getOffsetOmega(size_t i) const -> int64_t {
    return schedule.getOffsetOmega()[i];
  }
  [[nodiscard]] auto getFusionOmega(size_t i) -> int64_t & {
    return schedule.getFusionOmega()[i];
  }
  [[nodiscard]] auto getFusionOmega(size_t i) const -> int64_t {
    return schedule.getFusionOmega()[i];
  }
  [[nodiscard]] auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return schedule.getOffsetOmega();
  }
  [[nodiscard]] auto getOffsetOmega() const -> PtrVector<int64_t> {
    return schedule.getOffsetOmega();
  }
  [[nodiscard]] auto getFusionOmega() -> MutPtrVector<int64_t> {
    return schedule.getFusionOmega();
  }
  [[nodiscard]] auto getFusionOmega() const -> PtrVector<int64_t> {
    return schedule.getFusionOmega();
  }
  [[nodiscard]] auto getSchedule(size_t d) const -> PtrVector<int64_t> {
    return getPhi()(d, _);
  }
  [[nodiscard]] auto getSchedule(size_t d) -> MutPtrVector<int64_t> {
    return getPhi()(d, _);
  }
  void schedulePhi(PtrMatrix<int64_t> indMat, size_t r) {
    // indMat indvars are indexed from outside<->inside
    // phi indvars are indexed from inside<->outside
    // so, indMat is indvars[outside<->inside] x array dim
    // phi is loop[outside<->inside] x
    // indvars[inside<->outside]
    MutPtrMatrix<int64_t> phi = getPhi();
    const size_t indR = size_t(indMat.numRow());
    const size_t phiOffset = size_t(phi.numCol()) - indR;
    for (size_t i = 0; i < r; ++i) {
      phi(i, _(begin, phiOffset)) = 0;
      phi(i, _(phiOffset, phiOffset + indR)) = indMat(_, i);
    }
    rank = r;
  }
  constexpr void unschedulePhi() { rank = 0; }
  [[nodiscard]] constexpr auto getOmegaOffset() const -> size_t {
    return omegaOffset;
  }
  void resetPhiOffset() { phiOffset = std::numeric_limits<unsigned>::max(); }
};
inline auto operator<<(llvm::raw_ostream &os, const ScheduledNode &node)
  -> llvm::raw_ostream & {
  os << "inNeighbors = ";
  for (auto m : node.getInNeighbors()) os << "v_" << m << ", ";
  os << "\noutNeighbors = ";
  for (auto m : node.getOutNeighbors()) os << "v_" << m << ", ";
  return os << "\n";
  ;
}

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
inline void resetDeepDeps(llvm::MutableArrayRef<CarriedDependencyFlag> v,
                          size_t d) {
  uint32_t mask = CarriedDependencyFlag::resetMaskFlag(d);
  for (auto &&x : v) x.flag &= mask;
}

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
struct LinearProgramLoopBlock {
  // TODO: figure out how to handle the graph's dependencies based on
  // operation/instruction chains.
  // Perhaps implicitly via the graph when using internal orthogonalization
  // and register tiling methods, and then generate associated constraints
  // or aliasing between schedules when running the ILP solver?
  // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
  // and all other other shared schedule parameters are aliases (i.e.,
  // identical)?
  // using VertexType = ScheduledNode;
private:
  [[no_unique_address]] llvm::SmallVector<MemoryAccess *, 14> memory;
  [[no_unique_address]] llvm::SmallVector<ScheduledNode, 0> nodes;
  // llvm::SmallVector<unsigned> memoryToNodeMap;
  [[no_unique_address]] llvm::SmallVector<Dependence, 0> edges;
  [[no_unique_address]] llvm::SmallVector<CarriedDependencyFlag, 16>
    carriedDeps;
  // llvm::SmallVector<bool> visited; // visited, for traversing graph
  [[no_unique_address]] llvm::DenseMap<llvm::User *, unsigned> userToMemory;
  // [[no_unique_address]] llvm::BumpPtrAllocator allocator;
  // llvm::SmallVector<llvm::Value *> symbols;
  [[no_unique_address]] Simplex omniSimplex;
  // we may turn off edges because we've exceeded its loop depth
  // or because the dependence has already been satisfied at an
  // earlier level.
  [[no_unique_address]] Vector<Rational> sol;
  // llvm::SmallVector<bool, 256> doNotAddEdge;
  // llvm::SmallVector<bool, 256> scheduled;
  [[no_unique_address]] size_t numPhiCoefs{0};
  [[no_unique_address]] size_t numOmegaCoefs{0};
  [[no_unique_address]] size_t numLambda{0};
  [[no_unique_address]] size_t numBounding{0};
  [[no_unique_address]] size_t numConstraints{0};
  [[no_unique_address]] size_t numActiveEdges{0};

public:
  void clear() {
    memory.clear();
    nodes.clear();
    edges.clear();
    carriedDeps.clear();
    userToMemory.clear();
    sol.clear();
    // allocator.Reset();
  }
  // TODO: `constexpr` once `llvm::SmallVector` supports it
  [[nodiscard]] auto numVerticies() const -> size_t { return nodes.size(); }
  [[nodiscard]] auto getVerticies() -> llvm::MutableArrayRef<ScheduledNode> {
    return nodes;
  }
  [[nodiscard]] auto getVerticies() const -> llvm::ArrayRef<ScheduledNode> {
    return nodes;
  }
  auto getMemoryAccesses() const -> llvm::ArrayRef<MemoryAccess *> {
    return memory;
  }
  auto getMemoryAccesses() -> llvm::MutableArrayRef<MemoryAccess *> {
    return memory;
  }
  auto getMemoryAccess(size_t i) -> MemoryAccess * { return memory[i]; }
  auto getNode(size_t i) -> ScheduledNode & { return nodes[i]; }
  [[nodiscard]] auto getNode(size_t i) const -> const ScheduledNode & {
    return nodes[i];
  }
  auto getNodes() -> llvm::MutableArrayRef<ScheduledNode> { return nodes; }
  auto getEdges() -> llvm::MutableArrayRef<Dependence> { return edges; }
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
    for (auto &mem : memory) d = std::max(d, mem->getNumLoops());
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
  void addEdge(MemoryAccess &mai, MemoryAccess &maj) {
    // note, axes should be fully delinearized, so should line up
    // as a result of preprocessing.
    if (size_t numDeps = Dependence::check(edges, mai, maj)) {
      size_t numEdges = edges.size();
      size_t e = numEdges - numDeps;
      do {
        edges[e].in->addEdgeOut(e);
        edges[e].out->addEdgeIn(e);
      } while (++e < numEdges);
    }
  }
  /// fills all the edges between memory accesses, checking for
  /// dependencies.
  void fillEdges() {
    // TODO: handle predicates
    for (size_t i = 1; i < memory.size(); ++i) {
      MemoryAccess &mai = *memory[i];
      for (size_t j = 0; j < i; ++j) {
        MemoryAccess &maj = *memory[j];
        if ((mai.basePointer != maj.basePointer) ||
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
  searchValueForStores(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                       ScheduledNode &node, llvm::User *user,
                       unsigned nodeIndex) -> bool {
    for (llvm::User *use : user->users()) {
      if (visited.contains(use)) continue;
      if (llvm::isa<llvm::StoreInst>(use)) {
        auto memAccess = userToMemory.find(use);
        if (memAccess == userToMemory.end())
          continue; // load is not a part of the LoopBlock
        unsigned memId = memAccess->getSecond();
        MemoryAccess *store = memory[memId];
        // this store will be treated as a load
        node.addMemory(memId, store, nodeIndex);
        return true;
      }
    }
    return false;
  }
  void checkUserForLoads(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                         ScheduledNode &node, llvm::User *user,
                         unsigned nodeIndex) {
    if (!user || visited.contains(user)) return;
    if (llvm::isa<llvm::LoadInst>(user)) {
      auto memAccess = userToMemory.find(user);
      if (memAccess == userToMemory.end())
        return; // load is not a part of the LoopBlock
      unsigned memId = memAccess->getSecond();
      node.addMemory(memId, memory[memId], nodeIndex);
    } else if (!searchValueForStores(visited, node, user, nodeIndex))
      searchOperandsForLoads(visited, node, user, nodeIndex);
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
  void searchOperandsForLoads(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                              ScheduledNode &node, llvm::User *u,
                              unsigned nodeIndex) {
    visited.insert(u);
    if (auto *s = llvm::dyn_cast<llvm::StoreInst>(u)) {
      if (auto *user = llvm::dyn_cast<llvm::User>(s->getValueOperand()))
        checkUserForLoads(visited, node, user, nodeIndex);
      return;
    }
    for (auto &&op : u->operands())
      if (auto *user = llvm::dyn_cast<llvm::User>(op.get()))
        checkUserForLoads(visited, node, user, nodeIndex);
  }
  void connect(unsigned inIndex, unsigned outIndex) {
    nodes[inIndex].addOutNeighbor(outIndex);
    nodes[outIndex].addInNeighbor(inIndex);
  }
  // the order of parameters is irrelevant, so swapping is irrelevant
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void connect(const BitSet<> &inIndexSet, const BitSet<> &outIndexSet) {
    for (auto inIndex : inIndexSet)
      for (auto outIndex : outIndexSet) connect(inIndex, outIndex);
  }
  [[nodiscard]] auto calcNumStores() const -> size_t {
    size_t numStores = 0;
    for (auto &m : memory) numStores += !(m->isLoad());
    return numStores;
  }
  /// When connecting a graph, we draw direct connections between stores and
  /// loads loads may be duplicated across stores to allow for greater
  /// reordering flexibility (which should generally reduce the ultimate
  /// amount of loads executed in the eventual generated code)
  void connectGraph() {
    // assembles direct connections in node graph
    llvm::SmallPtrSet<llvm::User *, 32> visited;
    nodes.reserve(calcNumStores());
    for (unsigned i = 0; i < memory.size(); ++i) {
      MemoryAccess *mai = memory[i];
      if (mai->isLoad()) continue;
      unsigned nodeIndex = nodes.size();
      ScheduledNode &node = nodes.emplace_back(i, mai, nodeIndex);
      searchOperandsForLoads(visited, node, mai->getInstruction(), nodeIndex);
      visited.clear();
    }
    for (auto &e : edges) connect(e.in->nodeIndex, e.out->nodeIndex);
    for (auto &&node : nodes) node.init();
    // now that we've assigned each MemoryAccess to a NodeIndex, we
    // build the actual graph
  }
  struct Graph {
    // a subset of Nodes
    BitSet<> nodeIds;
    BitSet<> activeEdges;
    llvm::MutableArrayRef<MemoryAccess *> mem;
    llvm::MutableArrayRef<ScheduledNode> nodes;
    llvm::ArrayRef<Dependence> edges;
    // llvm::SmallVector<bool> visited;
    // BitSet visited;
    auto operator&(const Graph &g) -> Graph {
      return Graph{nodeIds & g.nodeIds, activeEdges & g.activeEdges, mem, nodes,
                   edges};
    }
    auto operator|(const Graph &g) -> Graph {
      return Graph{nodeIds | g.nodeIds, activeEdges | g.activeEdges, mem, nodes,
                   edges};
    }
    auto operator&=(const Graph &g) -> Graph & {
      nodeIds &= g.nodeIds;
      activeEdges &= g.activeEdges;
      return *this;
    }
    auto operator|=(const Graph &g) -> Graph & {
      nodeIds |= g.nodeIds;
      activeEdges |= g.activeEdges;
      return *this;
    }
    [[nodiscard]] auto inNeighbors(size_t i) -> BitSet<> & {
      return nodes[i].getInNeighbors();
    }
    [[nodiscard]] auto outNeighbors(size_t i) -> BitSet<> & {
      return nodes[i].getOutNeighbors();
    }
    [[nodiscard]] auto inNeighbors(size_t i) const -> const BitSet<> & {
      return nodes[i].getInNeighbors();
    }
    [[nodiscard]] auto outNeighbors(size_t i) const -> const BitSet<> & {
      return nodes[i].getOutNeighbors();
    }
    [[nodiscard]] auto containsNode(size_t i) const -> bool {
      return nodeIds.contains(i);
    }
    [[nodiscard]] auto containsNode(BitSet<> &b) const -> bool {
      for (size_t i : b)
        if (nodeIds.contains(i)) return true;
      return false;
    }
    [[nodiscard]] auto missingNode(size_t i) const -> bool {
      return !containsNode(i);
    }
    [[nodiscard]] auto missingNode(size_t i, size_t j) const -> bool {
      return !(containsNode(i) && containsNode(j));
    }
    /// returns false iff e.in and e.out are both in graph
    /// that is, to be missing, both `e.in` and `e.out` must be missing
    /// in case of multiple instances of the edge, we check all of them
    /// if any are not missing, returns false
    /// only returns true if every one of them is missing.
    [[nodiscard]] auto missingNode(const Dependence &e) const -> bool {
      for (auto inIndex : e.in->nodeIndex)
        for (auto outIndex : e.out->nodeIndex)
          if (!missingNode(inIndex, outIndex)) return false;
      return true;
    }

    [[nodiscard]] auto isInactive(const Dependence &edge, size_t d) const
      -> bool {
      return edge.isInactive(d) || missingNode(edge);
    }
    [[nodiscard]] auto isInactive(const Dependence &edge) const -> bool {
      return missingNode(edge);
    }
    [[nodiscard]] auto isInactive(size_t e, size_t d) const -> bool {
      return !(activeEdges[e]) || isInactive(edges[e], d);
    }
    [[nodiscard]] auto isInactive(size_t e) const -> bool {
      return !(activeEdges[e]) || isInactive(edges[e]);
    }
    [[nodiscard]] auto isActive(size_t e, size_t d) const -> bool {
      return (activeEdges[e]) && (!isInactive(edges[e], d));
    }
    [[nodiscard]] auto isActive(size_t e) const -> bool {
      return (activeEdges[e]) && (!isInactive(edges[e]));
    }
    [[nodiscard]] constexpr auto begin()
      -> BitSliceView<ScheduledNode>::Iterator {
      return BitSliceView<ScheduledNode>{nodes, nodeIds}.begin();
    }
    [[nodiscard]] constexpr auto begin() const
      -> BitSliceView<ScheduledNode>::ConstIterator {
      const BitSliceView<ScheduledNode> bsv{nodes, nodeIds};
      return bsv.begin();
    }
    [[nodiscard]] static constexpr auto end() -> EndSentinel { return {}; }
    [[nodiscard]] auto wasVisited(size_t i) const -> bool {
      return nodes[i].wasVisited();
    }
    void visit(size_t i) { nodes[i].visit(); }
    void unVisit(size_t i) { nodes[i].unVisit(); }
    [[nodiscard]] auto getNumVertices() const -> size_t {
      return nodeIds.size();
    }
    [[nodiscard]] auto maxVertexId() const -> size_t {
      return nodeIds.maxValue();
    }
    [[nodiscard]] constexpr auto vertexIds() -> BitSet<> & { return nodeIds; }
    [[nodiscard]] constexpr auto vertexIds() const -> const BitSet<> & {
      return nodeIds;
    }
    [[nodiscard]] auto subGraph(const BitSet<> &components) -> Graph {
      return {components, activeEdges, mem, nodes, edges};
    }
    [[nodiscard]] auto split(const llvm::SmallVector<BitSet<>> &components)
      -> llvm::SmallVector<Graph, 0> {
      llvm::SmallVector<Graph, 0> graphs;
      graphs.reserve(components.size());
      for (auto &c : components) graphs.push_back(subGraph(c));
      return graphs;
    }
    [[nodiscard]] auto calcMaxDepth() const -> size_t {
      if (nodeIds.data.size() == 0) return 0;
      size_t d = 0;
      for (auto n : nodeIds) d = std::max(d, nodes[n].getNumLoops());
      return d;
    }
  };
  // bool connects(const Dependence &e, Graph &g0, Graph &g1, size_t d) const
  // {
  //     return ((e.in->getNumLoops() > d) && (e.out->getNumLoops() > d)) &&
  //            connects(e, g0, g1);
  // }
  auto connects(const Dependence &e, Graph &g0, Graph &g1) const -> bool {
    if (!e.in->isLoad()) {
      // e.in is a store
      size_t nodeIn = *e.in->nodeIndex.begin();
      bool g0ContainsNodeIn = g0.nodeIds.contains(nodeIn);
      bool g1ContainsNodeIn = g1.nodeIds.contains(nodeIn);
      if (!(g0ContainsNodeIn || g1ContainsNodeIn)) return false;
      for (size_t nodeOut : e.out->nodeIndex)
        if ((g0ContainsNodeIn && g1.nodeIds.contains(nodeOut)) ||
            (g1ContainsNodeIn && g0.nodeIds.contains(nodeOut)))
          return true;
    } else {
      // e.out must be a store
      size_t nodeOut = *e.out->nodeIndex.begin();
      bool g0ContainsNodeOut = g0.nodeIds.contains(nodeOut);
      bool g1ContainsNodeOut = g1.nodeIds.contains(nodeOut);
      if (!(g0ContainsNodeOut || g1ContainsNodeOut)) return false;
      for (auto nodeIn : e.in->nodeIndex)
        if ((g0ContainsNodeOut && g1.nodeIds.contains(nodeIn)) ||
            (g1ContainsNodeOut && g0.nodeIds.contains(nodeIn)))
          return true;
    }
    return false;
  }
  auto fullGraph() -> Graph {
    return {BitSet<>::dense(nodes.size()), BitSet<>::dense(edges.size()),
            memory, nodes, edges};
  }
  void fillUserToMemoryMap() {
    for (unsigned i = 0; i < memory.size(); ++i)
      userToMemory.insert(std::make_pair(memory[i]->getInstruction(), i));
  }
  auto getOverlapIndex(const Dependence &edge) -> Optional<size_t> {
    MemoryAccess *store;
    MemoryAccess *other;
    if (edge.in->isLoad()) {
      // edge.out is a store
      store = edge.out;
      other = edge.in;
    } else {
      // edge.in is a store
      store = edge.in;
      other = edge.out;
    }
    size_t index = *store->nodeIndex.begin();
    if (other->nodeIndex.contains(index)) return index;
    return {};
  }
  auto optOrth(Graph g) -> std::optional<BitSet<>> {

    const size_t maxDepth = calcMaxDepth();
    // check for orthogonalization opportunities
    bool tryOrth = false;
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (edge.in->isLoad() == edge.out->isLoad()) continue;
      Optional<size_t> maybeIndex = getOverlapIndex(edge);
      if (!maybeIndex) continue;
      size_t index = *maybeIndex;
      ScheduledNode &node = nodes[index];
      if (node.phiIsScheduled(0) ||
          (edge.in->indexMatrix() != edge.out->indexMatrix()))
        continue;
      PtrMatrix<int64_t> indMat = edge.in->indexMatrix();
      size_t r = NormalForm::rank(indMat);
      if (r == edge.in->getNumLoops()) continue;
      // TODO handle linearly dependent acceses, filtering them out
      if (r == edge.in->getArrayDim()) {
        node.schedulePhi(indMat, r);
        tryOrth = true;
      }
    }
    if (tryOrth) {
      if (std::optional<BitSet<>> opt = optimize(g, 0, maxDepth)) return opt;
      for (auto &&n : nodes) n.unschedulePhi();
    }
    return optimize(std::move(g), 0, maxDepth);
  }
  [[nodiscard]] auto countNumLambdas(const Graph &g, size_t d) const -> size_t {
    size_t c = 0;
    for (size_t e = 0; e < edges.size(); ++e)
      c += ((g.isInactive(e, d)) ? 0 : edges[e].getNumLambda());
    return c;
  }
  [[nodiscard]] auto countNumBoundingCoefs(const Graph &g, size_t d) const
    -> size_t {
    size_t c = 0;
    for (size_t e = 0; e < edges.size(); ++e)
      c += (g.isInactive(e, d) ? 0 : edges[e].getNumSymbols());
    return c;
  }
  void countAuxParamsAndConstraints(const Graph &g, size_t d) {
    size_t a = 0, b = 0, c = 0, ae = 0;
    for (size_t e = 0; e < edges.size(); ++e) {
      if (g.isInactive(e, d)) continue;
      const Dependence &edge = edges[e];
      size_t mlt = edge.in->nodeIndex.size() * edge.out->nodeIndex.size();
      a += mlt * edge.getNumLambda();
      b += mlt * edge.depPoly.S.size();
      c += mlt * edge.getNumConstraints();
      ae += mlt;
    }
    numLambda = a;
    numBounding = b;
    numConstraints = c;
    numActiveEdges = ae;
  }
  void countNumParams(const Graph &g, size_t depth) {
    setScheduleMemoryOffsets(g, depth);
    countAuxParamsAndConstraints(g, depth);
  }
  void addMemory(MemoryAccess *m) {
#ifndef NDEBUG
    for (auto o : memory) assert(o->getInstruction() != m->getInstruction());
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
  [[nodiscard]] constexpr auto getLambdaOffset() const -> size_t {
    return 1 + numBounding + numActiveEdges + numPhiCoefs + numOmegaCoefs;
  }
  [[nodiscard]] auto hasActiveEdges(const Graph &g,
                                    const MemoryAccess &mem) const -> bool {
    for (auto &e : mem.edgesIn)
      if (!g.isInactive(e)) return true;
    // else
    //     llvm::errs() << "hasActiveEdge In false for: " << edges[e];
    for (auto &e : mem.edgesOut)
      if (!g.isInactive(e)) return true;
    // else
    //     llvm::errs() << "hasActiveEdge Out false for: " << edges[e];
    return false;
  }
  [[nodiscard]] auto hasActiveEdges(const Graph &g, const MemoryAccess &mem,
                                    size_t d) const -> bool {
    for (auto &e : mem.edgesIn)
      if (!g.isInactive(e, d)) return true;
    // else
    //     llvm::errs() << "hasActiveEdge In d = " << d
    //                  << " false for: " << edges[e];
    for (auto &e : mem.edgesOut)
      if (!g.isInactive(e, d)) return true;
    // else
    //     llvm::errs() << "hasActiveEdge Out d = " << d
    //                  << " false for: " << edges[e];
    return false;
  }
  [[nodiscard]] auto hasActiveEdges(const Graph &g, const ScheduledNode &node,
                                    size_t d) const -> bool {
    for (auto memId : node.getMemory())
      if (hasActiveEdges(g, *memory[memId], d)) return true;
    return false;
  }
  [[nodiscard]] auto hasActiveEdges(const Graph &g,
                                    const ScheduledNode &node) const -> bool {
    for (auto memId : node.getMemory())
      if (hasActiveEdges(g, *memory[memId])) return true;
    return false;
  }
  void setScheduleMemoryOffsets(const Graph &g, size_t d) {
    size_t pInit = numBounding + numActiveEdges + 1, p = pInit;
    for (auto &&node : nodes) {
      if ((d >= node.getNumLoops()) || (!hasActiveEdges(g, node, d))) continue;
      if (!node.phiIsScheduled(d)) p = node.updatePhiOffset(p);
    }
    numPhiCoefs = p - pInit;
    size_t o = p;
    for (auto &&node : nodes) {
      if ((d > node.getNumLoops()) || (!hasActiveEdges(g, node, d))) continue;
      o = node.updateOmegaOffset(o);
    }
    numOmegaCoefs = o - p;
  }
  void validateMemory() {
    for (auto mem : memory)
      assert(1 + mem->getNumLoops() == mem->omegas.size());
  }
  void validateEdges() {
    for (auto &edge : edges) {
      assert(edge.in->getNumLoops() + edge.out->getNumLoops() ==
             edge.getNumPhiCoefficients());
      // 2 == 1 for const offset + 1 for w
      assert(2 + edge.depPoly.getNumLambda() + edge.getNumPhiCoefficients() +
               edge.getNumOmegaCoefficients() ==
             size_t(edge.dependenceSatisfaction.getConstraints().numCol()));
    }
  }
  void instantiateOmniSimplex(const Graph &g, size_t d,
                              bool satisfyDeps = false) {
    // defines numScheduleCoefs, numLambda, numBounding, and
    // numConstraints
    omniSimplex.clearReserve(numConstraints + numOmegaCoefs,
                             1 + numBounding + numActiveEdges + numPhiCoefs +
                               2 * numOmegaCoefs + numLambda);
    omniSimplex.resizeForOverwrite(numConstraints,
                                   1 + numBounding + numActiveEdges +
                                     numPhiCoefs + numOmegaCoefs + numLambda);
    auto C{omniSimplex.getConstraints()};
    C = 0;
    // layout of omniSimplex:
    // Order: C, then priority to minimize
    // all : C, u, w, Phis, omegas, lambdas
    // rows give constraints; each edge gets its own
    // constexpr size_t numOmega =
    //     DependencePolyhedra::getNumOmegaCoefficients();
    size_t w = 1 + numBounding;
    Row c = 0;
    Col l = getLambdaOffset(), u = 1;
    for (size_t e = 0; e < edges.size(); ++e) {
      Dependence &edge = edges[e];
      if (g.isInactive(e, d)) continue;
      BitSet<> &outNodeIndexSet = edge.out->nodeIndex;
      BitSet<> &inNodeIndexSet = edge.in->nodeIndex;
      const auto [satC, satL, satPp, satPc, satO, satW] =
        edge.splitSatisfaction();
      const auto [bndC, bndL, bndPp, bndPc, bndO, bndWU] = edge.splitBounding();
      const size_t numSatConstraints = satC.size();
      const size_t numBndConstraints = bndC.size();
      for (auto outNodeIndex : outNodeIndexSet) {
        const ScheduledNode &outNode = nodes[outNodeIndex];
        for (auto inNodeIndex : inNodeIndexSet) {
          const ScheduledNode &inNode = nodes[inNodeIndex];

          Row cc = c + numSatConstraints;
          Row ccc = cc + numBndConstraints;

          Col ll = l + satL.numCol();
          Col lll = ll + bndL.numCol();
          C(_(c, cc), _(l, ll)) = satL;
          C(_(cc, ccc), _(ll, lll)) = bndL;
          l = lll;

          // bounding
          C(_(cc, ccc), w++) = bndWU(_, 0);
          Col uu = u + bndWU.numCol() - 1;
          C(_(cc, ccc), _(u, uu)) = bndWU(_, _(1, end));
          u = uu;
          if (satisfyDeps) C(_(c, cc), 0) = satC + satW;
          else C(_(c, cc), 0) = satC;
          C(_(cc, ccc), 0) = bndC;
          // now, handle Phi and Omega
          // phis are not constrained to be 0
          if (outNodeIndex == inNodeIndex) {
            // llvm::errs() << "outNodeIndex == inNodeIndex\n";
            if (d < outNode.getNumLoops()) {
              if (satPc.numCol() == satPp.numCol()) {
                if (outNode.phiIsScheduled(d)) {
                  // add it constants
                  auto sch = outNode.getSchedule(d);
                  C(_(c, cc), 0) -= satPc * sch[_(end - satPc.numCol(), end)] +
                                    satPp * sch[_(end - satPp.numCol(), end)];
                  C(_(cc, ccc), 0) -=
                    bndPc * sch[_(end - bndPc.numCol(), end)] +
                    bndPp * sch[_(end - satPp.numCol(), end)];
                } else {
                  // FIXME: phiChild = [14:18), 4 cols
                  // while Dependence seems to indicate 2
                  // loops why the disagreement?
                  auto phiChild = outNode.getPhiOffset();
                  C(_(c, cc), _(phiChild - satPc.numCol(), phiChild)) =
                    satPc + satPp;
                  C(_(cc, ccc), _(phiChild - bndPc.numCol(), phiChild)) =
                    bndPc + bndPp;
                }
              } else if (outNode.phiIsScheduled(d)) {
                // add it constants
                // note that loop order in schedule goes
                // inner -> outer
                // so we need to drop inner most if one has less
                auto sch = outNode.getSchedule(d);
                auto schP = sch[_(end - satPp.numCol(), end)];
                auto schC = sch[_(end - satPc.numCol(), end)];
                C(_(c, cc), 0) -= satPc * schC + satPp * schP;
                C(_(cc, ccc), 0) -= bndPc * schC + bndPp * schP;
              } else if (satPc.numCol() < satPp.numCol()) {
                auto phiChild = outNode.getPhiOffset();
                Col P = satPc.numCol();
                auto m = phiChild - P;
                C(_(c, cc), _(phiChild - satPp.numCol(), m)) =
                  satPp(_, _(begin, end - P));
                C(_(cc, ccc), _(phiChild - bndPp.numCol(), m)) =
                  bndPp(_, _(begin, end - P));
                C(_(c, cc), _(m, phiChild)) = satPc + satPp(_, _(end - P, end));
                C(_(cc, ccc), _(m, phiChild)) =
                  bndPc + bndPp(_, _(end - P, end));
              } else /* if (satPc.numCol() > satPp.numCol()) */ {
                auto phiChild = outNode.getPhiOffset();
                Col P = satPp.numCol();
                auto m = phiChild - P;
                C(_(c, cc), _(phiChild - satPc.numCol(), m)) =
                  satPc(_, _(begin, end - P));
                C(_(cc, ccc), _(phiChild - bndPc.numCol(), m)) =
                  bndPc(_, _(begin, end - P));
                C(_(c, cc), _(m, phiChild)) = satPc(_, _(end - P, end)) + satPp;
                C(_(cc, ccc), _(m, phiChild)) =
                  bndPc(_, _(end - P, end)) + bndPp;
              }
              C(_(c, cc), outNode.getOmegaOffset()) = satO(_, 0) + satO(_, 1);
              C(_(cc, ccc), outNode.getOmegaOffset()) = bndO(_, 0) + bndO(_, 1);
            }
          } else {
            // llvm::errs() << "outNodeIndex != inNodeIndex\n";
            if (d < edge.out->getNumLoops())
              updateConstraints(C, outNode, satPc, bndPc, d, c, cc, ccc);
            if (d < edge.in->getNumLoops())
              updateConstraints(C, inNode, satPp, bndPp, d, c, cc, ccc);
            // Omegas are included regardless of rotation
            if (d < edge.out->getNumLoops()) {
              C(_(c, cc), outNode.getOmegaOffset()) = satO(_, !edge.forward);
              C(_(cc, ccc), outNode.getOmegaOffset()) = bndO(_, !edge.forward);
            }
            if (d < edge.in->getNumLoops()) {
              C(_(c, cc), inNode.getOmegaOffset()) = satO(_, edge.forward);
              C(_(cc, ccc), inNode.getOmegaOffset()) = bndO(_, edge.forward);
            }
          }
          c = ccc;
        }
      }
    }
  }
  void updateConstraints(MutPtrMatrix<int64_t> C, const ScheduledNode &node,
                         PtrMatrix<int64_t> sat, PtrMatrix<int64_t> bnd,
                         size_t d, Row c, Row cc, Row ccc) {
    if (node.phiIsScheduled(d)) {
      // add it constants
      auto sch = node.getSchedule(d);
      // order is inner <-> outer
      // so we need the end of schedule if it is larger
      C(_(c, cc), 0) -= sat * sch[_(end - sat.numCol(), end)];
      C(_(cc, ccc), 0) -= bnd * sch[_(end - bnd.numCol(), end)];
    } else {
      assert(sat.numCol() == bnd.numCol());
      // add it to C
      auto phiChild = node.getPhiOffset();
      C(_(c, cc), _(phiChild - sat.numCol(), phiChild)) = sat;
      C(_(cc, ccc), _(phiChild - bnd.numCol(), phiChild)) = bnd;
    }
  }
  [[nodiscard]] auto deactivateSatisfiedEdges(Graph &g, size_t d) -> BitSet<> {
    if (allZero(sol[_(begin, numBounding + numActiveEdges)])) return {};
    size_t u = 0, w = numBounding;
    BitSet deactivated;
    for (size_t e = 0; e < edges.size(); ++e) {
      if (g.isInactive(e, d)) continue;
      const Dependence &edge = edges[e];
      Col uu = u + edge.dependenceBounding.getConstraints().numCol() -
               (2 + edge.depPoly.getNumLambda() + edge.getNumPhiCoefficients() +
                edge.getNumOmegaCoefficients());
      if ((sol[w++] != 0) || (!(allZero(sol[_(u, uu)])))) {
        g.activeEdges.remove(e);
        deactivated.insert(e);
        for (size_t inIndex : edge.in->nodeIndex)
          carriedDeps[inIndex].setCarriedDependency(d);
        for (size_t outIndex : edge.out->nodeIndex)
          carriedDeps[outIndex].setCarriedDependency(d);
      }
      u = size_t(uu);
    }
    return deactivated;
  }
  void updateSchedules(const Graph &g, size_t depth) {
#ifndef NDEBUG
    if (depth & 1) {
      bool allZero = true;
      for (auto &s : sol) allZero &= (s == 0);
      if (allZero) llvm::errs() << "omniSimplex = " << omniSimplex << "\n";
      assert(!allZero);
    }
#endif
    for (auto &&node : nodes) {
      if (depth >= node.getNumLoops()) continue;
      if (!hasActiveEdges(g, node)) {
        node.getOffsetOmega()[depth] = std::numeric_limits<int64_t>::min();
        if (!node.phiIsScheduled(depth))
          node.getSchedule(depth) = std::numeric_limits<int64_t>::min();
        continue;
      }
      Rational sOmega = sol[node.getOmegaOffset() - 1];
      // TODO: handle s.denominator != 1
      if (!node.phiIsScheduled(depth)) {
        auto phi = node.getSchedule(depth);
        auto s = sol[node.getPhiOffsetRange() - 1];
        int64_t baseDenom = sOmega.denominator;
        int64_t l = lcm(denomLCM(s), baseDenom);
        for (size_t i = 0; i < phi.size(); ++i)
          assert(((s[i].numerator * l) / (s[i].denominator)) >= 0);
        if (l == 1) {
          node.getOffsetOmega(depth) = sOmega.numerator;
          for (size_t i = 0; i < phi.size(); ++i) phi[i] = s[i].numerator;
        } else {
          node.getOffsetOmega(depth) = (sOmega.numerator * l) / baseDenom;
          for (size_t i = 0; i < phi.size(); ++i)
            phi[i] = (s[i].numerator * l) / (s[i].denominator);
        }
        assert(!(allZero(phi)));
        // node.schedule.getPhi()(depth, _) =
        //     sol(node.getPhiOffset() - 1) *
        //     denomLCM(sol(node.getPhiOffset() - 1));
      } else {
        node.getOffsetOmega(depth) = sOmega.numerator;
      }
#ifndef NDEBUG
      if (!node.phiIsScheduled(depth)) {
        int64_t l = denomLCM(sol[node.getPhiOffsetRange() - 1]);
        for (size_t i = 0; i < node.getPhi().numCol(); ++i)
          assert(node.getPhi()(depth, i) ==
                 sol[node.getPhiOffsetRange() - 1][i] * l);
      }
#endif
    }
  }
  [[nodiscard]] static auto lexSign(PtrVector<int64_t> x) -> int64_t {
    for (int64_t it : llvm::reverse(x))
      if (it) return 2 * (it > 0) - 1;
    return 0;
  }
  void addIndependentSolutionConstraints(const Graph &g, size_t depth) {
    omniSimplex.reserveExtraRows(memory.size());
    if (depth == 0) {
      // add ones >= 0
      for (auto &&node : nodes) {
        if (node.phiIsScheduled(depth) || (!hasActiveEdges(g, node))) continue;
        auto c{omniSimplex.addConstraintAndVar()};
        c[0] = 1;
        c[node.getPhiOffsetRange()] = 1;
        c[end] = -1; // for >=
      }
      return;
    }
    IntMatrix A, N;
    for (auto &&node : nodes) {
      if (node.phiIsScheduled(depth) || (depth >= node.getNumLoops()) ||
          (!hasActiveEdges(g, node)))
        continue;
      A = node.getPhi()(_(0, depth), _).transpose();
      NormalForm::nullSpace11(N, A);
      auto c{omniSimplex.addConstraintAndVar()};
      c[0] = 1;
      MutPtrVector<int64_t> cc{c[node.getPhiOffsetRange()]};
      // sum(N,dims=1) >= 1 after flipping row signs to be lex > 0
      for (size_t m = 0; m < N.numRow(); ++m) cc += N(m, _) * lexSign(N(m, _));
      c[end] = -1; // for >=
    }
    assert(!allZero(omniSimplex.getConstraints()(end, _)));
  }
  [[nodiscard]] static auto nonZeroMask(const AbstractVector auto &x)
    -> uint64_t {
    assert(x.size() <= 64);
    uint64_t m = 0;
    for (auto y : x) m = ((m << 1) | (y != 0));
    return m;
  }
  static void nonZeroMasks(llvm::SmallVector<uint64_t> &masks,
                           const AbstractMatrix auto &A) {
    const auto [M, N] = A.size();
    assert(N <= 64);
    masks.resize_for_overwrite(M);
    for (size_t m = 0; m < M; ++m) masks[m] = nonZeroMask(A(m, _));
  }
  [[nodiscard]] static auto nonZeroMasks(const AbstractMatrix auto &A)
    -> llvm::SmallVector<uint64_t> {
    llvm::SmallVector<uint64_t> masks;
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
          node.getSchedule(depth) = std::numeric_limits<int64_t>::min();
        continue;
      }
      node.getOffsetOmega(depth) = 0;
      node.getSchedule(depth) = std::numeric_limits<int64_t>::min();
    }
  }
  void resetPhiOffsets() {
    for (auto &&node : nodes) node.resetPhiOffset();
  }
  [[nodiscard]] auto isSatisfied(Dependence &e, size_t d) -> bool {
    for (size_t inIndex : e.in->nodeIndex) {
      for (size_t outIndex : e.out->nodeIndex) {
        Schedule *first = &(nodes[inIndex].getSchedule());
        Schedule *second = &(nodes[outIndex].getSchedule());
        if (!e.forward) std::swap(first, second);
        if (!e.isSatisfied(*first, *second, d)) return false;
      }
    }
    return true;
  }
  [[nodiscard]] auto canFuse(Graph &g0, Graph &g1, size_t d) -> bool {
    for (auto &e : edges) {
      if ((e.in->getNumLoops() <= d) || (e.out->getNumLoops() <= d))
        return false;
      if (connects(e, g0, g1))
        if (!isSatisfied(e, d)) return false;
    }
    return true;
  }
  [[nodiscard]] auto breakGraph(Graph g, size_t d) -> std::optional<BitSet<>> {
    auto components = Graphs::stronglyConnectedComponents(g);
    if (components.size() <= 1) return {};
    // components are sorted in topological order.
    // We split all of them, solve independently,
    // and then try to fuse again after if/where optimal schedules
    // allow it.
    llvm::errs() << "splitting graph!\n";
    auto graphs = g.split(components);
    assert(graphs.size() == components.size());
    BitSet satDeps;
    for (auto &sg : graphs) {
      if (d >= sg.calcMaxDepth()) continue;
      countAuxParamsAndConstraints(sg, d);
      setScheduleMemoryOffsets(sg, d);
      if (std::optional<BitSet<>> sat = optimizeLevel(sg, d)) satDeps |= *sat;
      else return {}; // give up
    }
    int64_t unfusedOffset = 0;
    // For now, just greedily try and fuse from top down
    // we do this by setting the Omegas in a loop.
    // If fusion is legal, we don't increment the Omega offset.
    // else, we do.
    Graph *gp = &graphs[0];
    llvm::SmallVector<unsigned> baseGraphs;
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
      if (std::optional<BitSet<>> sat =
            optimize(std::move(graphs[i]), d, graphs[i].calcMaxDepth())) {
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
    -> std::optional<BitSet<>> {
    if (numPhiCoefs == 0) {
      setSchedulesIndependent(g, d);
      return BitSet{};
    }
    instantiateOmniSimplex(g, d);
    addIndependentSolutionConstraints(g, d);
    assert(!allZero(omniSimplex.getConstraints()(end, _)));
    if (omniSimplex.initiateFeasible()) {
      llvm::errs() << "optimizeLevel = " << d << ": infeasible solution!!!\n";
      return {};
    }
    sol.resizeForOverwrite(getLambdaOffset() - 1);
    omniSimplex.lexMinimize(sol);
    updateSchedules(g, d);
    return deactivateSatisfiedEdges(g, d);
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
  [[nodiscard]] auto
  optimizeSatDep(Graph g, size_t d, size_t maxDepth, BitSet<> depSatLevel,
                 const BitSet<> // NOLINT(bugprone-easily-swappable-parameters)
                   &depSatNest, // NOLINT(bugprone-easily-swappable-parameters)
                 BitSet<> activeEdges) -> BitSet<> {
    // if we're here, there are satisfied deps in both
    // depSatLevel and depSatNest
    // what we want to know is, can we satisfy all the deps
    // in depSatNest?
    depSatLevel |= depSatNest;
    const size_t numSatNest = depSatLevel.size();
    if (numSatNest) {
      // backup in case we fail
      // activeEdges was the old original; swap it in
      std::swap(g.activeEdges, activeEdges);
      BitSet nodeIds = g.nodeIds;
      llvm::SmallVector<Schedule, 0> oldSchedules;
      for (auto &n : g) oldSchedules.push_back(n.getSchedule());
      llvm::SmallVector<CarriedDependencyFlag, 16> oldCarriedDeps = carriedDeps;
      resetDeepDeps(carriedDeps, d);

      countAuxParamsAndConstraints(g, d);
      setScheduleMemoryOffsets(g, d);
      instantiateOmniSimplex(g, d, true);
      addIndependentSolutionConstraints(g, d);
      if (!omniSimplex.initiateFeasible()) {
        sol.resizeForOverwrite(getLambdaOffset() - 1);
        omniSimplex.lexMinimize(sol);
        // lexMinimize(g, sol, d);
        updateSchedules(g, d);
        BitSet depSat = deactivateSatisfiedEdges(g, d);
        if (std::optional<BitSet<>> depSatN = optimize(g, d + 1, maxDepth))
          return depSat |= *depSatN;
      }
      // we failed, so reset solved schedules
      std::swap(g.activeEdges, activeEdges);
      std::swap(g.nodeIds, nodeIds);
      auto oldNodeIter = oldSchedules.begin();
      for (auto &&n : g) n.getSchedule() = *(oldNodeIter++);
      std::swap(carriedDeps, oldCarriedDeps);
    }
    return depSatLevel;
  }
  /// optimize at depth `d`
  /// receives graph by value, so that it is not invalidated when
  /// recursing
  [[nodiscard]] auto optimize(Graph g, size_t d, size_t maxDepth)
    -> std::optional<BitSet<>> {
    if (d >= maxDepth) return BitSet{};
    countAuxParamsAndConstraints(g, d);
    setScheduleMemoryOffsets(g, d);
    // if we fail on this level, break the graph
    BitSet activeEdgesBackup = g.activeEdges;
    if (std::optional<BitSet<>> depSat = optimizeLevel(g, d)) {
      const size_t numSat = depSat->size();
      if (std::optional<BitSet<>> depSatNest = optimize(g, d + 1, maxDepth)) {
        if (numSat && depSatNest->size())
          return optimizeSatDep(std::move(g), d, maxDepth, std::move(*depSat),
                                *depSatNest, std::move(activeEdgesBackup));
        return *depSat |= *depSatNest;
      }
    }
    return breakGraph(std::move(g), d);
  }
  // returns true on failure
  [[nodiscard]] auto optimize() -> std::optional<BitSet<>> {
    fillEdges();
    fillUserToMemoryMap();
    connectGraph();
    carriedDeps.resize(nodes.size());
#ifndef NDEBUG
    validateMemory();
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
    // memNodesWithOutEdges{BitSet::dense(lblock.memory.size())};
    os << "\nLoopBlock Edges (#edges = " << lblock.edges.size() << "):";
    for (auto &edge : lblock.edges) {
      os << "\n\tEdge = " << edge;
      for (size_t inIndex : edge.in->nodeIndex) {
        const Schedule &sin = lblock.getNode(inIndex).getSchedule();
        os << "Schedule In: nodeIndex = " << edge.in->nodeIndex
           << "\nref = " << *edge.in << "\ns.getPhi()" << sin.getPhi()
           << "\ns.getFusionOmega() = " << sin.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sin.getOffsetOmega();
      }
      for (size_t outIndex : edge.out->nodeIndex) {
        const Schedule &sout = lblock.getNode(outIndex).getSchedule();
        os << "\n\nSchedule Out:\nnodeIndex = " << edge.out->nodeIndex
           << "; ref = " << *edge.out << "\ns.getPhi()" << sout.getPhi()
           << "\ns.getFusionOmega() = " << sout.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sout.getOffsetOmega();
      }
      llvm::errs() << "\n\n";
    }
    os << "\nLoopBlock schedule (#mem accesses = " << lblock.memory.size()
       << "):\n\n";
    for (auto mem : lblock.memory) {
      os << "Ref = " << *mem;
      for (size_t nodeIndex : mem->nodeIndex) {
        const Schedule &s = lblock.getNode(nodeIndex).getSchedule();
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
static_assert(std::ranges::range<LinearProgramLoopBlock::Graph>);
static_assert(Graphs::AbstractGraph<LinearProgramLoopBlock::Graph>);
