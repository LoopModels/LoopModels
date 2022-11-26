#pragma once

#include "./ArrayReference.hpp"
#include "./BitSets.hpp"
#include "./DependencyPolyhedra.hpp"
#include "./Graphs.hpp"
#include "./LinearAlgebra.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Orthogonalize.hpp"
#include "./Polyhedra.hpp"
#include "./Schedule.hpp"
#include "./Simplex.hpp"
#include "MemoryAccess.hpp"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

template <std::integral I>
[[maybe_unused]] static void insertSortedUnique(llvm::SmallVectorImpl<I> &v,
                                                const I &x) {
    for (auto it = v.begin(), ite = v.end(); it != ite; ++it) {
        if (*it < x)
            continue;
        if (*it > x)
            v.insert(it, x);
        return;
    }
    v.push_back(x);
}

struct ScheduledNode {
    [[no_unique_address]] BitSet memory{};
    [[no_unique_address]] BitSet inNeighbors{};
    [[no_unique_address]] BitSet outNeighbors{};
    [[no_unique_address]] Schedule schedule{};
    [[no_unique_address]] uint32_t phiOffset{0};   // used in LoopBlock
    [[no_unique_address]] uint32_t omegaOffset{0}; // used in LoopBlock
    [[no_unique_address]] uint32_t carriedDependence{0};
    [[no_unique_address]] uint8_t numLoops{0};
    [[no_unique_address]] uint8_t rank{0};
    [[no_unique_address]] bool visited{false};
    void addMemory(MemoryAccess *mem, unsigned memId, unsigned nodeIndex) {
        mem->addNodeIndex(nodeIndex);
        memory.insert(memId);
        numLoops = std::max(numLoops, uint8_t(mem->getNumLoops()));
    }
    bool wasVisited() const { return visited; }
    void visit() { visited = true; }
    void unVisit() { visited = false; }
    size_t getNumLoops() const { return numLoops; }
    bool phiIsScheduled(size_t d) const { return d < rank; }

    size_t updatePhiOffset(size_t p) { return phiOffset = p + numLoops; }
    size_t updateOmegaOffset(size_t o) {
        omegaOffset = o;
        return ++o;
    }
    size_t getPhiOffset() const { return phiOffset; }
    Range<size_t, size_t> getPhiOffsetRange() const {
        return _(phiOffset - numLoops, phiOffset);
    }

    ScheduledNode operator|(const ScheduledNode &s) const {
        uint8_t nL = std::max(numLoops, s.numLoops);
        return {memory | s.memory,
                (inNeighbors | s.inNeighbors),
                (outNeighbors | s.outNeighbors),
                Schedule(nL),
                0,
                0,
                nL};
    }
    ScheduledNode &operator|=(const ScheduledNode &s) {
        memory |= s.memory;
        outNeighbors |= s.outNeighbors;
        numLoops = std::max(numLoops, s.numLoops);
        return *this;
    }
    PtrVector<int64_t> getSchedule(size_t d) const {
        return schedule.getPhi()(d, _);
    }
};

