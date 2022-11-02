#pragma once
// #include "./CallableStructs.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "ArrayReference.hpp"
#include "Macro.hpp"
#include "MemoryAccess.hpp"
#include <bits/ranges_algo.h>
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
    divergence,
    unreachable,
    returned,
    visited,
    unknown,
    loopexit
};
[[maybe_unused]] static BBChain allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    llvm::SmallVectorImpl<const llvm::BasicBlock *> &path,
    const llvm::BasicBlock *BBsrc, const llvm::BasicBlock *BBdst) {
    if (BBsrc == BBdst) {
        path.push_back(BBsrc);
        return BBChain::reached;
    } else if (visit(visitedBBs, BBsrc)) {
        return BBChain::visited;
    } else if (const llvm::Instruction *term = BBsrc->getTerminator()) {
        if (const llvm::BranchInst *BI =
                llvm::dyn_cast<llvm::BranchInst>(term)) {
            BBChain dst0 = allForwardPathsReach(visitedBBs, path,
                                                BI->getSuccessor(0), BBdst);
            if (!BI->isConditional()) {
                if (dst0 == BBChain::reached)
                    path.push_back(BBsrc);
                return dst0;
            }
            BBChain dst1 = allForwardPathsReach(visitedBBs, path,
                                                BI->getSuccessor(1), BBdst);
            // TODO handle divergences
            if (dst0 == BBChain::unreachable) {
                if (dst1 == BBChain::reached)
                    path.push_back(BBsrc);
                return dst1;
            } else if (dst1 == BBChain::unreachable) {
                if (dst0 == BBChain::reached)
                    path.push_back(BBsrc);
                return dst0;
            } else if ((dst0 == dst1) && (dst0 == BBChain::reached)) {
                return BBChain::divergence;
            } else
                return BBChain::unknown;
        }
    } else if (const llvm::UnreachableInst *UI =
                   llvm::dyn_cast<llvm::UnreachableInst>(term))
        // TODO: add option to allow moving earlier?
        return BBChain::unreachable;

    return BBChain::unknown;
}
[[maybe_unused]] static bool allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    llvm::SmallVectorImpl<const llvm::BasicBlock *> &path,
    llvm::ArrayRef<llvm::BasicBlock *> BBsrc, const llvm::BasicBlock *BBdst) {
    size_t reached = 0;
    llvm::SmallVector<const llvm::BasicBlock *> pathBackup;
    for (auto &BB : BBsrc) {
        auto dst = allForwardPathsReach(visitedBBs, path, BB, BBdst);
        if (dst == BBChain::reached) {
            path.push_back(BB);
            ++reached;
        } else if (dst != BBChain::unreachable)
            return false;
    }
    std::ranges::reverse(path);
    // TODO: handle divergences
    return reached == 1;
}

struct LoopTree {
    [[no_unique_address]] llvm::Loop *loop;
    [[no_unique_address]] llvm::SmallVector<unsigned> subLoops;
    // length number of sub loops + 1
    // - this loop's header to first loop preheader
    // - first loop's exit to next loop's preheader...
    // - etc
    // - last loop's exit to this loop's latch

