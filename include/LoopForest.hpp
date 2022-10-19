#pragma once
// #include "./CallableStructs.hpp"
#include "./Loops.hpp"
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <vector>

struct LoopTree;
struct LoopForest {
    std::vector<LoopTree> loops;
    void pushBack(llvm::Loop *, LoopTree *, llvm::ScalarEvolution *);
};

struct LoopTree {
    llvm::Loop *loop;
    AffineLoopNest *affineLoop;
    LoopForest subLoops; // incomplete type, don't know size
    LoopTree *parentLoop;
    llvm::PHINode *indVar;
    size_t depth;
};

void LoopForest::pushBack(llvm::Loop *L, LoopTree *parentLoop,
                          llvm::ScalarEvolution *SE) {
    size_t d = parentLoop ? parentLoop->depth + 1 : 0;
    loops.push_back(
        LoopTree{L, nullptr, {}, parentLoop, L->getInductionVariable(*SE), d});
    LoopTree &newTree = loops.back();
    newTree.subLoops.loops.reserve(L->getSubLoops().size());
    for (auto sL : *L)
        newTree.subLoops.pushBack(sL, &newTree, SE);
}
