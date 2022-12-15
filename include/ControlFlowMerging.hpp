#pragma once

#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./Predicate.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/InstructionCost.h>
#include <set>

void buildInstructionGraph(llvm::BumpPtrAllocator &alloc,
                           Instruction::Cache &cache,
                           LinearProgramLoopBlock &LB) {
    for (auto mem : LB.getMemoryAccesses()) {
        if (mem->nodeIndex.isEmpty())
            continue;
        // TODO: add a means for cache.get to stop adding operands
        // that are outside of LB, as we don't care about that part of the
        // graph.
        auto inst = cache.get(alloc, mem->getInstruction());
        inst->id.ptr.ref = &mem->ref;
    }
}

// merge all instructions from toMerge into merged
inline void merge(llvm::SmallPtrSetImpl<Instruction *> &merged,
                  llvm::SmallPtrSetImpl<Instruction *> &toMerge) {
    merged.insert(toMerge.begin(), toMerge.end());
}

// represents the cost of merging key=>values; cost is hopefully negative.
// cost is measured in reciprocal throughput
struct MergingCost {
    // mergeMap can be thought of as containing doubly linked lists/cycles,
    // e.g. a -> b -> c -> a, where a, b, c are instructions.
    // if we merge c and d, we have
    // c -> a => c -> d
    // d      => d -> a
    // yielding: c -> d -> a -> b -> c
    // if instead we have d <-> e, then
    // c -> a => c -> e
    // d -> e => d -> a
    // yielding c -> e -> d -> a -> b -> c
    // that is, if we're fusing c and d, we can make each point toward
    // what the other one was pointing to, in order to link the chains.
    llvm::DenseMap<Instruction *, Instruction *> mergeMap;
    llvm::DenseMap<Instruction *, llvm::SmallPtrSet<Instruction *, 16> *>
        ancestorMap;
    llvm::InstructionCost cost;
    /// returns `true` if `key` was already in ancestors
    /// returns `false` if it had to initialize
    auto initAncestors(llvm::BumpPtrAllocator &alloc, Instruction *key)
        -> llvm::SmallPtrSet<Instruction *, 16> * {
        auto *set = alloc.Allocate<llvm::SmallPtrSet<Instruction *, 16>>();
        /// instructions are considered their own ancestor for our purposes
        set->insert(key);
        for (auto *op : key->operands) {
            if (auto f = ancestorMap.find(op); f != ancestorMap.end()) {
                set->insert(f->second->begin(), f->second->end());
            }
        }
        ancestorMap[key] = set;
        return set;
    }
    [[nodiscard]] auto visited(Instruction *key) const -> bool {
        return ancestorMap.count(key);
    }
    auto getAncestors(llvm::BumpPtrAllocator &alloc, Instruction *key)
        -> llvm::SmallPtrSet<Instruction *, 16> * {
        if (auto it = ancestorMap.find(key); it != ancestorMap.end())
            return it->second;
        return initAncestors(alloc, key);
    }
    auto getAncestors(Instruction *key)
        -> llvm::SmallPtrSet<Instruction *, 16> * {
        if (auto it = ancestorMap.find(key); it != ancestorMap.end())
            return it->second;
        return nullptr;
    }
    auto findMerge(Instruction *key) -> Instruction * {
        if (auto it = mergeMap.find(key); it != mergeMap.end())
            return it->second;
        return nullptr;
    }
    auto findMerge(Instruction *key) const -> Instruction * {
        if (auto it = mergeMap.find(key); it != mergeMap.end())
            return it->second;
        return nullptr;
    }
    /// isMerged(Instruction *key) const -> bool
    /// returns true if `key` is merged with any other Instruction
    auto isMerged(Instruction *key) const -> bool {
        return mergeMap.count(key);
    }
    /// isMerged(Instruction *I, Instruction *J) const -> bool
    /// returns true if `I` and `J` are merged with each other
    auto isMerged(Instruction *I, Instruction *J) const -> bool {
        Instruction *K = J;
        do {
            if (I == K)
                return true;
            K = findMerge(K);
        } while (K && K != J);
        return false;
    }
    // follows the cycle, traversing H -> mergeMap[H] -> mergeMap[mergeMap[H]]
    // ... until it reaches E, updating the ancestorMap pointer at each level of
    // the recursion.
    void cycleUpdateMerged(llvm::SmallPtrSet<Instruction *, 16> *ancestors,
                           Instruction *E, Instruction *H) {
        while (H != E) {
            ancestorMap[H] = ancestors;
            H = mergeMap[H];
        }
    }
    static constexpr auto popBit(uint8_t x) -> std::pair<bool, uint8_t> {
        return {x & 1, x >> 1};
    }
    void merge(llvm::BumpPtrAllocator &alloc, Instruction *O, Instruction *I) {
        auto aI = ancestorMap.find(I);
        auto aO = ancestorMap.find(O);
        assert(aI != ancestorMap.end());
        assert(aO != ancestorMap.end());
        // in the old MergingCost where they're separate instructions,
        // we leave their ancestor PtrMaps intact.
        // in the new MergingCost where they're the same instruction,
        // we assign them the same ancestor Claptrap.
        auto *merged =
            new (alloc) llvm::SmallPtrSet<Instruction *, 16>(*aI->second);
        merged->insert(aO->second->begin(), aO->second->end());
        aI->second = merged;
        aO->second = merged;
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
        size_t numOperands = I->getOperands().size();
        assert(numOperands == O->getOperands().size());
        uint8_t associativeOpsFlag = I->associativeOperandsFlag();
        // For example,
        // we keep track of which operands we've already merged,
        // f(a, b), f(b, b)
        // we can't merge b twice!
        uint8_t mergedOperandsI = 0, mergedOperandsO = 0;
        size_t numSelects = numOperands;
        for (size_t i = 0; i < numOperands; ++i) {
            auto *opI = I->getOperand(i);
            auto *opO = O->getOperand(i);
            auto [assoc, assocFlag] = popBit(associativeOpsFlag);
            associativeOpsFlag = assocFlag;
            bool mergedI = mergedOperandsI & 1, mergedO = mergedOperandsO & 1;
            // if both operands were merged, we can ignore it's associativity
            // assocFlag &= ~(mergedFlagI & mergedFlagO);
            if ((!mergedI) && (!mergedO) && isMerged(opI, opO)) {
                --numSelects;
                continue;
            } else if (!((assoc) && (assocFlag))) {
                // this op isn't associative with any remaining
                continue;
            }
            // we only look forward
            size_t j = i;
            uint8_t mask = 1;
            while (assocFlag) {
                auto shift = std::countr_zero(assocFlag);
                j += ++shift;
                mask <<= shift;
                assocFlag >>= shift;
                auto *opIJ = I->getOperand(j);
                auto *opOJ = O->getOperand(j);
                // if elements in these pairs weren't already used
                // to drop a select, and they're merged with each other
                // we'll use them now to drop a select.
                if (((mergedOperandsO & mask) == 0) && isMerged(opI, opOJ)) {
                    --numSelects;
                    mergedOperandsO |= mask;
                    break;
                } else if (((mergedOperandsI & mask) == 0) &&
                           isMerged(opIJ, opO)) {
                    --numSelects;
                    mergedOperandsI |= mask;
                    break;
                }
            }
            mergedOperandsI >>= 1;
            mergedOperandsO >>= 1;
        }
        // TODO:
        // increase cost by numSelects, decrease cost by `I`'s cost
        auto mI = findMerge(I);
        if (mI)
            cycleUpdateMerged(merged, I, mI);
        // fuse the merge map cycles
        if (auto mO = findMerge(O)) {
            cycleUpdateMerged(merged, O, mO);
            if (mI) {
                mergeMap[I] = mO;
                mergeMap[O] = mI;
            } else {
                mergeMap[I] = mO;
                mergeMap[O] = I;
            }
        } else if (mI) {
            mergeMap[O] = mI;
            mergeMap[I] = O;
        } else {
            mergeMap[I] = O;
            mergeMap[O] = I;
        }
    }
};

