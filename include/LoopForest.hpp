#pragma once
// #include "./CallableStructs.hpp"
#include "./BitSets.hpp"
#include "./LoopBlock.hpp"
#include "./Loops.hpp"
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
    bool pushBack(llvm::Loop *, llvm::ScalarEvolution *,
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
    void addZeroLowerBounds();
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree);
// TODO: should depth be stored in LoopForests instead?
struct LoopTree {
    llvm::Loop *loop;
    AffineLoopNest affineLoop;
    LoopForest subLoops; // incomplete type, don't know size
    LoopTree *parentLoop;
    llvm::PHINode &indVar;
    llvm::Value &initialIVValue;
    llvm::Value &finalIVValue;
    // llvm::Loop::LoopBounds bounds;
    bool isLoopSimplifyForm() const { return loop->isLoopSimplifyForm(); }
    llvm::Optional<llvm::Loop::LoopBounds>
    getBounds(llvm::ScalarEvolution *SE) {
        return loop->getBounds(*SE);
    }
    LoopTree(llvm::Loop *L, LoopForest sL, llvm::PHINode *iV,
             llvm::Loop::LoopBounds bounds)
        : loop(L),
          affineLoop(bounds.getInitialIVValue(), bounds.getFinalIVValue()),
          subLoops(std::move(sL)), parentLoop(nullptr), indVar(*iV),
          initialIVValue(bounds.getInitialIVValue()),
          finalIVValue(bounds.getFinalIVValue()) {
        // initialize the AffineLoopNest
        llvm::errs() << "new loop";
        CSHOWLN(affineLoop.getNumLoops());
        SHOWLN(affineLoop.A);
        assert(
            llvm::dyn_cast<llvm::ConstantInt>(bounds.getStepValue())->isOne());
    }
    bool addOuterLoop(llvm::Loop *OL, llvm::Loop::LoopBounds &LB,
                      llvm::PHINode *indVar) {
        bool ret = affineLoop.addLoop(OL, LB, indVar);
        llvm::errs() << "add outer";
        CSHOWLN(affineLoop.getNumLoops());
        SHOWLN(affineLoop.A);
        return ret;
        return affineLoop.addLoop(OL, LB, indVar);
    }
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const LoopTree &tree) {
        return os << tree.affineLoop << tree.subLoops;
    }
    void addZeroLowerBounds() {
        affineLoop.addZeroLowerBounds();
        subLoops.addZeroLowerBounds();
    }
};

LoopForest::LoopForest(std::vector<LoopTree> loops) : loops(std::move(loops)){};
void LoopForest::clear() { loops.clear(); }
bool LoopForest::invalid(std::vector<LoopForest> &forests, LoopForest forest) {
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
bool LoopForest::pushBack(llvm::Loop *L, llvm::ScalarEvolution *SE,
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

    if (llvm::PHINode *indVar = L->getInductionVariable(*SE)) {
        if (llvm::Optional<llvm::Loop::LoopBounds> LB =
                llvm::Loop::LoopBounds::getBounds(*L, *indVar, *SE)) {
            if (llvm::ConstantInt *step =
                    llvm::dyn_cast<llvm::ConstantInt>(LB->getStepValue())) {
                if (!step->isOne()) // TODO: canonicalize?
                    return LoopForest::invalid(forests, std::move(subForest));
                if (subForest.size()) {
                    LoopForest backupForest{subForest};
                    for (auto &&SLT : subForest)
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
size_t LoopForest::size() const { return loops.size(); }
LoopTree &LoopForest::operator[](size_t i) {
    assert(i < loops.size());
    return loops[i];
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const LoopForest &tree) {
    for (auto &loop : tree.loops)
        os << loop;
    return os;
}
