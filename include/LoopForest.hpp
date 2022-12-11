#pragma once
// #include "./CallableStructs.hpp"
#include "./ArrayReference.hpp"
#include "./BitSets.hpp"
#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./MemoryAccess.hpp"
#include "./Predicate.hpp"
#include <cstddef>
#include <iterator>
#include <limits>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>
#include <vector>

struct LoopTree {
    [[no_unique_address]] llvm::Loop *loop;
    [[no_unique_address]] llvm::SmallVector<LoopTree *> subLoops;
    // length number of sub loops + 1
    // - this loop's header to first loop preheader
    // - first loop's exit to next loop's preheader...
    // - etc
    // - last loop's exit to this loop's latch

    // in addition to requiring simplify form, we require a single exit block
    [[no_unique_address]] llvm::SmallVector<Predicate::Map> paths;
    [[no_unique_address]] AffineLoopNest<true> affineLoop;
    [[no_unique_address]] LoopTree *parentLoop{nullptr};
    [[no_unique_address]] llvm::SmallVector<MemoryAccess, 0> memAccesses{};

    [[nodiscard]] auto isLoopSimplifyForm() const -> bool {
        return loop->isLoopSimplifyForm();
    }

    LoopTree(const LoopTree &) = default;
    LoopTree(LoopTree &&) = default;
    auto operator=(const LoopTree &) -> LoopTree & = default;
    auto operator=(LoopTree &&) -> LoopTree & = default;
    LoopTree(llvm::SmallVector<LoopTree *> sL,
             llvm::SmallVector<Predicate::Map> paths)
        : loop(nullptr), subLoops(std::move(sL)), paths(std::move(paths)) {}

    LoopTree(llvm::Loop *L, llvm::SmallVector<LoopTree *> sL,
             const llvm::SCEV *BT, llvm::ScalarEvolution &SE,
             llvm::SmallVector<Predicate::Map> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(L, BT, SE) {
#ifndef NDEBUG
        if (loop)
            for (auto &&chain : paths)
                for (auto &&pbb : chain)
                    assert(loop->contains(pbb.basicBlock));
#endif
    }

    LoopTree(llvm::Loop *L, AffineLoopNest<true> aln,
             llvm::SmallVector<LoopTree *> sL,
             llvm::SmallVector<PredicatedChain> paths)
        : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
          affineLoop(std::move(aln)) {
#ifndef NDEBUG
        if (loop)
            for (auto &&chain : paths)
                for (auto &&pbb : chain)
                    assert(loop->contains(pbb.basicBlock));
#endif
    }
    [[nodiscard]] auto getNumLoops() const -> size_t {
        return affineLoop.getNumLoops();
    }

    friend auto operator<<(llvm::raw_ostream &os, const LoopTree &tree)
        -> llvm::raw_ostream & {
        if (tree.loop) {
            os << (*tree.loop) << "\n" << tree.affineLoop << "\n";
        } else {
            os << "top-level:\n";
        }
        for (auto branch : tree.subLoops)
            os << *branch;
        return os << "\n";
    }
    [[nodiscard]] auto dump() const -> llvm::raw_ostream & {
        return llvm::errs() << *this;
    }
    void addZeroLowerBounds(llvm::DenseMap<llvm::Loop *, LoopTree *> &loopMap) {
        // SHOWLN(this);
        // SHOWLN(affineLoop.A);
        affineLoop.addZeroLowerBounds();
        for (auto tree : subLoops) {
            tree->addZeroLowerBounds(loopMap);
            tree->parentLoop = this;
        }
        if (loop)
            loopMap.insert(std::make_pair(loop, this));
    }
    auto begin() { return subLoops.begin(); }
    auto end() { return subLoops.end(); }
    [[nodiscard]] auto begin() const { return subLoops.begin(); }
    [[nodiscard]] auto end() const { return subLoops.end(); }
    [[nodiscard]] auto size() const -> size_t { return subLoops.size(); }

    // try to add Loop L, as well as all of L's subLoops
    // if invalid, create a new LoopForest, and add it to forests instead
    // loopTrees are the cache of all LoopTrees
    //
    // forests is the collection of forests considered together
    static auto pushBack(llvm::BumpPtrAllocator &alloc,
                         llvm::SmallVector<LoopTree *> &forests,
                         llvm::SmallVector<LoopTree *> &branches, llvm::Loop *L,
                         llvm::ScalarEvolution &SE) -> size_t {
        const std::vector<llvm::Loop *> &subLoops{L->getSubLoops()};
        llvm::BasicBlock *H = L->getHeader();
        llvm::BasicBlock *E = L->getExitingBlock();
        bool anyFail = (E == nullptr) || (!L->isLoopSimplifyForm());
        if (anyFail)
            SHOWLN(E);
        if (anyFail)
            SHOWLN(L->isLoopSimplifyForm());
        return pushBack(alloc, forests, branches, L, SE, subLoops, H, E,
                        anyFail);
    }
    static auto pushBack(llvm::BumpPtrAllocator &alloc,
                         llvm::SmallVector<LoopTree *> &forests,
                         llvm::SmallVector<LoopTree *> &branches, llvm::Loop *L,
                         llvm::ScalarEvolution &SE,
                         llvm::ArrayRef<llvm::Loop *> subLoops,
                         llvm::BasicBlock *H, llvm::BasicBlock *E, bool anyFail)
        -> size_t {
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
        llvm::SmallVector<LoopTree *> subForest;
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
                    split(alloc, forests, subForest, paths, subLoops, i);
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
                size_t itDepth = pushBack(alloc, forests, subForest, N, SE);
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
                    split(alloc, forests, subForest, paths);
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
                return invalid(alloc, forests, subForest, paths, subLoops);
            assert(subForest.size());
            finalStart = subLoops.back()->getExitBlock();
        } else
            finalStart = H;
        llvm::errs() << "Starting second pass in pushBack\n";
        SHOWLN(subForest.size());
        if (subForest.size()) { // add subloops
            AffineLoopNest<true> &subNest = subForest.front()->affineLoop;
            SHOWLN(subNest.getNumLoops());
            if (subNest.getNumLoops() > 1) {
                visitedBBs.clear();
                if (allForwardPathsReach(visitedBBs, path, finalStart, E, L)) {
                    paths.push_back(std::move(path));
                    auto *newTree = new (alloc)
                        LoopTree{L, subNest.removeInnerMost(),
                                 std::move(subForest), std::move(paths)};
                    branches.push_back(newTree);
                    return ++interiorDepth0;
                } else {
                    llvm::errs() << "No direct path from:\n"
                                 << *finalStart << "\nTo:\n"
                                 << *E << "\n";
                }
            }
            // } else if (auto BT = SE.getBackedgeTakenCount(L)) {
        } else if (auto BT = getBackedgeTakenCount(SE, L)) {
            // we're at the bottom of the recursion
            if (!llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
                llvm::errs() << "about to add loop: " << *L
                             << "\nwith backedge taken count: " << *BT << "\n";
                auto *BTNW = noWrapSCEV(SE, BT);
                llvm::errs() << "after no-wrapping:\n" << *BTNW << "\n";
                if (allForwardPathsReach(visitedBBs, path, finalStart, E, L)) {

                    paths.push_back(std::move(path));
                    auto *newTree = new (alloc) LoopTree{
                        L, std::move(subForest), BTNW, SE, std::move(paths)};
                    branches.push_back(newTree);
                    return 1;
                }
            }
        }
        llvm::errs()
            << "pushBack returning 0 because end of function reached.\nLoop: "
            << *L << "\n";
        SHOW(subForest.size());
        if (subForest.size()) {
            CSHOWLN(subForest.front()->getNumLoops());
        } else
            llvm::errs() << "\n";
        return invalid(alloc, forests, subForest, paths, subLoops);
    }