struct CarriedDependencyFlag {
    [[no_unique_address]] uint32_t flag{0};
    constexpr bool carriesDependency(size_t d) { return (flag >> d) & 1; }
    constexpr void setCarriedDependency(size_t d) {
        flag |= (uint32_t(1) << uint32_t(d));
    }
    static constexpr uint32_t resetMaskFlag(size_t d) {
        return ((uint32_t(1) << uint32_t(d)) - uint32_t(1));
    }
    // resets all but `d` deps
    constexpr void resetDeepDeps(size_t d) { flag &= resetMaskFlag(d); }
};
[[maybe_unused]] static void
resetDeepDeps(llvm::MutableArrayRef<CarriedDependencyFlag> v, size_t d) {
    uint32_t mask = CarriedDependencyFlag::resetMaskFlag(d);
    for (auto &&x : v)
        x.flag &= mask;
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
struct LoopBlock { // : BaseGraph<LoopBlock, ScheduledNode> {
    // llvm::SmallVector<ArrayReference, 0> refs;
    // TODO: figure out how to handle the graph's dependencies based on
    // operation/instruction chains.
    // Perhaps implicitly via the graph when using internal orthogonalization
    // and register tiling methods, and then generate associated constraints
    // or aliasing between schedules when running the ILP solver?
    // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
    // and all other other shared schedule parameters are aliases (i.e.,
    // identical)?
    // using VertexType = ScheduledNode;
    [[no_unique_address]] llvm::SmallVector<MemoryAccess *> memory;
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
    void clear() {
        memory.clear();
        nodes.clear();
        edges.clear();
        carriedDeps.clear();
        userToMemory.clear();
        sol.clear();
        // allocator.Reset();
    }
    size_t numVerticies() const { return nodes.size(); }
    llvm::MutableArrayRef<ScheduledNode> getVerticies() { return nodes; }
    llvm::ArrayRef<ScheduledNode> getVerticies() const { return nodes; }
    struct OutNeighbors {
        LoopBlock &loopBlock;
        ScheduledNode &node;
        // size_t size()const{return node.num
    };
    OutNeighbors outNeighbors(size_t idx) {
        return OutNeighbors{*this, nodes[idx]};
    }
    [[nodiscard]] size_t calcMaxDepth() const {
        size_t d = 0;
        for (auto &mem : memory)
            d = std::max(d, mem->getNumLoops());
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
            ArrayReference &refI = mai.ref;
            for (size_t j = 0; j < i; ++j) {
                MemoryAccess &maj = *memory[j];
                ArrayReference &refJ = maj.ref;
                if ((refI.basePointer != refJ.basePointer) ||
                    ((mai.isLoad) && (maj.isLoad)))
                    continue;
                addEdge(mai, maj);
            }
        }
    }
    /// used in searchOperandsForLoads
    /// if an operand is stored, we can reload it.
    /// This will insert a new store memory access.
    bool searchValueForStores(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                              ScheduledNode &node, llvm::User *user,
                              unsigned nodeIndex) {
        for (llvm::User *use : user->users()) {
            if (visited.contains(use))
                continue;
            if (llvm::isa<llvm::StoreInst>(use)) {
                auto memAccess = userToMemory.find(use);
                if (memAccess == userToMemory.end())
                    continue; // load is not a part of the LoopBlock
                unsigned memId = memAccess->getSecond();
                MemoryAccess *store = memory[memId];
                node.addMemory(store, memId, nodeIndex);
                // MemoryAccess *load = new (allocator) MemoryAccess(*store);
                // load->isLoad = true;
                // node.addMemory(load, memory.size(), nodeIndex);
                // memory.push_back(load);
                // TODO: need to add edges and correct edgesIn and edgesOut
                //
                return true;
            }
        }
        return false;
    }
    void checkUserForLoads(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                           ScheduledNode &node, llvm::User *user,
                           unsigned nodeIndex) {
        if (!user || visited.contains(user))
            return;
        if (llvm::isa<llvm::LoadInst>(user)) {
            auto memAccess = userToMemory.find(user);
            if (memAccess == userToMemory.end())
                return; // load is not a part of the LoopBlock
            unsigned memId = memAccess->getSecond();
            node.addMemory(memory[memId], memId, nodeIndex);
        } else if (!searchValueForStores(visited, node, user, nodeIndex))
            searchOperandsForLoads(visited, node, user, nodeIndex);
    }
    /// We search uses of user `u` for any stores so that we can assign the use
    /// and the store the same schedule. This is done because it is assumed the
    /// data is held in registers (or, if things go wrong, spilled to the stack)
    /// in between a load and a store. A complication is that LLVM IR can be
    /// messy, e.g. we may have %x = load %a %y = call foo(x) store %y, %b %z =
    /// call bar(y) store %z, %c here, we might lock all three operations
    /// together. However, this limits reordering opportunities; we thus want to
    /// insert a new load instruction so that we have: %x = load %a %y = call
    /// foo(x) store %y, %b %y.reload = load %b %z = call bar(y.reload) store %z,
    /// %c and we create a new edge from `store %y, %b` to `load %b`.
    void searchOperandsForLoads(llvm::SmallPtrSet<llvm::User *, 32> &visited,
                                ScheduledNode &node, llvm::User *u,
                                unsigned nodeIndex) {
        visited.insert(u);
        if (llvm::StoreInst *s = llvm::dyn_cast<llvm::StoreInst>(u))
            return checkUserForLoads(
                visited, node, llvm::dyn_cast<llvm::User>(s->getValueOperand()),
                nodeIndex);
        for (auto &&op : u->operands())
            checkUserForLoads(visited, node,
                              llvm::dyn_cast<llvm::User>(op.get()), nodeIndex);
    }
    void connect(unsigned inIndex, unsigned outIndex) {
        nodes[inIndex].outNeighbors.insert(outIndex);
        nodes[outIndex].inNeighbors.insert(inIndex);
    }
    void connect(BitSet inIndexSet, BitSet outIndexSet) {
        for (auto inIndex : inIndexSet)
            for (auto outIndex : outIndexSet)
                connect(inIndex, outIndex);
    }
    void connect(const Dependence &e) {
        for (auto inIndex : e.in->nodeIndex)
            for (auto outIndex : e.out->nodeIndex)
                if (inIndex != outIndex) {
                    llvm::errs() << "Connecting inIndex = " << inIndex
                                 << "; outIndex = " << outIndex << "\n";
                    connect(inIndex, outIndex);
                }
    }
    size_t calcNumStores() const {
        size_t numStores = 0;
        for (auto &m : memory)
            numStores += !(m->isLoad);
        return numStores;
    }
    /// When connecting a graph, we draw direct connections between stores and
    /// loads loads may be duplicated across stores to allow for greater
    /// reordering flexibility (which should generally reduce the ultimate amount
    /// of loads executed in the eventual generated code)
    void connectGraph() {
        // assembles direct connections in node graph
        llvm::SmallPtrSet<llvm::User *, 32> visited;
        nodes.reserve(calcNumStores());
        for (unsigned i = 0; i < memory.size(); ++i) {
            MemoryAccess *mai = memory[i];
            if (mai->isLoad)
                continue;
            unsigned nodeIndex = nodes.size();
            ScheduledNode &node = nodes.emplace_back();
            node.addMemory(mai, i, nodeIndex);
            searchOperandsForLoads(visited, node, mai->user, nodeIndex);
            visited.clear();
        }
        for (auto &e : edges)
            connect(e.in->nodeIndex, e.out->nodeIndex);
        for (auto &&node : nodes)
            node.schedule.init(node.getNumLoops());
        // now that we've assigned each MemoryAccess to a NodeIndex, we
        // build the actual graph
    }
    struct Graph {
        // a subset of Nodes
        BitSet nodeIds;
        BitSet activeEdges;
        llvm::MutableArrayRef<MemoryAccess *> mem;
        llvm::MutableArrayRef<ScheduledNode> nodes;
        llvm::ArrayRef<Dependence> edges;
        // llvm::SmallVector<bool> visited;
        // BitSet visited;
        Graph operator&(const Graph &g) {
            return Graph{nodeIds & g.nodeIds, activeEdges & g.activeEdges, mem,
                         nodes, edges};
        }
        Graph operator|(const Graph &g) {
            return Graph{nodeIds | g.nodeIds, activeEdges | g.activeEdges, mem,
                         nodes, edges};
        }
        Graph &operator&=(const Graph &g) {
            nodeIds &= g.nodeIds;
            activeEdges &= g.activeEdges;
            return *this;
        }
        Graph &operator|=(const Graph &g) {
            nodeIds |= g.nodeIds;
            activeEdges |= g.activeEdges;
            return *this;
        }
        [[nodiscard]] BitSet &inNeighbors(size_t i) {
            return nodes[i].inNeighbors;
        }
        [[nodiscard]] BitSet &outNeighbors(size_t i) {
            return nodes[i].outNeighbors;
        }
        [[nodiscard]] const BitSet &inNeighbors(size_t i) const {
            return nodes[i].inNeighbors;
        }
        [[nodiscard]] const BitSet &outNeighbors(size_t i) const {
            return nodes[i].outNeighbors;
        }
        [[nodiscard]] bool containsNode(size_t i) const {
            return nodeIds.contains(i);
        }
        [[nodiscard]] bool containsNode(BitSet &b) const {
            for (size_t i : b)
                if (nodeIds.contains(i))
                    return true;
            return false;
        }
        [[nodiscard]] bool missingNode(size_t i) const {
            return !containsNode(i);
        }
        [[nodiscard]] bool missingNode(size_t i, size_t j) const {
            return !(containsNode(i) && containsNode(j));
        }
        /// returns false iff e.in and e.out are both in graph
        /// that is, to be missing, both `e.in` and `e.out` must be missing
        /// in case of multiple instances of the edge, we check all of them
        /// if any are not missing, returns false
        /// only returns true if every one of them is missing.
        [[nodiscard]] bool missingNode(const Dependence &e) const {
            for (auto inIndex : e.in->nodeIndex)
                for (auto outIndex : e.out->nodeIndex)
                    if (!missingNode(inIndex, outIndex))
                        return false;
            return true;
        }

        [[nodiscard]] bool isInactive(const Dependence &edge, size_t d) const {
            return edge.isInactive(d) || missingNode(edge);
        }
        [[nodiscard]] bool isInactive(const Dependence &edge) const {
            return missingNode(edge);
        }
        [[nodiscard]] bool isInactive(size_t e, size_t d) const {
            return !(activeEdges[e]) || isInactive(edges[e], d);
        }
        [[nodiscard]] bool isInactive(size_t e) const {
            return !(activeEdges[e]) || isInactive(edges[e]);
        }
        [[nodiscard]] bool isActive(size_t e, size_t d) const {
            return (activeEdges[e]) && (!isInactive(edges[e], d));
        }
        [[nodiscard]] bool isActive(size_t e) const {
            return (activeEdges[e]) && (!isInactive(edges[e]));
        }
        BitSliceView<ScheduledNode>::Iterator begin() {
            return BitSliceView<ScheduledNode>{nodes, nodeIds}.begin();
        }
        BitSliceView<ScheduledNode>::ConstIterator begin() const {
            const BitSliceView<ScheduledNode> bsv{nodes, nodeIds};
            return bsv.begin();
        }
        BitSet::Iterator::End end() const { return {}; }
        bool wasVisited(size_t i) const { return nodes[i].visited; }
        void visit(size_t i) { nodes[i].visit(); }
        void unVisit(size_t i) { nodes[i].unVisit(); }
        size_t getNumVertices() const { return nodeIds.size(); }
        size_t maxVertexId() const { return nodeIds.maxValue(); }
        BitSet &vertexIds() { return nodeIds; }
        const BitSet &vertexIds() const { return nodeIds; }
        [[nodiscard]] Graph subGraph(const BitSet &components) {
            return {components, activeEdges, mem, nodes, edges};
        }
        [[nodiscard]] llvm::SmallVector<Graph, 0>
        split(const llvm::SmallVector<BitSet> &components) {
            llvm::SmallVector<Graph, 0> graphs;
            graphs.reserve(components.size());
            for (auto &c : components)
                graphs.push_back(subGraph(c));
            return graphs;
        }
        [[nodiscard]] size_t calcMaxDepth() const {
            if (nodeIds.data.size() == 0)
                return 0;
            size_t d = 0;
            for (auto n : nodeIds)
                d = std::max(d, nodes[n].getNumLoops());
            return d;
        }
    };
    // bool connects(const Dependence &e, Graph &g0, Graph &g1, size_t d) const
    // {
    //     return ((e.in->getNumLoops() > d) && (e.out->getNumLoops() > d)) &&
    //            connects(e, g0, g1);
    // }
    bool connects(const Dependence &e, Graph &g0, Graph &g1) const {
        if (!e.in->isLoad) {
            // e.in is a store
            size_t nodeIn = *e.in->nodeIndex.begin();
            bool g0ContainsNodeIn = g0.nodeIds.contains(nodeIn);
            bool g1ContainsNodeIn = g1.nodeIds.contains(nodeIn);
            if (!(g0ContainsNodeIn || g1ContainsNodeIn))
                return false;
            for (size_t nodeOut : e.out->nodeIndex)
                if ((g0ContainsNodeIn && g1.nodeIds.contains(nodeOut)) ||
                    (g1ContainsNodeIn && g0.nodeIds.contains(nodeOut)))
                    return true;
        } else {
            // e.out must be a store
            size_t nodeOut = *e.out->nodeIndex.begin();
            bool g0ContainsNodeOut = g0.nodeIds.contains(nodeOut);
            bool g1ContainsNodeOut = g1.nodeIds.contains(nodeOut);
            if (!(g0ContainsNodeOut || g1ContainsNodeOut))
                return false;
            for (auto nodeIn : e.in->nodeIndex) {
                if ((g0ContainsNodeOut && g1.nodeIds.contains(nodeIn)) ||
                    (g1ContainsNodeOut && g0.nodeIds.contains(nodeIn)))
                    return true;
            }
        }
        return false;
    }
    Graph fullGraph() {
        return {BitSet::dense(nodes.size()), BitSet::dense(edges.size()),
                memory, nodes, edges};
    }
    void fillUserToMemoryMap() {
        for (unsigned i = 0; i < memory.size(); ++i)
            userToMemory.insert(std::make_pair(memory[i]->user, i));
    }
    llvm::Optional<size_t> getOverlapIndex(const Dependence &edge) {
        MemoryAccess *store;
        MemoryAccess *other;
        if (edge.in->isLoad) {
            // edge.out is a store
            store = edge.out;
            other = edge.in;
        } else {
            // edge.in is a store
            store = edge.in;
            other = edge.out;
        }
        size_t index = *store->nodeIndex.begin();
        if (other->nodeIndex.contains(index))
            return index;
        return {};
    }
    llvm::Optional<BitSet> optOrth(Graph g) {

        const size_t maxDepth = calcMaxDepth();
        // check for orthogonalization opportunities
        bool tryOrth = false;
        for (size_t e = 0; e < edges.size(); ++e) {
            Dependence &edge = edges[e];
            if (edge.in->isLoad == edge.out->isLoad)
                continue;
            llvm::Optional<size_t> maybeIndex = getOverlapIndex(edge);
            if (!maybeIndex)
                continue;
            size_t index = *maybeIndex;
            ScheduledNode &node = nodes[index];
            if (node.phiIsScheduled(0) ||
                (edge.in->indexMatrix() != edge.out->indexMatrix()))
                continue;
            PtrMatrix<int64_t> indMat = edge.in->indexMatrix();
            size_t r = NormalForm::rank(indMat);
            if (r == edge.in->getNumLoops())
                continue;
            // TODO handle linearly dependent acceses, filtering them out
            if (r == edge.in->ref.getArrayDim()) {
                // indMat indvars are indexed from outside<->inside
                // phi indvars are indexed from inside<->outside
                // so, indMat is indvars[outside<->inside] x array dim
                // phi is loop[outside<->inside] x
                // indvars[inside<->outside]
                MutPtrMatrix<int64_t> phi = node.schedule.getPhi();
                const size_t indR = indMat.numRow();
                const size_t phiOffset = phi.numCol() - indR;
                for (size_t rr = 0; rr < r; ++rr) {
                    phi(rr, _(begin, phiOffset)) = 0;
                    phi(rr, _(phiOffset, phiOffset + indR)) = indMat(_, rr);
                }
                // node.schedule.getPhi()(_(0, r), _) =
                indMat.transpose();
                node.rank = r;
                tryOrth = true;
            }
        }
        if (tryOrth) {
            if (llvm::Optional<BitSet> opt = optimize(g, 0, maxDepth)) {
                llvm::errs() << "orth opt succeeded!\n";
                return opt;
            }
            for (auto &&n : nodes)
                n.rank = 0;
        }
        return optimize(std::move(g), 0, maxDepth);
    }
    [[nodiscard]] size_t countNumLambdas(const Graph &g, size_t d) const {
        size_t c = 0;
        for (size_t e = 0; e < edges.size(); ++e)
            c += ((g.isInactive(e, d)) ? 0 : edges[e].getNumLambda());
        return c;
    }
    [[nodiscard]] size_t countNumBoundingCoefs(const Graph &g, size_t d) const {
        size_t c = 0;
        for (size_t e = 0; e < edges.size(); ++e)
            c += (g.isInactive(e, d) ? 0 : edges[e].getNumSymbols());
        return c;
    }
    void countAuxParamsAndConstraints(const Graph &g, size_t d) {
        size_t a = 0, b = 0, c = 0, ae = 0;
        for (size_t e = 0; e < edges.size(); ++e) {
            if (g.isInactive(e, d))
                continue;
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
        for (auto o : memory)
            assert(o->user != m->user);
        memory.push_back(m);
    }
    // assemble omni-simplex
    // we want to order variables to be
    // us, ws, Phi^-, Phi^+, omega, lambdas
    // this gives priority for minimization

    // bounding, scheduled coefs, lambda
    // matches lexicographical ordering of minimization
    // bounding, however, is to be favoring minimizing `u` over `w`
    [[nodiscard]] size_t getLambdaOffset() const {
        return 1 + numBounding + numActiveEdges + numPhiCoefs + numOmegaCoefs;
    }
    [[nodiscard]] bool hasActiveEdges(const Graph &g,
                                      const MemoryAccess &mem) const {
        for (auto &e : mem.edgesIn)
            if (!g.isInactive(e))
                return true;
        // else
        //     llvm::errs() << "hasActiveEdge In false for: " << edges[e];
        for (auto &e : mem.edgesOut)
            if (!g.isInactive(e))
                return true;
        // else
        //     llvm::errs() << "hasActiveEdge Out false for: " << edges[e];
        return false;
    }
    [[nodiscard]] bool hasActiveEdges(const Graph &g, const MemoryAccess &mem,
                                      size_t d) const {
        for (auto &e : mem.edgesIn)
            if (!g.isInactive(e, d))
                return true;
        // else
        //     llvm::errs() << "hasActiveEdge In d = " << d
        //                  << " false for: " << edges[e];
        for (auto &e : mem.edgesOut)
            if (!g.isInactive(e, d))
                return true;
        // else
        //     llvm::errs() << "hasActiveEdge Out d = " << d
        //                  << " false for: " << edges[e];
        return false;
    }
    [[nodiscard]] bool hasActiveEdges(const Graph &g, const ScheduledNode &node,
                                      size_t d) const {
        for (auto memId : node.memory)
            if (hasActiveEdges(g, *memory[memId], d))
                return true;
        return false;
    }
    [[nodiscard]] bool hasActiveEdges(const Graph &g,
                                      const ScheduledNode &node) const {
        for (auto memId : node.memory)
            if (hasActiveEdges(g, *memory[memId]))
                return true;
        return false;
    }
    void setScheduleMemoryOffsets(const Graph &g, size_t d) {
        size_t pInit = numBounding + numActiveEdges + 1, p = pInit;
        for (auto &&node : nodes) {
            if ((d >= node.getNumLoops()) || (!hasActiveEdges(g, node, d)))
                continue;
            if (!node.phiIsScheduled(d))
                p = node.updatePhiOffset(p);
        }
        numPhiCoefs = p - pInit;
        size_t o = p;
        for (auto &&node : nodes) {
            if ((d > node.getNumLoops()) || (!hasActiveEdges(g, node, d)))
                continue;
            o = node.updateOmegaOffset(o);
        }
        numOmegaCoefs = o - p;
    }
    void validateMemory() {
        for (auto mem : memory)
            assert(1 + mem->ref.getNumLoops() == mem->omegas.size());
    }
    void validateEdges() {
        for (auto &edge : edges) {
            assert(edge.in->getNumLoops() + edge.out->getNumLoops() ==
                   edge.getNumPhiCoefficients());
            // 2 == 1 for const offset + 1 for w
            assert(2 + edge.depPoly.getNumLambda() +
                       edge.getNumPhiCoefficients() +
                       edge.getNumOmegaCoefficients() ==
                   edge.dependenceSatisfaction.getConstraints().numCol());
        }
    }
    void instantiateOmniSimplex(const Graph &g, size_t d,
                                bool satisfyDeps = false) {
        // defines numScheduleCoefs, numLambda, numBounding, and
        // numConstraints
        omniSimplex.reserve(numConstraints + numOmegaCoefs,
                            1 + numBounding + numActiveEdges + numPhiCoefs +
                                2 * numOmegaCoefs + numLambda);
        omniSimplex.resizeForOverwrite(
            numConstraints, 1 + numBounding + numActiveEdges + numPhiCoefs +
                                numOmegaCoefs + numLambda);
        auto C{omniSimplex.getConstraints()};
        C = 0;
        // layout of omniSimplex:
        // Order: C, then priority to minimize
        // all : C, u, w, Phis, omegas, lambdas
        // rows give constraints; each edge gets its own
        // constexpr size_t numOmega =
        //     DependencePolyhedra::getNumOmegaCoefficients();
        size_t u = 1, w = 1 + numBounding;
        size_t c = 0, l = getLambdaOffset();
        for (size_t e = 0; e < edges.size(); ++e) {
            Dependence &edge = edges[e];
            if (g.isInactive(e, d))
                continue;
            BitSet &outNodeIndexSet = edge.out->nodeIndex;
            BitSet &inNodeIndexSet = edge.in->nodeIndex;
            const auto [satC, satL, satPp, satPc, satO, satW] =
                edge.splitSatisfaction();
            const auto [bndC, bndL, bndPp, bndPc, bndO, bndWU] =
                edge.splitBounding();
            const size_t numSatConstraints = satC.size();
            const size_t numBndConstraints = bndC.size();
            for (auto outNodeIndex : outNodeIndexSet) {
                const ScheduledNode &outNode = nodes[outNodeIndex];
                for (auto inNodeIndex : inNodeIndexSet) {
                    const ScheduledNode &inNode = nodes[inNodeIndex];

                    size_t cc = c + numSatConstraints;
                    size_t ccc = cc + numBndConstraints;

                    size_t ll = l + satL.numCol();
                    size_t lll = ll + bndL.numCol();
                    C(_(c, cc), _(l, ll)) = satL;
                    C(_(cc, ccc), _(ll, lll)) = bndL;
                    l = lll;

                    // bounding
                    C(_(cc, ccc), w++) = bndWU(_, 0);
                    size_t uu = u + bndWU.numCol() - 1;
                    C(_(cc, ccc), _(u, uu)) = bndWU(_, _(1, end));
                    u = uu;
                    if (satisfyDeps)
                        C(_(c, cc), 0) = satC + satW;
                    else
                        C(_(c, cc), 0) = satC;
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
                                    C(_(c, cc), 0) -=
                                        satPc *
                                            sch(_(end - satPc.numCol(), end)) +
                                        satPp *
                                            sch(_(end - satPp.numCol(), end));
                                    C(_(cc, ccc), 0) -=
                                        bndPc *
                                            sch(_(end - bndPc.numCol(), end)) +
                                        bndPp *
                                            sch(_(end - satPp.numCol(), end));
                                } else {
                                    // FIXME: phiChild = [14:18), 4 cols
                                    // while Dependence seems to indicate 2
                                    // loops why the disagreement?
                                    auto phiChild = outNode.getPhiOffset();
                                    C(_(c, cc), _(phiChild - satPc.numCol(),
                                                  phiChild)) = satPc + satPp;
                                    C(_(cc, ccc), _(phiChild - bndPc.numCol(),
                                                    phiChild)) = bndPc + bndPp;
                                }
                            } else if (outNode.phiIsScheduled(d)) {
                                // add it constants
                                // note that loop order in schedule goes
                                // inner -> outer
                                // so we need to drop inner most if one has less
                                auto sch = outNode.getSchedule(d);
                                auto schP = sch(_(end - satPp.numCol(), end));
                                auto schC = sch(_(end - satPc.numCol(), end));
                                C(_(c, cc), 0) -= satPc * schC + satPp * schP;
                                C(_(cc, ccc), 0) -= bndPc * schC + bndPp * schP;
                            } else if (satPc.numCol() < satPp.numCol()) {
                                auto phiChild = outNode.getPhiOffset();
                                size_t P = satPc.numCol();
                                auto m = phiChild - P;
                                C(_(c, cc), _(phiChild - satPp.numCol(), m)) =
                                    satPp(_, _(begin, end - P));
                                C(_(cc, ccc), _(phiChild - bndPp.numCol(), m)) =
                                    bndPp(_, _(begin, end - P));
                                C(_(c, cc), _(m, phiChild)) =
                                    satPc + satPp(_, _(end - P, end));
                                C(_(cc, ccc), _(m, phiChild)) =
                                    bndPc + bndPp(_, _(end - P, end));
                            } else /* if (satPc.numCol() > satPp.numCol()) */ {
                                auto phiChild = outNode.getPhiOffset();
                                size_t P = satPp.numCol();
                                auto m = phiChild - P;
                                C(_(c, cc), _(phiChild - satPc.numCol(), m)) =
                                    satPc(_, _(begin, end - P));
                                C(_(cc, ccc), _(phiChild - bndPc.numCol(), m)) =
                                    bndPc(_, _(begin, end - P));
                                C(_(c, cc), _(m, phiChild)) =
                                    satPc(_, _(end - P, end)) + satPp;
                                C(_(cc, ccc), _(m, phiChild)) =
                                    bndPc(_, _(end - P, end)) + bndPp;
                            }
                            C(_(c, cc), outNode.omegaOffset) =
                                satO(_, 0) + satO(_, 1);
                            C(_(cc, ccc), outNode.omegaOffset) =
                                bndO(_, 0) + bndO(_, 1);
                        }
                    } else {
                        // llvm::errs() << "outNodeIndex != inNodeIndex\n";
                        if (d < edge.out->getNumLoops())
                            updateConstraints(C, outNode, satPc, bndPc, d, c,
                                              cc, ccc);
                        if (d < edge.in->getNumLoops())
                            updateConstraints(C, inNode, satPp, bndPp, d, c, cc,
                                              ccc);
                        // Omegas are included regardless of rotation
                        if (d < edge.out->getNumLoops()) {
                            C(_(c, cc), outNode.omegaOffset) =
                                satO(_, !edge.forward);
                            C(_(cc, ccc), outNode.omegaOffset) =
                                bndO(_, !edge.forward);
                        }
                        if (d < edge.in->getNumLoops()) {
                            C(_(c, cc), inNode.omegaOffset) =
                                satO(_, edge.forward);
                            C(_(cc, ccc), inNode.omegaOffset) =
                                bndO(_, edge.forward);
                        }
                    }
                    c = ccc;
                }
            }
        }
    }
    void updateConstraints(MutPtrMatrix<int64_t> C, const ScheduledNode &node,
                           PtrMatrix<int64_t> sat, PtrMatrix<int64_t> bnd,
                           size_t d, size_t c, size_t cc, size_t ccc) {
        if (node.phiIsScheduled(d)) {
            // add it constants
            auto sch = node.getSchedule(d);
            // order is inner <-> outer
            // so we need the end of schedule if it is larger
            C(_(c, cc), 0) -= sat * sch(_(end - sat.numCol(), end));
            C(_(cc, ccc), 0) -= bnd * sch(_(end - bnd.numCol(), end));
        } else {
            assert(sat.numCol() == bnd.numCol());
            // add it to C
            auto phiChild = node.getPhiOffset();
            C(_(c, cc), _(phiChild - sat.numCol(), phiChild)) = sat;
            C(_(cc, ccc), _(phiChild - bnd.numCol(), phiChild)) = bnd;
        }
    }
    BitSet deactivateSatisfiedEdges(Graph &g, size_t d) {
        if (allZero(sol(_(begin, numBounding + numActiveEdges))))
            return {};
        size_t u = 0, w = numBounding;
        BitSet deactivated;
        for (size_t e = 0; e < edges.size(); ++e) {
            if (g.isInactive(e, d))
                continue;
            const Dependence &edge = edges[e];
            size_t uu =
                u + edge.dependenceBounding.getConstraints().numCol() -
                (2 + edge.depPoly.getNumLambda() +
                 edge.getNumPhiCoefficients() + edge.getNumOmegaCoefficients());
            if (sol(w++) || (!(allZero(sol(_(u, uu)))))) {
                g.activeEdges.remove(e);
                deactivated.insert(e);
                for (size_t inIndex : edge.in->nodeIndex)
                    carriedDeps[inIndex].setCarriedDependency(d);
                for (size_t outIndex : edge.out->nodeIndex)
                    carriedDeps[outIndex].setCarriedDependency(d);
            }
            u = uu;
        }
        return deactivated;
    }
    void updateSchedules(const Graph &g, size_t depth) {
#ifndef NDEBUG
        if (depth & 1) {
            bool allZero = true;
            for (auto &s : sol) {
                allZero &= (s == 0);
            }
            if (allZero)
                SHOWLN(omniSimplex);
            assert(!allZero);
        }
#endif
        for (auto &&node : nodes) {
            if (depth >= node.getNumLoops())
                continue;
            if (!hasActiveEdges(g, node)) {
                node.schedule.getOffsetOmega()(depth) =
                    std::numeric_limits<int64_t>::min();
                if (!node.phiIsScheduled(depth))
                    node.schedule.getPhi()(depth, _) =
                        std::numeric_limits<int64_t>::min();
                continue;
            }
            node.schedule.getOffsetOmega()(depth) = sol(node.omegaOffset - 1);
            if (!node.phiIsScheduled(depth)) {
                auto phi = node.schedule.getPhi()(depth, _);
                auto s = sol(node.getPhiOffsetRange() - 1);
                int64_t l = denomLCM(s);
                for (size_t i = 0; i < phi.size(); ++i)
                    assert(((s(i).numerator * l) / (s(i).denominator)) >= 0);
                if (l == 1)
                    for (size_t i = 0; i < phi.size(); ++i)
                        phi(i) = s(i).numerator;
                else
                    for (size_t i = 0; i < phi.size(); ++i)
                        phi(i) = (s(i).numerator * l) / (s(i).denominator);
                assert(!(allZero(phi)));
                // node.schedule.getPhi()(depth, _) =
                //     sol(node.getPhiOffset() - 1) *
                //     denomLCM(sol(node.getPhiOffset() - 1));
            }
#ifndef NDEBUG
            if (!node.phiIsScheduled(depth)) {
                int64_t l = denomLCM(sol(node.getPhiOffsetRange() - 1));
                for (size_t i = 0; i < node.schedule.getPhi().numCol(); ++i)
                    assert(node.schedule.getPhi()(depth, i) ==
                           sol(node.getPhiOffsetRange() - 1)(i) * l);
            }
#endif
        }
    }
    [[nodiscard]] static int64_t lexSign(PtrVector<int64_t> x) {
        for (auto it = x.rbegin(); it != x.rend(); ++it)
            if (*it)
                return 2 * (*it > 0) - 1;
        return 0;
    }
    void addIndependentSolutionConstraints(const Graph &g, size_t depth) {
        omniSimplex.reserveExtraRows(memory.size());
        if (depth == 0) {
            // add ones >= 0
            for (auto &&node : nodes) {
                if (node.phiIsScheduled(depth) || (!hasActiveEdges(g, node)))
                    continue;
                auto c{omniSimplex.addConstraintAndVar()};
                c(0) = 1;
                c(node.getPhiOffsetRange()) = 1;
                c(end) = -1; // for >=
            }
            return;
        }
        IntMatrix A, N;
        for (auto &&node : nodes) {
            if (node.phiIsScheduled(depth) || (depth >= node.getNumLoops()) ||
                (!hasActiveEdges(g, node)))
                continue;
            A = node.schedule.getPhi()(_(0, depth), _).transpose();
            NormalForm::nullSpace11(N, A);
            auto c{omniSimplex.addConstraintAndVar()};
            c(0) = 1;
            MutPtrVector<int64_t> cc{c(node.getPhiOffsetRange())};
            // sum(N,dims=1) >= 1 after flipping row signs to be lex > 0
            for (size_t m = 0; m < N.numRow(); ++m)
                cc += N(m, _) * lexSign(N(m, _));
            c(end) = -1; // for >=
        }
        assert(!allZero(omniSimplex.getConstraints()(end, _)));
    }
    static uint64_t nonZeroMask(const AbstractVector auto &x) {
        assert(x.size() <= 64);
        uint64_t m = 0;
        for (auto y : x)
            m = ((m << 1) | (y != 0));
        return m;
    }
    static void nonZeroMasks(llvm::SmallVector<uint64_t> &masks,
                             const AbstractMatrix auto &A) {
        const auto [M, N] = A.size();
        assert(N <= 64);
        masks.resize_for_overwrite(M);
        for (size_t m = 0; m < M; ++m)
            masks[m] = nonZeroMask(A(m, _));
    }
    static llvm::SmallVector<uint64_t>
    nonZeroMasks(const AbstractMatrix auto &A) {
        llvm::SmallVector<uint64_t> masks;
        nonZeroMasks(masks, A);
        return masks;
    }
    static uint64_t nonZeroMask(const AbstractMatrix auto A) {
        const auto [M, N] = A.size();
        assert(N <= 64);
        uint64_t mask = 0;
        for (size_t m = 0; m < M; ++m)
            mask |= nonZeroMask(A(m, _));
        return mask;
    }
    void setSchedulesIndependent(const Graph &g, size_t depth) {
        // IntMatrix A, N;
        for (auto &&node : nodes) {
            if ((depth >= node.getNumLoops()) || node.phiIsScheduled(depth))
                continue;
            if (!hasActiveEdges(g, node)) {
                node.schedule.getOffsetOmega()(depth) =
                    std::numeric_limits<int64_t>::min();
                if (!node.phiIsScheduled(depth))
                    node.schedule.getPhi()(depth, _) =
                        std::numeric_limits<int64_t>::min();
                continue;
            }
            node.schedule.getOffsetOmega()(depth) = 0;
            MutSquarePtrMatrix<int64_t> phi = node.schedule.getPhi();
            phi(depth, _) = std::numeric_limits<int64_t>::min();
            // llvm::SmallVector<uint64_t> indexMasks;
            // if (depth) {
            //     A = phi(_(0, depth), _).transpose();
            //     NormalForm::nullSpace11(N, A);
            //     // we check array references to see if we can find one index
            //     // uint64_t nullMask = nonZeroMask(N);
            //     // for (MemoryAccess *mem : g.mem){
            //     //     nonZeroMasks(indexMasks,
            //     //     mem->ref.indexMatrix().transpose());

            //     // }
            //     phi(depth, _) = N(0, _) * lexSign(N(0, _));
            //     llvm::errs() << "Set schedules independent:\n";
            //     SHOWLN(phi(depth, _));
            // } else {
            //     phi(depth, _(begin, end - 1)) = 0;
            //     phi(depth, end) = 1;
            // }
        }
    }
    void resetPhiOffsets() {
        for (auto &&node : nodes)
            node.phiOffset = std::numeric_limits<unsigned>::max();
    }
    bool isSatisfied(Dependence &e, size_t d) {
        for (size_t inIndex : e.in->nodeIndex) {
            for (size_t outIndex : e.out->nodeIndex) {
                Schedule *first = &(nodes[inIndex].schedule);
                Schedule *second = &(nodes[outIndex].schedule);
                if (!e.forward)
                    std::swap(first, second);
                if (!e.isSatisfied(*first, *second, d))
                    return false;
            }
        }
        return true;
    }
    bool canFuse(Graph &g0, Graph &g1, size_t d) {
        for (auto &e : edges) {
            if ((e.in->getNumLoops() <= d) || (e.out->getNumLoops() <= d))
                return false;
            if (connects(e, g0, g1))
                if (!isSatisfied(e, d))
                    return false;
        }
        return true;
    }
    [[nodiscard]] llvm::Optional<BitSet> breakGraph(Graph g, size_t d) {
        auto components = Graphs::stronglyConnectedComponents(g);
        if (components.size() <= 1)
            return {};
        // components are sorted in topological order.
        // We split all of them, solve independently,
        // and then try to fuse again after if/where optimal schedules
        // allow it.
        llvm::errs() << "splitting graph!\n";
        auto graphs = g.split(components);
        assert(graphs.size() == components.size());
        BitSet satDeps;
        for (auto &sg : graphs) {
            if (d >= sg.calcMaxDepth())
                continue;
            countAuxParamsAndConstraints(sg, d);
            setScheduleMemoryOffsets(sg, d);
            if (llvm::Optional<BitSet> sat = optimizeLevel(sg, d)) {
                satDeps |= *sat;
            } else {
                return {}; // give up
            }
        }
        size_t unfusedOffset = 0;
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
                for (auto &&v : *gp)
                    v.schedule.getFusionOmega()[d] = unfusedOffset;
                ++unfusedOffset;
                // gi is the new base graph
                gp = &gi;
                baseGraphs.push_back(i);
            } else // fuse
                (*gp) |= gi;
        }
        // set omegas for gp
        for (auto &&v : *gp)
            v.schedule.getFusionOmega()[d] = unfusedOffset;
        ++d;
        // size_t numSat = satDeps.size();
        for (auto i : baseGraphs)
            if (llvm::Optional<BitSet> sat = optimize(
                    std::move(graphs[i]), d, graphs[i].calcMaxDepth())) {
                // TODO: try and satisfy extra dependences
                // if ((numSat > 0) && (sat->size()>0)){}
                satDeps |= *sat;
            } else {
                return {};
            }
        // remove
        return satDeps;
    }
    //     void lexMinimize(const Graph &g, Vector<Rational> &sol,
    //     size_t depth){
    // 	// omniSimplex.lexMinimize(sol);
    // #ifndef NDEBUG
    //         assert(omniSimplex.inCanonicalForm);
    //         omniSimplex.assertCanonical();
    //         // SHOWLN(omniSimplex);
    // #endif
    //         for (size_t v = 0; v < numActiveEdges + numBounding;)
    //             omniSimplex.lexMinimize(++v);
    // 	for (auto &&node : nodes) {
    //             if (depth >= node.getNumLoops())
    //                 continue;
    //             if (!hasActiveEdges(g, node))
    // 		continue;
    // 	    omniSimplex.lexMinimize(node.getPhiOffset());
    // 	}
    // 	for (auto &&node : nodes) {
    //             if (depth >= node.getNumLoops())
    //                 continue;
    //             if (!hasActiveEdges(g, node))
    // 		continue;
    // 	    omniSimplex.lexMinimize(node.omegaOffset);
    // 	}
    //         omniSimplex.copySolution(sol);
    //     }
    [[nodiscard]] llvm::Optional<BitSet> optimizeLevel(Graph &g, size_t d) {
        if (numPhiCoefs == 0) {
            setSchedulesIndependent(g, d);
            return BitSet{};
        }
        instantiateOmniSimplex(g, d);
        addIndependentSolutionConstraints(g, d);
        assert(!allZero(omniSimplex.getConstraints()(end, _)));
        if (omniSimplex.initiateFeasible()) {
            llvm::errs() << "optimizeLevel = " << d
                         << ": infeasible solution!!!\n";
            return {};
        }
        sol.resizeForOverwrite(getLambdaOffset() - 1);
        omniSimplex.lexMinimize(sol);
        updateSchedules(g, d);
        return deactivateSatisfiedEdges(g, d);
    }
    BitSet optimizeSatDep(Graph g, size_t d, size_t maxDepth,
                          BitSet depSatLevel, BitSet depSatNest,
                          BitSet activeEdges) {
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
            for (auto &n : g)
                oldSchedules.push_back(n.schedule);
            llvm::SmallVector<CarriedDependencyFlag, 16> oldCarriedDeps =
                carriedDeps;
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
                if (llvm::Optional<BitSet> depSatN =
                        optimize(g, d + 1, maxDepth))
                    return depSat |= *depSatN;
            }
            // we failed, so reset solved schedules
            std::swap(g.activeEdges, activeEdges);
            std::swap(g.nodeIds, nodeIds);
            auto oldNodeIter = oldSchedules.begin();
            for (auto &&n : g)
                n.schedule = *(oldNodeIter++);
            std::swap(carriedDeps, oldCarriedDeps);
        }
        return depSatLevel;
    }
    /// optimize at depth `d`
    /// receives graph by value, so that it is not invalidated when
    /// recursing
    [[nodiscard]] llvm::Optional<BitSet> optimize(Graph g, size_t d,
                                                  size_t maxDepth) {
        if (d >= maxDepth)
            return BitSet{};
        countAuxParamsAndConstraints(g, d);
        setScheduleMemoryOffsets(g, d);
        // if we fail on this level, break the graph
        BitSet activeEdgesBackup = g.activeEdges;
        if (llvm::Optional<BitSet> depSat = optimizeLevel(g, d)) {
            const size_t numSat = depSat->size();
            if (llvm::Optional<BitSet> depSatNest =
                    optimize(g, d + 1, maxDepth)) {
                if (numSat && depSatNest->size())
                    return optimizeSatDep(
                        std::move(g), d, maxDepth, std::move(*depSat),
                        std::move(*depSatNest), std::move(activeEdgesBackup));
                return *depSat |= *depSatNest;
            }
        }
        return breakGraph(std::move(g), d);
    }
    // returns true on failure
    [[nodiscard]] llvm::Optional<BitSet> optimize() {
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

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const LoopBlock &lblock) {
        os << "\nLoopBlock graph (#nodes = " << lblock.nodes.size() << "):\n";
        for (size_t i = 0; i < lblock.nodes.size(); ++i) {
            const auto &v = lblock.nodes[i];
            os << "v_" << i << ":\nmem =\n";
            for (auto m : v.memory)
                os << *lblock.memory[m]->user << "\n";
            os << "inNeighbors = ";
            for (auto m : v.inNeighbors)
                os << "v_" << m << ", ";
            os << "\noutNeighbors = ";
            for (auto m : v.outNeighbors)
                os << "v_" << m << ", ";
            os << "\n\n";
        }
        // BitSet
        // memNodesWithOutEdges{BitSet::dense(lblock.memory.size())};
        os << "\nLoopBlock Edges (#edges = " << lblock.edges.size() << "):\n";
        for (auto &edge : lblock.edges) {
            os << "\tEdge = " << edge;
            for (size_t inIndex : edge.in->nodeIndex) {
                const Schedule &sin = lblock.nodes[inIndex].schedule;
                os << "Schedule In:\nnodeIndex = " << edge.in->nodeIndex
                   << "; ref = " << edge.in->ref << "\ns.getPhi()"
                   << sin.getPhi()
                   << "\ns.getFusionOmega() = " << sin.getFusionOmega()
                   << "\ns.getOffsetOmega() = " << sin.getOffsetOmega();
            }
            for (size_t outIndex : edge.out->nodeIndex) {
                const Schedule &sout = lblock.nodes[outIndex].schedule;
                os << "\n\nSchedule Out:\nnodeIndex = " << edge.out->nodeIndex
                   << "; ref = " << edge.out->ref << "\ns.getPhi()"
                   << sout.getPhi()
                   << "\ns.getFusionOmega() = " << sout.getFusionOmega()
                   << "\ns.getOffsetOmega() = " << sout.getOffsetOmega();
            }
            llvm::errs() << "\n\n";
        }
        os << "\nLoopBlock schedule (#mem accesses = " << lblock.memory.size()
           << "):\n\n";
        for (auto mem : lblock.memory) {
            os << "Ref = " << mem->ref;
            for (size_t nodeIndex : mem->nodeIndex) {
                const Schedule &s = lblock.nodes[nodeIndex].schedule;
                os << "\nnodeIndex = " << nodeIndex << "\ns.getPhi()"
                   << s.getPhi()
                   << "\ns.getFusionOmega() = " << s.getFusionOmega()
                   << "\ns.getOffsetOmega() = " << s.getOffsetOmega() << "\n";
            }
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
static_assert(std::ranges::range<LoopBlock::Graph>);
static_assert(Graphs::AbstractGraph<LoopBlock::Graph>);
