#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "Dicts/BumpVector.hpp"
#include "IR/Cache.hpp"
#include "IR/Instruction.hpp"
#include "IR/Predicate.hpp"
#include "Utilities/Allocators.hpp"
#include <Containers/BitSets.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/InstructionCost.h>

namespace poly::IR {
// merge all instructions from toMerge into merged
inline void merge(aset<Node *> &merged, aset<Node *> &toMerge) {
  merged.insert(toMerge.begin(), toMerge.end());
}
struct ReMapper {
  dict::map<Node *, Node *> reMap;
  auto operator[](Node *J) -> Node * {
    if (auto f = reMap.find(J); f != reMap.end()) return f->second;
    return J;
  }
  void remapFromTo(Node *K, Node *J) { reMap[K] = J; }
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
  amap<Node *, Node *> mergeMap;
  llvm::SmallVector<std::pair<Node *, Node *>> mergeList;
  amap<Node *, aset<Node *> *> ancestorMap;
  llvm::InstructionCost cost;
  /// returns `true` if `key` was already in ancestors
  /// returns `false` if it had to initialize
  auto initAncestors(BumpAlloc<> &alloc, Inst *key) -> aset<Node *> * {

    auto *set = alloc.construct<aset<Node *>>(alloc);
    /// instructions are considered their own ancestor for our purposes
    set->insert(key);
    key->getOperands()->forEach([&](Node *op) {
      if (auto *f = ancestorMap.find(op); f != ancestorMap.end())
        set->insert(f->second->begin(), f->second->end());
    });
    ancestorMap[key] = set;
    return set;
  }
  auto begin() -> decltype(mergeList.begin()) { return mergeList.begin(); }
  auto end() -> decltype(mergeList.end()) { return mergeList.end(); }
  [[nodiscard]] auto visited(Node *key) const -> bool {
    return ancestorMap.count(key);
  }
  auto getAncestors(BumpAlloc<> &alloc, Node *key) -> aset<Node *> * {
    if (auto *it = ancestorMap.find(key); it != ancestorMap.end())
      return it->second;
    return initAncestors(alloc, key);
  }
  auto getAncestors(Node *key) -> aset<Node *> * {
    if (auto *it = ancestorMap.find(key); it != ancestorMap.end())
      return it->second;
    return nullptr;
  }
  auto findMerge(Node *key) -> Node * {
    if (auto *it = mergeMap.find(key); it != mergeMap.end()) return it->second;
    return nullptr;
  }
  auto findMerge(Node *key) const -> Node * {
    if (const auto *it = mergeMap.find(key); it != mergeMap.end())
      return it->second;
    return nullptr;
  }
  /// isMerged(Node *key) const -> bool
  /// returns true if `key` is merged with any other Instruction
  auto isMerged(Node *key) const -> bool { return mergeMap.count(key); }
  /// isMerged(Node *I, Node *J) const -> bool
  /// returns true if `I` and `J` are merged with each other
  // note: this is not the same as `isMerged(I) && isMerged(J)`,
  // as `I` and `J` may be merged with different Instructions
  // however, isMerged(I, J) == isMerged(J, I)
  // so we ignore easily swappable parameters
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto isMerged(Node *L, Node *J) const -> bool {
    Node *K = J;
    do {
      if (L == K) return true;
      K = findMerge(K);
    } while (K && K != J);
    return false;
  }
  // follows the cycle, traversing H -> mergeMap[H] -> mergeMap[mergeMap[H]]
  // ... until it reaches E, updating the ancestorMap pointer at each level of
  // the recursion.
  void cycleUpdateMerged(aset<Node *> *ancestors, Node *E, Node *H) {
    while (H != E) {
      ancestorMap[H] = ancestors;
      H = mergeMap[H];
    }
  }
  static constexpr auto popBit(uint8_t x) -> std::pair<bool, uint8_t> {
    return {x & 1, x >> 1};
  }

