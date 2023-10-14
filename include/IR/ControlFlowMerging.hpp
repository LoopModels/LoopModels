#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/BBPredPath.hpp"
#include "IR/Cache.hpp"
#include "IR/Instruction.hpp"
#include "IR/Predicate.hpp"
#include "Alloc/Arena.hpp"
#include <Containers/BitSets.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>

namespace poly::IR {
// merge all instructions from toMerge into merged
inline void merge(aset<Instruction *> &merged, aset<Instruction *> &toMerge) {
  merged.insert(toMerge.begin(), toMerge.end());
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
  amap<Instruction *, Instruction *> mergeMap;
  math::BumpPtrVector<std::pair<Instruction *, Instruction *>> mergeList;
  amap<Instruction *, aset<Instruction *> *> ancestorMap;
  llvm::InstructionCost cost;

  auto getAncestors(Value *op) -> aset<Instruction *> * {
    if (auto *I = llvm::dyn_cast<Instruction>(op))
      if (auto *f = ancestorMap.find(I); f != ancestorMap.end())
        return f->second;
    return nullptr;
  }
  auto setAncestors(Value *op, aset<Instruction *> *ancestors) {
    if (auto *I = llvm::dyn_cast<Instruction>(op)) ancestorMap[I] = ancestors;
  }
  /// returns `true` if `key` was already in ancestors
  /// returns `false` if it had to initialize
  // NOLINTNEXTLINE(misc-no-recursion)
  auto initAncestors(Arena<> *alloc, Instruction *key)
    -> aset<Instruction *> * {

    auto *set = alloc->construct<aset<Instruction *>>(alloc);
    /// instructions are considered their own ancestor for our purposes
    set->insert(key);
    ancestorMap[key] = set;
    for (Value *op : key->getOperands()) {
      if (auto *I = llvm::dyn_cast<Compute>(op); I && I->isComplete()) {
        auto *A = getAncestors(alloc, I);
        set->insert(A->begin(), A->end());
      }
    }
    return set;
  }
  auto begin() -> decltype(mergeList.begin()) { return mergeList.begin(); }
  auto end() -> decltype(mergeList.end()) { return mergeList.end(); }
  [[nodiscard]] auto visited(Instruction *key) const -> bool {
    return ancestorMap.count(key);
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getAncestors(Arena<> *alloc, Instruction *I) -> aset<Instruction *> * {
    auto *&f = ancestorMap[I];
    if (!f) f = initAncestors(alloc, I);
    return f;
  }
  auto getAncestors(Instruction *key) -> aset<Instruction *> * {
    if (auto *it = ancestorMap.find(key); it != ancestorMap.end())
      return it->second;
    return nullptr;
  }
  auto findMerge(Instruction *key) -> Instruction * {
    if (auto *it = mergeMap.find(key); it != mergeMap.end()) return it->second;
    return nullptr;
  }
  auto findMerge(Instruction *key) const -> Instruction * {
    if (const auto *it = mergeMap.find(key); it != mergeMap.end())
      return it->second;
    return nullptr;
  }
  /// isMerged(Instruction *key) const -> bool
  /// returns true if `key` is merged with any other Instruction
  auto isMerged(Instruction *key) const -> bool { return mergeMap.count(key); }
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
  void cycleUpdateMerged(aset<Instruction *> *ancestors, Instruction *E,
                         Instruction *H) {
    while (H != E) {
      setAncestors(H, ancestors);
      H = mergeMap[H];
    }
  }
  static constexpr auto popBit(uint8_t x) -> std::pair<bool, uint8_t> {
    return {x & 1, x >> 1};
  }

  struct Allocate {
    Arena<> *alloc; // short term allocator
    IR::Cache &cache;
    ReMapper &reMap;
    amap<Instruction *, Predicate::Set> &valToPred;
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
    amap<Instruction *, Predicate::Set> &valToPred;
    Predicate::Intersection pred;
    UList<Value *> *predicates;

    constexpr explicit operator unsigned() const { return 0; }
    void select(size_t i, Value *A, Value *B) {
      A = reMap[A];
      B = reMap[B];
      Compute *C = cache.createSelect(pred, A, B, predicates);
      Predicate::Set pS;
      if (auto *I = llvm::dyn_cast<Instruction>(A))
        pS.Union(alloc, valToPred[I]);
      if (auto *I = llvm::dyn_cast<Instruction>(B))
        pS.Union(alloc, valToPred[I]);
      valToPred[C] = pS;
      operands[i] = C;
    }
  };

  static auto init(Allocate a, Instruction *A, Instruction *B)
    -> SelectAllocator {
    Predicate::Intersection P = a.valToPred[A].getConflict(a.valToPred[B]);
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
    // additionally, we can check `I->associativeOperandsFlag()`
    // select(p, f(a,b), f(c,a)) => f(a, select(p, b, c))
    // we need to figure out which operands we're merging with which,
    //
    // We need to give special consideration to the case where
    // arguments are merged, as this may be common when two
    // control flow branches have relatively similar pieces.
    // E.g., if b and c are already merged,
    // and if `f`'s ops are associative, then we'd get
    // select(p, f(a,b), f(c,a)) => f(a, b)
    // so we need to check if any operand pairs are merged with each other.
    // note `isMerged(a,a) == true`, so that's the one query we need to use.
    auto selector = init(selects, A, B);
    MutPtrVector<Value *> operandsA = A->getOperands();
    MutPtrVector<Value *> operandsB = B->getOperands();
    size_t numOperands = operandsA.size();
    assert(numOperands == operandsB.size());
    /// associate ops means `f(a, b) == f(b, a)`
    uint8_t associativeOpsFlag = B->associativeOperandsFlag();
    // For example,
    // we keep track of which operands we've already merged,
    // f(a, b), f(b, b)
    // we can't merge b twice!
    for (size_t i = 0; i < numOperands; ++i) {
      auto *opA = A->getOperand(i);
      auto *opB = B->getOperand(i);
      auto [assoc, assocFlag] = popBit(associativeOpsFlag);
      associativeOpsFlag = assocFlag;
      if (opA == opB) continue;
      // if both operands were merged, we can ignore it's associativity
      if (isMerged(opB, opA)) {
        // we cast, because isMerged confirms they're Instructions, given
        // that opA != opB, which we checked above
        continue;
      }
      if (!((assoc) && (assocFlag))) {
        // this op isn't associative with any remaining
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
        auto *opjA = A->getOperand(j);
        auto *opjB = B->getOperand(j);
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

  void merge(Arena<> *alloc, const llvm::TargetTransformInfo &TTI,
             unsigned int vectorBits, Instruction *A, Instruction *B) {
    mergeList.emplace_back(A, B);
    auto *aA = ancestorMap.find(B);
    auto *aB = ancestorMap.find(A);
    assert(aA != ancestorMap.end());
    assert(aB != ancestorMap.end());
    // in the old MergingCost where they're separate instructions,
    // we leave their ancestor PtrMaps intact.
    // in the new MergingCost where they're the same instruction,
    // we assign them the same ancestors.
    auto *merged = new (alloc) aset<Instruction *>(*aA->second);
    merged->insert(aB->second->begin(), aB->second->end());
    aA->second = merged;
    aB->second = merged;
    unsigned numSelects = mergeOperands(A, B, Count{});
    // TODO:
    // increase cost by numSelects, decrease cost by `I`'s cost
    unsigned int W = vectorBits / B->getNumScalarBits();
    if (numSelects)
      cost += numSelects * Operation::selectCost(TTI, B->getType(W));
    cost -= B->getCost(TTI, VectorWidth(W)).recipThroughput;
    auto *mB = findMerge(B);
    if (mB) cycleUpdateMerged(merged, B, mB);
    // fuse the merge map cycles
    auto &mMA = mergeMap[A], &mMB = mergeMap[B];
    if (auto *mA = findMerge(A)) {
      cycleUpdateMerged(merged, A, mA);
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

  inline void mergeInstructions(IR::Cache &cache, Arena<> *tAlloc,
                                Instruction *A, Instruction *B,
                                amap<Instruction *, Predicate::Set> &valToPred,
                                ReMapper &reMap, UList<Value *> *pred) {
    A = reMap[A];
    B = reMap[B];
    if (A == B) return; // is this possible?
    invariant(A->getNumOperands(), B->getNumOperands());
    if (auto *I = llvm::dyn_cast<Instruction>(A)) {
      auto *J = llvm::cast<Instruction>(B);
      // could be stores
      if (auto *C = llvm::dyn_cast<Compute>(I)) {
        Compute *D = cache.copyCompute(C);
        MergingCost::Allocate allocInst{tAlloc,    cache, reMap,
                                        valToPred, pred,  D->getOperands()};
        mergeOperands(I, J, allocInst);
        D = cache.cse(D);
        cache.replaceAllUsesWith(A, D);
        reMap.remapFromTo(A, D);
        A = D; // D replaces `A` as new op
      } else {
        invariant(Node::VK_Stow == I->getKind());
        MergingCost::Allocate allocInst{tAlloc,    cache, reMap,
                                        valToPred, pred,  I->getOperands()};
        mergeOperands(I, J, allocInst);
      }
    }
    cache.replaceAllUsesWith(B, A);
    reMap.remapFromTo(B, A);
  }
};

// NOLINTNEXTLINE(misc-no-recursion)
inline void mergeInstructions(
  Arena<> *alloc, IR::Cache &cache, Predicate::Map &predMap,
  const llvm::TargetTransformInfo &TTI, unsigned int vectorBits,
  amap<Instruction::Identifier, math::ResizeableView<Instruction *, unsigned>>
    opMap,
  amap<Instruction *, Predicate::Set> &valToPred,
  llvm::SmallVectorImpl<MergingCost *> &mergingCosts, Instruction *J,
  llvm::BasicBlock *BB, Predicate::Set &preds) {
  // have we already visited?
  if (mergingCosts.front()->visited(J)) return;
  for (auto *C : mergingCosts) {
    if (C->visited(J)) return;
    C->initAncestors(alloc, J);
  }
  auto op = J->getIdentifier();
  // TODO: confirm that `vec` doesn't get moved if `opMap` is resized
  auto &vec = opMap[op];
  // consider merging with every instruction sharing an opcode
  for (Instruction *other : vec) {
    // check legality
    // illegal if:
    // 1. pred intersection not empty
    if (!preds.intersectionIsEmpty(valToPred[other])) continue;
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
      MC->merge(alloc, TTI, vectorBits, other, J);
    }
  }
  // descendants aren't legal merge candidates, so check before merging
  for (Instruction *U : J->getUsers()) {
    if (llvm::BasicBlock *BBU = U->getBasicBlock()) {
      if (BBU == BB) // fast path, skip lookup
        mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                          valToPred, mergingCosts, llvm::cast<Instruction>(U),
                          BB, preds);
      else if (auto *f = predMap.find(BBU); f != predMap.end())
        mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                          valToPred, mergingCosts, llvm::cast<Instruction>(U),
                          BBU, f->second);
    }
  }
  // descendants aren't legal merge candidates, so push after merging
  if (vec.getCapacity() <= vec.size())
    vec.reserve(alloc, std::max(unsigned(8), 2 * vec.size()));
  vec.push_back(J);
  valToPred[J] = preds;
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
[[nodiscard]] inline auto
mergeInstructions(IR::Cache &cache, Predicate::Map &predMap,
                  const llvm::TargetTransformInfo &TTI, Arena<> tAlloc,
                  unsigned vectorBits, TreeResult tr) -> TreeResult {
  auto [completed, trret] = cache.completeInstructions(&predMap, tr);
  tr = trret;
  if (!predMap.isDivergent()) return tr;
  // there is a divergence in the control flow that we can ideally merge
  amap<Instruction::Identifier, math::ResizeableView<Instruction *, unsigned>>
    opMap{&tAlloc};
  amap<Instruction *, Predicate::Set> valToPred{&tAlloc};
  llvm::SmallVector<MergingCost *> mergingCosts;
  mergingCosts.emplace_back(&tAlloc);
  // We search through incomplete instructions inside the predMap
  // this should yield all merge candidates.L
  for (auto *C = completed; C; C = static_cast<Compute *>(C->getNext())) {
    auto *f = predMap.find(C->getLLVMInstruction());
    invariant(f != predMap.end());
    mergeInstructions(&tAlloc, cache, predMap, TTI, vectorBits, opMap,
                      valToPred, mergingCosts, C, f->first, f->second);
  }
  MergingCost *minCostStrategy = *std::ranges::min_element(
    mergingCosts, [](MergingCost *a, MergingCost *b) { return *a < *b; });
  // and then apply it to the instructions.
  ReMapper reMap;
  // we use `alloc` for objects intended to live on

  // merge pair through `select`ing the arguments that differ
  for (auto [A, B] : *minCostStrategy)
    minCostStrategy->mergeInstructions(cache, &tAlloc, A, B, valToPred, reMap,
                                       predMap.getPredicates());
  return tr;
}

} // namespace poly::IR
