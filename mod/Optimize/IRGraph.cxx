#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <llvm/Analysis/CaptureTracking.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Casting.h>
#include <ranges>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Pair.cxx"
#include "Dicts/Dict.cxx"
#include "Dicts/Trie.cxx"
#include "IR/Address.cxx"
#include "IR/Array.cxx"
#include "IR/Cache.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#include "IR/Phi.cxx"
#include "IR/TreeResult.cxx"
#include "LinearProgramming/LoopBlock.cxx"
#include "LinearProgramming/ScheduledNode.cxx"
#include "Math/Array.cxx"
#include "Math/AxisTypes.cxx"
#include "Math/Constructors.cxx"
#include "Math/NormalForm.cxx"
#include "Optimize/Legality.cxx"
#include "Polyhedra/Dependence.cxx"
#include "Polyhedra/Loops.cxx"
#include "Support/Iterators.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Optional.cxx"
#include "Utilities/Valid.cxx"
#else
export module HeuristicOptimizer;
import Arena;
import Array;
import ArrayConstructors;
import Invariant;
import IR;
import Legality;
import ListIterator;
import NormalForm;
import Optional;
import Pair;
import Trie;
import Valid;
#endif

using math::MutPtrVector, alloc::Arena;

/// Addr::operator->(const poly::Dependencies& deps)
/// drop `this` from the graph, and remove it from `deps`
inline void drop(IR::Addr *dropped, poly::Dependencies &deps,
                 MutPtrVector<int32_t> loop_deps, IR::Addr *replacement,
                 math::ResizeableView<int32_t, math::Length<>> &removed) {
  utils::invariant(dropped != replacement);
  // NOTE: dropped doesn't get removed from the `origAddr` list/the addrChain
  if (IR::Loop *L = dropped->getLoop(); L->getChild() == dropped)
    L->setChild(dropped->getNext());
  (void)dropped->removeFromList();
  bool rstow = replacement->isStore();
  //    0  1  2  3
  // [ -1, 3, 1, 0]
  // list: 2, 1, 3, 0
  // when `id = 1`, next is set to `3`
  // First, update all already `removed`
  for (ptrdiff_t i = removed.size(); i--;) {
    int32_t id = removed[i];
    if (deps.output(id) == dropped) {
      if (deps.input(id) == replacement) {
        removed.erase_swap_last(i);
        IR::removeEdge(loop_deps, id);
      } else deps.output(id) = replacement;
    } else if (deps.input(id) == dropped) {
      if (deps.output(id) == replacement) {
        removed.erase_swap_last(i);
        IR::removeEdge(loop_deps, id);
      } else deps.input(id) = replacement;
    }
  }
  for (int32_t id : deps.inputEdgeIDs(dropped)) {
    utils::invariant(deps.output(id) == dropped);
    IR::Addr *in = deps.input(id);
    deps.removeEdge(id, in, nullptr);
    if ((in != replacement) && (rstow || in->isStore())) {
      deps.output(id) = replacement;
      removed.push_back_within_capacity(id);
      // if (std::ranges::find_if(removed, [=](int32_t x) -> bool {
      //       return x == id;
      //     }) == removed.end())
      //   removed.push_back_within_capacity(id);
    } else {
      IR::removeEdge(loop_deps, id);
      // int32_t old_first = replacement->getEdgeIn();
      // if (old_first >= 0) deps[old_first].prevIn() = id;
      // deps[id].prevIn() = -1;
      // deps[id].nextIn() = old_first;
      // replacement->setEdgeIn(id);
    }
  }
  for (int32_t id : deps.outputEdgeIDs(dropped)) {
    utils::invariant(deps.input(id) == dropped);
    IR::Addr *out = deps.output(id);
    deps.removeEdge(id, nullptr, out);
    if ((out != replacement) && (rstow || out->isStore())) {
      deps.input(id) = replacement;
      removed.push_back_within_capacity(id);
      // if (std::ranges::find_if(removed, [=](int32_t x) -> bool {
      //       return x == id;
      //     }) == removed.end())
      //   removed.push_back_within_capacity(id);
    } else {
      IR::removeEdge(loop_deps, id);
      // // we need to maintain sorting of replacement->outputEdgeIDs()
      // // dropped was an edge dropped -id-> x
      // // we're updating it to be replacement -id-> x
      // // we require that `replacement->outputEdgeIDs()` be top-sorted
      // // thus, we must replace `id` such that `out` is at the correct place.
      // // dropped->outputs = [ ,
      // // replacement->outputs = [
      // int32_t old_first = replacement->getEdgeOut();
      // if (old_first >= 0) deps[old_first].prevOut() = id;
      // deps[id].prevOut() = -1;
      // deps[id].nextOut() = old_first;
      // replacement->setEdgeOut(id);
    }
  }
}

/// Addr::operator->(const poly::Dependencies& deps)
/// drop `dropped` from the graph, and remove it from `deps`
inline void drop(IR::Addr *A, poly::Dependencies &deps,
                 MutPtrVector<int32_t> loop_deps) {
  // NOTE: this doesn't get removed from the `origAddr` list/the addrChain
  if (IR::Loop *L = A->getLoop(); L->getChild() == A) L->setChild(A->getNext());
  (void)A->removeFromList();
  for (int32_t id : deps.inputEdgeIDs(A)) {
    utils::invariant(deps.output(id) == A);
    deps.removeEdge(id, deps.input(id), nullptr);
    IR::removeEdge(loop_deps, id);
  }
  for (int32_t id : deps.outputEdgeIDs(A)) {
    utils::invariant(deps.input(id) == A);
    deps.removeEdge(id, nullptr, deps.output(id));
    IR::removeEdge(loop_deps, id);
  }
}

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif

