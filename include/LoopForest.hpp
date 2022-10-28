#pragma once
// #include "./CallableStructs.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
#include "Macro.hpp"
#include <iterator>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
#include <vector>

struct LoopTree;
struct LoopForest {
    std::vector<LoopTree> loops;
    // definitions due to incomplete types
    size_t pushBack(llvm::Loop *, llvm::ScalarEvolution &,
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
    inline auto &front() { return loops.front(); }
    inline void clear();
    void addZeroLowerBounds();
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree);
// TODO: should depth be stored in LoopForests instead?
struct LoopTree {
    llvm::Loop *loop;
    AffineLoopNest affineLoop;
    LoopForest subLoops; // incomplete type, don't know size
    LoopTree *parentLoop;
    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }
    llvm::Optional<llvm::Loop::LoopBounds>
    getBounds(llvm::ScalarEvolution *SE) {
        return loop->getBounds(*SE);
    }
    LoopTree(llvm::Loop *L, LoopForest sL, const llvm::SCEV *BT,
             llvm::ScalarEvolution &SE)
        : loop(L), affineLoop(L, BT, SE), subLoops(std::move(sL)),
          parentLoop(nullptr) {
        // initialize the AffineLoopNest
        llvm::errs() << "new loop";
        CSHOWLN(affineLoop.getNumLoops());
        SHOWLN(affineLoop.A);
        for (auto v : affineLoop.symbols)
            SHOWLN(*v);
    }
    LoopTree(llvm::Loop *L, AffineLoopNest aln, LoopForest sL)
        : loop(L), affineLoop(aln), subLoops(sL), parentLoop(nullptr) {}
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const LoopTree &tree) {
        return os << tree.affineLoop << "\n" << tree.subLoops << "\n";
    }
    void addZeroLowerBounds() {
        affineLoop.addZeroLowerBounds();
        subLoops.addZeroLowerBounds();
    }
};

LoopForest::LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
void LoopForest::clear() { loops.clear(); }
inline bool LoopForest::invalid(std::vector<LoopForest> &forests,
                                LoopForest forest) {
    if (forest.size())
        forests.push_back(std::move(forest));
    return true;
}
void LoopForest::addZeroLowerBounds() {
    for (auto &&tree : loops)
        tree.addZeroLowerBounds();
}

// try to add Loop L, as well as all of L's subLoops
// if invalid, create a new LoopForest, and add it to forests instead
size_t LoopForest::pushBack(llvm::Loop *L, llvm::ScalarEvolution &SE,
                            std::vector<LoopForest> &forests) {
    auto &subLoops{L->getSubLoops()};
    LoopForest subForest;
    size_t interiorDepth0 = 0;
    if (subLoops.size()) {
        bool anyFail = false;
        for (size_t i = 0; i < subLoops.size(); ++i) {
            size_t itDepth = subForest.pushBack(subLoops[i], SE, forests);
            if (itDepth == 0) {
                anyFail = true;
                if (subForest.size()) {
                    forests.push_back(std::move(subForest));
                    subForest.clear();
                }
            } else if (i == 0) {
                interiorDepth0 = itDepth;
            }
            // if (subForest.pushBack(subLoops[i], SE, forests)) {
            //     anyFail = true;
            //     if (subForest.size()) {
            //         forests.push_back(std::move(subForest));
            //         subForest.clear();
            //     }
            // }
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
