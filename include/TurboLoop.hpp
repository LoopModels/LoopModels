#pragma once

#include "integerMap.hpp"
#include "poset.hpp"
#include "tree.hpp"
#include "llvm/IR/PassManager.h"
#include <llvm/ADT/ArrayRef.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Value.h>

// requires `isRecursivelyLCSSAForm`
class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
  public:
    llvm::PreservedAnalyses run(llvm::Function &F,
                                llvm::FunctionAnalysisManager &AM);
    ValueToPosetMap valueToPosetMap;
    PartiallyOrderedSet poset;
    Tree tree;
    // llvm::AssumptionCache *AC;
    const llvm::TargetLibraryInfo *TLI;
    const llvm::TargetTransformInfo *TTI;
    llvm::LoopInfo *LI;
    llvm::ScalarEvolution *SE;
    unsigned registerCount;

    static bool invariant(const llvm::Value *V, llvm::ArrayRef<llvm::Loop*> LPS){
	for (auto *LP : LPS){
	    if (!(LP -> isLoopInvariant(V))){
		return false;
	    }
	}
	return true;
    }
    
    void descend(Tree &tree, llvm::SmallVector<llvm::Loop *, 4> outerLoops,
                 llvm::Loop *LP) {
        auto boundsRoot = LP->getBounds(*SE);
	size_t numOuter = outerLoops.size();
        if (boundsRoot.hasValue()) {
            auto bounds = boundsRoot.getValue();
	    auto step = bounds.getStepValue();
	    if (!invariant(step, outerLoops)){
		tree.push_back(LP);
		return;
	    }
	    auto &start = bounds.getInitialIVValue();
	    auto &stop = bounds.getFinalIVValue();
	    
            llvm::errs() << "\nloop bounds: " << start
                         << " : " << *step << " : "
                         << stop << "\n";
            
	    
        } else {
            // TODO: check if reachable; if not we can safely ignore
            // TODO: insert unoptimizable op representing skipped loop?
            // The concern is we want some understanding of the dependencies
            // between the unoptimized block and optimized block, in case
            // we want to move loops around. Otherwise, this is basically
            // a volatile barrier.
            tree.push_back(LP);
            return;
            // continue;
            // Alt TODO: insert a remark
            // return llvm::PreservedAnalyses::all();
        }
        // llvm::LoopNest LN = llvm::LoopNest(*LP, SE);
        // size_t nestDepth = LN.getNestDepth();

        for (auto *B : LP->getBlocks()) {
            std::cout << "Basic block:\n";
            for (auto &I : *B) {
                llvm::errs() << I << "\n";
            }
        }
    }
};
