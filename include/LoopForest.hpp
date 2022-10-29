#pragma once
// #include "./CallableStructs.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "ArrayReference.hpp"
#include "Macro.hpp"
#include <cstddef>
#include <iterator>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>
#include <vector>

struct LoopTree;
struct LoopForest {
    std::vector<LoopTree> loops;
    // definitions due to incomplete types
    size_t pushBack(llvm::SmallVector<AffineLoopNest, 0> &, llvm::Loop *,
                    llvm::ScalarEvolution &, std::vector<LoopForest> &);
    LoopForest() = default;
    LoopForest(std::vector<LoopTree> loops);
    // LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
    LoopForest(auto itb, auto ite) : loops(itb, ite){};

    inline size_t size() const;
    static size_t invalid(std::vector<LoopForest> &forests, LoopForest forest);
    inline LoopTree &operator[](size_t);
    inline auto begin() { return loops.begin(); }
    inline auto begin() const { return loops.begin(); }
    inline auto end() { return loops.end(); }
    inline auto end() const { return loops.end(); }
    inline auto rbegin() { return loops.rbegin(); }
    inline auto rbegin() const { return loops.rbegin(); }
    inline auto rend() { return loops.rend(); }
    inline auto rend() const { return loops.rend(); }
    inline auto &front() { return loops.front(); }
    inline void clear();
    void addZeroLowerBounds(llvm::MutableArrayRef<AffineLoopNest>,
                            llvm::DenseMap<llvm::Loop *, LoopTree *> &);
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree);
// TODO: should depth be stored in LoopForests instead?
struct LoopTree {
    llvm::Loop *loop;
    LoopForest subLoops; // incomplete type, don't know size
    unsigned affineLoopID;
    LoopTree *parentLoop;
    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }
    // llvm::Optional<llvm::Loop::LoopBounds>
    // getBounds(llvm::ScalarEvolution *SE) {
    //     return loop->getBounds(*SE);
    // }
    // LoopTree(llvm::Loop *L, LoopForest sL, const llvm::SCEV *BT,
    //          llvm::ScalarEvolution &SE)
    //     : loop(L), affineLoop(L, BT, SE), subLoops(std::move(sL)),
    //       parentLoop(nullptr) {
    //     // initialize the AffineLoopNest
    //     llvm::errs() << "new loop";
    //     CSHOWLN(affineLoop.getNumLoops());
    //     SHOWLN(affineLoop.A);
    //     for (auto v : affineLoop.symbols)
    //         SHOWLN(*v);
    // }
    // LoopTree(llvm::Loop *L, AffineLoopNest aln, LoopForest sL)
    //     : loop(L), affineLoop(aln), subLoops(sL), parentLoop(nullptr) {}

    LoopTree(llvm::Loop *L, LoopForest sL, unsigned affineLoopID)
        : loop(L), subLoops(std::move(sL)), affineLoopID(affineLoopID),
          parentLoop(nullptr) {}

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const LoopTree &tree) {
        return os << (*tree.loop) << "\n" << tree.subLoops << "\n";
    }
    void
    addZeroLowerBounds(llvm::MutableArrayRef<AffineLoopNest> affineLoopNests,
                       llvm::DenseMap<llvm::Loop *, LoopTree *> &loopMap) {
        affineLoopNests[affineLoopID].addZeroLowerBounds();
        for (auto &&tree : subLoops) {
            tree.addZeroLowerBounds(affineLoopNests, loopMap);
            tree.parentLoop = this;
        }
        loopMap.insert(std::make_pair(loop, this));
    }
    void setParentLoops() {
        for (auto &&tree : subLoops)
            tree.parentLoop = this;
    }
    void setParentLoopsRecursive() {
        for (auto &&tree : subLoops) {
            tree.parentLoop = this;
            tree.setParentLoopsRecursive();
        }
    }
};

LoopForest::LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
void LoopForest::clear() { loops.clear(); }
inline size_t LoopForest::invalid(std::vector<LoopForest> &forests,
                                  LoopForest forest) {
    if (forest.size())
        forests.push_back(std::move(forest));
    return 0;
}
void LoopForest::addZeroLowerBounds(
    llvm::MutableArrayRef<AffineLoopNest> affineLoopNests,
    llvm::DenseMap<llvm::Loop *, LoopTree *> &loopMap) {
    for (auto &&tree : loops)
        tree.addZeroLowerBounds(affineLoopNests, loopMap);
}

