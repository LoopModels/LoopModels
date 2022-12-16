#pragma once

#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./Predicate.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/InstructionCost.h>
#include <set>
#include <sys/select.h>

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
    // if instead we're merging c and d, but d is also paired d <-> e, then
    // c -> a => c -> e
    // d -> e => d -> a
    // yielding c -> e -> d -> a -> b -> c
    // that is, if we're fusing c and d, we can make each point toward
    // what the other one was pointing to, in order to link the chains.
    llvm::DenseMap<Instruction *, Instruction *> mergeMap;
    llvm::SmallVector<std::pair<Instruction *, Instruction *>> mergeList;
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
    auto begin() -> decltype(mergeList.begin()) { return mergeList.begin(); }
    auto end() -> decltype(mergeList.end()) { return mergeList.end(); }
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

    struct Allocate {
        llvm::BumpPtrAllocator &alloc;
        Instruction::Cache &cache;
    };
    struct Count {};

    struct SelectCounter {
        size_t numSelects{0};
        constexpr operator size_t() const { return numSelects; }
        constexpr void merge(size_t, Instruction *, Instruction *) {}
        constexpr void select(size_t, Instruction *, Instruction *) {
            ++numSelects;
        }
    };
    struct SelectAllocator {
        llvm::BumpPtrAllocator &alloc;
        Instruction::Cache &cache;
        llvm::MutableArrayRef<Instruction *> operands;
        constexpr operator llvm::MutableArrayRef<Instruction *>() const {
            return operands;
        }
        void merge(size_t i, Instruction *A, Instruction *B) {}
        void select(size_t i, Instruction *A, Instruction *B) {
            auto *S = Instruction::createSelect(alloc, A, B);
            operands[i] = S;
        }
    };
    static auto init(Allocate a, Instruction *A, Instruction *)
        -> SelectAllocator {
        size_t numOps = A->getNumOperands();
        auto **operandsPtr = a.alloc.Allocate<Instruction *>(numOps);
        llvm::MutableArrayRef<Instruction *> operands(operandsPtr, numOps);
        return SelectAllocator{a.alloc, a.cache, operands};
    }
    static auto init(Count, Instruction *, Instruction *) -> SelectCounter {
        return SelectCounter{0};
    }
    // An abstraction that runs an algorithm to look for merging opportunities,
    // either counting the number of selects needed, or allocating selects
    // and returning the new operand vector.
    // We generally aim to have analysis/cost modeling and code generation
    // take the same code paths, to both avoid code duplication and to
    // make sure the cost modeling reflects the actual code we're generating.
    template <typename S>
    auto mergeOperands(Instruction *A, Instruction *B, S selects) {
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
        llvm::MutableArrayRef<Instruction *> operandsA = A->getOperands();
        llvm::MutableArrayRef<Instruction *> operandsB = B->getOperands();
        size_t numOperands = operandsA.size();
        assert(numOperands == operandsB.size());
        uint8_t associativeOpsFlag = B->associativeOperandsFlag();
        // For example,
        // we keep track of which operands we've already merged,
        // f(a, b), f(b, b)
        // we can't merge b twice!
        // size_t numSelects = numOperands;
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
            } else if (!((assoc) && (assocFlag))) {
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
                } else if (isMerged(opjB, opA)) {
                    std::swap(operandsB[i], operandsB[j]);
                    selector.merge(i, opA, opjB);
                    merged = true;
                    break;
                }
            }
            // we couldn't find any candidates
            if (!merged) {
                selector.select(i, opA, opB);
            }
        }
        return selector;
    };

    void merge(llvm::BumpPtrAllocator &alloc, llvm::TargetTransformInfo &TTI,
               unsigned int vectorBits, Instruction *A, Instruction *B) {
        mergeList.emplace_back(A, B);
        auto aA = ancestorMap.find(B);
        auto aB = ancestorMap.find(A);
        assert(aA != ancestorMap.end());
        assert(aB != ancestorMap.end());
        // in the old MergingCost where they're separate instructions,
        // we leave their ancestor PtrMaps intact.
        // in the new MergingCost where they're the same instruction,
        // we assign them the same ancestor Claptrap.
        auto *merged =
            new (alloc) llvm::SmallPtrSet<Instruction *, 16>(*aA->second);
        merged->insert(aB->second->begin(), aB->second->end());
        aA->second = merged;
        aB->second = merged;
        size_t numSelects = mergeOperands(A, B, Count{});
        // TODO:
        // increase cost by numSelects, decrease cost by `I`'s cost
        unsigned int W = vectorBits / B->getNumScalarBits();
        if (numSelects)
            cost += numSelects * B->selectCost(TTI, W);
        cost -= B->getCost(TTI, W).recipThroughput;
        auto mB = findMerge(B);
        if (mB)
            cycleUpdateMerged(merged, B, mB);
        // fuse the merge map cycles
        if (auto mA = findMerge(A)) {
            cycleUpdateMerged(merged, A, mA);
            if (mB) {
                mergeMap[B] = mA;
                mergeMap[A] = mB;
            } else {
                mergeMap[B] = mA;
                mergeMap[A] = B;
            }
        } else if (mB) {
            mergeMap[A] = mB;
            mergeMap[B] = A;
        } else {
            mergeMap[B] = A;
            mergeMap[A] = B;
        }
    }
    constexpr auto operator<(const MergingCost &other) const -> bool {
        return cost < other.cost;
    }
    constexpr auto operator>(const MergingCost &other) const -> bool {
        return cost > other.cost;
    }
};