    [[maybe_unused]] static auto
    invalid(llvm::BumpPtrAllocator &alloc,
            llvm::SmallVectorImpl<LoopTree *> &trees,
            llvm::SmallVectorImpl<LoopTree *> &subTree,
            llvm::SmallVectorImpl<PredicatedChain> &paths,
            const std::vector<llvm::Loop *> &subLoops) -> size_t {
        if (subTree.size()) {
            SHOW(subTree.size());
            CSHOWLN(paths.size());
            assert(subTree.size() == paths.size());
            if (llvm::BasicBlock *exit = subLoops.back()->getExitingBlock()) {
                paths.emplace_back(exit);
                auto *newTree =
                    new (alloc) LoopTree{std::move(subTree), std::move(paths)};
                trees.push_back(newTree);
            }
        }
        return 0;
    }
    [[maybe_unused]] static void
    split(llvm::BumpPtrAllocator &alloc,
          llvm::SmallVectorImpl<LoopTree *> &trees,
          llvm::SmallVectorImpl<LoopTree *> &subTree,
          llvm::SmallVectorImpl<PredicatedChain> &paths) {
        if (subTree.size()) {
            // SHOW(subTree.size());
            // CSHOWLN(paths.size());
            assert(1 + subTree.size() == paths.size());
            auto *newTree =
                new (alloc) LoopTree{std::move(subTree), std::move(paths)};
            trees.push_back(newTree);
            subTree.clear();
        }
        paths.clear();
    }
    [[maybe_unused]] static void
    split(llvm::BumpPtrAllocator &alloc,
          llvm::SmallVectorImpl<LoopTree *> &trees,
          llvm::SmallVector<LoopTree *> &subTree,
          llvm::SmallVector<PredicatedChain> &paths,
          const std::vector<llvm::Loop *> &subLoops, size_t i) {
        if (i && subTree.size()) {
            if (llvm::BasicBlock *exit = subLoops[--i]->getExitingBlock()) {
                // SHOW(subTree.size());
                // CSHOWLN(paths.size());
                assert(subTree.size() == paths.size());
                paths.emplace_back(exit);
                auto *newTree =
                    new (alloc) LoopTree{std::move(subTree), std::move(paths)};
                trees.push_back(newTree);
                subTree.clear();
                paths.clear();
            }
            subTree.clear();
        }
        paths.clear();
    }
    void dumpAllMemAccess() const {
        llvm::errs() << "dumpAllMemAccess for ";
        if (loop)
            llvm::errs() << *loop << "\n";
        else
            llvm::errs() << "toplevel\n";
        for (auto &mem : memAccesses)
            SHOWLN(mem);
        for (auto sL : subLoops)
            sL->dumpAllMemAccess();
    }

    // void fill(BlockPredicates &bp){
    // 	for (auto &c : chains)
    // 	    c.fill(bp);
    // }
};
