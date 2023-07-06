#pragma once

// #include "./ControlFlowMerging.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "LinearProgramming/LoopBlock.hpp"
#include "LinearProgramming/ScheduledNode.hpp"
#include "Polyhedra/Dependence.hpp"
#include "Polyhedra/Schedule.hpp"
#include <Math/Array.hpp>
#include <Math/Math.hpp>
#include <Utilities/Allocators.hpp>
#include <algorithm>
#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <string_view>
#include <utility>

namespace poly::CostModeling {

class CPURegisterFile {
  [[no_unique_address]] uint8_t maximumVectorWidth;
  [[no_unique_address]] uint8_t numVectorRegisters;
  [[no_unique_address]] uint8_t numGeneralPurposeRegisters;
  [[no_unique_address]] uint8_t numPredicateRegisters;

  // hacky check for has AVX512
  static inline auto hasAVX512(llvm::LLVMContext &C,
                               const llvm::TargetTransformInfo &TTI) -> bool {
    return TTI.isLegalMaskedExpandLoad(
      llvm::FixedVectorType::get(llvm::Type::getDoubleTy(C), 8));
  }

  static auto estimateNumPredicateRegisters(
    llvm::LLVMContext &C, const llvm::TargetTransformInfo &TTI) -> uint8_t {
    if (TTI.supportsScalableVectors()) return 8;
    // hacky check for AVX512
    if (hasAVX512(C, TTI)) return 7; // 7, because k0 is reserved for unmasked
    return 0;
  }
  // returns vector width in bits, ignoring mprefer-vector-width
  static auto estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                         const llvm::TargetTransformInfo &TTI)
    -> uint8_t {
    uint8_t twiceMaxVectorWidth = 2;
    auto *f32 = llvm::Type::getFloatTy(C);
    llvm::InstructionCost prevCost = TTI.getArithmeticInstrCost(
      llvm::Instruction::FAdd,
      llvm::FixedVectorType::get(f32, twiceMaxVectorWidth));
    while (true) {
      llvm::InstructionCost nextCost = TTI.getArithmeticInstrCost(
        llvm::Instruction::FAdd,
        llvm::FixedVectorType::get(f32, twiceMaxVectorWidth *= 2));
      if (nextCost > prevCost) break;
      prevCost = nextCost;
    }
    return 16 * twiceMaxVectorWidth;
  }

public:
  CPURegisterFile(llvm::LLVMContext &C, const llvm::TargetTransformInfo &TTI) {
    maximumVectorWidth = estimateMaximumVectorWidth(C, TTI);
    numVectorRegisters = TTI.getNumberOfRegisters(true);
    numGeneralPurposeRegisters = TTI.getNumberOfRegisters(false);
    numPredicateRegisters = estimateNumPredicateRegisters(C, TTI);
  }
  constexpr auto getNumVectorBits() const -> uint8_t {
    return maximumVectorWidth;
  }
  constexpr auto getNumVector() const -> uint8_t { return numVectorRegisters; }
  constexpr auto getNumScalar() const -> uint8_t {
    return numGeneralPurposeRegisters;
  }
  constexpr auto getNumPredicate() const -> uint8_t {
    return numPredicateRegisters;
  }
};
// struct CPUExecutionModel {};

// Plan for cost modeling:
// 1. Build Instruction graph
// 2. Iterate over all PredicatedChains, merging instructions across branches
// where possible
// 3. Create a loop tree structure for optimization
// 4. Create InstructionBlocks at each level.

// void pushBlock(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
//                llvm::SmallPtrSet<llvm::BasicBlock *, 32> &chainBBs,
//                Predicates &pred, llvm::BasicBlock *BB) {
//     assert(chainBBs.contains(block));
//     chainBBs.erase(BB);
//     // we only want to extract relevant instructions, i.e. parents of
//     stores for (llvm::Instruction &instr : *BB) {
//         if (trackInstr.contains(&instr))
//             instructions.emplace_back(pred, instr);
//     }
//     llvm::Instruction *term = BB->getTerminator();
//     if (!term)
//         return;
//     switch (term->getNumSuccessors()) {
//     case 0:
//         return;
//     case 1:
//         BB = term->getSuccessor(0);
//         if (chainBBs.contains(BB))
//             pushBlock(trackInstr, chainBBs, pred, BB);
//         return;
//     case 2:
//         break;
//     default:
//         assert(false);
//     }
//     auto succ0 = term->getSuccessor(0);
//     auto succ1 = term->getSuccessor(1);
//     if (chainBBs.contains(succ0) && chainBBs.contains(succ1)) {
//         // TODO: we need to fuse these blocks.

//     } else if (chainBBs.contains(succ0)) {
//         pushBlock(trackInstr, chainBBs, pred, succ0);
//     } else if (chainBBs.contains(succ1)) {
//         pushBlock(trackInstr, chainBBs, pred, succ1);
//     }
// }
template <typename T> using Vec = math::ResizeableView<T, unsigned>;

// TODO: instead of this, update in-place and ensure all Addr are over-allocated
// to correspond with max depth?
// Because we parse in reverse order, we have max possible depth of
// `ScheduledNode`s using it at time we create.

