#pragma once
// #include "./CallableStructs.hpp"
#include "./ArrayReference.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./MemoryAccess.hpp"
#include "./Predicate.hpp"
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
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const BBChain &chn) {
    switch (chn) {
    case BBChain::reached:
        return os << "reached";
    case BBChain::divergence:
        return os << "divergence";
    case BBChain::unreachable:
        return os << "unreachable";
    case BBChain::returned:
        return os << "returned";
    case BBChain::visited:
        return os << "visited";
    case BBChain::unknown:
        return os << "unknown";
    case BBChain::loopexit:
        return os << "loop exit";
    default:
        assert(false && "unreachable");
    }
}

// TODO:
// 1. see why L->contains(BBsrc) does not work; does it only contain BBs in it
// directly, and not nested another loop deeper?
// 2. We are ignoring cycles for now; we must ensure this is done correctly
[[maybe_unused]] static BBChain allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    PredicatedChain &path, llvm::BasicBlock *BBsrc, llvm::BasicBlock *BBdst,
    Predicates pred, llvm::BasicBlock *BBhead, llvm::Loop *L) {
    llvm::errs() << "allForwardPathsReached BBsrc = " << BBsrc
                 << "\nBBdst = " << BBdst;
    llvm::errs() << "\nallForwardPathsReached BBsrc = " << *BBsrc
                 << "\nBBdst = " << *BBdst;
    for (auto &BBinPath : path)
        SHOWLN(BBinPath.basicBlock);
    if (L)
        llvm::errs() << "\nL->contains(BBsrc) = " << L->contains(BBsrc);
    llvm::errs() << "\n\n";
    if (BBsrc == BBdst) {
        SHOWLN(BBsrc);
        path.emplace_back(std::move(pred), BBsrc);
        llvm::errs() << "reached\n";
        return BBChain::reached;
    } else if (L && (!(L->contains(BBsrc)))) {
        // oops, we seem to have skipped the preheader in entering L
        // must skip over a guard
        llvm::errs() << "Exited the loop!\n";
        // llvm::errs() << "Skipped preheader! There must've been some sort of "
        // "loop guard\n";
        // TODO: give a more appropriate enum value?
        return BBChain::returned;
    } else if (visit(visitedBBs, BBsrc)) {
        if (BBsrc == BBhead) // TODO: add another enum?
            return BBChain::returned;
        // TODO: need to be able to handle temporarily split and rejoined path
        llvm::errs() << "BBhead = " << *BBhead << "\n";
        if (path.contains(BBsrc))
            return BBChain::reached;
        llvm::errs() << "Returning returned because already visited\n";
        return BBChain::returned;
        // return BBChain::unknown;
        // return BBChain::visited;
    } else if (const llvm::Instruction *term = BBsrc->getTerminator()) {
        llvm::errs() << "Checking terminator\n";
        SHOWLN(*term);
        if (const llvm::BranchInst *BI =
                llvm::dyn_cast<llvm::BranchInst>(term)) {
            SHOWLN(BI->isUnconditional());
            // SHOWLN(*BI->getSuccessor(0));
            if (BI->isUnconditional()) {
                BBChain dst0 =
                    allForwardPathsReach(visitedBBs, path, BI->getSuccessor(0),
                                         BBdst, pred, BBhead, L);
                if (dst0 == BBChain::reached)
                    SHOWLN(BBsrc);
                if (dst0 == BBChain::reached)
                    path.emplace_back(std::move(pred), BBsrc);
                return dst0;
            }
            // SHOWLN(*BI->getSuccessor(1));
            Predicates conditionedPred = pred & BI->getCondition();
            BBChain dst0 =
                allForwardPathsReach(visitedBBs, path, BI->getSuccessor(0),
                                     BBdst, conditionedPred, BBhead, L);
            // if ((dst0 != BBChain::reached) && (dst0 != BBChain::unreachable))
            llvm::errs() << "dst0 = " << dst0 << "\n";
            if (dst0 == BBChain::unknown)
                return BBChain::unknown; // if bad values, return early
            BBChain dst1 = allForwardPathsReach(
                visitedBBs, path, BI->getSuccessor(1), BBdst,
                std::move(conditionedPred.flipLastCondition()), BBhead, L);
            llvm::errs() << "dst0 = " << dst0 << "; dst1 = " << dst1 << "\n";

            // TODO handle divergences
            if ((dst0 == BBChain::unreachable) || (dst0 == BBChain::returned)) {
                if (dst1 == BBChain::reached)
                    SHOWLN(BBsrc);
                if (dst1 == BBChain::reached)
                    path.conditionOnLastPred().emplace_back(std::move(pred),
                                                            BBsrc);
                return dst1;
            } else if ((dst1 == BBChain::unreachable) ||
                       (dst1 == BBChain::returned)) {
                if (dst0 == BBChain::reached)
                    SHOWLN(BBsrc);
                if (dst0 == BBChain::reached)
                    path.conditionOnLastPred().emplace_back(std::move(pred),
                                                            BBsrc);
                return dst0;
            } else if (dst0 == dst1) {
                if (dst0 == BBChain::reached)
                    SHOWLN(BBsrc);
                if (dst0 == BBChain::reached)
                    path.emplace_back(std::move(pred), BBsrc);
                return dst0;
            } else {
                llvm::errs() << "Returning unknown because dst0 = " << dst0
                             << " and dst1 = " << dst1 << " di\n";
                return BBChain::unknown;
            }
        } else if (const llvm::UnreachableInst *UI =
                       llvm::dyn_cast<llvm::UnreachableInst>(term))
            // TODO: add option to allow moving earlier?
            return BBChain::unreachable;
        else if (const llvm::ReturnInst *RI =
                     llvm::dyn_cast<llvm::ReturnInst>(term))
            return BBChain::returned;
    }
    llvm::errs() << "\nReturning unknown because we fell through\n";
    return BBChain::unknown;
}
[[maybe_unused]] static bool allForwardPathsReach(
    llvm::SmallPtrSet<const llvm::BasicBlock *, 32> &visitedBBs,
    PredicatedChain &path, llvm::ArrayRef<llvm::BasicBlock *> BBsrc,
    llvm::BasicBlock *BBdst, llvm::Loop *L) {
    visitedBBs.clear();
    bool reached = false;
    for (auto &BB : BBsrc) {
        if (BB == BBdst) {
            reached = true;
            path.push_back(BB);
            continue;
        }
        auto dst = allForwardPathsReach(visitedBBs, path, BB, BBdst, {}, BB, L);
        if (dst == BBChain::reached) {
            reached = true;
#ifndef NDEBUG
            bool foundBB = false;
            SHOWLN(BB);
            for (auto &BBinPathFinal : path) {
                SHOWLN(BBinPathFinal.basicBlock);
                foundBB |= (BBinPathFinal.basicBlock == BB);
            }
            assert(foundBB);
#endif
            // } else if (dst != BBChain::unreachable) {
        } else if (dst == BBChain::unknown) {
            llvm::errs() << "failed because dst was: " << dst << "\n";
            return false;
        }
    }
    path.reverse();
    return reached;
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
    [[no_unique_address]] llvm::SmallVector<PredicatedChain> paths;
    [[no_unique_address]] AffineLoopNest<true> affineLoop;
    [[no_unique_address]] unsigned parentLoop{
        std::numeric_limits<unsigned>::max()};
    [[no_unique_address]] llvm::SmallVector<MemoryAccess, 0> memAccesses{};

    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }

    LoopTree(llvm::SmallVector<unsigned> sL,
             llvm::SmallVector<PredicatedChain> paths)
        : loop(nullptr), subLoops(std::move(sL)), paths(std::move(paths)) {}

    LoopTree(llvm::Loop *L, llvm::SmallVector<unsigned> sL,
             const llvm::SCEV *BT, llvm::ScalarEvolution &SE,
             llvm::SmallVector<PredicatedChain> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(L, BT, SE),
          parentLoop(std::numeric_limits<unsigned>::max()) {
#ifndef NDEBUG
        if (loop)
            for (auto &&chain : paths)
                for (auto &&pbb : chain)
                    assert(loop->contains(pbb.basicBlock));
#endif
    }

    LoopTree(llvm::Loop *L, AffineLoopNest<true> aln,
             llvm::SmallVector<unsigned> sL,
             llvm::SmallVector<PredicatedChain> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(std::move(aln)),
          parentLoop(std::numeric_limits<unsigned>::max()) {
#ifndef NDEBUG
        if (loop)
            for (auto &&chain : paths)
                for (auto &&pbb : chain)
                    assert(loop->contains(pbb.basicBlock));
#endif
    }
    // LoopTree(llvm::Loop *L, AffineLoopNest<true> *aln, LoopForest sL)
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
        // SHOWLN(this);
        // SHOWLN(affineLoop.A);
        affineLoop.addZeroLowerBounds();
        for (auto tid : subLoops) {
            auto &tree = loopTrees[tid];
            tree.addZeroLowerBounds(loopTrees, loopMap, tid);
            tree.parentLoop = myId;
        }
        if (loop)
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
        if (anyFail)
            SHOWLN(E);
        if (anyFail)
            SHOWLN(L->isLoopSimplifyForm());
        return pushBack(loopTrees, forests, branches, L, SE, subLoops, H, E,
                        anyFail);
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
        if (L) {
            llvm::errs() << "Current pushBack depth = " << L->getLoopDepth()
                         << "\n";
            SHOWLN(*L);
        } else
            llvm::errs() << "Current pushBack depth = toplevel\n";
        llvm::SmallVector<unsigned> subForest;
        llvm::SmallVector<PredicatedChain> paths;
        PredicatedChain path;
        size_t interiorDepth0 = 0;
        llvm::SmallPtrSet<const llvm::BasicBlock *, 32> visitedBBs;
        llvm::BasicBlock *finalStart;
        if (size_t numSubLoops = subLoops.size()) {
            llvm::SmallVector<llvm::BasicBlock *> exitBlocks;
            exitBlocks.push_back(H);
            // llvm::BasicBlock *PB = H;
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
                // llvm::errs() << ""
                SHOWLN(*N);
                if (llvm::BranchInst *G = N->getLoopGuardBranch()) {
                    llvm::errs() << "Loop Guard:\n" << *G << "\n";
                }
                llvm::BasicBlock *PH = N->getLoopPreheader();
                // exit block might == header block of next loop!
                // equivalently, exiting block of one loop may be preheader of
                // next! but we compare exit block with header here
                llvm::errs() << "All BBs in *N:\n";
                for (auto B : *N)
                    llvm::errs() << *B;
                llvm::errs() << "\n";
                if (((exitBlocks.size() != 1) ||
                     (N->getHeader() != exitBlocks.front())) &&
                    (!allForwardPathsReach(visitedBBs, path, exitBlocks, PH,
                                           L))) {
                    llvm::errs() << "path failed for loop :" << *N << "\n";
                    P = nullptr;
                    anyFail = true;
                    split(loopTrees, forests, subForest, paths, subLoops, i);
                    exitBlocks.clear();
                    if (i + 1 < numSubLoops)
                        exitBlocks.push_back(
                            subLoops[i + 1]->getLoopPreheader());
                    paths.emplace_back(N->getLoopPreheader());
                } else {
                    P = N;
                    paths.push_back(std::move(path));
                }
                path.clear();
                llvm::errs()
                    << "pre-pushBack (subForest.size(),paths.size()) = ("
                    << subForest.size() << ", " << paths.size() << ")\n";
                size_t itDepth = pushBack(loopTrees, forests, subForest, N, SE);
                llvm::errs()
                    << "post-pushBack (subForest.size(),paths.size()) = ("
                    << subForest.size() << ", " << paths.size() << ")\n";
                SHOWLN(itDepth);
                if (itDepth == 0) {
                    llvm::errs() << "recursion failed for loop :" << *N << "\n";
                    P = nullptr;
                    anyFail = true;
                    // subForest.size() == 0 if we just hit the
                    // !allForwardPathsReach branch meaning it wouldn't need to
                    // push path However, if we didn't hit that branch, we
                    // pushed to path but not to subForest
                    assert(subForest.size() + 1 == paths.size());
                    // truncate last to drop extra blocks
                    paths.back().truncate(1);
                    split(loopTrees, forests, subForest, paths);
                    exitBlocks.clear();
                    if (i + 1 < numSubLoops)
                        exitBlocks.push_back(
                            subLoops[i + 1]->getLoopPreheader());
                } else if (i == 0) {
                    interiorDepth0 = itDepth;
                }
            }
            // assert(paths.size() == subForest.size());
            // bug: anyFail == true, subForest.size() == 1, paths.size() == 2
            if (anyFail)
                llvm::errs()
                    << "pushBack returning 0 because anyFail == true.\n";
            if (anyFail)
                return invalid(loopTrees, forests, subForest, paths, subLoops);
            assert(subForest.size());
            finalStart = subLoops.back()->getExitBlock();
        } else
            finalStart = H;
        llvm::errs() << "Starting second pass in pushBack\n";
        SHOWLN(subForest.size());
        if (subForest.size()) { // add subloops
            AffineLoopNest<true> &subNest =
                loopTrees[subForest.front()].affineLoop;
            SHOWLN(subNest.getNumLoops());
            if (subNest.getNumLoops() > 1) {
                visitedBBs.clear();
                if (allForwardPathsReach(visitedBBs, path, finalStart, E, L)) {
                    branches.push_back(loopTrees.size());
                    paths.push_back(std::move(path));
                    loopTrees.emplace_back(L, subNest.removeInnerMost(),
                                           std::move(subForest),
                                           std::move(paths));
                    return ++interiorDepth0;
                } else {
                    llvm::errs() << "No direct path from:\n"
                                 << *finalStart << "\nTo:\n"
                                 << *E << "\n";
                }
            }
            // } else if (auto BT = SE.getBackedgeTakenCount(L)) {
        } else if (auto BT = getBackedgeTakenCount(SE, L)) {
            if (!llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
                llvm::errs() << "about to add loop: " << *L
                             << "\nwith backedge taken count: " << *BT << "\n";
                auto *BTNW = noWrapSCEV(SE, BT);
                llvm::errs() << "after no-wrapping:\n" << *BTNW << "\n";
                if (allForwardPathsReach(visitedBBs, path, finalStart, E, L)) {
                    branches.push_back(loopTrees.size());
                    paths.push_back(std::move(path));
                    loopTrees.emplace_back(L, std::move(subForest), BTNW, SE,
                                           std::move(paths));
                    return 1;
                }
            }
        }
        llvm::errs()
            << "pushBack returning 0 because end of function reached.\nLoop: "
            << *L << "\n";
        SHOW(subForest.size());
        if (subForest.size()) {
            CSHOWLN(loopTrees[subForest.front()].getNumLoops());
        } else
            llvm::errs() << "\n";
        return invalid(loopTrees, forests, subForest, paths, subLoops);
    }

    [[maybe_unused]] static size_t
    invalid(llvm::SmallVectorImpl<LoopTree> &loopTrees,
            llvm::SmallVectorImpl<unsigned> &trees,
            llvm::SmallVector<unsigned> &subTree,
            llvm::SmallVector<PredicatedChain> &paths,
            const std::vector<llvm::Loop *> &subLoops) {
        if (subTree.size()) {
            SHOW(subTree.size());
            CSHOWLN(paths.size());
            assert(subTree.size() == paths.size());
            if (llvm::BasicBlock *exit = subLoops.back()->getExitingBlock()) {
                paths.emplace_back(exit);
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
          llvm::SmallVector<PredicatedChain> &paths) {
        if (subTree.size()) {
            // SHOW(subTree.size());
            // CSHOWLN(paths.size());
            assert(1 + subTree.size() == paths.size());
            trees.push_back(loopTrees.size());
            loopTrees.emplace_back(std::move(subTree), std::move(paths));
            subTree.clear();
        }
        paths.clear();
    }
    [[maybe_unused]] static void
    split(llvm::SmallVectorImpl<LoopTree> &loopTrees,
          llvm::SmallVectorImpl<unsigned> &trees,
          llvm::SmallVector<unsigned> &subTree,
          llvm::SmallVector<PredicatedChain> &paths,
          const std::vector<llvm::Loop *> &subLoops, size_t i) {
        if (i && subTree.size()) {
            if (llvm::BasicBlock *exit = subLoops[--i]->getExitingBlock()) {
                // SHOW(subTree.size());
                // CSHOWLN(paths.size());
                assert(subTree.size() == paths.size());
                paths.emplace_back(exit);
                trees.push_back(loopTrees.size());
                loopTrees.emplace_back(std::move(subTree), std::move(paths));
                subTree.clear();
                paths.clear();
            }
            subTree.clear();
        }
        paths.clear();
    }
    void dumpAllMemAccess(llvm::ArrayRef<LoopTree> loopTrees) const {
        llvm::errs() << "dumpAllMemAccess for ";
        if (loop)
            llvm::errs() << *loop << "\n";
        else
            llvm::errs() << "toplevel\n";
        for (auto &mem : memAccesses)
            SHOWLN(mem);
        for (auto id : subLoops)
            loopTrees[id].dumpAllMemAccess(loopTrees);
    }
};