    // in addition to requiring simplify form, we require a single exit block
    [[no_unique_address]] llvm::SmallVector<
        llvm::SmallVector<const llvm::BasicBlock *, 2>>
        paths;
    [[no_unique_address]] AffineLoopNest affineLoop;
    [[no_unique_address]] unsigned parentLoop{
        std::numeric_limits<unsigned>::max()};
    [[no_unique_address]] llvm::SmallVector<MemoryAccess, 0> memAccesses{};

    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }

    LoopTree(
        llvm::SmallVector<unsigned> sL,
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths)
        : loop(nullptr), subLoops(std::move(sL)), paths(std::move(paths)) {}

    LoopTree(
        llvm::Loop *L, llvm::SmallVector<unsigned> sL, const llvm::SCEV *BT,
        llvm::ScalarEvolution &SE,
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(L, BT, SE),
          parentLoop(std::numeric_limits<unsigned>::max()) {}

    LoopTree(
        llvm::Loop *L, AffineLoopNest aln, llvm::SmallVector<unsigned> sL,
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(std::move(aln)),
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
        const std::vector<llvm::Loop *> &subLoops{L->getSubLoops()};
        llvm::BasicBlock *H = L->getHeader();
        llvm::BasicBlock *E = L->getExitingBlock();
        bool anyFail = (E == nullptr) || (!L->isLoopSimplifyForm());
        pushBack(loopTrees, forests, branches, L, SE, subLoops, H, E, anyFail);
    }
    static size_t pushBack(llvm::SmallVectorImpl<LoopTree> &loopTrees,
                           llvm::SmallVector<unsigned> &forests,
                           llvm::SmallVector<unsigned> &branches, llvm::Loop *L,
                           llvm::ScalarEvolution &SE,
                           const std::vector<llvm::Loop *> &subLoops,
                           llvm::BasicBlock *H, llvm::BasicBlock *E,
                           bool anyFail) {
        // how to avoid double counting? Probably shouldn't be an issue:
        // can have an empty BB vector;
        // when splitting, we're in either scenario:
        // 1. We keep both loops but split because we don't have a direct path
        // -- not the case here!
        // 2. We're discarding one LoopTree; thus no duplication, give the BB to
        // the one we don't discard.
        //
        // approach:
        llvm::SmallVector<unsigned> subForest;
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths;
        llvm::SmallVector<const llvm::BasicBlock *, 2> path;
        size_t interiorDepth0 = 0;
        llvm::SmallPtrSet<const llvm::BasicBlock *, 32> visitedBBs;
        llvm::BasicBlock *finalStart;
        if (size_t numSubLoops = subLoops.size()) {
            llvm::SmallVector<llvm::BasicBlock *> exitBlocks;
            exitBlocks.push_back(H);
            llvm::BasicBlock *PB = H;
            llvm::Loop *P = nullptr;
            for (size_t i = 0; i < numSubLoops; ++i) {
                llvm::Loop *N = subLoops[i];
                if (P) {
                    exitBlocks.clear();
                    // if we have a previous loop, does
                    P->getExitBlocks(exitBlocks);
                    // reach
                    // subLoops[i]->getLoopPreheader();
                    visitedBBs.clear();
                }
                // find back from prev exit blocks to preheader of next
                llvm::BasicBlock *PH = N->getLoopPreheader();
                // exit block might == header block of next loop!
                // equivalently, exiting block of one loop may be preheader of
                // next! but we compare exit block with header here
                if (((exitBlocks.size() != 1) ||
                     (N->getHeader() != exitBlocks.front())) &&
                    (!allForwardPathsReach(visitedBBs, path, exitBlocks, PH))) {
                    P = nullptr;
                    anyFail = true;
                    split(loopTrees, forests, subForest, paths, subLoops, i);
                    exitBlocks.clear();
                    if (i + 1 < numSubLoops)
                        exitBlocks.push_back(
                            subLoops[i + 1]->getLoopPreheader());
                    llvm::SmallVector<const llvm::BasicBlock *, 2> vexit{
                        N->getLoopPreheader()};
                    paths.push_back(std::move(vexit));
                } else {
                    P = N;
                    paths.push_back(std::move(path));
                }
                path.clear();
                size_t itDepth = pushBack(loopTrees, forests, subForest, N, SE);
                if (itDepth == 0) {
                    P = nullptr;
                    anyFail = true;
                    // subForest.size() == 0 if we just hit the
                    // !allForwardPathsReach branch meaning it wouldn't need to
                    // push path However, if we didn't hit that branch, we
                    // pushed to path but not to subForest
                    assert(subForest.size() + 1 == paths.size());
                    paths.back().truncate(
                        1); // truncate last to drop extra blocks
                    split(loopTrees, forests, subForest, paths);
                    exitBlocks.clear();
                    if (i + 1 < numSubLoops)
                        exitBlocks.push_back(
                            subLoops[i + 1]->getLoopPreheader());
                } else if (i == 0) {
                    interiorDepth0 = itDepth;
                }
            }
            if (anyFail)
                return invalid(loopTrees, forests, subForest, paths, subLoops);
            assert(subForest.size());
            finalStart = subLoops.back()->getExitBlock();
        } else
            finalStart = H;
        if (subForest.size()) { // add subloops
            AffineLoopNest &subNest = loopTrees[subForest.front()].affineLoop;
            if (subNest.getNumLoops() > 1) {
                if (allForwardPathsReach(visitedBBs, path, finalStart, E) ==
                    BBChain::reached) {
                    branches.push_back(loopTrees.size());
                    paths.push_back(std::move(path));
                    loopTrees.emplace_back(L, subNest.removeInnerMost(),
                                           std::move(subForest),
                                           std::move(paths));
                    return ++interiorDepth0;
                }
            }
        } else if (auto BT = SE.getBackedgeTakenCount(L)) {
            if (!llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
                if (allForwardPathsReach(visitedBBs, path, finalStart, E) ==
                    BBChain::reached) {
                    branches.push_back(loopTrees.size());
                    paths.push_back(std::move(path));
                    loopTrees.emplace_back(L, std::move(subForest), BT, SE,
                                           std::move(paths));
                    return 1;
                }
            }
        }
        return invalid(loopTrees, forests, subForest, paths, subLoops);
    }

    [[maybe_unused]] static size_t invalid(
        llvm::SmallVectorImpl<LoopTree> &loopTrees,
        llvm::SmallVectorImpl<unsigned> &trees,
        llvm::SmallVector<unsigned> &subTree,
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths,
        const std::vector<llvm::Loop *> &subLoops) {
        assert(subTree.size() == paths.size());
        if (subTree.size()) {
            if (llvm::BasicBlock *exit = subLoops.back()->getExitingBlock()) {
                llvm::SmallVector<const llvm::BasicBlock *, 2> vexit{exit};
                paths.emplace_back(vexit);
                trees.push_back(loopTrees.size());
                loopTrees.emplace_back(std::move(subTree), std::move(paths));
            }
        }
        return 0;
    }
    [[maybe_unused]] static void
    split(llvm::SmallVectorImpl<LoopTree> &loopTrees,
          llvm::SmallVectorImpl<unsigned> &trees,
          llvm::SmallVector<unsigned> &subTree,
          llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>>
              paths) {
        if (subTree.size()) {
            trees.push_back(loopTrees.size());
            loopTrees.emplace_back(std::move(subTree), std::move(paths));
            subTree.clear();
            paths.clear();
        }
    }
    [[maybe_unused]] static void split(
        llvm::SmallVectorImpl<LoopTree> &loopTrees,
        llvm::SmallVectorImpl<unsigned> &trees,
        llvm::SmallVector<unsigned> &subTree,
        llvm::SmallVector<llvm::SmallVector<const llvm::BasicBlock *, 2>> paths,
        const std::vector<llvm::Loop *> &subLoops, size_t i) {
        if (i && subTree.size()) {
            if (llvm::BasicBlock *exit = subLoops[--i]->getExitingBlock()) {
                llvm::SmallVector<const llvm::BasicBlock *, 2> vexit{exit};
                paths.push_back(std::move(vexit));
                trees.push_back(loopTrees.size());
                loopTrees.emplace_back(std::move(subTree), std::move(paths));
                subTree.clear();
                paths.clear();
            }
        }
    }
    void dumpAllMemAccess(llvm::ArrayRef<LoopTree> loopTrees) const {
        for (auto &mem : memAccesses)
            SHOWLN(mem);
        for (auto id : subLoops)
            loopTrees[id].dumpAllMemAccess(loopTrees);
    }
};
