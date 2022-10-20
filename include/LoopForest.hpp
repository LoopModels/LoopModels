#pragma once
// #include "./CallableStructs.hpp"
#include "./Loops.hpp"
#include "BitSets.hpp"
#include "LoopBlock.hpp"
#include <iterator>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <vector>

struct LoopTree;
struct LoopForest {
    std::vector<LoopTree> loops;
    // definitions due to incomplete types
    bool pushBack(llvm::Loop *, LoopTree *, llvm::ScalarEvolution *,
                  std::vector<LoopForest> &);
    LoopForest() = default;
    LoopForest(std::vector<LoopTree> loops);
    // LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
    LoopForest(auto itb, auto ite) : loops(itb, ite){};
    size_t size() const;
    LoopTree &operator[](size_t);
    auto begin() { return loops.begin(); }
    auto begin() const { return loops.begin(); }
    auto end() { return loops.end(); }
    auto end() const { return loops.end(); }
    auto rbegin() { return loops.rbegin(); }
    auto rbegin() const { return loops.rbegin(); }
    auto rend() { return loops.rend(); }
    auto rend() const { return loops.rend(); }
};

// TODO: should depth be stored in LoopForests instead?
struct LoopTree {
    llvm::Loop *loop;
    AffineLoopNest affineLoop;
    LoopForest subLoops; // incomplete type, don't know size
    LoopTree *parentLoop;
    llvm::PHINode *indVar;
    size_t depth;
    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }
    llvm::Optional<llvm::Loop::LoopBounds>
    getBounds(llvm::ScalarEvolution *SE) {
        return loop->getBounds(*SE);
    }
};

LoopForest::LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};

// if AffineLoopNest
[[nodiscard]] bool LoopForest::pushBack(llvm::Loop *L, LoopTree *parentLoop,
                                        llvm::ScalarEvolution *SE,
                                        std::vector<LoopForest> &forests) {
    size_t d = parentLoop ? parentLoop->depth + 1 : 0;
    loops.push_back(
        LoopTree{L, {}, {}, parentLoop, L->getInductionVariable(*SE), d});
    LoopTree &newTree = loops.back();
    newTree.subLoops.loops.reserve(L->getSubLoops().size());
    // NOTE: loops contain subloops in program order (opposite of LoopInfo)
    BitSet failingLoops;
    auto &subLoops{L->getSubLoops()};
    for (size_t i = 0; i < subLoops.size(); ++i)
        if (newTree.subLoops.pushBack(subLoops[i], &newTree, SE, forests))
            failingLoops.uncheckedInsert(i);
    if (size_t nFail = failingLoops.size()) {
        if (nFail == subLoops.size()) // nothing to salvage
            return true;
        // We add all consecutive batches of non-failed trees as new forests;
        // These forests are now complete (with respect to the forest
        // initialization), so we don't need to return to them here.
        size_t low = 0;
        auto fit = failingLoops.begin();
        // we search for the first chunk that can be the start
        // of the vector we don't erase, which we push into `forests`
        size_t mid = *fit;
        while ((mid != low) && (fit != failingLoops.end())) {
            low = mid + 1;
            mid = *(++fit);
        }
        // [low, mid) is the range we preserve
        // now we search for all other ranges to push_back
        size_t n = mid + 1;
        for (; fit != failingLoops.end(); ++fit) {
            size_t o = *fit;
            if (o != n)
                forests.emplace_back(std::make_move_iterator(loops.begin()) + n,
                                     std::make_move_iterator(loops.begin()) +
                                         o);
            n = o + 1;
        }
        if (n != loops.size())
            forests.emplace_back(std::make_move_iterator(loops.begin()) + n,
                                 std::make_move_iterator(loops.end()));
        // now we remove the early bits
        if (low)
            loops.erase(loops.begin(), loops.begin() + low);
        loops.resize(mid - low);
        forests.push_back(LoopForest{std::move(loops)});
        return true;
    }

    // now that we have returned and all inner loops are valid,
    // we check this loop's induction variables.
    // We compare them vs the interior loops (descending in once again),
    // updating them and checking if they are still affine as a function
    // of this loop. If not, we salvage what we can, and return `true`
    // If still affine, we build this loops AffineLoopNest and return `false`.
    if (!L->isLoopSimplifyForm()) {
        forests.push_back(std::move(loops));
        return true;
    }
    return false;
}
size_t LoopForest::size() const { return loops.size(); }
LoopTree &LoopForest::operator[](size_t i) {
    assert(i < loops.size());
    return loops[i];
}