void mergeInstructions(
    llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
    Predicate::Map &predMap,
    llvm::DenseMap<std::pair<llvm::Intrinsic::ID, llvm::Intrinsic::ID>,
                   llvm::SmallVector<std::pair<Instruction *, Predicate::Set>>>
        &opMap,
    llvm::SmallVectorImpl<MergingCost *> &mergingCosts, Instruction *I,
    llvm::BasicBlock *BB, Predicate::Set &preds) {
    // have we already visited?
    if (mergingCosts.front()->visited(I))
        return;
    for (auto C : mergingCosts) {
        if (C->visited(I))
            return;
        C->initAncestors(alloc, I);
    }
    auto op = I->getOpPair();
    // TODO: confirm that `vec` doesn't get moved if `opMap` is resized
    auto &vec = opMap[op];
    // consider merging with every instruction sharing an opcode
    for (auto &pair : vec) {
        Instruction *other = pair.first;
        // check legality
        // illegal if:
        // 1. pred intersection not empty
        if (!preds.emptyIntersection(pair.second))
            continue;
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
            if (C->getAncestors(I)->contains(other)) {
                continue;
            }
            // we shouldn't have to check the opposite condition
            // if (C->getAncestors(other)->contains(I))
            // because we are traversing in topological order
            // that is, we haven't visited any descendants of `I`
            // so only an ancestor had a chance
            auto *MC = new (alloc) MergingCost(*C);
            // MC is a copy of C, except we're now merging
            MC->merge(alloc, other, I);
        }
    }
    // descendants aren't legal merge candidates, so check before merging
    for (Instruction *U : I->getUsers()) {
        if (llvm::BasicBlock *BBU = U->getBasicBlock()) {
            if (BBU == BB) {
                // fast path, skip lookup
                mergeInstructions(alloc, cache, predMap, opMap, mergingCosts, U,
                                  BB, preds);
            } else if (auto f = predMap.find(BBU); f != predMap.rend()) {
                mergeInstructions(alloc, cache, predMap, opMap, mergingCosts, U,
                                  BBU, f->second);
            }
        }
    }
    // descendants aren't legal merge candidates, so push after merging
    vec.push_back({I, preds});
    // TODO: prune bad candidates from mergingCosts
}

