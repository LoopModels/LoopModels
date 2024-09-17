#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <utility>

#ifndef USE_MODULE
#include "Dicts/Trie.cxx"
#include "Target/Machine.cxx"
#include "Containers/Pair.cxx"
#include "IR/IR.cxx"
#include "Utilities/Invariant.cxx"
#include "Containers/BitSets.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#else
export module ControlFlowMerging;
import Arena;
import Array;
import BitSet;
import Invariant;
import IR;
import Pair;
import TargetMachine;
import Trie;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
// merge all instructions from toMerge into merged
inline void merge(alloc::Arena<> *alloc,
                  dict::InlineTrie<Instruction *> *merged,
                  dict::InlineTrie<Instruction *> *toMerge) {
  toMerge->foreachk([=](Instruction *I) { merged->insert(alloc, I); });
}
class ReMapper {
  // we can map Values to Instructions (e.g. selects)
  dict::map<Instruction *, Instruction *> reMap;

public:
  auto operator[](Instruction *J) -> Instruction * {
    if (auto f = reMap.find(J); f != reMap.end()) return f->second;
    return J;
  }
  auto operator[](Value *J) -> Value * {
    if (auto *I = llvm::dyn_cast<Instruction>(J)) return (*this)[I];
    return J;
  }
  void remapFromTo(Instruction *K, Instruction *J) { reMap[K] = J; }
};

// represents the cost of merging key=>values; cost is hopefully negative.
// cost is measured in reciprocal throughput
struct MergingCost {
  // mergeMap can be thought of as containing doubly linked lists/cycles,
  // e.g. a -> b -> c -> a, where a, b, c are instructions.
  // if we merge c and d, we have
  // c -> a => c -> d
  // d      => d -> a
  // yielding: c -> d -> a -> b -> c
  // if instead we're merging c and d, but d is also paired d <-> e, then
  // c -> a => c -> e
  // d -> e => d -> a
  // yielding c -> e -> d -> a -> b -> c
  // that is, if we're fusing c and d, we can make each point toward
  // what the other one was pointing to, in order to link the chains.
  dict::InlineTrie<Instruction *, Instruction *> mergeMap;
  math::ResizeableView<containers::Pair<Instruction *, Instruction *>,
                       math::Length<>>
    mergeList;
  dict::InlineTrie<Instruction *, dict::InlineTrie<Instruction *> *>
    ancestorMap;
  llvm::InstructionCost cost;

  using CostKind = Instruction::CostKind;

