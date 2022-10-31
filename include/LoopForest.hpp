#pragma once
// #include "./CallableStructs.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "ArrayReference.hpp"
#include "Macro.hpp"
#include "MemoryAccess.hpp"
#include <cstddef>
#include <iterator>
#include <limits>
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

// struct LoopTree;
// struct LoopForest {
//     llvm::SmallVector<LoopTree *> loops;
//     // definitions due to incomplete types
//     size_t pushBack(llvm::SmallVectorImpl<LoopTree> &, llvm::Loop *,
//                     llvm::ScalarEvolution &,
//                     llvm::SmallVector<LoopForest, 0> &);
//     LoopForest() = default;
//     LoopForest(llvm::SmallVector<LoopTree *> loops);
//     // LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
//     LoopForest(auto itb, auto ite) : loops(itb, ite){};

//     inline size_t size() const;
//     static size_t invalid(llvm::SmallVector<LoopForest, 0> &forests,
//                           LoopForest forest);
//     inline LoopTree *operator[](size_t i) { return loops[i]; }
//     inline auto begin() { return loops.begin(); }
//     inline auto begin() const { return loops.begin(); }
//     inline auto end() { return loops.end(); }
//     inline auto end() const { return loops.end(); }
//     inline auto rbegin() { return loops.rbegin(); }
//     inline auto rbegin() const { return loops.rbegin(); }
//     inline auto rend() { return loops.rend(); }
//     inline auto rend() const { return loops.rend(); }
//     inline auto &front() { return loops.front(); }
//     inline void clear();
//     void addZeroLowerBounds(llvm::DenseMap<llvm::Loop *, LoopTree *> &);
// };
// llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree);
// TODO: should depth be stored in LoopForests instead?

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