// returns a pair of `operands, reassociable` if `I`
// is a `Compute` or `Phi`.
// In the case of `Phi`, it only returns the first operand.
constexpr auto getCompOrPhiOperands(IR::Instruction *I)
  -> Pair<MutPtrVector<IR::Value *>, uint32_t> {
  if (auto *C = llvm::dyn_cast<IR::Compute>(I))
    return {.first = C->getOperands(), .second = C->reassociableArgs()};
  if (auto *P = llvm::dyn_cast<IR::Phi>(I))
    return {.first = P->getOperands()[_(0, 1)], .second = 1};
  return {.first = {nullptr, math::length(0)}, .second = 0};
}
inline auto dynCastCompOrPhi(IR::Value *v) -> IR::Instruction * {
  if (llvm::isa<Compute>(v) || llvm::isa<Phi>(v))
    return llvm::cast<IR::Instruction>(v);
  return nullptr;
}
inline auto findComp(Value *src, Instruction *dst) -> bool;
// NOLINTNEXTLINE misc-no-recursion
inline auto find(Value *src, Value *op) {
  auto *c = dynCastCompOrPhi(op);
  return c && findComp(src, c);
}

/// Defined here, because we're using `Compute`
// NOLINTNEXTLINE misc-no-recursion
inline auto findComp(Value *src, Instruction *dst) -> bool {
  MutPtrVector<IR::Value *> ops = getCompOrPhiOperands(dst).first;
  return std::ranges::any_of(ops, [=](Value *op) -> bool {
    if (op != src && !find(src, op)) return false;
    op->linkReductionDst(dst);
    return true;
  });
}

