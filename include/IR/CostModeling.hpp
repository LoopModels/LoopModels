#pragma once

// #include "./ControlFlowMerging.hpp"
#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "LinearProgramming/LoopBlock.hpp"
#include "LinearProgramming/ScheduledNode.hpp"
#include "Polyhedra/Dependence.hpp"
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
  [[nodiscard]] constexpr auto getNumVectorBits() const -> uint8_t {
    return maximumVectorWidth;
  }
  [[nodiscard]] constexpr auto getNumVector() const -> uint8_t {
    return numVectorRegisters;
  }
  [[nodiscard]] constexpr auto getNumScalar() const -> uint8_t {
    return numGeneralPurposeRegisters;
  }
  [[nodiscard]] constexpr auto getNumPredicate() const -> uint8_t {
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
  // NOLINTNEXTLINE(misc-no-recursion)
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
// NOLINTNEXTLINE(misc-no-recursion)
inline auto searchLoopIndependentUsers(IR::Dependencies deps, IR::Loop *L,
                                       IR::Node *N, uint8_t depth,
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
    for (IR::Addr *m : a->outputAddrs(deps, depth)) {
      ret *= searchLoopIndependentUsers(deps, L, m, depth, summary);
      if (ret.independent) continue;
      a->setDependsOnParentLoop();
      return ret;
    }
  }
  // if it isn't a Loop, must be an `Instruction`
  IR::Value *I = llvm::cast<IR::Instruction>(N);
  for (IR::Node *U : I->getUsers()) {
    ret *= searchLoopIndependentUsers(deps, L, U, depth, summary);
    if (ret.independent) continue;
    I->setDependsOnParentLoop();
    return ret;
  }
  // then we can push it to the front of the list, meaning it is hoisted out
  if (a) {
    if (ret.summary.notIndexedByLoop == a)
      ret.summary.notIndexedByLoop = llvm::cast_or_null<IR::Addr>(a->getNext());
  }
  I->removeFromList();
  I->insertAfter(ret.summary.afterExit);
  ret.summary.afterExit = I;
  I->visit(depth);
  return ret;
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto visitLoopDependent(IR::Dependencies deps, IR::Loop *L, IR::Node *N,
                               uint8_t depth, IR::Node *body) -> IR::Node * {
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
  if (auto *A = llvm::dyn_cast<IR::Addr>(N)) {
    for (IR::Addr *m : A->outputAddrs(deps, depth)) {
      if (m->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, m, depth, body);
    }
  }
  if (auto *I = llvm::dyn_cast<IR::Instruction>(N)) {
    for (IR::Node *U : I->getUsers()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, U, depth, body);
    }
  } else if (auto *S = llvm::dyn_cast<IR::Loop>(N)) {
    for (IR::Node *U : S->getChild()->nodes()) {
      if (U->wasVisited(depth)) continue;
      body = visitLoopDependent(deps, L, U, depth, body);
    }
  }
#ifndef NDEBUG
  if (N->getLoop() == L) N->visit(depth);
#endif
  if (N->getLoop() == L) body = N->setNext(body);
  return body;
}
inline auto topologicalSort(IR::Dependencies deps, IR::Loop *root,
                            unsigned depth) -> IR::Node * {
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
    summary = searchLoopIndependentUsers(deps, root, N, depth, summary).summary;
  // summary.afterExit will be hoisted out; every member has been marked as
  // `visited` So, now we search all of root's users, i.e. every addr that
  // depends on it
  IR::Node *body;
  for (IR::Node *N : summary.indexedByLoop->nodes())
    body = visitLoopDependent(deps, root, N, depth, body);
  body = root->setNext(body); // now we can place the loop
  for (IR::Node *N : summary.notIndexedByLoop->nodes())
    body = visitLoopDependent(deps, root, N, depth, body);
  // and any remaining edges
  return body;
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto buildGraph(IR::Dependencies deps, IR::Loop *root, unsigned depth)
  -> IR::Node * {
  // We build the instruction graph, via traversing the tree, and then
  // top sorting as we recurse out
  for (IR::Loop *child : root->subLoops()) buildGraph(deps, child, depth + 1);
  return topologicalSort(deps, root, depth);
}

inline auto addAddrToGraph(Arena<> *salloc, Arena<> *lalloc,
                           lp::ScheduledNode *nodes) -> IR::Loop * {
  auto s = salloc->scope();
  LoopTree *root = LoopTree::root(salloc, lalloc);
  for (lp::ScheduledNode *node : nodes->getAllVertices())
    root->addNode(salloc, lalloc, node);
  return root->getLoop();
}
inline void eliminateAddr(IR::Addr *a, IR::Addr *b) {
  if (a->indexMatrix() != b->indexMatrix()) return;
  /// are there any addr between them?
  if (a->isStore()) {
    if (b->isStore()) { // Write->Write
      // Are there reads in between? If so, we must keep--
      // --unless we're storing the same value twice (???)
      // without other intervening store-edges.
      // Without reads in between, it's safe.
    } else { // Write->Read
      // Can we replace the read with using the written value?
      if (a->getLoop() != b->getLoop()) return;
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
    if (a->getLoop() != b->getLoop()) return;
    // Any writes in between them?
  } // Read->Write, can't delete either
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
// E.g. check if the array is a `llvm::isNonEscapingLocalObject` and allocated
// by `llvm::isRemovableAlloc`.
inline void removeRedundantAddr(IR::Dependencies deps, IR::Addr *addr) {
  for (IR::Addr *a : addr->eachAddr()) {
    for (poly::Dependence *d = a->getEdgeOut(); d; d = d->getNextOutput()) {
      IR::Addr *b = d->output();
      eliminateAddr(a, b);
    }
  }
}
//
// Considering reordering legality, example
// for (int i = 0: i < I; ++i){
//   for (int j = 0 : j < i; ++j){
//     x[i] -= x[j]*U[j,i];
//   }
//   x[i] /= U[i,i];
// }
// We have an edge from the store `x[i] = x[i] / U[i,i]=` to the load of
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
//   x[i+1] /= U[i+1,i+1]; // store 1
//   { // perform unrolled j = i + 1 iter
//     int j = i+1; // these all depend on store 1
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   x[i+2] /= U[i+2,i+2]; // store 2
//   { // perform unrolled j = i + 2 iter
//     int j = i+2; // this depends on store 2
//     x[i+3] -= x[j]*U[j,i+3];
//   }
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
// conditionally (on our schedule) independent.
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
///
/// Optimize the schedule
inline void optimize(IR::Cache &instr, Arena<> *lalloc,
                     lp::LoopBlock::OptimizationResult res) {
  /// we must build the IR::Loop
  /// Initially, to help, we use a nested vector, so that we can index into it
  /// using the fusion omegas. We allocate it with the longer lived `instr`
  /// alloc, so we can checkpoint it here, and use alloc for other IR nodes.
  Arena<> *salloc = instr.getAllocator();

  IR::Node *N = buildGraph(addAddrToGraph(salloc, lalloc, res.nodes), 0);
  // `N` is the head of the topologically sorted graph
  // We now try to remove redundant memory operations

  removeRedundantAddr(res.addr.addr);
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