  struct Allocate {
    BumpAlloc<> &alloc;
    IR::Cache &cache;
    ReMapper &reMap;
  };
  struct Count {};

  struct SelectCounter {
    unsigned numSelects{0};
    constexpr operator unsigned() const { return numSelects; }
    constexpr void merge(size_t, Node *, Node *) {}
    constexpr void select(size_t, Node *, Node *) { ++numSelects; }
  };
  struct SelectAllocator {
    BumpAlloc<> &alloc;
    IR::Cache &cache;
    ReMapper &reMap;
    MutPtrVector<Node *> operands;
    constexpr operator MutPtrVector<Node *>() const { return operands; }
    void merge(size_t i, Node *A, Node *B) {
      operands[i] = reMap[A]->replaceAllUsesOf(reMap[B]);
    }
    void select(size_t i, Node *A, Node *B) {
      A = reMap[A];
      B = reMap[B];
      operands[i] = cache.createSelect(alloc, A, B)
                      ->replaceAllOtherUsesOf(A)
                      ->replaceAllOtherUsesOf(B);
    }
  };
  static auto init(Allocate a, Node *A) -> SelectAllocator {
    size_t numOps = A->getNumOperands();
    auto **operandsPtr = a.alloc.allocate<Node *>(numOps);
    MutPtrVector<Node *> operands{operandsPtr, numOps};
    return SelectAllocator{a.alloc, a.cache, a.reMap, operands};
  }
  static auto init(Count, Node *) -> SelectCounter { return SelectCounter{0}; }
  // An abstraction that runs an algorithm to look for merging opportunities,
  // either counting the number of selects needed, or allocating selects
  // and returning the new operand vector.
  // We generally aim to have analysis/cost modeling and code generation
  // take the same code paths, to both avoid code duplication and to
  // make sure the cost modeling reflects the actual code we're generating.
  template <typename S> auto mergeOperands(Node *A, Node *B, S selects) {
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
    auto selector = init(selects, A);
    MutPtrVector<Node *> operandsA = A->getOperands();
    MutPtrVector<Node *> operandsB = B->getOperands();
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
      // if both operands were merged, we can ignore it's associativity
      if (isMerged(opB, opA)) {
        selector.merge(i, opA, opB);
        // --numSelects;
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
          selector.merge(i, opjA, opB);
          merged = true;
          break;
        }
        if (isMerged(opjB, opA)) {
          std::swap(operandsB[i], operandsB[j]);
          selector.merge(i, opA, opjB);
          merged = true;
          break;
        }
      }
      // we couldn't find any candidates
      if (!merged) selector.select(i, opA, opB);
    }
    return selector;
  }

  void merge(BumpAlloc<> &alloc, llvm::TargetTransformInfo &TTI,
             unsigned int vectorBits, Node *A, Node *B) {
    mergeList.emplace_back(A, B);
    auto *aA = ancestorMap.find(B);
    auto *aB = ancestorMap.find(A);
    assert(aA != ancestorMap.end());
    assert(aB != ancestorMap.end());
    // in the old MergingCost where they're separate instructions,
    // we leave their ancestor PtrMaps intact.
    // in the new MergingCost where they're the same instruction,
    // we assign them the same ancestors.
    auto *merged = new (alloc) aset<Node *>(*aA->second);
    merged->insert(aB->second->begin(), aB->second->end());
    aA->second = merged;
    aB->second = merged;
    unsigned numSelects = mergeOperands(A, B, Count{});
    // TODO:
    // increase cost by numSelects, decrease cost by `I`'s cost
    unsigned int W = vectorBits / B->getNumScalarBits();
    if (numSelects) cost += numSelects * B->selectCost(TTI, W);
    cost -= B->getCost(TTI, W).recipThroughput;
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
};

