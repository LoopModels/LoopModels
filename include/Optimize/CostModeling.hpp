#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "LinearProgramming/LoopBlock.hpp"
#include "LinearProgramming/ScheduledNode.hpp"
#include "Polyhedra/Dependence.hpp"
#include <Alloc/Arena.hpp>
#include <Math/Array.hpp>
#include <Math/Math.hpp>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/CaptureTracking.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
namespace poly::IR {
/// If this is a store of a reassocialbe reduction, this sets the
/// `reassociableReduction` field to the corresponding load, and that field of
/// the load to `this` store.
/// It requires `Addr` to have been sorted, so we check the first output edge of
/// this store. If that edge is a load within the same loop, and has a time
/// dependence, we check for a reassociable chain of compute operations
/// connecting them. If such a chain, without any non-reassociable chains,
/// exists, then we mark them as reassociable.
/// Note, with sorting
/// for (int i = 0; i < I; ++i)
///   for (int j = 0; j < J; ++j)
///     x[i] = x[i] + A[j,i] * y[j];
///   x[i] = acc;
///
/// we have the store `x[i]` is the source for the `x[i]` load on a future
/// `j` iteration.
/// However, our IR would be optimized into:
///
/// for (int i = 0; i < I; ++i){
///   acc = x[i];
///   for (int j = 0; j < J; ++j)
///     acc += A[j,i] * y[j];
///   x[i] = acc;
/// }
///
/// The same thing applies: `j` is the loop that satifies the dependency,
/// but we hoisted the load/store pair out.
/// This must be called after `sortEdges`, so that output edges of the store
/// `x[i] = acc` are top sorted. The load `acc = x[i]` should be the very
/// first output topologically -- afterall, it occus before the store!!
/// TODO: does `Addr` hoisting handle this??
/// Consider also the example:
/// int64_t x[1]{};
/// for (ptrdiff_t n = 0; n < N; ++n){
///   x[0] = x[0] + y[n];
///   z[n] = x[0];
/// }
/// this is harder to understand than, but behaves the same as
/// z[0] = y[n];
/// for (ptrdiff_t n = 1; n < N; ++n){
///   z[n] = z[n-1] + y[n];
/// }
/// int64_t x[1]{z[N-1]};
/// which does not have any reductions.
/// This should be handled because, if we had a loop like
/// int64_t x[1]{};
/// for (ptrdiff_t n = 0; n < N; ++n) x[0] = x[0] + y[n];
/// it should be optimized into
/// int64_t x[1]{};
/// auto xv = x[0];
/// for (ptrdiff_t n = 0; n < N; ++n) xv = xv + y[n];
/// x[0] = xv;
/// However, the assignment `z[n]` should block the hoisting of the load/store
/// and we can check that failure to hoist for verifying legality.
constexpr inline void
Addr::maybeReassociableReduction(const Dependencies &deps) {
  if (isLoad()) return;
  // we should have a store whose first output edge is the load for
  // the following iteration. This iter is the reverse-time
  // edge.
  auto edges{outputEdgeIDs(deps, getCurrentDepth())};
  auto B = edges.begin();
  if (B == edges.end()) return;
  poly::Dependence::ID id{*B};
  if (deps.revTimeEdge(id) < 0) return;
  IR::Addr *dst = deps.output(id);
  if (dst->isStore() || (getLoop() != dst->getLoop())) return;
  // if we failed to hoist the `Addr` out of time-dims, then we cannot optimize.
  if (getCurrentDepth() > deps.satLevel(id)) return;
  if (reassociableReduction == dst) return; // multiple time dims, already found
  auto *c = llvm::dyn_cast<IR::Compute>(getStoredVal());
  if (!c) return;
  if (findThroughReassociable(dst, c) != 1) return;
  reassociableReduction = dst;
  dst->reassociableReduction = this;
}

} // namespace poly::IR
namespace poly::CostModeling {
using poly::Dependence;
// struct CPUExecutionModel {};

template <typename T> using Vec = math::ResizeableView<T, ptrdiff_t>;

// TODO: instead of this, update in-place and ensure all Addr are
// over-allocated to correspond with max depth? Because we parse in reverse
// order, we have max possible depth of `ScheduledNode`s using it at time we
// create.

/// LoopTree
/// A tree of loops, with an indexable vector of IR::Loop*s, to facilitate
/// construction of the IR::Loop graph, from the fusion omegas
class LoopTree {
  // The root of this subtree
  Valid<IR::Loop> loop;
  // LoopTree *parent{nullptr}; // do we need this?
  Vec<LoopTree *> children{};
  unsigned depth{0};
  // We do not need to know the previous loop, as dependencies between
  // the `Addr`s and instructions will determine the ordering.
  constexpr LoopTree(Arena<> *lalloc, poly::Loop *L, LoopTree *parent_)
    : loop{lalloc->create<IR::Loop>(parent_->depth + 1, L)},
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
  // NOLINTNEXTLINE(misc-no-recursion)
  void addNode(Arena<> *salloc, Arena<> *lalloc, lp::ScheduledNode *node) {
    if (node->getNumLoops() == depth) {
      // Then it belongs here, and we add loop's dependencies.
      // We only need to add deps to support SCC/top sort now.
      // We also apply the rotation here.
      // For dependencies in SCC iteration, only indvar deps get iterated.
      auto [Pinv, denom] = math::NormalForm::scaledInv(node->getPhi());
      Valid<poly::Loop> explicitLoop =
        node->getLoopNest()->rotate(lalloc, Pinv, node->getOffset());
      for (IR::Addr *m : node->localAddr()) {
        m->rotate(explicitLoop, Pinv, denom, node->getOffsetOmega(),
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
        children[i] = new (salloc) LoopTree{lalloc, node->getLoopNest(), this};
      numChildren = idx + 1;
    }
    children[idx]->addNode(salloc, lalloc, node);
  }
  constexpr auto getChildren() -> Vec<LoopTree *> { return children; }
  constexpr auto getLoop() -> IR::Loop * { return loop; }
};

inline void hoist(IR::Node *N, IR::Loop *P, int depth) {
  N->setParent(P);
  N->setCurrentDepth(depth);
}

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
/// inline auto searchLoopIndependentUsers(IR::Dependencies deps, IR::Loop *L,
///                                        IR::Node *N, uint8_t depth,
///                                        LoopDepSummary summary)
///
///   Searches `N` and it's users for loop-independent users, and returns them
/// as a list to process.
///   This exits early if it finds a dependent user, meaning it will only return
/// a partial list in this case. We search the entire graph eventually, meaning
/// the remainder will be processed later.
///   We return a `LoopDepSummary, bool` pair, where the `bool` is true if `N`
///   was
/// loop independent. We use the `bool` rather than a `nullptr` or optional so
/// that we can still return those results we did find on failure.
///  NOLINTNEXTLINE(misc-no-recursion)
inline auto searchLoopIndependentUsers(const IR::Dependencies &deps,
                                       IR::Loop *L, IR::Node *N, int depth,
                                       LoopDepSummary summary)
  -> LoopIndependent {
  if (N->dependsOnParentLoop()) return {summary, false};
  if (llvm::isa<IR::Loop>(N)) return {summary, false};
  if (IR::Loop *P = N->getLoop(); P != L)
    return {summary, !(P && L->contains(P))};
  LoopIndependent ret{summary, true};
  auto *a = llvm::dyn_cast<IR::Addr>(N);
  if (a) {
    a->removeFromList();
    if (a->indexedByInnermostLoop()) {
      a->insertAfter(ret.summary.indexedByLoop);
      ret.summary.indexedByLoop = a;
      return {summary, false};
    }
    a->insertAfter(ret.summary.notIndexedByLoop);
    ret.summary.notIndexedByLoop = a;
    for (IR::Addr *m : a->unhoistableOutputs(deps, depth - 1)) {
      ret *= searchLoopIndependentUsers(deps, L, m, depth, summary);
      if (ret.independent) continue;
      a->setDependsOnParentLoop();
      return ret;
    }
  }
  // if it isn't a Loop or Addr, must be an `Instruction`
  IR::Value *I = llvm::cast<IR::Instruction>(N);
  for (IR::Node *U : I->getUsers()) {
    ret *= searchLoopIndependentUsers(deps, L, U, depth, summary);
    if (ret.independent) continue;
    I->setDependsOnParentLoop();
    return ret;
  }
  // then we can push it to the front of the list, meaning it is hoisted out
  if (a && (ret.summary.notIndexedByLoop == a))
    ret.summary.notIndexedByLoop = llvm::cast_or_null<IR::Addr>(a->getNext());
  I->removeFromList();
  I->insertAfter(ret.summary.afterExit);
  ret.summary.afterExit = I;
  I->visit(depth);
  return ret;
}
/// `R`: remove from loop, if not `nullptr`, set the parent of `N` to `R`
/// `R` is applied recursivvely, forwarded to all calls.
// NOLINTNEXTLINE(misc-no-recursion)
inline auto visitLoopDependent(const IR::Dependencies &deps, IR::Loop *L,
                               IR::Node *N, int depth, IR::Node *body,
                               IR::Loop *R = nullptr) -> IR::Node * {
  invariant(N->getVisitDepth() != 254);
  // N may have been visited as a dependent of an inner loop, which is why
  // `visited` accepts a depth argument
  if (N->wasVisited(depth) || !(L->contains(N))) return body;
#ifndef NDEBUG
  // Our goal here is to check for cycles in debug mode.
  // Each level of our graph is acyclic, meaning that there are no cycles at
  // that level when traversing only edges active at that given level.
  // However, when considering edges active at level `I`, we may have cycles
  // at level `J` if `J>I`. In otherwords, here we are traversing all edges
  // active at `I=depth`. Within subloops, which necessarily have depth
  // `J>I`, we may have cycles.
  //
  // Thus, we need to prevent getting stuck in a cycle for these deeper loops
  // by setting `N->visit(depth)` here, so `wasVisited` will allow them to
  // immediately return. But, in debug mode, we'll set nodes of the same depth
  // to `254` to check for cycles.
  if (N->getLoop() == L) N->visit(254);
  else N->visit(depth);
#else
  N->visit(depth);
#endif
  // iterate over users
  if (auto *A = llvm::dyn_cast<IR::Addr>(N)) {
    // Note that `topologicalSort` calls `searchLoopIndependentUsers` which
    // checks whether an `Addr` is `indexedByInnermostLoop`.
    //
    // Note that here `depth` is `0` for top-level, 1 for the outer most loop,
    // etc. That is, loops are effectively 1-indexed here, while `satLevel`
    // is effectively 0-indexed by loop.
    //   Example 1:
    // for (ptrdiff_t m = 0; m < M; ++m)
    //   for (ptrdiff_t n = 0; n < N; ++n)
    //     for (ptrdiff_t k = 0; k < K; ++k) C[m,n] = C[m,n] + A[m,k]*B[k,n];
    // we have cyclic dependencies between the load from/store to `C[m,n]`.
    // The `C[m,n]` load -> `C[m,n]` store was not satisfied by any loop, so
    // the sat level is 255.
    // The `C[m,n]` store -> `C[m,n]` load has satLevel = 2.
    //   Example 2:
    // for (ptrdiff_t m = 0; m < M; ++m)
    //   for (ptrdiff_t n = 1; n < N; ++n) C[m,n] = C[m,n] + C[m,n-1];
    // we again have a cyple, from the load `C[m,n-1]` to the store `C[m,n]`,
    // and from the store `C[m,n]` to the load `C[m,n-1]` on the following
    // iteration.
    // The former has a sat level of 255, while the latter has a sat level of
    // `1`.
    //
    // isActive(depth) == satLevel() > depth
    //
    // a. load->store is not satisfied by any loop, instead handled by sorting
    //    of instructions in the innermost loop, i.e. sat is depth=3.
    // b. store->load is carried by the `k` loop, i.e. sat is depth=2.
    //    Because `2 > (3-1) == false`, we do not add it here,
    //    its sorting isn't positional!
    //
    // TODO:
    // - [ ] I think the current algorithm may illegally hoist certain
    //       dependencies carried on this loop. Specifically, we can hoist
    //       addresses that (a) are not indexed by this loop, but need to be
    //       repeated anyway because of some other address operation, while that
    //       combination can't be moved to registers, e.g. because their index
    //       matrices are not equal.
    //       We need to distinguish between order within the loop, for the
    //       purpose of this topsort, and placement with respect to the loop.
    //       Simply, we perhaps should simply avoid hoisting when we carry
    //       a dependence that doesn't meet the criteria of `unhoistableOutputs`
    // - [ ] Incorporate the legality setting here?
    for (IR::Addr *m : A->unhoistableOutputs(deps, depth - 1)) {
      if (m->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, m, depth, body, R);
    }
  }
  if (auto *I = llvm::dyn_cast<IR::Instruction>(N)) {
    for (IR::Node *U : I->getUsers()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, U, depth, body, R);
    }
  } else if (auto *S = llvm::dyn_cast<IR::Loop>(N)) {
    for (IR::Node *U : S->getChild()->nodes()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, U, depth, body, R);
    }
  }
#ifndef NDEBUG
  if (N->getLoop() == L) N->visit(depth);
#endif
  if (N->getLoop() == L) body = N->setNext(body);
  if (R) hoist(N, R, depth - 1);
  return body;
}
inline void addBody(const IR::Dependencies &deps, IR::Loop *root, int depth,
                    IR::Node *nodes) {
  IR::Exit exit{}; // use to capture last node
  IR::Node *body{&exit};
  for (IR::Node *N : nodes->nodes())
    body = visitLoopDependent(deps, root, N, depth, body);
  body = root->setChild(body); // now we can place the loop
  IR::Node *last = exit.getPrev();
  if (last) last->setNext(nullptr);
  root->setLast(last);
}
inline void topologicalSort(const IR::Dependencies &deps, IR::Loop *root,
                            int depth) {
  // basic plan for the top sort:
  // We iterate across all users, once all of node's users have been added,
  // we push it to the front of the list. Thus, we get a top-sorted list.
  // We're careful about the order, so that this top sort should LICM all the
  // addresses that it can.
  //
  // We must push the exit before the root (as the exit depends on the loop,
  // and we iterate users). The exit doesn't use any in this block, so we
  // begin by trying to push any instructions that don't depend on the loop.
  // If we fail to push them (i.e., because they have uses that do depend on
  // the loop), then they get added to a revisit queue. Any instructions we
  // are able to push-front before we push the exit, implicitly happen after
  // the exit, i.e. they have been LICMed into the exit block. We unvisit the
  // revisit-queue, and add them back to the main worklist. Then, we proceed
  // with a depth-first topological sort normally (iterating over uses,
  // pushing to the front), starting with the loop root, so that it gets
  // pushed to the front as soon as possible. That is, so that it happens as
  // late as possible Any instructions that get pushed to the front afterwards
  // have been LICMed into the loop pre-header.
  //
  // In this first pass, we iterate over all nodes, pushing those
  // that can be hoisted after the exit block.
  //
  IR::Node *C = root->getChild();
  LoopDepSummary summary{};
  for (IR::Node *N : C->nodes())
    summary = searchLoopIndependentUsers(deps, root, N, depth, summary).summary;
  // summary.afterExit will be hoisted out; every member has been marked as
  // `visited` So, now we search all of root's users, i.e. every addr that
  // depends on it
  root->setNext(summary.afterExit);
  IR::Loop *P = root->getLoop();
  for (IR::Node *N : summary.afterExit->nodes()) hoist(N, P, depth - 1);
  addBody(deps, root, depth, summary.indexedByLoop);
  IR::Node *body{root};
  for (IR::Node *N : summary.notIndexedByLoop->nodes())
    body = visitLoopDependent(deps, root, N, depth, body, P);
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto buildSubGraph(const IR::Dependencies &deps, IR::Loop *root,
                          int depth, uint32_t id) -> uint32_t {
  // We build the instruction graph, via traversing the tree, and then
  // top sorting as we recurse out
  for (IR::Loop *child : root->subLoops())
    id = buildSubGraph(deps, child, depth + 1, id);
  root->setMeta(id++);

  // The very outer `root` needs to have all instr constituents
  // we also need to add the last instruction of each loop as `last`
  topologicalSort(deps, root, depth);
  return id;
}
inline auto buildGraph(const IR::Dependencies &deps, IR::Loop *root)
  -> uint32_t {
  // We build the instruction graph, via traversing the tree, and then
  // top sorting as we recurse out
  uint32_t id = 0;
  for (IR::Loop *child : root->subLoops())
    id = buildSubGraph(deps, child, 1, id);

  // The very outer `root` needs to have all instr constituents
  // we also need to add the last instruction of each loop as `last`
  addBody(deps, root, 0, root->getChild());
  return id;
}

inline auto addAddrToGraph(Arena<> *salloc, Arena<> *lalloc,
                           lp::ScheduledNode *nodes) -> IR::Loop * {
  auto s = salloc->scope();
  // `root` is top level loop
  LoopTree *root = LoopTree::root(salloc, lalloc);
  for (lp::ScheduledNode *node : nodes->getAllVertices())
    root->addNode(salloc, lalloc, node);
  return root->getLoop();
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto hasFutureReadsCore(dict::aset<llvm::BasicBlock *> &successors,
                               llvm::Instruction *I) -> bool {
  for (auto *U : I->users()) {
    auto *UI = llvm::dyn_cast<llvm::Instruction>(U);
    if (!UI) continue;
    if (UI->mayReadFromMemory() && successors.count(UI->getParent()))
      return true;
    if (llvm::isa<llvm::GetElementPtrInst>(UI) &&
        hasFutureReadsCore(successors, UI))
      return true;
    // TODO: don't just give up if we cast to int?
    if (llvm::isa<llvm::PtrToIntInst>(UI) || llvm::isa<llvm::BitCastInst>(UI))
      return true;
  }
  return false;
}
inline auto hasFutureReads(Arena<> *alloc, dict::set<llvm::BasicBlock *> &LBBs,
                           llvm::Instruction *I) -> bool {
  auto s = alloc->scope();
  dict::aset<llvm::BasicBlock *> successors{alloc};
  for (llvm::BasicBlock *S : llvm::successors(I->getParent()))
    if (!LBBs.count(S)) successors.insert(S);
  return hasFutureReadsCore(successors, I);
}

struct LoopDepSatisfaction {
  IR::Dependencies &deps;
  MutPtrVector<int32_t> loopDeps;

  constexpr auto dependencyIDs(IR::Loop *L) {
    return utils::VForwardRange{loopDeps.begin(), L->getEdge()};
  }
  constexpr auto depencencies(IR::Loop *L) {
    return dependencyIDs(L) | deps.getEdgeTransform();
  }
};

class IROptimizer {
  IR::Dependencies &deps;
  IR::Cache &instructions;
  dict::set<llvm::BasicBlock *> &LBBs;
  dict::set<llvm::CallBase *> &eraseCandidates;
  IR::Loop *root_;
  MutPtrVector<int32_t> loopDeps;
  Arena<> *lalloc_;
  llvm::TargetLibraryInfo *TLI;

  /// `loopDepSats` places the dependencies at the correct loop level so that
  /// we can more easily check all dependencies carried by a particular loop.
  /// We use these for checks w/ respect to unrolling and vectorization
  /// legality.
  /// The returned vector is an integer vector, giving a mapping of loops
  /// to depencencies handled at that level.
  /// We can use these dependencies for searching reductions for
  /// trying to prove legality.
  static auto loopDepSats(Arena<> *alloc, IR::Dependencies &deps,
                          lp::LoopBlock::OptimizationResult res)
    -> MutPtrVector<int32_t> {
    MutPtrVector<int32_t> loopDeps{math::vector<int32_t>(alloc, deps.size())};
    // place deps at sat level for loops
    for (IR::Addr *a : res.addr.getAddr()) {
      IR::Loop *L = a->getLoop();
      for (int32_t id : a->inputEdgeIDs(deps)) {
        uint8_t lvl = deps.satLevel(IR::Dependence::ID{id});
        L->getLoopAtDepth(lvl)->addEdge(loopDeps, id);
      }
    }
    return loopDeps;
  }
  [[nodiscard]] constexpr auto getLoopDeps() const -> LoopDepSatisfaction {
    return {deps, loopDeps};
  }
  // this compares `a` with each of its active outputs.
  inline void eliminateAddr(IR::Addr *a) {
    for (int32_t id : a->outputEdgeIDs(deps, a->getCurrentDepth())) {
      IR::Addr *b = deps.output(Dependence::ID{id});
      // TODO: also check loop extants
      if (a->indexMatrix() != b->indexMatrix() ||
          a->getOffsetOmega() != b->getOffsetOmega())
        return;
      if (a->isStore()) {
        // On a Write->Write, we remove the first write.
        if (b->isStore()) return a->drop(deps);
        // Write->Load, we will remove the load if it's in the same block as the
        // write, and we can forward the stored value.
        if (a->getLoop() != b->getLoop()) return;
        instructions.replaceAllUsesWith(b, a->getStoredVal());
        b->drop(deps);
      } else if (b->isLoad()) { // Read->Read
        // If they're not in the same loop, we need to reload anyway
        if (a->getLoop() != b->getLoop()) return;
        // If they're in the same loop, we can delete the second read
        instructions.replaceAllUsesWith(b, a);
        b->drop(deps);
      } else return; // Read->Write, can't delete either
    }
  }
  // we eliminate temporaries that meet these conditions:
  // 1. are only ever stored to (this can be achieved via
  // load-elimination/stored-val forwarding in `removeRedundantAddr`)
  // 2. are non-escaping, i.e. `llvm::isNonEscapingLocalObject`
  // 3. returned by `llvm::isRemovableAlloc`
  inline auto eliminateTemporaries(IR::AddrChain addr) -> unsigned {
    auto s = lalloc_->scope();
    dict::aset<IR::Addr *> loaded{lalloc_};
    for (IR::Addr *a : addr.getAddr())
      if (a->isLoad()) loaded.insert(a);
    unsigned remaining = 0;
    for (IR::Addr *a : addr.getAddr()) {
      if (a->isDropped()) continue;
      ++remaining;
      if (loaded.contains(a)) continue;
      const llvm::SCEVUnknown *ptr = a->getArrayPointer();
      auto *call = llvm::dyn_cast<llvm::CallBase>(ptr->getValue());
      if (!call) continue;
      if (!llvm::isNonEscapingLocalObject(call, nullptr)) continue;
      if (!llvm::isRemovableAlloc(call, TLI)) continue;
      if (hasFutureReads(lalloc_, LBBs, call)) continue;
      a->drop(deps);
      // we later check if any uses remain other than the associated free
      // if not, we can delete them.
      // We may want to go ahead and do this here. We don't for now,
      // because we have live `llvm::Instruction`s that we haven't removed yet.
      // TODO: revisit when handling code generation (and deleting old code)
      eraseCandidates.insert(call);
      --remaining;
    }
    return remaining;
  }

  // plan: SCC? Iterate over nodes in program order?
  // then we can iterate in order.
  // What to do about depth?
  // We may have
  // for (i : I){
  //   for (j : J){
  //     A[j] = x; // store
  //     y = A[j]; // load
  //   }
  // }
  // In this case, we do have a cycle:
  // A[j]^s_i -> A[j]^l_i
  // A[j]^l_i -> A[j]^s_{i+1}
  // However, this cycle does not prohibit deleting the load,
  // replacing it with `y = x`.
  // This still holds true if the load were a second store:
  // for (i : I){
  //   for (j : J){
  //     A[j] = x; // store
  //     A[j] = y; // load
  //   }
  // }
  // We could stick with the single `y` store.
  // Thus, for eliminating memory operations at a depth of 2,
  // we are only concerned with dependencies still valid at a depth of 2.
  // for (int i = 0 : i < I; ++i){
  //   x[i] /= U[i,i];
  //   for (int j = i+1; j < I; ++j){
  //     x[j] -= x[i]*U[i,j];
  //   }
  // }
  // Maybe just do the dumb thing?
  // Walk the graph for addr costs, and at the same time,
  // check the addr for eliminability, checking against what we've stored thus
  // far.
  // We currently do not store load-load edges, which is why only checking
  // edge relationships is not ideal.
  // We may store load-load edges in the future, as these could be used as
  // part of the cost function of the linear program, i.e. we'd want to
  // minimize the distance between loads (but allow reordering them).
  //
  // I think a reasonable approach is:
  // Have a map from array pointer to Addr. Addrs form a chain.
  // as we walk the graph, add each newly encountered addr to the front of the
  // chain and check if we can eliminate it, or any of its predecessors.
  //
  // Note (bracketed means we might be able to eliminate):
  // Read->[Read] could eliminate read
  // Read->Write no change
  // Write->[Read] can forward written value
  // [Write]->Write can eliminate first write
  // Thus, we can fuse this pass with our address cost calculation.
  // We check if we can eliminate before calculating the new cost.
  // The only case where we may remove an old value, write->write,
  // we could just take the old cost and assign it to the new write.
  // TODO: if we have only writes to a non-escaping array, we should
  // be able to eliminate these writes too, and then also potentially
  // remove that array temporary (e.g., if it were malloc'd).
  // E.g. check if the array is a `llvm::isNonEscapingLocalObject` and
  // allocated by `llvm::isRemovableAlloc`.
  void removeRedundantAddr(IR::AddrChain addr) {
    // outputEdges are sorted topologically from first to last.
    // Example:
    // for (int i = 0; i < I; ++i){
    //   acc = x[i];           // Statement: 0
    //   for (int j = 0; j < i; ++j){
    //     acc -= x[j]*U[j,i]; // Statement: 1
    //   }
    //   x[i] = acc;           // Statement: 2
    //   x[i] = x[i] / U[i,i]; // Statement: 3
    // }
    // Here, we have a lot of redundant edges connecting the various `x[i]`s.
    // We also have output edges between the `x[i]` and the `x[j]` load in
    // statement 1. It is, however, satisfied at `x[i]`'s depth, and ignored.
    // So, what would happen here:
    // S0R->S2W, no change; break.
    // S2W->S3R, replace read with stored value forwarding.
    // S2W->S3W, remove S2W as it is shadowed by S3W.
    // NOTE: we rely on the `ListRange` iterator supporting safely removing the
    // current iter from the list.
    for (IR::Addr *a : addr.getAddr()) eliminateAddr(a);
  }
  /// `sortEdges` sorts each `Addr`'s output edges
  /// So that each `Addr`'s output edges are sorted based on the
  /// topological ordering of the outputs.
  /// The approach to sorting edges is to iterate through nodes backwards
  /// whenever we encounter an `Addr`, we push it to the front of each
  /// output edge list to which it belongs.
  /// We also assigning each `Addr` an order by decrementing an integer each
  /// time we encounter one. This is also necessary for Addr elimination, as we
  /// want to find the first topologically greater Addr.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto sortEdges(IR::Loop *R, int32_t pos) -> int32_t {
    for (IR::Node *n = R->getLast(); n != R; n = n->getPrev()) {
      if (auto *L = llvm::dyn_cast<IR::Loop>(n)) {
        pos = sortEdges(L, pos);
        continue;
      }
      auto *a = llvm::dyn_cast<IR::Addr>(n);
      if (!a) continue;
      a->setTopPosition(pos--);
      // for each input edge, we push `a` to the front of the output list
      for (int32_t id : a->inputEdgeIDs(deps)) {
        if (deps.prevOut(Dependence::ID{id}) < 0) continue;
        deps.removeOutEdge(id);
        IR::Addr *b = deps.input(Dependence::ID{id});
        int32_t oldFirst = b->getEdgeOut();
        deps.prevOut(Dependence::ID{oldFirst}) = id;
        deps.prevOut(Dependence::ID{id}) = -1;
        deps.nextOut(Dependence::ID{id}) = oldFirst;
        b->setEdgeOut(id);
      }
    }
    return pos;
  }
  void findReductions(IR::AddrChain addr) {
    for (IR::Addr *a : addr.getAddr()) a->maybeReassociableReduction(deps);
  };

public:
  IROptimizer(IR::Dependencies &deps, IR::Cache &instr,
              dict::set<llvm::BasicBlock *> &loopBBs,
              dict::set<llvm::CallBase *> &eraseCandidates_, IR::Loop *root,
              Arena<> *lalloc, lp::LoopBlock::OptimizationResult res,
              uint32_t numLoops)
    : deps{deps}, instructions{instr}, LBBs{loopBBs},
      eraseCandidates{eraseCandidates_}, root_{root}, lalloc_{lalloc} {
    sortEdges(root_, 0);
    removeRedundantAddr(res.addr);
    unsigned numAddr = eliminateTemporaries(res.addr);
    findReductions(res.addr);
    loopDeps = loopDepSats(lalloc, deps, res);
    /// TODO: legality check
    // plan now is to have a `BitArray` big enough to hold `numLoops` entries
    // and `numAddr` rows; final axis is contiguous vs non-contiguous
    // Additionally, we will have a vector of unroll strategies to consider
    // LoopDependencies *ld = LoopDependencies::create(lalloc_, numLoops,
    // numAddr);
  }
};

//
// Considering reordering legality, example
// for (int i = 0: i < I; ++i){
//   for (int j = 0 : j < i; ++j){
//     x[i] -= x[j]*U[j,i];
//   }
//   x[i] /= U[i,i];
// }
// We have an edge from the store `x[i] = x[i] / U[i,i]` to the load of
// `x[j]`, when `j = ` the current `i`, on some future iteration.
// We want to unroll;
// for (int i = 0: i < I-3; i += 4){
//   for (int j = 0 : j < i; ++j){
//     x[i] -= x[j]*U[j,i];
//     x[i+1] -= x[j]*U[j,i+1];
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   x[i] /= U[i,i]; // store 0
//   { // perform unrolled j = i iter
//     int j = i; // these all depend on store 0
//     x[i+1] -= x[j]*U[j,i+1];
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+1 iteration for i=i iter goes here (but doesn't happen)
//   x[i+1] /= U[i+1,i+1]; // store 1
//   { // perform unrolled j = i + 1 iter
//     int j = i+1; // these all depend on store 1
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+2 iteration for i=i iter goes here (but doesn't happen)
//   // j+2 iteration for i=i+1 iter goes here (but doesn't happen)
//   x[i+2] /= U[i+2,i+2]; // store 2
//   { // perform unrolled j = i + 2 iter
//     int j = i+2; // this depends on store 2
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+3 iteration for i=i iter goes here (but doesn't happen)
//   // j+3 iteration for i=i+1 iter goes here (but doesn't happen)
//   // j+3 iteration for i=i+2 iter goes here (but doesn't happen)
//   x[i+3] /= U[i+3,i+3];
// }
// The key to legality here is that we peel off the dependence polyhedra
// from the loop's iteration space.
// We can then perform the dependent iterations in order.
// With masking, the above code can be vectorized in this manner.
// The basic approach is that we have the dependence polyhedra:
//
// 0 <= i_s < I
// 0 <= i_l < I
// 0 <= j_l < i_l
// i_s = j_l // dependence, yields same address in `x`
//
// Note that our schedule sets
// i_s = i_l
// Which gives:
// i_l = i_s = j_l < i_l
// a contradiction, meaning that the dependency is
// conditionally (on our schedule) satisfied.
// Excluding the `i_s = i_l` constraint from the
// polyhedra gives us the region of overlap.
//
// When unrolling by `U`, we get using `U=4` as an example:
// i^0_s + 1 = i^1_s
// i^0_s + 2 = i^2_s
// i^0_s + 3 = i^3_s
// 0 <= i^0_s < I
// 0 <= i^1_s < I
// 0 <= i^2_s < I
// 0 <= i^3_s < I
// 0 <= i^0_l < I
// 0 <= i^1_l < I
// 0 <= i^2_l < I
// 0 <= i^3_l < I
// 0 <= j_l < i^0_l
// 0 <= j_l < i^1_l
// 0 <= j_l < i^2_l
// 0 <= j_l < i^3_l
// i^0_s = j_l ||  i^1_s = j_l || i^2_s = j_l || i^3_s = j_l
// where the final union can be replaced with
// i^0_s = j_l ||  i^0_s+1 = j_l || i^0_s+2 = j_l || i^0_s+3 = j_l
// i^0_s <= j_1 <= i^0_s+3
//
// Similarly, we can compress the other inequalities...
// 0 <= i^0_s < I - 3
// 0 <= i^0_l < I - 3
// 0 <= j_l < i^0_l
// i^0_s <= j_1 <= i^0_s+3 // dependence region
//
// So, the parallel region is the union
// i^0_s > j_1 || j_1 > i^0_s+3
//
// In this example, note that the region `j_1 > i^0_s+3` is empty
// so we have one parallel region, and then one serial region.
//
// Lets consider simpler checks. We have
// [ 1 0 ] : x[i] -=
// [ 0 1 ] : x[j]
// [ 1 ]   : x[i] /=
// we have a dependency when `i == j`. `i` carries the dependency, but we can
// peel off the independent iters from `j`, and unroll `i` for these.
//
// How to identify:
// [ 1 -1 ]
// vs, if we had two `x[i]` or two `x[j]`
// [ 0, 0 ]
// An idea: look for non-zero so we can peel?
// Or should we look specifically for `x[i] == x[j]` type pattern?
// E.g., if we had
// [ i,  j, k,  l ]
// [ 2, -1, 2, -1 ]
// we'd need a splitting algorithm.
// E.g., split on the 2nd loop, so we get `j == 2*i + 2*k - l`
// With this, we'd split iterations into groups
// j  < 2*i + 2*k - l
// j == 2*i + 2*k - l
// j  > 2*i + 2*k - l
// Subsetting the `k` and `l` iteration spaces may be a little annoying,
// so we may initially want to restrict ourselves to peeling the innermost loop.
///
/// Optimize the schedule
inline void optimize(IR::Dependencies deps, IR::Cache &instr,
                     dict::set<llvm::BasicBlock *> &loopBBs,
                     dict::set<llvm::CallBase *> &eraseCandidates,
                     Arena<> *lalloc, lp::LoopBlock::OptimizationResult res) {
  // we must build the IR::Loop
  // Initially, to help, we use a nested vector, so that we can index into it
  // using the fusion omegas. We allocate it with the longer lived `instr`
  // alloc, so we can checkpoint it here, and use alloc for other IR nodes.
  // The `instr` allocator is more generally the longer lived allocator,
  // as it allocates the actual nodes.

  IR::Loop *root = addAddrToGraph(instr.getAllocator(), lalloc, res.nodes);
  uint32_t numLoops = buildGraph(deps, root);
  // `N` is the head of the topologically sorted graph
  // We now try to remove redundant memory operations

  IROptimizer(deps, instr, loopBBs, eraseCandidates, root, lalloc, res,
              numLoops);
}

/*
// NOLINTNEXTLINE(misc-no-recursion)
inline auto printSubDotFile(Arena<> *alloc, llvm::raw_ostream &out,
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

inline void printDotFile(Arena<> *alloc, llvm::raw_ostream &out) {
map<LoopTreeSchedule *, std::string> names;
llvm::SmallVector<std::string> addrNames(numAddr_);
names[this] = "toplevel";
out << "digraph LoopNest {\n";
auto p = alloc.scope();
printSubDotFile(alloc, out, names, addrNames, subTrees.size(), nullptr);
printDotEdges(out, addrNames);
out << "}\n";
}
*/
// class LoopForestSchedule : LoopTreeSchedule {
//   [[no_unique_address]] Arena<> *allocator;
// };
} // namespace poly::CostModeling