/// LoopTree
/// A tree of loops, with an indexable vector of IR::Loop*s, to facilitate
/// construction of the IR::Loop graph, from the fusion omegas
class LoopTree {
  // The root of this subtree
  NotNull<IR::Loop> loop;
  LoopTree *parent{nullptr}; // do we need this?
  Vec<LoopTree *> children{};
  unsigned depth{0};
  // We do not need to know the previous loop, as dependencies between
  // the `Addr`s and instructions will determine the ordering.
  constexpr LoopTree(Arena<> *lalloc, LoopTree *parent_)
    : loop{lalloc->create<IR::Loop>(parent_->depth + 1)}, parent(parent_),
      depth(parent_->depth + 1) {
    // allocate the root node, and connect it to parent's node, as well as
    // previous loop of the same level.
    loop->setParent(parent_->loop);
  }
  constexpr LoopTree(Arena<> *lalloc) : loop{lalloc->create<IR::Loop>(0)} {}

public:
  static auto root(Arena<> *salloc, Arena<> *lalloc) -> LoopTree * {
    return new (salloc) LoopTree(lalloc);
  }
  // salloc: Short lived allocator, for the indexable `Vec`s
  // Longer lived allocator, for the IR::Loop nodes
  void addNode(Arena<> *salloc, Arena<> *lalloc, lp::ScheduledNode *node) {
    if (node->getNumLoops() == depth) {
      // Then it belongs here, and we add loop's dependencies.
      // We only need to add deps to support SCC/top sort now.
      // We also apply the rotation here.
      // For dependencies in SCC iteration, only indvar deps get iterated.
      auto [Pinv, denom] = math::NormalForm::scaledInv(node->getPhi());
      NotNull<poly::Loop> affloop =
        node->getLoopNest()->rotate(lalloc, Pinv, node->getOffset());
      for (IR::Addr *m : node->localAddr()) {
        m->rotate(affloop, Pinv, denom, node->getOffsetOmega(),
                  node->getOffset());
        loop->insertAfter(m);
      }
      return;
    }
    // we need to find the sub-loop tree to which we add `node`
    ptrdiff_t idx = node->getFusionOmega(depth);
    invariant(idx >= 0);
    ptrdiff_t numChildren = children.size();
    if (idx >= children.size()) {
      if (idx >= children.getCapacity()) {
        // allocate extra capacity
        children.reserve(salloc, 2 * (idx + 1));
      }
      // allocate new nodes and resize
      children.resize(idx + 1);
      for (ptrdiff_t i = numChildren; i < idx + 1; ++i)
        children[i] = new (salloc) LoopTree{lalloc, this};
      numChildren = idx + 1;
    }
    children[idx]->addNode(salloc, lalloc, node);
  }
  constexpr auto getChildren() -> Vec<LoopTree *> { return children; }
  constexpr auto getLoop() -> IR::Loop * { return loop; }
};

struct LoopDepSummary {
  IR::Node *afterExit{nullptr};
  IR::Addr *indexedByLoop{nullptr};
  IR::Addr *notIndexedByLoop{nullptr};
};
struct LoopIndependent {
  LoopDepSummary summary;
  bool independent;
  constexpr auto operator*=(LoopIndependent other) -> LoopIndependent & {
    summary = other.summary;
    independent = independent && other.independent;
    return *this;
  }
};
//