/// from dst, search through operands for `src`
/// TODO: accumulate latency as we go!
/// Maybe store visited, to avoid potentially revisiting?
// NOLINTNEXTLINE misc-no-recursion
constexpr auto findThroughReassociable(Value *src,
                                       Instruction *dst) -> unsigned {
  auto [ops, reassociable] = getCompOrPhiOperands(dst);
  // foundflag&1 == found reassociable
  // foundflag&2 == found non-reassociable
  unsigned foundflag = 0;
  for (Value *op : ops) {
    IR::Instruction *c = dynCastCompOrPhi(op);
    bool found{false};
    if (reassociable & 1) {
      if (op == src) {
        foundflag |= 1;
        found = true;
      } else if (c) {
        unsigned f = findThroughReassociable(src, c);
        if (!f) continue;
        foundflag |= f;
        found = true;
      }
    } else if ((op == src) || (c && findComp(src, c))) {
      found = true;
      foundflag = 0x2;
    }
    if (found) llvm::cast<Instruction>(op)->linkReductionDst(dst);
    if (foundflag & 2) return 0x2;
    reassociable >>= 1;
  }
  return foundflag;
}
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
///
/// In a reduction, `in` must be a load and `out` a store
/// This should only be called once, between nearest load/store pair
/// as it doesn't store detecting invalidity.
/// It checks for invalidity, in which case it doesn't set the reassociable
/// reduction.
constexpr void maybeReassociableReduction(IR::Phi *P) {
  // we only run for `isJoinPhi()`, searching up
  if (P->isAccumPhi()) return;
  // we should have a store whose first output edge is the load for
  // the following iteration. This iter is the reverse-time edge.
  auto [src, dst] = P->getOpArray();
  auto *C = dynCastCompOrPhi(dst);
  if (!C) return;
  unsigned flag = findThroughReassociable(src, C);
  // NOTE: we indicate reassociable reduction by linking the phi back to dst
  if (flag == 1) P->linkReductionDst(C);
}

} // namespace IR
#ifdef USE_MODULE
export namespace CostModeling {
#else
namespace CostModeling {
#endif
using containers::Pair;
using poly::Dependence;
struct LoopDepSummary {
  // Has been hoisted out
  IR::Node *after_exit_{nullptr};
  // FIXME: the next two fields should be using `getNextAddr`/`origNext`
  // Must depend on a loop
  IR::Addr *indexed_by_loop_{nullptr};
  // For deferred processing, may or may not ultimately be hoistable
  IR::Addr *not_indexed_by_loop_{nullptr};
};
struct LoopIndependent {
  LoopDepSummary summary_;
  bool independent_;
};
/// LoopTree
/// A tree of loops, with an indexable vector of IR::Loop*s, to facilitate
/// construction of the IR::Loop graph, from the fusion omegas
class LoopTree {
  template <typename T> using Vec = math::ResizeableView<T, math::Length<>>;
  // The root of this subtree
  utils::Valid<IR::Loop> loop_;
  Vec<LoopTree *> children_;
  int depth_{0};
  // We do not need to know the previous loop, as dependencies between
  // the `Addr`s and instructions will determine the ordering.
  auto index(lp::ScheduledNode *node) const -> const LoopTree * {
    const LoopTree *L = this;
    for (ptrdiff_t d = depth_, D = node->getNumLoops(); d < D; ++d)
      L = L->children_[node->getFusionOmega(depth_)];
    return L;
  }
  auto index(lp::ScheduledNode *node) -> LoopTree * {
    LoopTree *L = this;
    for (ptrdiff_t d = depth_, D = node->getNumLoops(); d < D; ++d)
      L = L->children_[node->getFusionOmega(depth_)];
    return L;
  }
  static auto notAfterExit(IR::Node *N, LoopDepSummary summary,
                           int depth1) -> LoopIndependent {
    N->removeFromList();
    N->setUsedByInner();
    if (auto *A = llvm::dyn_cast<IR::Addr>(N)) {
      if (A->checkDependsOnLoop(depth1 - 1))
        summary.indexed_by_loop_ =
          llvm::cast<IR::Addr>(A->setNext(summary.indexed_by_loop_));
      else
        summary.not_indexed_by_loop_ =
          llvm::cast<IR::Addr>(A->setNext(summary.not_indexed_by_loop_));
    }
    return {summary, false};
  }
  /// Called on all `Addr`s (and recursively called on their users).
  /// The objective is to categorize all `Addr` into `LoopDepSummary`'s
  /// fields's. That is, we want to
  /// 1. find all `Node`s that can be placed `afterExit`, i.e. hoisted after the
  /// loop.
  /// 2. categorize the remaining `Addr` into those indexed by the loop, and
  /// those that are not indexed by the loop.
  ///
  ///
  ///
  /// We set `visit0(depth)`; for those added to `afterExit`, we additionally
  /// set `visit1(depth)`.
  ///
  ///
  ///
  ///
  ///
  /// Searches `N` and it's users for loop-independent users, and returns them
  /// as a list to process.
  /// This exits early if it finds a dependent user, meaning it will only
  /// return a partial list in this case. We search the entire graph eventually,
  /// meaning the remainder will be processed later.
  /// We return a `LoopDepSummary, bool` pair, where the `bool` is true if `N`
  /// was loop independent. We use the `bool` rather than a `nullptr` or
  /// optional so that we can still return those results we did find on failure.
  ///
  /// Note that we assume `IR::Node *N` is contained in an `Addr*` chain
  /// that it is safe to remove it from. For this reason, the iterators we
  /// define actually get and set `next` immediately.
  ///
  /// Additionally, we set `visit(depth)` for those added to `afterExit`
  ///
  static auto searchLoopIndependentUsers( //  NOLINT(misc-no-recursion)
    poly::Dependencies &deps, IR::Loop *L, IR::Node *N, int depth1,
    LoopDepSummary summary, IR::Node **S) -> LoopIndependent {
    // we do loop related checks eagerly, rather than caching
    if (auto *O = llvm::dyn_cast<IR::Loop>(N))
      return {summary, (L != O) && !L->contains(O)};
    // We move from inside->outside
    // Thus, if `N` wasn't hoisted out of an interior loop already, it must
    // depend on that interior loop, and thus necessarily `L` as well.
    // Alternatively, if `N` isn't nested inside `L`, then it doesn't depend on
    // it, and we don't sink it!
    if (IR::Loop *P = N->getLoop(); P && P != L)
      return {summary, !(P && L->contains(P))};
    if (N->visited0(depth1)) return {summary, !N->checkUsedByInner()};
    N->visit0(depth1);
    if (N == *S) *S = N->getNext();
    if (N->checkDependsOnLoop(depth1 - 1))
      return notAfterExit(N, summary, depth1);
    auto *a = llvm::dyn_cast<IR::Addr>(N);
    if (a) {
      a->removeFromList();
      utils::invariant(!a->indexedByInnermostLoop());
      // it isn't indexed by the inner most loop;
      // either we leave it here, or (if possible) move it into `afterExit`
      summary.not_indexed_by_loop_ =
        llvm::cast<IR::Addr>(a->setNext(summary.not_indexed_by_loop_));
      // TODO: does this catch all instances?
      for (poly::Dependence d : deps.outputEdges(a))
        if (d.checkRegisterEligible()) continue;
      // NOTE: this was changed from depth1 to depth1-1 without testing
      for (IR::Addr *m : deps.unhoistableOutputs(a, depth1 - 1)) {
        auto [s, i] =
          searchLoopIndependentUsers(deps, L, m, depth1, summary, S);
        summary = s;
        if (i) continue;
        a->setUsedByInner();
        return {summary, false};
      }
    }
    // if it isn't a Loop or Addr, must be an `Instruction`
    // because we call this only on `Addr`s and their users.
    auto *I = llvm::cast<IR::Instruction>(N);
    for (IR::Node *U : I->getUsers()) {
      auto [s, i] = searchLoopIndependentUsers(deps, L, U, depth1, summary, S);
      summary = s;
      if (i) continue;
      I->setUsedByInner();
      return {summary, false};
    }
    // we are pusing `N` to the front of `afterExit`
    // if it is currently at the front of `notIndexedByLoop`,
    // `removeFromList()` won't remove it from `notIndexedByLoop`,
    // so we check here and do so manually.
    if (a && (summary.not_indexed_by_loop_ == a))
      summary.not_indexed_by_loop_ = llvm::cast_or_null<IR::Addr>(a->getNext());
    I->removeFromList();
    summary.after_exit_ = I->setNext(summary.after_exit_);
    I->visit1(depth1);
    return {summary, true};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto visitUsers(poly::Dependencies &deps, IR::Loop *L, IR::Node *N,
                         int depth1, IR::Node *body, IR::Node **E, IR::Loop *R,
                         IR::Cache *inst) -> IR::Node * {
    if (auto *SL = llvm::dyn_cast<IR::Loop>(N))
      for (IR::Node *C : SL->getChild()->nodes()) // subloops assumed non-empty
        body = visitUsers(deps, L, C, depth1, body, E, R, inst);
    // iterate over users
    else if (auto *A = llvm::dyn_cast<IR::Addr>(N))
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
      // we again have a cycle, from the load `C[m,n-1]` to the store `C[m,n]`,
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
      //       repeated anyway because of some other address operation, while
      //       that combination can't be moved to registers, e.g. because their
      //       index matrices are not equal. We need to distinguish between
      //       order within the loop, for the purpose of this topsort, and
      //       placement with respect to the loop. Simply, we perhaps should
      //       simply avoid hoisting when we carry a dependence that doesn't
      //       meet the criteria of `unhoistableOutputs`
      // - [ ] Incorporate the legality setting here?
      for (IR::Addr *m : deps.unhoistableOutputs(A, depth1 - 1))
        if (!m->visited1(depth1))
          body = visitLoopDependent(deps, L, m, depth1, body, E, R, inst);
    if (auto *I = llvm::dyn_cast<IR::Instruction>(N))
      for (IR::Node *U : I->getUsers())
        if (!U->visited1(depth1))
          body = visitLoopDependent(deps, L, U, depth1, body, E, R, inst);
    return body;
  }
  /// `R`: remove from loop, if not `nullptr`, set the parent of `N` to `R`
  /// `R` is applied recursively, forwarded to all calls.
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto visitLoopDependent(poly::Dependencies &deps, IR::Loop *L,
                                 IR::Node *N, int depth1, IR::Node *body,
                                 IR::Node **E, IR::Loop *R,
                                 IR::Cache *inst) -> IR::Node * {
    utils::invariant(N->getVisitDepth1() != 254);
    // N may have been visited as a dependent of an inner loop, which is why
    // `visited` accepts a depth argument
    bool direct_nest = N->getLoop() == nullptr || N->getLoop() == L;
    N = direct_nest ? N : L->getSubloop(N);
    if (!N || N->visited1(depth1)) return body;
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
    // by setting `N->visit(depth)` here, so `visited` will allow them to
    // immediately return. But, in debug mode, we'll set nodes of the same depth
    // to `254` to check for cycles.
    if (!llvm::isa<IR::Loop>(N)) N->visit1(254);
    else N->visit1(depth1);
#else
    N->visit1(depth);
#endif
    body = visitUsers(deps, L, N, depth1, body, E, R, inst);
#ifndef NDEBUG
    if (!llvm::isa<IR::Loop>(N)) N->visit1(depth1);
#endif
    if (N == *E) *E = N->getNext();
    body = N->removeFromList()->setNext(body);
    if (R) {
      // this is where code gets hoisted out in front
      N->hoist(R, depth1 - 1, L);
      if (auto *A = llvm::dyn_cast<IR::Addr>(N)) {
        A->hoistedInFront();
        if (A->isLoad()) {
          for (auto d : deps.outputEdges(A, depth1 - 1)) {
            if (!d.isRegisterEligible()) continue;
            auto *B = d.output();
            if (!llvm::isa<IR::Instruction>(B->getStoredVal())) continue;
            utils::invariant(B->isStore()); // deps have at least 1 store
            inst->createPhiPair(A, B, L);
          }
        }
      }
    } else N->setParentLoop(L);
    return body;
  }
  static void setSubLoops(IR::Loop *L) {
    IR::Loop *S = nullptr;
    for (IR::Node *N = L->getLast(); N; N = N->getPrev())
      if (auto *R = llvm::dyn_cast<IR::Loop>(N)) S = R;
      else N->setSubLoop(S);
  }
  static void addBody(poly::Dependencies &deps, IR::Loop *L, int depth,
                      IR::Node *nodes) {
    IR::Exit exit{}; // use to capture last node
    IR::Node *body{&exit};
    for (IR::Node *N = nodes, *E; N; N = E) {
      E = N->getNext();
      body = visitLoopDependent(deps, L, N, depth, body, &E, nullptr, nullptr);
    }
    utils::invariant(body->getPrev() == nullptr);
    // body->setPrev(nullptr);
    if (body != &exit) body = L->setChild(body); // now we can place the loop
    IR::Node *last = exit.getPrev();
    if (last) last->setNext(nullptr);
    L->setLast(last);
  }
  static constexpr auto initialAfterExit(IR::Loop *L,
                                         IR::Loop *P) -> IR::Node * {
    if (!P) return nullptr; // L was toplevel
    // Aside from `L` being top level, order isn't so important at the moment,
    // because it'll get top sorted as we recurse out.
    // Thus, the initial set of `Addr` stored in `getChild()` being wrong
    // isn't an issue.
    IR::Node *C = P->getChild();
    return C != L ? C : nullptr;
  }
  static void topologicalSort(poly::Dependencies &deps, IR::Loop *L, int depth1,
                              IR::Cache &inst) {
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
    // Currently, looks like (except `P->getChild` points directly to `append`)
    // P
    // \-> [L, append->nodes()]
    //      \-> C->nodes()
    IR::Loop *P = L->getLoop();
    // FIXME: initialAfterExit returning wrong result?
    IR::Node *C = L->getChild(), *append = initialAfterExit(L, P);
    LoopDepSummary summary{append};
    // Now, this loop may also have children that are hoistable
    // How can we iterate over the current children, `C->nodes()`?
    // We don't want to invalidate our iteration.
    // `searchLoopIndependentUsers` receives an `&next` arg; if we visit `next`
    // because it happens to be a user, we update it. This should also mean we
    // don't need `origNext`, and can have a single loop.
    for (IR::Node *B = C, *N; B; B = N) {
      N = B->getNext();
      summary =
        searchLoopIndependentUsers(deps, L, B, depth1, summary, &N).summary_;
    }
    // for (IR::Node *N : C->nodes())
    //   summary = searchLoopIndependentUsers(deps, L, N, depth,
    //   summary).summary;
    // summary.afterExit will be hoisted out; every member has been marked as
    // `visited` So, now we search all of root's users, i.e. every addr that
    // depends on it
    auto [afterExit, indexedByLoop, notIndexedByLoop] = summary;
    L->setNext(afterExit);
    if (afterExit != append) {
      IR::Loop *S = append ? append->getSubLoop() : nullptr;
      for (IR::Node *N = afterExit; N != append; N = N->getNext()) {
        N->hoist(P, depth1 - 1, S);
        if (IR::Addr *A = llvm::dyn_cast<Addr>(N)) A->hoistedBehind();
      }
    }
    addBody(deps, L, depth1, indexedByLoop);
    setSubLoops(L);
    IR::Node *body{L};
    // Now, anything that wasn't already visited in `addBody` is legal
    // to hoist out in front.
    for (IR::Node *N = notIndexedByLoop, *E; N; N = E) {
      utils::invariant(N->getNaturalDepth() < depth1);
      E = N->getNext();
      body = visitLoopDependent(deps, L, N, depth1, body, &E, P, &inst);
    }
    // The order should be
    // P
    // \-> [hoisted in front, L, afterExit, append]
    //                         \-> loop's contents
    P->setChild(body);
  }

  static auto root(Arena<> *salloc, Arena<> *lalloc) -> LoopTree * {
    return salloc->create<LoopTree>(lalloc);
  }
  void addLeaf(Arena<> salloc, Arena<> *lalloc, lp::ScheduledNode *node,
               poly::Dependencies &deps, MutPtrVector<int32_t> loopDeps) {
    // Then it belongs here, and we add loop's dependencies.
    // We only need to add deps to support SCC/top sort now.
    // We also apply the rotation here.
    // For dependencies in SCC iteration, only indvar deps get iterated.
    auto [Pinv, denom] = math::NormalForm::scaledInv(&salloc, node->getPhi());
    // FIXME: what if this loop alrady has a `::poly::Loop`?
    // Check to ensure compatibility? Make a list of them?
    // Also add it to `Addr`? We need to consider the case of loop fusion,
    // where loop bounds may not precisely correspond to one another.
    // What info will code gen's legalization need?
    utils::Valid<poly::Loop> explicit_loop =
      node->getLoopNest()->rotate(lalloc, Pinv, node->getOffset());
    IR::Addr *chain{llvm::cast_or_null<IR::Addr>(loop_->getChild())};
    for (IR::Addr *m : node->localAddr()) {
      m->rotate(salloc, explicit_loop, Pinv, denom, node->getOffsetOmega(),
                node->getOffset());
      m->setChild(nullptr);
      chain = llvm::cast<IR::Addr>(m->removeFromList()->setNext(chain));
      m->setParentLoop(loop_);
      for (int32_t id : deps.inputEdgeIDs(m)) {
        // FIXME: why do we have `satLevel() == 0`?
        // Seems like we're mixing `depth0` and `depth1`s.
        uint8_t lvl = deps[id].satLevel() >> 1;
        loop_->getLoopAtDepth(lvl + 1)->addEdge(loopDeps, id);
      }
    }
    loop_->setChild(chain);
    loop_->setAffineLoop(explicit_loop);
  }
  /// salloc: Short lived allocator, for the indexable `Vec`s
  /// Longer lived allocator, for the IR::Loop nodes
  /// We don't need `origNext` anymore, so that gets reset here.
  // NOLINTNEXTLINE(misc-no-recursion)
  void addNode(Arena<> *salloc, Arena<> *lalloc, lp::ScheduledNode *node,
               poly::Dependencies &deps, MutPtrVector<int32_t> loopDeps) {
    // FIXME: need to `setChild` for all instructions to point to
    // the following `IR::Loop`
    if (node->getNumLoops() == depth_) {
      addLeaf(*salloc, lalloc, node, deps, loopDeps);
      return;
    }
    // we need to find the sub-loop tree to which we add `node`
    ptrdiff_t idx = node->getFusionOmega(depth_);
    utils::invariant(idx >= 0);
    if (ptrdiff_t num_children = children_.size(); idx >= num_children) {
      if (idx >= children_.getCapacity())
        children_.reserve(salloc, 2 * (idx + 1));
      // allocate new nodes and resize
      children_.resize(idx + 1);
      for (ptrdiff_t i = num_children; i < idx + 1; ++i) children_[i] = nullptr;
    }
    auto *C = children_[idx];
    if (!C) children_[idx] = C = salloc->create<LoopTree>(lalloc, this);
    C->addNode(salloc, lalloc, node, deps, loopDeps);
  }
  constexpr auto subLoops() -> Vec<LoopTree *> { return children_; }
  constexpr auto getLoop() -> IR::Loop * { return loop_; }
  [[nodiscard]] constexpr auto getDepth() const -> int {
    utils::invariant(depth_ >= 0);
    return depth_;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void buildSubGraph(poly::Dependencies &deps, IR::Cache &inst) {
    // We build the instruction graph, via traversing the tree, and then
    // top sorting as we recurse out
    for (LoopTree *child : subLoops() | std::views::reverse)
      child->buildSubGraph(deps, inst);
    // The very outer `root` needs to have all instr constituents
    // we also need to add the last instruction of each loop as `last`
    topologicalSort(deps, loop_, getDepth(), inst);
  }

public:
  constexpr LoopTree(Arena<> *lalloc) : loop_{lalloc->create<IR::Loop>(0)} {}
  constexpr LoopTree(Arena<> *lalloc, LoopTree *parent_)
    : loop_{lalloc->create<IR::Loop>(parent_->depth_ + 1)},
      depth_(parent_->depth_ + 1) {
    // allocate the root node, and connect it to parent's node, as well as
    // previous loop of the same level.
    // We do not yet set parent_->loop->child = loop
    loop_->setParentLoop(parent_->loop_);
  }
  static auto buildGraph(Arena<> salloc, IR::Cache &inst,
                         poly::Dependencies &deps, lp::ScheduledNode *nodes)
    -> Pair<IR::Loop *, MutPtrVector<int32_t>> {
    Arena<> *lalloc = inst.getAllocator();
    MutPtrVector<int32_t> loop_deps{math::vector<int32_t>(lalloc, deps.size())};
    LoopTree *root = LoopTree::root(&salloc, lalloc);
    for (lp::ScheduledNode *node : nodes->getAllVertices())
      root->addNode(&salloc, lalloc, node, deps, loop_deps);
    // We build the instruction graph, via traversing the tree, and then
    // top sorting as we recurse out
    for (LoopTree *child : root->subLoops()) child->buildSubGraph(deps, inst);

    // The very outer `root` needs to have all instr constituents
    // we also need to add the last instruction of each loop as `last`
    IR::Loop *toplevel = root->getLoop();
    addBody(deps, toplevel, 0, toplevel->getChild());
    toplevel->setAffineLoop();
    return {toplevel, loop_deps};
  }
};

// NOLINTNEXTLINE(misc-no-recursion)
inline auto hasFutureReadsCore(dict::InlineTrie<llvm::BasicBlock *> &successors,
                               llvm::Instruction *I) -> bool {
  for (auto *U : I->users()) {
    auto *UI = llvm::dyn_cast<llvm::Instruction>(U);
    if (!UI) continue;
    if (UI->mayReadFromMemory() && successors[UI->getParent()]) return true;
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
  dict::InlineTrie<llvm::BasicBlock *> successors{};
  for (llvm::BasicBlock *S : llvm::successors(I->getParent()))
    if (!LBBs.count(S)) successors.insert(alloc, S);
  return hasFutureReadsCore(successors, I);
}

struct LoopDepSatisfaction {
  poly::Dependencies &deps_;
  MutPtrVector<int32_t> loop_deps_;

  constexpr auto dependencyIDs(IR::Loop *L) -> utils::VForwardRange {
    return {loop_deps_.begin(), L->getEdge()};
  }
  constexpr auto dependencies(IR::Loop *L) {
    return dependencyIDs(L) | deps_.getEdgeTransform();
  }
  constexpr auto legality(IR::Loop *L) -> Legality {
    Legality l{};
    for (int32_t did : dependencyIDs(L))
      if (!updateLegality(&l, L, did)) break;
    return l;
  }
  inline void setLoopLegality(IR::Loop *L) {
    CostModeling::Legality legal;
    for (int32_t did : dependencyIDs(L))
      if (!updateLegality(&legal, L, did)) break;
    // check following BB for Phi
    for (auto *P = llvm::dyn_cast_or_null<IR::Phi>(L->getNext()); P;
         P = llvm::dyn_cast_or_null<IR::Phi>(P->getNext())) {
      if (!P->isReassociable()) {
        ++legal.ordered_reduction_count_;
        // FIXME: can we check if the dep that produced this was peelable?
        // deps.determinePeelDepth ?
        legal.reorderable_ = false;
      } else ++legal.unordered_reduction_count_;
    }
    L->setLegality(legal);
  }

private:
  auto updateLegality(Legality *l, IR::Loop *L, int32_t did) -> bool {
    // we're assuming we break and stop updating once !reorderable
    utils::invariant(l->reorderable_);
    // note: the dependence hasn't been rotated
    Dependence d{deps_[did]};
    if (d.satLevel() & 1) return true;
    utils::Optional<size_t> peel = deps_.determinePeelDepth(L, did);
    if (peel) l->peel_flag_ |= (1 << (*peel));
    return (l->reorderable_ = peel.hasValue());
  }
};
class IROptimizer {
  poly::Dependencies &deps_;
  IR::Cache &instructions_;
  dict::set<llvm::BasicBlock *> &lbbs_;
  dict::set<llvm::CallBase *> &erase_candidates_;
  IR::Loop *root_;
  /// `loopDepSats` places the dependencies at the correct loop level so that
  /// we can more easily check all dependencies carried by a particular loop.
  /// We use these for checks w/ respect to unrolling and vectorization
  /// legality.
  /// The returned vector is an integer vector, giving a mapping of loops
  /// to depencencies handled at that level.
  /// We can use these dependencies for searching reductions for
  /// trying to prove legality.
  MutPtrVector<int32_t> loop_deps_;
  Arena<> *lalloc_;
  llvm::TargetLibraryInfo *tli_;
  int loop_count_;

  // we eliminate temporaries that meet these conditions:
  // 1. are only ever stored to (this can be achieved via
  // load-elimination/stored-val forwarding in `removeRedundantAddr`)
  // 2. are non-escaping, i.e. `llvm::isNonEscapingLocalObject`
  // 3. returned by `llvm::isRemovableAlloc`
  auto eliminateTemporaries(IR::AddrChain addr) -> unsigned {
    auto s = lalloc_->scope();
    unsigned remaining = 0;
    for (IR::Addr *a : addr.getAddr()) {
      if (a->isDropped()) continue;
      ++remaining;
      if (a->isLoad()) continue;
      IR::Value *ptr = a->getArrayPointer();
      auto *cv = llvm::dyn_cast<IR::CVal>(ptr);
      if (!cv) continue;
      auto *call = llvm::dyn_cast<llvm::CallBase>(cv->getVal());
      if (!call) continue;
      if (!llvm::isNonEscapingLocalObject(call, nullptr)) continue;
      if (!llvm::isRemovableAlloc(call, tli_)) continue;
      if (hasFutureReads(lalloc_, lbbs_, call)) continue;
      drop(a, deps_, loop_deps_);
      // we later check if any uses remain other than the associated free
      // if not, we can delete them.
      // We may want to go ahead and do this here. We don't for now,
      // because we have live `llvm::Instruction`s that we haven't removed
      // yet.
      // TODO: revisit when handling code generation (and deleting old code)
      erase_candidates_.insert(call);
      --remaining;
    }
    return remaining;
  }

  // this compares `a` with each of its active outputs.
  auto eliminateAddr(IR::Addr *a,
                     math::ResizeableView<int32_t, math::Length<>> removed)
    -> math::ResizeableView<int32_t, math::Length<>> {
    for (int32_t id : deps_.outputEdgeIDs(a, a->getCurrentDepth() - 1)) {
      IR::Addr *b = deps_[id].output();
      if (b->wasDropped()) continue;
      // TODO: also check loop extants
      if (a->indexMatrix() != b->indexMatrix() ||
          a->getOffsetOmega() != b->getOffsetOmega())
        break;
      if (a->isStore()) {
        // On a Write->Write, we remove the first write.
        if (b->isStore()) {
          a->getStoredVal()->getUsers().remove(a);
          drop(a, deps_, loop_deps_, b, removed);
          break;
        }
        // Write->Load, we will remove the load if it's in the same block as the
        // write, and we can forward the stored value.
        if (a->getLoop() != b->getLoop()) break;
        instructions_.replaceAllUsesWith(b, a->getStoredVal());
        drop(b, deps_, loop_deps_, a, removed);
      } else if (b->isLoad()) { // Read->Read
        // If they're not in the same loop, we need to reload anyway
        if (a->getLoop() != b->getLoop()) break;
        // If they're in the same loop, we can delete the second read
        instructions_.replaceAllUsesWith(b, a);
        drop(b, deps_, loop_deps_, a, removed);
      } else break; // Read->Write, can't delete either
    }
    return removed;
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
  auto
  removeRedundantAddr(IR::AddrChain addr,
                      math::ResizeableView<int32_t, math::Length<>> removed)
    -> math::ResizeableView<int32_t, math::Length<>> {
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
    for (IR::Addr *a : addr.getAddr()) removed = eliminateAddr(a, removed);
    return removed;
  }
  // this compares `a` with each of its active outputs.
  void eliminateAddr(IR::Addr *a) {
    for (int32_t id : deps_.outputEdgeIDs(a, a->getCurrentDepth() - 1)) {
      IR::Addr *b = deps_[id].output();
      if (b->wasDropped()) continue;
      // TODO: also check loop extants
      if (a->indexMatrix() != b->indexMatrix() ||
          a->getOffsetOmega() != b->getOffsetOmega())
        break;
      if (a->isStore()) {
        // On a Write->Write, we remove the first write.
        if (b->isStore()) {
          b->mergeHoistFlag(a); // keep b
          a->getStoredVal()->getUsers().remove(a);
          drop(a, deps_, loop_deps_);
          break;
        }
        // Write->Load, we will remove the load if it's in the same block as the
        // write, and we can forward the stored value.
        if (a->getLoop() != b->getLoop()) break;
        a->mergeHoistFlag(b); // keep a
        instructions_.replaceAllUsesWith(b, a->getStoredVal());
        drop(b, deps_, loop_deps_);
      } else if (b->isLoad()) { // Read->Read
        // If they're not in the same loop, we need to reload anyway
        if (a->getLoop() != b->getLoop()) break;
        // If they're in the same loop, we can delete the second read
        a->mergeHoistFlag(b); // keep a
        instructions_.replaceAllUsesWith(b, a);
        drop(b, deps_, loop_deps_);
      } else break; // Read->Write, can't delete either
    }
  }
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
  auto pruneAddr(IR::AddrChain addr) -> IR::AddrChain {
    sortEdges(root_, 0);
    removeRedundantAddr(addr);
    addr.removeDropped();
    return addr;
    // auto s = lalloc_->scope();
    // ptrdiff_t ndeps = deps_.size();
    // math::ResizeableView<int32_t, math::Length<>> removed{
    //   lalloc_, math::capacity(ndeps)};
    // for (;;) {
    //   sortEdges(root_, 0);
    //   removed = removeRedundantAddr(addr, removed);
    //   addr.removeDropped();
    //   if (removed.empty()) break;
    //   if (!deps_.insertDependencies(removed)) break;
    //   removed.clear();
    // }
    // return addr;
  }
  /// `sortEdges` sorts each `Addr`'s output edges
  /// So that each `Addr`'s output edges match the topological ordering of the
  /// outputs. The approach to sorting edges is to iterate through nodes
  /// backwards whenever we encounter an `Addr`, we push it to the front of each
  /// output edge list to which it belongs.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto sortEdges(IR::Loop *R, int32_t pos) -> int32_t {
    for (IR::Node *n = R->getLast(); n; n = n->getPrev()) {
      if (auto *L = llvm::dyn_cast<IR::Loop>(n)) {
        pos = sortEdges(L, pos);
        continue;
      }
      auto *a = llvm::dyn_cast<IR::Addr>(n);
      if (!a) continue;
      // TODO: shouldn't need this?
      a->setTopPosition(pos--);
      // for each input edge, we push `a` to the front of the output list
      for (int32_t id : deps_.inputEdgeIDs(a)) {
        if (deps_[id].prevOut() < 0) continue;
        deps_.removeOutEdge(id);
        IR::Addr *b = deps_[id].input();
        int32_t old_first = b->getEdgeOut();
        deps_[old_first].prevOut() = id;
        deps_[id].prevOut() = -1;
        deps_[id].nextOut() = old_first;
        b->setEdgeOut(id);
      }
    }
    return pos;
  }
  static constexpr auto inc1(std::array<int, 2> idx) -> std::array<int, 2> {
    return {idx[0], ++idx[1]};
  }
  // Post-simplification pass over the IR.
  // Sets topidx, blkidx, and also checks for reassociable reductions.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto setTopIdx(IR::Loop *root, std::array<int, 2> idx) -> std::array<int, 2> {
    for (IR::Node *N : root->getChild()->nodes())
      if (auto *I = llvm::dyn_cast<IR::Instruction>(N)) {
        idx = I->setPosition(idx);
        I->calcLoopMask();
        if (auto *P = llvm::dyn_cast<IR::Phi>(I)) maybeReassociableReduction(P);
      } else idx = inc1(setTopIdx(llvm::cast<IR::Loop>(N), inc1(idx)));
    return idx;
  }
  /// A loop's `getEdge` needs to be updated after possibly having
  /// removed it from `loop_deps`
  /// When we remove edges in `removeEdge`, we leave the value pointing
  /// to where it used to point, so we can simply follow that here to update.
  /// The `removeEdge` comment discusses how this implementaiton works.
  void dropDroppedDependencies(IR::Loop *L) {
    int32_t edge = L->getEdge();
    if (deps_.input(edge)->wasDropped() || deps_.output(edge)->wasDropped())
      L->setEdge(loop_deps_[edge]);
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto setLegality_(IR::Loop *L) -> int {
    dropDroppedDependencies(L);
    getLoopDeps().setLoopLegality(L);
    int cnt = 1;
    for (IR::Loop *SL : L->subLoops()) cnt += setLegality_(SL);
    return cnt;
  }
  auto setLegality(IR::Loop *root) -> int {
    int cnt = 0;
    for (IR::Loop *L : root->subLoops()) cnt += setLegality_(L);
    return cnt;
  }
  [[nodiscard]] constexpr auto getLoopDeps() const -> LoopDepSatisfaction {
    return {deps_, loop_deps_};
  }
  [[nodiscard]] constexpr auto getLoopCount() const -> int {
    return loop_count_;
  }

  IROptimizer(poly::Dependencies &deps, IR::Cache &instr,
              dict::set<llvm::BasicBlock *> &loopBBs,
              dict::set<llvm::CallBase *> &erase_candidates, IR::Loop *root,
              MutPtrVector<int32_t> loopDeps_, Arena<> *lalloc,
              lp::LoopBlock::OptimizationResult res)
    : deps_{deps}, instructions_{instr}, lbbs_{loopBBs},
      erase_candidates_{erase_candidates}, root_{root}, loop_deps_{loopDeps_},
      lalloc_{lalloc} {
    res.addr = pruneAddr(res.addr);
    eliminateTemporaries(res.addr); // returns numAddr
    setTopIdx(root_, {0, 0});
    loop_count_ = setLegality(root);
    /// TODO: legality check
    // plan now is to have a `BitArray` big enough to hold `numLoops` entries
    // and `numAddr` rows; final axis is contiguous vs non-contiguous
    // Additionally, we will have a vector of unroll strategies to consider
    // LoopDependencies *ld = LoopDependencies::create(lalloc_, numLoops,
    // numAddr);
  }

public:
  static auto optimize(Arena<> salloc, poly::Dependencies &deps,
                       IR::Cache &inst, dict::set<llvm::BasicBlock *> &loopBBs,
                       dict::set<llvm::CallBase *> &eraseCandidates,
                       lp::LoopBlock::OptimizationResult res)
    -> containers::Tuple<IR::Loop *, LoopDepSatisfaction, int> {
    auto [root, loopDeps] = LoopTree::buildGraph(salloc, inst, deps, res.nodes);
    IROptimizer opt(deps, inst, loopBBs, eraseCandidates, root, loopDeps,
                    &salloc, res);
    return {root, opt.getLoopDeps(), opt.getLoopCount()};
  }
};

} // namespace CostModeling