// NOLINTNEXTLINE(misc-no-recursion)
inline void
mergeInstructions(BumpAlloc<> &alloc, IR::Cache &cache, Predicate::Map &predMap,
                  llvm::TargetTransformInfo &TTI, unsigned int vectorBits,
                  amap<std::pair<Instruction::Intrinsic, llvm::Type *>,
                       BumpPtrVector<std::pair<Node *, Predicate::Set>>> &opMap,
                  llvm::SmallVectorImpl<MergingCost *> &mergingCosts, Node *J,
                  llvm::BasicBlock *BB, Predicate::Set &preds) {
  // have we already visited?
  if (mergingCosts.front()->visited(J)) return;
  for (auto *C : mergingCosts) {
    if (C->visited(J)) return;
    C->initAncestors(alloc, J);
  }
  auto op = J->getOpType();
  // TODO: confirm that `vec` doesn't get moved if `opMap` is resized
  auto &vec = opMap[op];
  // consider merging with every instruction sharing an opcode
  for (auto &pair : vec) {
    Node *other = pair.first;
    // check legality
    // illegal if:
    // 1. pred intersection not empty
    if (!preds.intersectionIsEmpty(pair.second)) continue;
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
      auto *MC = alloc.construct<MergingCost>(*C);
      // MC is a copy of C, except we're now merging
      MC->merge(alloc, TTI, vectorBits, other, J);
    }
  }
  // descendants aren't legal merge candidates, so check before merging
  for (Node *U : J->getUsers()) {
    if (llvm::BasicBlock *BBU = U->getBasicBlock()) {
      if (BBU == BB) {
        // fast path, skip lookup
        mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                          mergingCosts, U, BB, preds);
      } else if (auto *f = predMap.find(BBU); f != predMap.rend()) {
        mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                          mergingCosts, U, BBU, f->second);
      }
    }
  }
  // descendants aren't legal merge candidates, so push after merging
  vec.emplace_back(J, preds);
  // TODO: prune bad candidates from mergingCosts
}

/// mergeInstructions(
///    BumpAlloc<> &alloc,
///    IR::Cache &cache,
///    BumpAlloc<> &tmpAlloc,
///    Predicate::Map &predMap
/// )
/// merges instructions from predMap what have disparate control flow.
/// NOTE: calls tmpAlloc.Reset(); this should be an allocator specific for
/// merging as it allocates a lot of memory that it can free when it is done.
/// TODO: this algorithm is exponential in time and memory.
/// Odds are that there's way smarter things we can do.
inline void mergeInstructions(BumpAlloc<> &alloc, IR::Cache &cache,
                              Predicate::Map &predMap,
                              llvm::TargetTransformInfo &TTI,
                              BumpAlloc<> &tAlloc, unsigned int vectorBits) {
  if (!predMap.isDivergent()) return;
  auto p = tAlloc.scope();
  // there is a divergence in the control flow that we can ideally merge
  amap<std::pair<Instruction::Intrinsic, llvm::Type *>,
       BumpPtrVector<std::pair<Node *, Predicate::Set>>>
    opMap{tAlloc};
  llvm::SmallVector<MergingCost *> mergingCosts;
  mergingCosts.emplace_back(alloc);
  for (auto &pred : predMap)
    for (llvm::Instruction &lI : *pred.first)
      if (Node *J = cache[&lI])
        mergeInstructions(tAlloc, cache, predMap, TTI, vectorBits, opMap,
                          mergingCosts, J, pred.first, pred.second);
  MergingCost *minCostStrategy = *std::ranges::min_element(
    mergingCosts, [](auto *a, auto *b) { return *a < *b; });
  // and then apply it to the instructions.
  ReMapper reMap;
  // we use `alloc` for objects intended to live on
  for (auto &pair : *minCostStrategy) {
    // merge pair through `select`ing the arguments that differ
    auto [A, B] = pair;
    A = reMap[A];
    B = reMap[B];
    auto operands = minCostStrategy->mergeOperands(
      A, B, MergingCost::Allocate{alloc, cache, reMap});
    A->replaceAllUsesOf(B)->setOperands(operands);
    reMap.remapFromTo(B, A);
  }
}

} // namespace poly::IR