// searches `N` and it's users for loop-independent users
// this exits early if it finds a dependent user; we search everything
// anyway, so we'll revist later anyway.
// We return a `IR::Node *, bool` pair, where the `bool` is true if
// `N` was loop independent.
// We do this rather than something like returning a `nullptr`, as
// we may have descended into instructions, found some users that are
// but then also found some that are not; we need to return `false`
// in this case, but we of course want to still return those we found.
inline auto searchLoopIndependentUsers(IR::Loop *L, IR::Node *N, uint8_t depth,
                                       LoopDepSummary summary)
  -> LoopIndependent {
  if (N->dependsOnParentLoop()) return {summary, false};
  if (llvm::isa<IR::Loop>(N)) return {summary, false};
  if (IR::Loop *P = N->getLoop(); P != L)
    return {summary, !(P && L->contains(P))};
  LoopIndependent ret{summary, true};
  IR::Addr *a = llvm::dyn_cast<IR::Addr>(N);
  if (a) {
    a->removeFromList();
    if (a->indexedByInnermostLoop()) {
      a->insertAfter(ret.summary.indexedByLoop);
      ret.summary.indexedByLoop = a;
      return {summary, false};
    } else {
      a->insertAfter(ret.summary.notIndexedByLoop);
      ret.summary.notIndexedByLoop = a;
    }
    for (IR::Addr *m : a->outputAddrs(depth)) {
      ret *= searchLoopIndependentUsers(L, m, depth, summary);
      if (ret.independent) continue;
      a->setDependsOnParentLoop();
      return ret;
    }
  }
  // if it isn't a Loop, must be an `Instruction`
  IR::Value *I = llvm::cast<IR::Instruction>(N);
  for (IR::Node *U : I->getUsers()) {
    ret *= searchLoopIndependentUsers(L, U, depth, summary);
    if (ret.independent) continue;
    I->setDependsOnParentLoop();
    return ret;
  }
  // then we can push it to the front of the list
  if (a && ret.summary.notIndexedByLoop == a)
    ret.summary.notIndexedByLoop = llvm::cast_or_null<IR::Addr>(a->getNext());
  I->removeFromList();
  I->insertAfter(ret.summary.afterExit);
  ret.summary.afterExit = I;
  I->visit(depth);
  return ret;
}
inline auto visitLoopDependent(IR::Loop *L, IR::Node *N, uint8_t depth,
                               IR::Node *body) -> IR::Node * {
  invariant(N->getVisitDepth() != 254);
  // N may have been visited as a dependent of an inner loop, which is why
  // `visited` accepts a depth argument
  if (N->wasVisited(depth) || !(L->contains(N))) return body;
#ifndef NDEBUG
  // Our goal here is to check for cycles in debug mode.
  // Each level of our graph is acyclic, meaning that there are no cycles at
  // that level when traversing only edges active at that given level. However,
  // when considering edges active at level `I`, we may have cycles at level `J`
  // if `J>I`. In otherwords, here we are travering all edges active at
  // `I=depth`. Within subloops, which necessarilly have depth `J>I`, we may
  // have cycles.
  //
  // Thus, we need to prevent getting stuck in a cycle for these deeper loops by
  // setting `N->visit(depth)` here, so `wasVisited` will allow them to
  // immediately return. But, in debug mode, we'll set nodes of the same depth
  // to `254` to check for cycles.
  if (N->getLoop() == L) N->visit(254);
  else N->visit(depth);
#else
  N->visit(depth);
#endif
  // iterate over users
  if (IR::Addr *A = llvm::dyn_cast<IR::Addr>(N)) {
    for (IR::Addr *m : A->outputAddrs(depth)) {
      if (m->wasVisited(depth)) continue;
      body = visitLoopDependent(L, m, depth, body);
    }
  }
  if (IR::Instruction *I = llvm::cast<IR::Instruction>(N)) {
    for (IR::Node *U : I->getUsers()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(L, U, depth, body);
    }
  } else if (IR::Loop *S = llvm::cast<IR::Loop>(N)) {
    for (IR::Node *U : S->getChild()->nodes()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(L, U, depth, body);
    }
  }
#ifndef NDEBUG
  if (N->getLoop() == L) N->visit(depth);
#endif
  if (N->getLoop() == L) body = N->setNext(body);
  return body;
}
inline auto topologicalSort(IR::Loop *root, unsigned depth) -> IR::Node * {
  // basic plan for the top sort:
  // We iterate across all users, once all of node's users have been added,
  // we push it to the front of the list. Thus, we get a top-sorted list.
  // We're careful about the order, so that this top sort should LICM all the
  // addresses that it can.
  //
  // We must push the exit before the root (as the exit depends on the loop, and
  // we iterate users).
  // The exit doesn't use any in this block, so we begin by trying to push any
  // instructions that don't depend on the loop. If we fail to push them (i.e.,
  // because they have uses that do depend on the loop), then they get added to
  // a revisit queue. Any instructions we are able to push-front before we push
  // the exit, implicitly happen after the exit, i.e. they have been LICMed into
  // the exit block. We unvisit the revisit-queue, and add them back to the main
  // worklist. Then, we proceed with a depth-first topological sort normally
  // (iterating over uses, pushing to the front), starting with the loop root,
  // so that it gets pushed to the front as soon as possible. That is, so that
  // it happens as late as possible Any instructions that get pushed to the
  // front afterwards have been LICMed into the loop pre-header.
  //
  // In this first pass, we iterate over all nodes, pushing those
  // that can be hoisted after the exit block.
  IR::Node *C = root->getChild();
  LoopDepSummary summary;
  for (IR::Node *N : C->nodes())
    summary = searchLoopIndependentUsers(root, N, depth, summary).summary;
  // summary.afterExit will be hoisted out; every member has been marked as
  // `visited` So, now we search all of root's users, i.e. every addr that
  // depends on it
  IR::Node *body;
  for (IR::Node *N : summary.indexedByLoop->nodes())
    body = visitLoopDependent(root, N, depth, body);
  body = root->setNext(body); // now we can place the loop
  for (IR::Node *N : summary.notIndexedByLoop->nodes())
    body = visitLoopDependent(root, N, depth, body);
  // and any remaining edges
  return body;
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto buildGraph(IR::Loop *root, unsigned depth) -> IR::Node * {
  // We build the instruction graph, via traversing the tree, and then
  // top sorting as we recurse out
  for (IR::Loop *child : root->subLoops()) buildGraph(child, depth + 1);
  return topologicalSort(root, depth);
}

inline auto addAddrToGraph(Arena<> *salloc, Arena<> *lalloc,
                           lp::ScheduledNode *nodes) -> IR::Loop * {
  auto s = salloc->scope();
  LoopTree *root = LoopTree::root(salloc, lalloc);
  for (lp::ScheduledNode *node : nodes->getAllVertices())
    root->addNode(salloc, lalloc, node);
  return root->getLoop();
}
inline void removeRedundantAddr(IR::Addr *addr) {
  for (IR::Addr *a : addr->eachAddr()) {
    for (IR::Addr *b : a->outputAddrs()) {
      if (a->indexMatrix() != b->indexMatrix()) continue;
      /// are there any addr between them?
      if (a->isStore()) {
        if (b->isStore()) { // Write->Write
          // Are there reads in between? If so, we must keep--
          // --unless we're storing the same value twice (???)
          // without other intervening store-edges.
          // Without reads in between, it's safe.
        } else { // Write->Read
          // Can we replace the read with using the written value?
          if (a->getLoop() != b->getLoop()) continue;
        }
      } else if (b->isLoad()) { // Read->Read
        // If they don't have the same parent, either...
        // They're in different branches of loops, and load can't live
        // in between them
        // for (i : I){
        //   for (j : J){
        //     A[i,j];
        //   }
        //   for (j : J){
        //     A[i,j];
        //   }
        // }
        // or it is a subloop, but dependencies prevented us from hoisting.
        if (a->getLoop() != b->getLoop()) continue;
        // Any writes in between them?
      } // Read->Write, can't delete either
    }
  }
}
/// Optimize the schedule
inline void optimize(IR::Cache &instr, Arena<> *lalloc,
                     lp::ScheduledNode *nodes, IR::Addr *addr) {
  /// we must build the IR::Loop
  /// Initially, to help, we use a nested vector, so that we can index into it
  /// using the fusion omegas. We allocate it with the longer lived `instr`
  /// alloc, so we can checkpoint it here, and use alloc for other IR nodes.
  Arena<> *salloc = instr.getAllocator();

  IR::Node *N = buildGraph(addAddrToGraph(salloc, lalloc, nodes), 0);
  // `N` is the head of the topologically sorted graph
  // We now try to remove redundant memory operations
  removeRedundantAddr(addr);
}

/// How should the IR look?
/// We could have a flat IR, which may be useful for things like loop placement
/// via SCC Alternatively, we could have a much more structured IR, where we
/// have the loop blocks and loops. Then how do we do SCC? Add dummies? Or, can
/// loops exist as IR components? What do we need to do with the IR?
/// 1. Placement, e.g. the SCC
/// 2. lifetime as a function of loops.
/// Could we do something with the LoopAndExit, where both are represented?
/// exit's parents are all loop members, loop's the prev loop?
/// Seems like it could be straightforward.
///
/// Perhaps define:
/// using vertex_t = std::variant<Address*,LoopStart,LoopEnd>
///
/// Perhaps, for now...focus on InstructionnBlock, where we do want an
/// Instruction linked list.
///
///
/// For register consumption -- we want to know last use676767
/// Can the Instruction's children

/// Given: llvm::SmallVector<LoopAndExit> subTrees;
/// subTrees[i].second is the preheader for
/// subTrees[i+1].first, which has exit block
/// subTrees[i+1].second

/// Initialized from a LoopBlock
/// First, all memory accesses are placed.
///  - Topologically sort at the same level
///  - Hoist out as far as posible
/// Then, merge eligible loads.
///  - I.e., merge loads that are in the same block with same address and not
///  aliasing stores in between
/// Finally, place instructions, seeded by stores, hoisted as far out as
/// possible. With this, we can begin cost modeling.
class LoopTreeSchedule {
  template <typename T> using Vec = math::ResizeableView<T, unsigned>;
  using BitSet = MemoryAccess::BitSet;

public:
  struct AddressGraph {
    using VertexType = Addr;
    [[no_unique_address]] MutPtrVector<Addr *> addresses;
    // we restrict the SCC order to preserve relative ordering of the sub loops
    // by adding loopNestAddrs. These contain arrays of [start, stop].
    // Each start has the preceding end as an input.
    // Each stop has the following start as an output.
    // Each start also has the enclosed mem accesses as outputs,
    // and each stop has the enclosed mem accesses as inputs (enclosed means the
    // memory accesses of the corresponding loop).
    [[no_unique_address]] MutPtrVector<std::array<BitSet, 2>> loopNestAddrs;
    [[nodiscard]] constexpr auto maxVertexId() const -> unsigned {
      return addresses.size() + 2 * loopNestAddrs.size();
    }
    [[nodiscard]] constexpr auto vertexIds() const -> Range<size_t, size_t> {
      return _(0, maxVertexId());
    }
    [[nodiscard]] auto getNumVertices() const -> size_t {
      return addresses.size();
    }
    // Graph index ordering: LoopEnds, ArrayRefs, LoopStarts
    // so that we have ends as early as possible, and starts a late
    [[nodiscard]] constexpr auto inNeighbors(size_t i) const -> BitSet {
      if (i < loopNestAddrs.size()) return loopNestAddrs[i][1]; // loop end
      i -= loopNestAddrs.size();
      if (i < addresses.size()) return addresses[i]->getParents();
      return loopNestAddrs[i - addresses.size()][0]; // loop start
    }
    [[nodiscard]] auto wasVisited(size_t i) const -> bool {
      return addresses[i]->wasVisited();
    }
    void visit(size_t i) { addresses[i]->visit(); }
    void unVisit(size_t i) { addresses[i]->unVisit(); }
    void clearVisited() {
      for (auto *a : addresses) a->unVisit();
    }
  };

private:
  template <typename T>
  static constexpr auto realloc(Arena<> *alloc, Vec<T> vec, unsigned nc)
    -> Vec<T> {
    return Vec<T>(alloc.reallocate<false>(vec.data(), vec.getCapacity(), nc),
                  vec.size(), nc);
  }
  template <typename T>
  static constexpr auto grow(Arena<> *alloc, Vec<T> vec, unsigned sz)
    -> Vec<T> {
    if (unsigned C = vec.getCapacity(); C < sz) {
      T *p = alloc.allocate<T>(sz + sz);
      for (unsigned i = 0; i < vec.size(); ++i) p[i] = std::move(vec[i]);
      return Vec<T>(p, sz, sz + sz);
    }
    vec.resizeForOverwrite(sz);
    return vec;
  }

  class InstructionBlock {
    Addr **addresses{nullptr};
    unsigned numAddr{0};
    unsigned capacity{0};

  public:
    [[nodiscard]] constexpr auto isInitialized() const -> bool {
      return addresses != nullptr;
    }
    [[nodiscard]] constexpr auto getAddr() -> PtrVector<Addr *> {
      return {addresses, numAddr, capacity};
    }
    [[nodiscard]] constexpr auto size() const -> unsigned { return numAddr; }
    [[nodiscard]] constexpr auto getCapacity() const -> unsigned {
      return capacity;
    }
    [[nodiscard]] constexpr auto operator[](unsigned i) const -> Addr * {
      invariant(i < numAddr);
      invariant(i < capacity);
      return addresses[i];
    }
    /// add space for `i` extra slots
    constexpr void reserveExtra(Arena<> *alloc, unsigned i) {
      unsigned oldCapacity = std::exchange(capacity, capacity + i);
      addresses = alloc.reallocate<false>(addresses, oldCapacity, capacity);
    }
    constexpr void initialize(Arena<> *alloc) {
      addresses = alloc.allocate<Addr *>(capacity);
    }
    constexpr void push_back(Addr *addr) {
      invariant(numAddr < capacity);
      addresses[numAddr++] = addr;
    }
    constexpr void push_back(Arena<> *alloc, Addr *addr) {
      if (numAddr >= capacity)
        reserveExtra(alloc, std::max<unsigned>(4, numAddr));
      addresses[numAddr++] = addr;
    }
    constexpr void push_front(Addr *addr) {
      invariant(numAddr < capacity);
      for (size_t i = 0; i < numAddr; ++i) {
        Addr *tmp = addresses[i];
        addresses[i] = addr;
        addr = tmp;
      }
      addresses[numAddr++] = addr;
    }
    constexpr void push_front(Arena<> *alloc, Addr *addr) {
      if (numAddr >= capacity)
        reserveExtra(alloc, std::max<unsigned>(4, numAddr));
      push_front(addr);
    }
    constexpr void incNumAddr(unsigned x) { capacity += x; }
    [[nodiscard]] constexpr auto try_delete(Addr *adr) -> bool {
      for (size_t i = 0; i < numAddr; ++i) {
        if (addresses[i] == adr) {
          addresses[i] = addresses[--numAddr];
          return true;
        }
      }
      return false;
    }
    constexpr void clear() { numAddr = 0; }
    auto printDotNodes(llvm::raw_ostream &os, size_t i,
                       llvm::SmallVectorImpl<std::string> &addrNames,
                       unsigned addrIndOffset, const std::string &parentLoop)
      -> size_t {
      for (auto *addr : getAddr()) {
        std::string f("f" + std::to_string(++i)), addrName = "\"";
        addrName += parentLoop;
        addrName += "\":";
        addrName += f;
        unsigned ind = addr->index() -= addrIndOffset;
        addrNames[ind] = addrName;
        addr->printDotName(os << "<tr><td port=\"" << f << "\"> ");

        os << "</td></tr>\n";
      }
      return i;
    }

    void printDotEdges(llvm::raw_ostream &os,
                       llvm::ArrayRef<std::string> addrNames) {
      for (auto *addr : getAddr()) {
        auto outN{addr->outNeighbors()};
        auto depS{addr->outDepSat()};
        for (size_t n = 0; n < outN.size(); ++n) {
          os << addrNames[addr->index()] << " -> "
             << addrNames[outN[n]->index()];
          if (depS[n] == 255) os << " [color=\"#00ff00\"];\n";
          else if (depS[n] == 127) os << " [color=\"#008080\"];\n";
          else
            os << " [label = \"dep_sat=" << unsigned(depS[n])
               << "\", color=\"#0000ff\"];\n";
        }
      }
    }
  };

  struct LoopAndExit {
    [[no_unique_address]] LoopTreeSchedule *subTree;
    [[no_unique_address]] InstructionBlock exit{};
    constexpr LoopAndExit(LoopTreeSchedule *tree) : subTree(tree) {}
    static constexpr auto construct(Arena<> *alloc, LoopTreeSchedule *L,
                                    uint8_t d, uint8_t blockIdx) {
      return LoopAndExit(alloc.create<LoopTreeSchedule>(L, d, blockIdx));
    }
  };
  /// Header of the loop.
  [[no_unique_address]] InstructionBlock header{};
  /// Variable number of sub loops and their associated exits.
  /// For the inner most loop, `subTrees.empty()`.
  [[no_unique_address]] Vec<LoopAndExit> subTrees{};
  [[no_unique_address]] LoopTreeSchedule *parent{nullptr};
  [[no_unique_address]] uint8_t depth;
  [[no_unique_address]] uint8_t numAddr_;
  [[no_unique_address]] uint8_t blckIdx{0};
  // notused yet
  /*
  [[no_unique_address]] uint8_t vectorizationFactor{1};
  [[no_unique_address]] uint8_t unrollFactor{1};
  [[no_unique_address]] uint8_t unrollPredcedence{1};
  */
  // i = 0 means self, i = 1 (default) returns parent, i = 2 parent's
  // parent...
  constexpr auto try_delete(Addr *adr) -> bool {
    return (adr->getLoopTreeSchedule() == this) &&
           getBlock(adr->getBlockIdx()).try_delete(adr);
  }
  // get index of block within parent
  [[nodiscard]] constexpr auto getBlockIdx() const -> unsigned {
    return blckIdx;
  }
  constexpr auto getParent(size_t i) -> LoopTreeSchedule * {
    invariant(i <= depth);
    LoopTreeSchedule *p = this;
    while (i--) p = p->parent;
    return p;
  }
  constexpr void incNumAddr(unsigned x) { header.incNumAddr(x); }
  constexpr auto getParent() -> LoopTreeSchedule * { return parent; }
  [[nodiscard]] constexpr auto getNumSubTrees() const -> unsigned {
    return subTrees.size();
  }
  [[nodiscard]] constexpr auto numBlocks() const -> unsigned {
    return getNumSubTrees() + 1;
  }
  constexpr auto getLoopAndExit(Arena<> *alloc, size_t i, size_t d)
    -> LoopAndExit & {
    if (size_t J = subTrees.size(); i >= J) {
      subTrees = grow(alloc, subTrees, i + 1);
      for (size_t j = J; j <= i; ++j)
        subTrees[j] = LoopAndExit::construct(alloc, this, d, j);
    }
    return subTrees[i];
  }
  auto getLoop(Arena<> *alloc, size_t i, uint8_t d) -> LoopTreeSchedule * {
    return getLoopAndExit(alloc, i, d).subTree;
  }
  auto getLoop(size_t i) -> LoopTreeSchedule * { return subTrees[i].subTree; }
  [[nodiscard]] auto getLoop(size_t i) const -> const LoopTreeSchedule * {
    return subTrees[i].subTree;
  }
  auto getBlock(size_t i) -> InstructionBlock & {
    if (i) return subTrees[i - 1].exit;
    return header;
  }
  [[nodiscard]] auto getBlock(size_t i) const -> const InstructionBlock & {
    if (i) return subTrees[i - 1].exit;
    return header;
  }
  /// Adds the schedule corresponding for the innermost loop.

  // this method descends
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto allocLoopNodes(Arena<> *alloc, AffineSchedule sch,
                             LoopTreeSchedule *L) -> LoopTreeSchedule * {
    auto fO = sch.getFusionOmega();
    unsigned numLoops = sch.getNumLoops();
    invariant(fO.size() - 1, numLoops);
    for (size_t i = 0; i < numLoops; ++i) L = L->getLoop(alloc, fO[i], i + 1);
    return L;
  }
  static constexpr void
  calcLoopNestAddrs(Vector<std::array<BitSet, 2>> &loopRelatives,
                    MutPtrVector<Addr *> loopAddrs, unsigned numAddr,
                    unsigned idx) {
    // TODO: only add dependencies for refs actually using the indvars
    BitSet headParents{}, exitParents{};
    unsigned indEnd = idx, indStart = loopRelatives.size() + indEnd + numAddr;
    // not first loop, connect headParents
    if (idx) headParents.insert(indStart - 1);
    bool empty = true;
    for (auto *a : loopAddrs) {
      // if (!a->dependsOnIndVars(depth)) continue;
      exitParents.insert(a->index());
      a->addParent(indStart);
      empty = false;
    }
    if (empty) exitParents.insert(indStart);
    loopRelatives[idx] = {headParents, exitParents};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto getLoopIdx(LoopTreeSchedule *L) const -> unsigned {
    LoopTreeSchedule *P = L->getParent();
    if (P == this) return L->getBlockIdx();
    return getLoopIdx(P);
  }
  constexpr auto getIdx(Addr *a) const -> unsigned {
    if (a->getLoopTreeSchedule() == this) return 2 * a->getBlockIdx();
    return 2 * getLoopIdx(a->getLoopTreeSchedule()) + 1;
  }
  void push_back(Arena<> *alloc, Addr *a, unsigned idx) {
    if (idx) subTrees[idx - 1].exit.push_back(alloc, a);
    else header.push_back(alloc, a);
  }
  void push_front(Arena<> *alloc, Addr *a, unsigned idx) {
    if (idx) subTrees[idx - 1].exit.push_front(alloc, a);
    else header.push_front(alloc, a);
  }
  // can we hoist forward out of this loop?
  // NOLINTNEXTLINE(misc-no-recursion)
  auto hoist(Arena<> *alloc, Addr *a, unsigned currentLoop)
    -> Optional<unsigned> {
    if (a->wasVisited3()) {
      if (a->getLoopTreeSchedule() == this) return a->getBlockIdx();
      return {};
    }
    a->visit3();
    // it isn't allowed to depend on deeper loops
    auto *L = a->getLoopTreeSchedule();
    bool placed = L != this;
    if ((placed && (L->getParent() != this || a->dependsOnIndVars(getDepth()))))
      return {};
    // we're trying to hoist into either idxFront or idxBack
    unsigned idxFront = 2 * currentLoop, idxBack = idxFront + 2;
    bool legalHoistFront = true, legalHoistBack = true;
    for (auto *p : a->inNeighbors(getDepth())) {
      auto pIdx = getIdx(p);
      if (pIdx <= idxFront) continue;
      bool fail = pIdx >= idxBack;
      if (!fail) {
        if (auto hIdx = hoist(alloc, p, currentLoop))
          fail = *hIdx > currentLoop;
        else fail = true;
      }
      if (fail) {
        legalHoistFront = false;
        break;
      }
    }
    if (legalHoistFront) {
      if (placed) {
        invariant(L->try_delete(a));
        a->setLoopTreeSchedule(this);
      }
      a->setBlockIdx(currentLoop);
      push_back(alloc, a, currentLoop);
      a->place();
      return currentLoop;
    }
    for (auto *c : a->outNeighbors(getDepth())) {
      auto cIdx = getIdx(c);
      if (cIdx >= idxBack) continue;
      bool fail = cIdx <= idxFront;
      if (!fail) {
        if (auto hIdx = hoist(alloc, c, currentLoop))
          fail = *hIdx <= currentLoop;
        else fail = true;
      }
      if (fail) {
        legalHoistBack = false;
        break;
      }
    }
    if (legalHoistBack) {
      if (placed) {
        invariant(L->try_delete(a));
        a->setLoopTreeSchedule(this);
      }
      a->setBlockIdx(currentLoop + 1);
      invariant(currentLoop < currentLoop + 1);
      push_front(alloc, a, currentLoop + 1);
      a->place();
      return currentLoop + 1;
    }
    // FIXME: what if !placed, but hoist failed?
    // what sort of ctrl path could produce that outcome?
    invariant(placed);
    return {};
  }
  // Two possible plans:
  // 1.
  // go from a ptr to an index-based approach
  // we want the efficient set operations that `BitSet` provides
  // The plan will be to add `addr`s from previous and following loops
  // as extra edges, so that we can ensure the SCC algorithm does
  // not mix addrs from different loops.
  // 2.
  // a) every addr has an index field
  // b) we create BitSets of ancestors
  // NOLINTNEXTLINE(misc-no-recursion)
  auto placeAddr(Arena<> *alloc, lp::LoopBlock &LB, MutPtrVector<Addr *> addr)
    -> unsigned {
    // we sort via repeatedly calculating the strongly connected components
    // of the address graph. The SCCs are in topological order.
    // If a load or store are isolated within a SCC from a sub-loop, we hoist
    // if it's indices do not depend on that loop.
    //
    // We will eventually insert all addr at this level and within sub-loops
    // into `addr`. We process them in batches, iterating based on `omega`
    // values. We only need to force separation here for those that are not
    // already separated.
    //
    unsigned numAddr = header.size(), numSubTrees = subTrees.size();
    addr[_(0, numAddr)] << header.getAddr();
#ifndef NDEBUG
    for (auto *a : addr[_(0, numAddr)]) invariant(!a->wasPlaced());
#endif
    header.clear();
    Vector<uint8_t> addrCounts;
    addrCounts.reserve(numSubTrees + 1);
    addrCounts.emplace_back(numAddr);
    for (auto &L : subTrees)
      addrCounts.emplace_back(
        numAddr += L.subTree->placeAddr(alloc, LB, addr[_(numAddr, end)]));

    addr = addr[_(0, numAddr)];
    numAddr_ = numAddr;
    for (unsigned i = 0; i < numAddr; ++i) {
      addr[i]->addToSubset();
      addr[i]->index() = i + numSubTrees;
    }
    for (auto *a : addr) a->calcAncestors(getDepth());
#ifndef NDEBUG
    for (auto *a : addr[_(addrCounts.front(), addrCounts.back())])
      invariant(a->wasPlaced());
#endif
    Vector<std::array<BitSet, 2>> loopRelatives{numSubTrees};
    if (numSubTrees) {
      // iterate over loops
      for (unsigned i = 0; i < subTrees.size(); ++i) {
        calcLoopNestAddrs(
          loopRelatives, addr[_(addrCounts[i], addrCounts[i + 1])], numAddr, i);
      }
    }
    // auto headerAddrs = addr[_(0, addrCounts[0])];
    llvm::SmallVector<BitSet> components;
    {
      AddressGraph g{addr, loopRelatives};
      Graphs::stronglyConnectedComponents(components, g);
    }
    size_t currentLoop = 0;
    InstructionBlock *B = &header;
    LoopTreeSchedule *L;
    bool inLoop = false;
    // two passes, first to place the addrs that don't need hoisting
    for (BitSet scc : components) {
      size_t sccsz = scc.size();
      if (sccsz == 1) {
        size_t indRaw = scc.front(), ind = indRaw - numSubTrees;
        if (ind < numAddr) {
          // four possibilities:
          // 1. inLoop && wasPlaced
          // 2. inLoop && !wasPlaced
          // 3. !inLoop && !wasPlaced
          // 4. !inLoop && wasPlaced - scc hoisted
          if (inLoop) continue;
          Addr *a = addr[ind];
          invariant(!a->wasPlaced());
          // if (a->wasPlaced()) {
          //   // scc's top sort decided we can hoist
          //   invariant(a->getLoopTreeSchedule()->try_delete(a));
          //   a->setLoopTreeSchedule(this);
          // }
          a->setBlockIdx(currentLoop);
          B->push_back(alloc, a);
          a->place();
          a->visit3();
        } else {
          // invariant((ind - numAddr) & 1, size_t(inLoop));
          // invariant(currentLoop, (ind - numAddr) >> 1);
          if (inLoop) B = &subTrees[currentLoop++].exit;
          else L = subTrees[currentLoop].subTree;
          inLoop = !inLoop;
        }
      } else {
        invariant(inLoop);
#ifndef NDEBUG
        for (size_t i : scc) assert(addr[i - numSubTrees]->wasPlaced());
#endif
      }
    }
    invariant(!inLoop);
    currentLoop = 0;
    B = &header;
    for (BitSet scc : components) {
      size_t sccsz = scc.size();
      if (sccsz == 1) {
        size_t indRaw = scc.front(), ind = indRaw - numSubTrees;
        if (ind < numAddr) {
          Addr *a = addr[ind];
          // four possibilities:
          // 1. inLoop && wasPlaced
          // 2. inLoop && !wasPlaced
          // 3. !inLoop && !wasPlaced
          // 4. !inLoop && wasPlaced - scc hoisted
          if (!inLoop) continue;
          // header: B
          // exit: subTrees[currentLoop].exit;
          // Here, we want a recursive approach
          // check children and check parents for hoistability
          // if all parents are hoistable in front, it can be hoisted in
          // front ditto if all children are hoistable behind we can reset
          // visited before each search
          if (!a->wasPlaced() || ((a->getLoopTreeSchedule() == L) &&
                                  !a->dependsOnIndVars(getDepth()))) {
            // hoist; do other loop members depend, or are depended on?
            invariant(hoist(alloc, a, currentLoop).hasValue() ||
                      a->wasPlaced());
          }
          a->place();
        } else {
          // invariant((ind - numAddr) & 1, size_t(inLoop));
          // invariant(currentLoop, (ind - numAddr) >> 1);
          if (inLoop) B = &subTrees[currentLoop++].exit;
          else L = subTrees[currentLoop].subTree;
          inLoop = !inLoop;
        }
      } else {
        invariant(inLoop);
#ifndef NDEBUG
        for (size_t i : scc) assert(addr[i - numSubTrees]->wasPlaced());
#endif
      }
    }
    for (auto *a : addr) a->resetBitfield();
    return numAddr;
  }
  template <typename T>
  static constexpr auto get(llvm::SmallVectorImpl<T> &x, size_t i) -> T & {
    if (i >= x.size()) x.resize(i + 1);
    return x[i];
  }
#ifndef NDEBUG
  void validate() { // NOLINT(misc-no-recursion)
    for (auto &subTree : subTrees) {
      assert(subTree.subTree->parent == this);
      assert(subTree.subTree->getDepth() == getDepth() + 1);
      subTree.subTree->validate();
    }
  }
  void validateMemPlacements() {}
#endif
public:
  [[nodiscard]] static auto init(Arena<> *alloc, lp::LoopBlock &LB)
    -> LoopTreeSchedule * {
    // TODO: can we shorten the life span of the instructions we
    // allocate here to `lalloc`? I.e., do we need them to live on after
    // this forest is scheduled?

    // we first add all memory operands
    // then, we licm
    // the only kind of replication that occur are store reloads
    // size_t maxDepth = 0;
    PtrVector<ScheduledNode> lnodes = LB.getNodes();
    Vector<LoopTreeSchedule *> loops{lnodes.size()};
    LoopTreeSchedule *root{alloc.create<LoopTreeSchedule>(nullptr, 0, 0)};
    unsigned numAddr = 0;
    for (size_t i = 0; i < lnodes.size(); ++i) {
      auto &node = lnodes[i];
      LoopTreeSchedule *L = loops[i] =
        allocLoopNodes(alloc, node.getSchedule(), root);
      unsigned numMem = node.getNumMem();
      L->incNumAddr(numMem);
      numAddr += numMem;
    }
#ifndef NDEBUG
    root->validate();
#endif
    for (size_t i = 0; i < lnodes.size(); ++i)
      lnodes[i].insertMem(alloc, LB.getMem(), loops[i]);
#ifndef NDEBUG
    root->validateMemPlacements();
#endif
    // we now have a vector of addrs
    for (auto &edge : LB.getEdges()) {
      // here we add all connections to the addrs
      // edges -> MA -> Addr
      edge.input()->getAddress()->addOut(edge.output()->getAddress(),
                                         edge.satLevel());
    }
    MutPtrVector<Addr *> addr{alloc.allocate<Addr *>(numAddr), numAddr};
    root->placeAddr(alloc, LB, addr);
    return root;
  }
  // void initializeInstrGraph(Arena<> *alloc, Instruction::Cache &cache,
  //                           Arena<> *tAlloc, LoopTree *loopForest,
  //                           lp::LoopBlock &LB,
  //                           llvm::TargetTransformInfo &TTI,
  //                           unsigned int vectorBits) {

  //   // buidInstructionGraph(alloc, cache);
  //   mergeInstructions(alloc, cache, loopForest, TTI, tAlloc, vectorBits);
  // }

  [[nodiscard]] constexpr auto getInitAddr(Arena<> *alloc)
    -> InstructionBlock & {
    if (!header.isInitialized()) header.initialize(alloc);
    return header;
  }
  [[nodiscard]] constexpr auto getDepth() const -> unsigned { return depth; }
  constexpr LoopTreeSchedule(LoopTreeSchedule *L, uint8_t d, uint8_t blockIdx)
    : subTrees{nullptr, 0, 0}, parent(L), depth(d), blckIdx(blockIdx) {}
  // NOLINTNEXTLINE(misc-no-recursion)
  void printDotEdges(llvm::raw_ostream &out,
                     llvm::ArrayRef<std::string> addrNames) {
    header.printDotEdges(out, addrNames);
    for (auto &subTree : subTrees) {
      subTree.subTree->printDotEdges(out, addrNames);
      subTree.exit.printDotEdges(out, addrNames);
    }
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto printSubDotFile(Arena<> *alloc, llvm::raw_ostream &out,
                       map<LoopTreeSchedule *, std::string> &names,
                       llvm::SmallVectorImpl<std::string> &addrNames,
                       unsigned addrIndOffset, poly::Loop *lret)
    -> poly::Loop * {
    poly::Loop *loop{nullptr};
    size_t j = 0;
    for (auto *addr : header.getAddr()) loop = addr->getAffLoop();
    for (auto &subTree : subTrees) {
      // `names` might realloc, relocating `names[this]`
      if (getDepth())
        names[subTree.subTree] = names[this] + "SubLoop#" + std::to_string(j++);
      else names[subTree.subTree] = "LoopNest#" + std::to_string(j++);
      if (loop == nullptr)
        for (auto *addr : subTree.exit.getAddr()) loop = addr->getAffLoop();
      loop = subTree.subTree->printSubDotFile(alloc, out, names, addrNames,
                                              addrIndOffset, loop);
    }
    const std::string &name = names[this];
    out << "\"" << name
        << "\" [shape=plain\nlabel = <<table><tr><td port=\"f0\">";
    // assert(depth == 0 || (loop != nullptr));
    if (loop && (getDepth() > 0)) {
      for (size_t i = loop->getNumLoops(), k = getDepth(); i > k;)
        loop = loop->removeLoop(alloc, --i);
      loop->pruneBounds(alloc);
      loop->printBounds(out);
    } else out << "Top Level";
    out << "</td></tr>\n";
    size_t i = header.printDotNodes(out, 0, addrNames, addrIndOffset, name);
    j = 0;
    std::string loopEdges;
    for (auto &subTree : subTrees) {
      std::string label = "f" + std::to_string(++i);
      out << " <tr> <td port=\"" << label << "\"> SubLoop#" << j++
          << "</td></tr>\n";
      loopEdges += "\"" + name + "\":f" + std::to_string(i) + " -> \"" +
                   names[subTree.subTree] + "\":f0 [color=\"#ff0000\"];\n";
      i = subTree.exit.printDotNodes(out, i, addrNames, addrIndOffset, name);
    }
    out << "</table>>];\n" << loopEdges;
    if (lret) return lret;
    if ((loop == nullptr) || (getDepth() <= 1)) return nullptr;
    return loop->removeLoop(alloc, getDepth() - 1);
  }

  void printDotFile(Arena<> *alloc, llvm::raw_ostream &out) {
    map<LoopTreeSchedule *, std::string> names;
    llvm::SmallVector<std::string> addrNames(numAddr_);
    names[this] = "toplevel";
    out << "digraph LoopNest {\n";
    auto p = alloc.scope();
    printSubDotFile(alloc, out, names, addrNames, subTrees.size(), nullptr);
    printDotEdges(out, addrNames);
    out << "}\n";
  }
};

static_assert(Graphs::AbstractIndexGraph<LoopTreeSchedule::AddressGraph>);
// class LoopForestSchedule : LoopTreeSchedule {
//   [[no_unique_address]] Arena<> *allocator;
// };
} // namespace poly::CostModeling

constexpr void
ScheduledNode::insertMem(Arena<> *alloc, PtrVector<MemoryAccess *> memAccess,
                         CostModeling::LoopTreeSchedule *L) const {
  // First, we invert the schedule matrix.
  SquarePtrMatrix<int64_t> Phi = schedule.getPhi();
  auto [Pinv, denom] = NormalForm::scaledInv(Phi);
  // TODO: if (s == 1) {}
  // TODO: make this function out of line
  auto &accesses{L->getInitAddr(alloc)};
  unsigned numMem = memory.size(), offset = accesses.size(),
           sId = std::numeric_limits<unsigned>::max() >> 1, j = 0;
  NotNull<poly::Loop> loop = loopNest->rotate(alloc, Pinv, offsets);
  for (size_t i : memory) {
    NotNull<MemoryAccess> mem = memAccess[i];
    bool isStore = mem->isStore();
    if (isStore) sId = j;
    ++j;
    size_t inputEdges = mem->inputEdges().size(),
           outputEdges = mem->outputEdges().size();
    Addr *addr = Addr::construct(
      alloc, loop, mem, isStore, Pinv, denom, schedule.getOffsetOmega(), L,
      inputEdges, isStore ? numMem - 1 : 1, outputEdges, offsets);
    mem->setAddress(addr);
    accesses.push_back(addr);
  }
  Addr *store = accesses[offset + sId];
  // addrs all need direct connections
  for (size_t i = 0, k = 0; i < numMem; ++i)
    if (i != sId) accesses[offset + i]->addDirectConnection(store, k++);
}