[[maybe_unused]] static bool
visit(llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
      const llvm::BasicBlock *BB) {
    if (visitedBBs.contains(BB))
        return true;
    visitedBBs.insert(BB);
    return false;
}
enum class BBChain {
    reached,
    split,
    unreachable,
    returned,
    visited,
    unknown,
    loopexit
};
[[maybe_unused]] static void split(std::vector<LoopForest> &forests,
                                   LoopForest &subForest) {
    if (subForest.size()) {
        forests.push_back(std::move(subForest));
        subForest.clear();
    }
}
[[maybe_unused]] static BBChain allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    const llvm::BasicBlock *BBsrc, const llvm::BasicBlock *BBdst) {
    if (BBsrc == BBdst) {
        return BBChain::reached;
    } else if (visit(visitedBBs, BBsrc)) {
        return BBChain::visited;
    } else if (const llvm::Instruction *term = BBsrc->getTerminator()) {
        if (const llvm::BranchInst *BI =
                llvm::dyn_cast<llvm::BranchInst>(term)) {
            BBChain dst0 =
                allForwardPathsReach(visitedBBs, BI->getSuccessor(0), BBdst);
            if (!BI->isConditional())
                return dst0;
            BBChain dst1 =
                allForwardPathsReach(visitedBBs, BI->getSuccessor(1), BBdst);
            if ((dst0 == BBChain::unreachable) || (dst0 == dst1)) {
                return dst1;
            } else if (dst1 == BBChain::unreachable) {
                return dst0;
            } else
                return BBChain::split;
        }
    } else if (const llvm::UnreachableInst *UI =
                   llvm::dyn_cast<llvm::UnreachableInst>(term))
        // TODO: add option to allow moving earlier?
        return BBChain::unreachable;

    return BBChain::unknown;
}
[[maybe_unused]] static bool allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    llvm::ArrayRef<llvm::BasicBlock *> BBsrc, const llvm::BasicBlock *BBdst) {
    for (auto &BB : BBsrc)
        if (allForwardPathsReach(visitedBBs, BB, BBdst) != BBChain::reached)
            return false;
    return BBsrc.size() > 0;
}

// try to add Loop L, as well as all of L's subLoops
// if invalid, create a new LoopForest, and add it to forests instead
size_t
LoopForest::pushBack(llvm::SmallVector<AffineLoopNest, 0> &affineLoopNests,
                     llvm::Loop *L, llvm::ScalarEvolution &SE,
                     std::vector<LoopForest> &forests) {
    auto &subLoops{L->getSubLoops()};
    LoopForest subForest;
    size_t interiorDepth0 = 0;
    if (subLoops.size()) {
        llvm::SmallVector<llvm::BasicBlock *> exitBlocks;
        llvm::SmallPtrSet<const llvm::BasicBlock *, 32> visitedBBs;
        bool anyFail = false;
        llvm::Loop *P = nullptr;
        for (size_t i = 0; i < subLoops.size(); ++i) {
            llvm::Loop *N = subLoops[i];
            if (P) {
                // if we have a previous loop, does
                // P->getExitBlocks(exitBlocks);
                exitBlocks.clear();
                P->getExitBlocks(exitBlocks);
                // reach
                // subLoops[i]->getLoopPreheader();
                // we need exactly 1 reachCount
                visitedBBs.clear();
                llvm::BasicBlock *PH = N->getLoopPreheader();
                if (!allForwardPathsReach(visitedBBs, exitBlocks, PH)) {
                    anyFail = true;
                    split(forests, subForest);
                    P = nullptr;
                } else
                    P = N;
            } else
                P = N;
            size_t itDepth = subForest.pushBack(affineLoopNests, P, SE, forests);
            if (itDepth == 0) {
                anyFail = true;
                P = nullptr;
                split(forests, subForest);
            } else if (i == 0) {
                interiorDepth0 = itDepth;
            }
        }
        if (anyFail)
            return LoopForest::invalid(forests, std::move(subForest));
        assert(subForest.size());
    }
    if (subForest.size()) { // add subloops
        AffineLoopNest &subNest = subForest.front().affineLoop;
        if (subNest.getNumLoops() > 1) {
            loops.emplace_back(L, subNest.removeInnerMost(),
                               std::move(subForest));
            return ++interiorDepth0;
        }
    } else if (auto BT = SE.getBackedgeTakenCount(L)) {
        if (!llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
            loops.emplace_back(L, std::move(subForest), BT, SE);
            return 1;
        }
    }
    return LoopForest::invalid(forests, std::move(subForest));
}
size_t LoopForest::size() const { return loops.size(); }
LoopTree &LoopForest::operator[](size_t i) {
    assert(i < loops.size());
    return loops[i];
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree) {
    for (auto &loop : tree.loops)
        os << loop << "\n";
    return os << "\n\n";
}
