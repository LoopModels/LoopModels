#pragma once
// #include "./CallableStructs.hpp"
#include "./Loops.hpp"
#include "BitSets.hpp"
#include "LoopBlock.hpp"
#include <iterator>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <vector>

struct LoopTree;
struct LoopForest {
    std::vector<LoopTree> loops;
    // definitions due to incomplete types
    bool pushBack(llvm::Loop *, llvm::ScalarEvolution *,
                  std::vector<LoopForest> &);
    bool pushBack(llvm::Loop *, LoopTree *, llvm::ScalarEvolution *,
                  std::vector<LoopForest> &);
    LoopForest() = default;
    LoopForest(std::vector<LoopTree> loops);
    // LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
    LoopForest(auto itb, auto ite) : loops(itb, ite){};

    inline size_t size() const;
    static bool invalid(std::vector<LoopForest> &forests, LoopForest forest);
    inline LoopTree &operator[](size_t);
    inline auto begin() { return loops.begin(); }
    inline auto begin() const { return loops.begin(); }
    inline auto end() { return loops.end(); }
    inline auto end() const { return loops.end(); }
    inline auto rbegin() { return loops.rbegin(); }
    inline auto rbegin() const { return loops.rbegin(); }
    inline auto rend() { return loops.rend(); }
    inline auto rend() const { return loops.rend(); }
    inline void clear();
};

// TODO: should depth be stored in LoopForests instead?
struct LoopTree {
    llvm::Loop *loop;
    AffineLoopNest affineLoop;
    LoopForest subLoops; // incomplete type, don't know size
    LoopTree *parentLoop;
    llvm::PHINode *indVar;
    llvm::Loop::LoopBounds bounds;
    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }
    llvm::Optional<llvm::Loop::LoopBounds>
    getBounds(llvm::ScalarEvolution *SE) {
        return loop->getBounds(*SE);
    }
    LoopTree(llvm::Loop *L, LoopForest subLoops, llvm::PHINode *indVar,
             llvm::Loop::LoopBounds bounds)
        : loop(L), affineLoop({}), subLoops(std::move(subLoops)),
          parentLoop(nullptr), indVar(indVar), bounds(std::move(bounds)) {
        // initialize the AffineLoopNest
    }
    bool addOuterLoop(llvm::Loop *OL, const llvm::Loop::LoopBounds &LB,
                      llvm::PHINode *indVar) {

        return true;
    }
};

LoopForest::LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
void LoopForest::clear() { loops.clear(); }
bool LoopForest::invalid(std::vector<LoopForest> &forests, LoopForest forest) {
    if (forest.size())
        forests.push_back(std::move(forest));
    return true;
}

// try to add Loop L, as well as all of L's subLoops
// if invalid, create a new LoopForest, and add it to forests instead
[[nodiscard]] bool LoopForest::pushBack(llvm::Loop *L,
                                        llvm::ScalarEvolution *SE,
                                        std::vector<LoopForest> &forests) {
    auto &subLoops{L->getSubLoops()};
    LoopForest subForest;
    if (subLoops.size()) {
        bool anyFail = false;
        for (size_t i = 0; i < subLoops.size(); ++i) {
            if (subForest.pushBack(subLoops[i], SE, forests)) {
                anyFail = true;
                if (subForest.size()) {
                    forests.push_back(std::move(subForest));
                    subForest.clear();
                }
            }
        }
        if (anyFail)
            return LoopForest::invalid(forests, std::move(subForest));
        assert(subForest.size());
    }

    if (llvm::Optional<llvm::Loop::LoopBounds> LB = L->getBounds(*SE)) {
        if (llvm::ConstantInt *step =
                llvm::dyn_cast<llvm::ConstantInt>(LB->getStepValue())) {
	    if (!step->isOne()) // TODO: canonicalize?
		return LoopForest::invalid(forests, std::move(subForest));
            if (llvm::PHINode *indVar = L->getInductionVariable(*SE)) {
                if (subForest.size()) {
                    LoopForest backupForest;
                    for (auto &SLT : subForest)
                        if (SLT.addOuterLoop(L, *LB, indVar)) {
                            forests.push_back(std::move(backupForest));
                            return true;
                        }
                }
                loops.emplace_back(L, std::move(subForest), indVar, *LB);
                return false;
            }
        }
    }
    return LoopForest::invalid(forests, std::move(subForest));
}
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
        return invalid(forests);
    }

    // now that we have returned and all inner loops are valid,
    // we check this loop's induction variables.
    // We compare them vs the interior loops (descending in once again),
    // updating them and checking if they are still affine as a function
    // of this loop. If not, we salvage what we can, and return `true`
    // If still affine, we build this loops AffineLoopNest and return `false`.
    if (!L->isLoopSimplifyForm())
        return invalid(forests);
    llvm::Optional<llvm::Loop::LoopBounds> LB = L->getBounds(*SE);
    if (!LB)
        return invalid(forests);

    return false;
}
size_t LoopForest::size() const { return loops.size(); }
LoopTree &LoopForest::operator[](size_t i) {
    assert(i < loops.size());
    return loops[i];
}