void mergeInstructions(
    llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
    Predicate::Map &predMap, llvm::TargetTransformInfo &TTI,
    unsigned int vectorBits,
    llvm::DenseMap<
        std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Type *>,
        llvm::SmallVector<std::pair<Instruction *, Predicate::Set>>> &opMap,
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
    auto op = I->getOpTripple();
    // TODO: confirm that `vec` doesn't get moved if `opMap` is resized
    auto &vec = opMap[op];
    // consider merging with every instruction sharing an opcode
    for (auto &pair : vec) {
        Instruction *other = pair.first;
        // check legality
        // illegal if:
        // 1. pred intersection not empty
        if (!preds.intersectionIsEmpty(pair.second))
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
            MC->merge(alloc, TTI, vectorBits, other, I);
        }
    }
    // descendants aren't legal merge candidates, so check before merging
    for (Instruction *U : I->getUsers()) {
        if (llvm::BasicBlock *BBU = U->getBasicBlock()) {
            if (BBU == BB) {
                // fast path, skip lookup
                mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                                  mergingCosts, U, BB, preds);
            } else if (auto f = predMap.find(BBU); f != predMap.rend()) {
                mergeInstructions(alloc, cache, predMap, TTI, vectorBits, opMap,
                                  mergingCosts, U, BBU, f->second);
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
/// TODO: this algorithm is exponential in time and memory.
/// Odds are that there's way smarter things we can do.
void mergeInstructions(llvm::BumpPtrAllocator &alloc,
                       llvm::BumpPtrAllocator &tAlloc,
                       Instruction::Cache &cache, Predicate::Map &predMap,
                       llvm::TargetTransformInfo &TTI,
                       unsigned int vectorBits) {
    if (!predMap.isDivergent())
        return;
    // there is a divergence in the control flow that we can ideally merge
    auto &opMap = *alloc.Allocate<llvm::DenseMap<
        std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Type *>,
        llvm::SmallVector<std::pair<Instruction *, Predicate::Set>>>>();
    llvm::SmallVector<MergingCost *> mergingCosts;
    mergingCosts.push_back(alloc.Allocate<MergingCost>());
    for (auto &pred : predMap) {
        for (llvm::Instruction &lI : *pred.first) {
            if (Instruction *I = cache[&lI]) {
                mergeInstructions(tAlloc, cache, predMap, TTI, vectorBits,
                                  opMap, mergingCosts, I, pred.first,
                                  pred.second);
            }
        }
    }
    // TODO:
    // pick the minimum cost mergingCost
    MergingCost *minCostStrategy = *std::ranges::min_element(
        mergingCosts, [](auto *a, auto *b) { return *a < *b; });
    // and then apply it to the instructions.
    llvm::DenseMap<Instruction *, Instruction *> reMap;
    // we use `alloc` for objects intended to live on
    for (auto &pair : *minCostStrategy) {
        // merge pair through `select`ing the arguments that differ
        auto [A, B] = pair;
        llvm::MutableArrayRef<Instruction *> operands =
            minCostStrategy->mergeOperands(A, B,
                                           MergingCost::Allocate{alloc, cache});
    }
    // free memory
    tAlloc.Reset();
}