/// mergeInstructions(
///    llvm::BumpPtrAllocator &alloc,
///    llvm::BumpPtrAllocator &tmpAlloc,
///    Instruction::Cache &cache,
///    Predicate::Map &predMap
/// )
/// merges instructions from predMap what have disparate control flow.
/// NOTE: calls tmpAlloc.Reset(); this should be an allocator specific for
/// merging as it allocates a lot of memory that it can free when it is done.
void mergeInstructions(llvm::BumpPtrAllocator &alloc,
                       llvm::BumpPtrAllocator &tAlloc,
                       Instruction::Cache &cache, Predicate::Map &predMap) {
    if (!predMap.isDivergent())
        return;
    // there is a divergence in the control flow that we can ideally merge
    auto &opMap = *alloc.Allocate<llvm::DenseMap<
        std::pair<llvm::Intrinsic::ID, llvm::Intrinsic::ID>,
        llvm::SmallVector<std::pair<Instruction *, Predicate::Set>>>>();
    llvm::SmallVector<MergingCost *> mergingCosts;
    mergingCosts.push_back(alloc.Allocate<MergingCost>());
    for (auto &pred : predMap) {
        for (llvm::Instruction &lI : *pred.first) {
            if (Instruction *I = cache[&lI]) {
                mergeInstructions(tAlloc, cache, predMap, opMap, mergingCosts,
                                  I, pred.first, pred.second);
            }
        }
    }
    tAlloc.Reset();
}