struct LoopTree {
    [[no_unique_address]] llvm::Loop *loop;
    [[no_unique_address]] llvm::SmallVector<unsigned> subLoops;
    [[no_unique_address]] AffineLoopNest affineLoop;
    [[no_unique_address]] unsigned parentLoop{
        std::numeric_limits<unsigned>::max()};
    [[no_unique_address]] llvm::SmallVector<MemoryAccess, 0> memAccesses{};

    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }

    LoopTree(llvm::SmallVector<unsigned> sL)
        : loop(nullptr), subLoops(std::move(sL)) {}

    LoopTree(llvm::Loop *L, llvm::SmallVector<unsigned> sL,
             const llvm::SCEV *BT, llvm::ScalarEvolution &SE)
        : loop(L), subLoops(std::move(sL)), affineLoop(L, BT, SE),
          parentLoop(std::numeric_limits<unsigned>::max()) {}

    LoopTree(llvm::Loop *L, AffineLoopNest aln, llvm::SmallVector<unsigned> sL)
        : loop(L), subLoops(sL), affineLoop(std::move(aln)),
          parentLoop(std::numeric_limits<unsigned>::max()) {}
    // LoopTree(llvm::Loop *L, AffineLoopNest *aln, LoopForest sL)
    // : loop(L), subLoops(sL), affineLoop(aln), parentLoop(nullptr) {}

    // LoopTree(llvm::Loop *L, LoopForest sL, unsigned affineLoopID)
    //     : loop(L), subLoops(std::move(sL)), affineLoopID(affineLoopID),
    //       parentLoop(nullptr) {}
    size_t getNumLoops() const { return affineLoop.getNumLoops(); }

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const LoopTree &tree) {
        if (tree.loop) {
            os << (*tree.loop) << "\n" << tree.affineLoop << "\n";
        } else {
            os << "top-level:\n";
        }
        for (auto branch : tree.subLoops)
            os << branch;
        return os << "\n";
    }
    llvm::raw_ostream &dump(llvm::raw_ostream &os,
                            llvm::ArrayRef<LoopTree> loopTrees) const {
        if (loop) {
            os << (*loop) << "\n" << affineLoop << "\n";
        } else {
            os << "top-level:\n";
        }
        for (auto branch : subLoops)
            loopTrees[branch].dump(os, loopTrees);
        return os << "\n";
    }
    llvm::raw_ostream &dump(llvm::ArrayRef<LoopTree> loopTrees) const {
        return dump(llvm::errs(), loopTrees);
    }
    void addZeroLowerBounds(llvm::MutableArrayRef<LoopTree> loopTrees,
                            llvm::DenseMap<llvm::Loop *, unsigned> &loopMap,
                            unsigned myId) {
        SHOWLN(this);
        // SHOWLN(affineLoop.A);
        affineLoop.addZeroLowerBounds();
        for (auto tid : subLoops) {
            auto &tree = loopTrees[tid];
            tree.addZeroLowerBounds(loopTrees, loopMap, tid);
            tree.parentLoop = myId;
        }
        loopMap.insert(std::make_pair(loop, myId));
    }
    auto begin() { return subLoops.begin(); }
    auto end() { return subLoops.end(); }
    auto begin() const { return subLoops.begin(); }
    auto end() const { return subLoops.end(); }
    size_t size() const { return subLoops.size(); }

    // try to add Loop L, as well as all of L's subLoops
    // if invalid, create a new LoopForest, and add it to forests instead
    // loopTrees are the cache of all LoopTrees
    static size_t pushBack(llvm::SmallVectorImpl<LoopTree> &loopTrees,
                           llvm::SmallVector<unsigned> &forests,
                           llvm::SmallVector<unsigned> &branches, llvm::Loop *L,
                           llvm::ScalarEvolution &SE) {
        auto &subLoops{L->getSubLoops()};
        llvm::SmallVector<unsigned> subForest;
        size_t interiorDepth0 = 0;
        if (subLoops.size()) {
            llvm::SmallVector<llvm::BasicBlock *> exitBlocks;
            llvm::SmallPtrSet<const llvm::BasicBlock *, 32> visitedBBs;
            bool anyFail = false;
            llvm::Loop *P = nullptr;
            for (size_t i = 0; i < subLoops.size(); ++i) {
                llvm::Loop *N = subLoops[i];
                if (P) {
                    exitBlocks.clear();
                    // if we have a previous loop, does
                    P->getExitBlocks(exitBlocks);
                    // reach
                    // subLoops[i]->getLoopPreheader();
                    visitedBBs.clear();
                    llvm::BasicBlock *PH = N->getLoopPreheader();
                    if (!allForwardPathsReach(visitedBBs, exitBlocks, PH)) {
                        anyFail = true;
                        split(loopTrees, forests, subForest);
                        P = nullptr;
                    } else
                        P = N;
                } else
                    P = N;
                size_t itDepth = pushBack(loopTrees, forests, subForest, P, SE);
                if (itDepth == 0) {
                    anyFail = true;
                    P = nullptr;
                    split(loopTrees, forests, subForest);
                } else if (i == 0) {
                    interiorDepth0 = itDepth;
                }
            }
            if (anyFail)
                return invalid(loopTrees, forests, subForest);
            assert(subForest.size());
        }
        if (subForest.size()) { // add subloops
            AffineLoopNest &subNest = loopTrees[subForest.front()].affineLoop;
            if (subNest.getNumLoops() > 1) {
                branches.push_back(loopTrees.size());
                loopTrees.emplace_back(L, subNest.removeInnerMost(),
                                       std::move(subForest));
                return ++interiorDepth0;
            }
        } else if (auto BT = SE.getBackedgeTakenCount(L)) {
            if (!llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
                branches.push_back(loopTrees.size());
                loopTrees.emplace_back(L, std::move(subForest), BT, SE);
                return 1;
            }
        }
        return invalid(loopTrees, forests, subForest);
    }

    [[maybe_unused]] static size_t
    invalid(llvm::SmallVectorImpl<LoopTree> &loopTrees,
            llvm::SmallVectorImpl<unsigned> &trees,
            llvm::SmallVector<unsigned> &subTree) {
        if (subTree.size()) {
            trees.push_back(loopTrees.size());
            loopTrees.emplace_back(std::move(subTree));
        }
        return 0;
    }
    [[maybe_unused]] static void
    split(llvm::SmallVectorImpl<LoopTree> &loopTrees,
          llvm::SmallVectorImpl<unsigned> &trees,
          llvm::SmallVector<unsigned> &subTree) {
        if (subTree.size()) {
            trees.push_back(loopTrees.size());
            loopTrees.emplace_back(std::move(subTree));
            subTree.clear();
        }
    }
    void dumpAllMemAccess(llvm::ArrayRef<LoopTree> loopTrees) const {
        for (auto &mem : memAccesses)
            SHOWLN(mem);
        for (auto id : subLoops)
            loopTrees[id].dumpAllMemAccess(loopTrees);
    }
};