  auto getAncestors(Value *op) -> dict::InlineTrie<Instruction *> * {
    if (auto *I = llvm::dyn_cast<Instruction>(op))
      if (auto f = ancestorMap.find(I)) return *f;
    return nullptr;
  }
  auto setAncestors(Arena<> *alloc, Value *op,
                    dict::InlineTrie<Instruction *> *ancestors) {
    if (auto *I = llvm::dyn_cast<Instruction>(op))
      ancestorMap[alloc, I] = ancestors;
  }
  /// returns `true` if `key` was already in ancestors
  /// returns `false` if it had to initialize
  // NOLINTNEXTLINE(misc-no-recursion)
  auto initAncestors(Arena<> *alloc,
                     Instruction *key) -> dict::InlineTrie<Instruction *> * {

    auto *set = alloc->construct<dict::InlineTrie<Instruction *>>();
    /// instructions are considered their own ancestor for our purposes
    set->insert(alloc, key);
    ancestorMap[alloc, key] = set;
    for (Value *op : getOperands(key)) {
      if (auto *I = llvm::dyn_cast<Compute>(op); I && I->isComplete()) {
        auto *A = getAncestors(alloc, I);
        set->merge(alloc, A);
      }
    }
    return set;
  }
  auto begin() -> decltype(mergeList.begin()) { return mergeList.begin(); }
  auto end() -> decltype(mergeList.end()) { return mergeList.end(); }
  [[nodiscard]] auto visited(Instruction *key) const -> bool {
    return ancestorMap.find(key).hasValue();
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getAncestors(Arena<> *alloc,
                    Instruction *I) -> dict::InlineTrie<Instruction *> * {
    auto *&f = ancestorMap[alloc, I];
    if (!f) f = initAncestors(alloc, I);
    return f;
  }
  auto getAncestors(Instruction *key) -> dict::InlineTrie<Instruction *> * {
    if (auto it = ancestorMap.find(key)) return *it;
    return nullptr;
  }
  auto findMerge(Instruction *key) -> Instruction * {
    if (auto it = mergeMap.find(key)) return *it;
    return nullptr;
  }
  auto findMerge(Instruction *key) const -> Instruction * {
    if (auto it = mergeMap.find(key)) return *it;
    return nullptr;
  }
  /// isMerged(Instruction *key) const -> bool
  /// returns true if `key` is merged with any other Instruction
  auto isMerged(Instruction *key) const -> bool {
    return mergeMap.find(key).hasValue();
  }
  /// isMerged(Instruction *I, Instruction *J) const -> bool
  /// returns true if `I` and `J` are merged with each other
  // note: this is not the same as `isMerged(I) && isMerged(J)`,
  // as `I` and `J` may be merged with different Instructions
  // however, isMerged(I, J) == isMerged(J, I)
  // so we ignore easily swappable parameters
  auto isMerged(Instruction *L, Instruction *J) const -> bool {
    Instruction *K = J;
    do {
      if (L == K) return true;
      K = findMerge(K);
    } while (K && K != J);
    return false;
  }
  auto isMerged(Value *L, Value *J) const -> bool {
    if (L == J) return true;
    if (auto *I = llvm::dyn_cast<Instruction>(L))
      if (auto *K = llvm::dyn_cast<Instruction>(J)) return isMerged(I, K);
    return false;
  }
  // follows the cycle, traversing H -> mergeMap[H] -> mergeMap[mergeMap[H]]
  // ... until it reaches E, updating the ancestorMap pointer at each level of
  // the recursion.
  void cycleUpdateMerged(Arena<> *alloc,
                         dict::InlineTrie<Instruction *> *ancestors,
                         Instruction *E, Instruction *H) {
    while (H != E) {
      setAncestors(alloc, H, ancestors);
      auto optH = mergeMap.find(H);
      invariant(optH.hasValue());
      H = *optH;
    }
  }
  static constexpr auto popBit(uint8_t x) -> containers::Pair<bool, uint8_t> {
    return {bool(x & 1), uint8_t(x >> 1)};
  }

  struct Allocate {
    Arena<> *alloc; // short term allocator
    IR::Cache &cache;
    ReMapper &reMap;
    dict::InlineTrie<Instruction *, Predicate::Set> &valToPred;
    UList<Value *> *predicates;
    MutPtrVector<Value *> operands;
  };
  struct Count {};

  struct SelectCounter {
    unsigned numSelects{0};
    constexpr explicit operator unsigned() const { return numSelects; }
    constexpr void select(size_t, Value *, Value *) { ++numSelects; }
  };
  struct SelectAllocator {
    Arena<> *alloc; // short term allocator
    IR::Cache &cache;
    ReMapper &reMap;
    MutPtrVector<Value *> operands;
    dict::InlineTrie<Instruction *, Predicate::Set> &valToPred;
    Predicate::Intersection pred;
    UList<Value *> *predicates;

    constexpr explicit operator unsigned() const { return 0; }
    void select(size_t i, Value *A, Value *B) {
      A = reMap[A];
      B = reMap[B];
      Compute *C = cache.createSelect(pred, A, B, predicates);
      Predicate::Set pS;
      // TODO: must `valToPred` contain `A` and `B` already?
      if (auto *I = llvm::dyn_cast<Instruction>(A))
        if (auto fp = valToPred.find(I)) pS.Union(alloc, *fp);
      if (auto *I = llvm::dyn_cast<Instruction>(B))
        if (auto fp = valToPred.find(I)) pS.Union(alloc, *fp);
      valToPred[alloc, C] = pS;
      operands[i] = C;
    }
  };

  static auto init(Allocate a, Instruction *A,
                   Instruction *B) -> SelectAllocator {
    Predicate::Intersection P =
      a.valToPred[a.alloc, A].getConflict(a.valToPred[a.alloc, B]);
    return SelectAllocator{a.alloc,     a.cache, a.reMap,     a.operands,
                           a.valToPred, P,       a.predicates};
  }
  static auto init(Count, Instruction *, Instruction *) -> SelectCounter {
    return SelectCounter{0};
  }
  // merge the operands of `A` and `B`
  // An abstraction that runs an algorithm to look for merging opportunities,
  // either counting the number of selects needed, or allocating selects
  // and returning the new operand vector.
  // We generally aim to have analysis/cost modeling and code generation
  // take the same code paths, to both avoid code duplication and to
  // make sure the cost modeling reflects the actual code we're generating.
  template <typename S>
  auto mergeOperands(Instruction *A, Instruction *B, S selects) {
    // TODO: does this update the predicates?
    // now, we need to check everything connected to O and I in the mergeMap
    // to see if any of them need to be updated.
    // TODO: we want to estimate select cost
    // worst case scenario is 1 select per operand (p is pred):
    // select(p, f(a,b), f(c,d)) => f(select(p, a, c), select(p, b, d))
    // but we can often do better, e.g. we may have
    // select(p, f(a,b), f(c,b)) => f(select(p, a, c), b)
    // additionally, we can check `commutativeOperandsFlag(I)`
    // select(p, f(a,b), f(c,a)) => f(a, select(p, b, c))
    // we need to figure out which operands we're merging with which,
    //
    // We need to give special consideration to the case where
    // arguments are merged, as this may be common when two
    // control flow branches have relatively similar pieces.
    // E.g., if b and c are already merged,
    // and if `f`'s ops are commutative, then we'd get
    // select(p, f(a,b), f(c,a)) => f(a, b)
    // so we need to check if any operand pairs are merged with each other.
    // note `isMerged(a,a) == true`, so that's the one query we need to use.
    auto selector = init(selects, A, B);
    MutPtrVector<Value *> operandsA = getOperands(A);
    MutPtrVector<Value *> operandsB = getOperands(B);
    ptrdiff_t numOperands = operandsA.size();
    assert(numOperands == operandsB.size());
    /// associate ops means `f(a, b) == f(b, a)`
    uint8_t commutativeOpsFlag = commutativeOperandsFlag(B);
    // For example,
    // we keep track of which operands we've already merged,
    // f(a, b), f(b, b)
    // we can't merge b twice!
    for (ptrdiff_t i = 0; i < numOperands; ++i) {
      auto *opA = getOperand(A, i);
      auto *opB = getOperand(B, i);
      auto [assoc, assocFlag] = popBit(commutativeOpsFlag);
      commutativeOpsFlag = assocFlag;
      if (opA == opB) continue;
      // if both operands were merged, we can ignore it's associativity
      if (isMerged(opB, opA)) {
        // we cast, because isMerged confirms they're Instructions, given
        // that opA != opB, which we checked above
        continue;
      }
      if (!((assoc) && (assocFlag))) {
        // this op isn't commutative with any remaining
        selector.select(i, opA, opB);
        continue;
      }
      // we look forward
      size_t j = i;
      bool merged = false;
      while (assocFlag) {
        auto shift = std::countr_zero(assocFlag);
        j += ++shift;
        assocFlag >>= shift;
        auto *opjA = getOperand(A, j);
        auto *opjB = getOperand(B, j);
        // if elements in these pairs weren't already used
        // to drop a select, and they're merged with each other
        // we'll use them now to drop a select.
        if (isMerged(opB, opjA)) {
          std::swap(operandsA[i], operandsA[j]);
          merged = true;
          break;
        }
        if (isMerged(opjB, opA)) {
          std::swap(operandsB[i], operandsB[j]);
          merged = true;
          break;
        }
      }
      // we couldn't find any candidates
      if (!merged) selector.select(i, opA, opB);
    }
    return unsigned(selector);
  }
  template <bool TTI>
  void merge(Arena<> *alloc, target::Machine<TTI> target,
             unsigned int vectorBits, Instruction *A, Instruction *B) {
    mergeList.emplace_backa(alloc, A, B);
    auto aA = ancestorMap.find(B);
    auto aB = ancestorMap.find(A);
    invariant(aA.hasValue());
    invariant(aB.hasValue());
    // in the old MergingCost where they're separate instructions,
    // we leave their ancestor PtrMaps intact.
    // in the new MergingCost where they're the same instruction,
    // we assign them the same ancestors.
    auto *merged = alloc->create<dict::InlineTrie<Instruction *>>();
    merged->merge(alloc, *aA);
    merged->merge(alloc, *aB);
    *aA = merged;
    *aB = merged;
    unsigned numSelects = mergeOperands(A, B, Count{});
    // TODO:
    // increase cost by numSelects, decrease cost by `I`'s cost
    unsigned int W = vectorBits / B->getNumScalarBits();
    if (numSelects)
      cost += numSelects * Operation::selectCost(target, B->getType(W));
    cost -=
      getCost(B, target, W,
              llvm::TargetTransformInfo::TargetCostKind::TCK_RecipThroughput);
    auto *mB = findMerge(B);
    if (mB) cycleUpdateMerged(alloc, merged, B, mB);
    // fuse the merge map cycles
    auto &mMA = mergeMap[alloc, A], &mMB = mergeMap[alloc, B];
    if (auto *mA = findMerge(A)) {
      cycleUpdateMerged(alloc, merged, A, mA);
      if (mB) {
        mMB = mA;
        mMA = mB;
      } else {
        mMB = mA;
        mMA = B;
      }
    } else if (mB) {
      mMA = mB;
      mMB = A;
    } else {
      mMB = A;
      mMA = B;
    }
  }
  auto operator<(const MergingCost &other) const -> bool {
    return cost < other.cost;
  }
  auto operator>(const MergingCost &other) const -> bool {
    return cost > other.cost;
  }

  void
  mergeInstructions(IR::Cache &cache, Arena<> *tAlloc, Instruction *A,
                    Instruction *B,
                    dict::InlineTrie<Instruction *, Predicate::Set> &valToPred,
                    ReMapper &reMap, UList<Value *> *pred) {
    A = reMap[A];
    B = reMap[B];
    if (A == B) return; // is this possible?
    invariant(getNumOperands(A), getNumOperands(B));
    // could be stores
    if (auto *C = llvm::dyn_cast<Compute>(A)) {
      Compute *D = cache.copyCompute(C);
      MergingCost::Allocate allocInst{tAlloc,    cache, reMap,
                                      valToPred, pred,  D->getOperands()};
      mergeOperands(A, B, allocInst);
      D = cache.cse(D);
      cache.replaceUsesByUsers(A, D);
      reMap.remapFromTo(A, D);
      A = D; // D replaces `A` as new op
    } else {
      invariant(Node::VK_Stow == A->getKind());
      MergingCost::Allocate allocInst{tAlloc,    cache, reMap,
                                      valToPred, pred,  getOperands(A)};
      mergeOperands(A, B, allocInst);
    }
    cache.replaceUsesByUsers(B, A);
    reMap.remapFromTo(B, A);
  }
};

template <bool TTI> // NOLINTNEXTLINE(misc-no-recursion)
inline void mergeInstructions(
  Arena<> *alloc, IR::Cache &cache, Predicate::Map &predMap,
  target::Machine<TTI> target, unsigned int vectorBits,
  dict::InlineTrie<Instruction::Identifier,
                   math::ResizeableView<Instruction *, math::Length<>>>
    opMap,
  dict::InlineTrie<Instruction *, Predicate::Set> &valToPred,
  llvm::SmallVectorImpl<MergingCost *> &mergingCosts, Instruction *J,
  llvm::BasicBlock *BB, Predicate::Set &preds) {
  // have we already visited?
  if (mergingCosts.front()->visited(J)) return;
  for (auto *C : mergingCosts) {
    if (C->visited(J)) return;
    C->initAncestors(alloc, J);
  }
  auto op = getIdentifier(J);
  // TODO: confirm that `vec` doesn't get moved if `opMap` is resized
  auto &vec = opMap[alloc, op];
  // consider merging with every instruction sharing an opcode
  for (Instruction *other : vec) {
    // check legality
    // illegal if:
    // 1. pred intersection not empty
    if (!preds.intersectionIsEmpty(valToPred[alloc, other])) continue;
    // 2. one op descends from another
    // because of our traversal pattern, this should not happen
    // unless a fusion took place
    // A -> B -> C
    //   -> D -> E
    // if we merge B and E, it would be illegal to merge C and D
    // because C is an ancestor of B-E, and D is a predecessor of B-E
    size_t numMerges = mergingCosts.size();
    // we may push into mergingCosts, so to avoid problems of iterator
    // invalidation, we use an indexed loop
    for (size_t i = 0; i < numMerges; ++i) {
      MergingCost *C = mergingCosts[i];
      if (C->getAncestors(J)->contains(other)) continue;
      // we shouldn't have to check the opposite condition
      // if (C->getAncestors(other)->contains(I))
      // because we are traversing in topological order
      // that is, we haven't visited any descendants of `I`
      // so only an ancestor had a chance
      auto *MC = alloc->construct<MergingCost>(*C);
      // MC is a copy of C, except we're now merging
      MC->merge(alloc, target, vectorBits, other, J);
    }
  }
  // descendants aren't legal merge candidates, so check before merging
  for (Instruction *U : J->getUsers()) {
    llvm::BasicBlock *BBU = nullptr;
    if (Addr *A = llvm::dyn_cast<Addr>(U)) BBU = A->getBasicBlock();
    else if (Compute *C = llvm::dyn_cast<Compute>(U)) BBU = C->getBasicBlock();
    if (BBU == BB) // fast path, skip lookup
      mergeInstructions(alloc, cache, predMap, target, vectorBits, opMap,
                        valToPred, mergingCosts, U, BB, preds);
    else if (BBU) {
      if (auto *f = predMap.find(BBU); f != predMap.end())
        mergeInstructions(alloc, cache, predMap, target, vectorBits, opMap,
                          valToPred, mergingCosts, U, BBU, f->second);
    }
  }
  // descendants aren't legal merge candidates, so push after merging
  if (vec.getCapacity() <= vec.size())
    vec.reserve(alloc, std::max(ptrdiff_t(8), 2 * vec.size()));
  vec.push_back_within_capacity(J);
  valToPred[alloc, J] = preds;
  // TODO: prune bad candidates from mergingCosts
}

/// mergeInstructions(
///    Arena<> *alloc,
///    IR::Cache &cache,
///    Arena<> *tmpAlloc,
///    Predicate::Map &predMap
/// )
/// merges instructions from predMap what have disparate control flow.
/// NOTE: calls tmpAlloc.Reset(); this should be an allocator specific for
/// merging as it allocates a lot of memory that it can free when it is done.
/// TODO: this algorithm is exponential in time and memory.
/// Odds are that there's way smarter things we can do.
template <bool TTI>
[[nodiscard]] inline auto
mergeInstructions(IR::Cache &cache, Predicate::Map &predMap,
                  target::Machine<TTI> target, Arena<> tAlloc,
                  unsigned vectorBits, LLVMIRBuilder LB,
                  TreeResult tr) -> TreeResult {
  auto [completed, trret] = cache.completeInstructions(&predMap, LB, tr);
  tr = trret;
  if (!predMap.isDivergent()) return tr;
  // there is a divergence in the control flow that we can ideally merge
  dict::InlineTrie<Instruction::Identifier,
                   math::ResizeableView<Instruction *, math::Length<>>>
    op_map{};
  dict::InlineTrie<Instruction *, Predicate::Set> val_to_pred{};
  llvm::SmallVector<MergingCost *> merging_costs;
  merging_costs.push_back(tAlloc.create<MergingCost>());
  // We search through incomplete instructions inside the predMap
  // this should yield all merge candidates.L
  for (auto *C = completed; C; C = static_cast<Compute *>(C->getNext())) {
    auto *f = predMap.find(C->getLLVMInstruction());
    invariant(f != predMap.end());
    mergeInstructions(&tAlloc, cache, predMap, target, vectorBits, op_map,
                      val_to_pred, merging_costs, C, f->first, f->second);
  }
  MergingCost *min_cost_strategy = *std::ranges::min_element(
    merging_costs, [](MergingCost *a, MergingCost *b) { return *a < *b; });
  // and then apply it to the instructions.
  ReMapper re_map;
  // we use `alloc` for objects intended to live on

  // merge pair through `select`ing the arguments that differ
  for (auto [A, B] : *min_cost_strategy)
    min_cost_strategy->mergeInstructions(cache, &tAlloc, A, B, val_to_pred,
                                         re_map, predMap.getPredicates());
  return tr;
}

} // namespace IR