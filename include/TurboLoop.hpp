#pragma once

#include "integerMap.hpp"
#include "loops.hpp"
#include "poset.hpp"
#include "tree.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
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

    static bool invariant(
        llvm::Value *V,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
            4> const &LPS) {
        for (auto LP = LPS.rbegin(); LP != LPS.rend(); ++LP) {
            bool changed = false;
            bool invariant = LP->first->makeLoopInvariant(V, changed);
            if (!(changed | invariant)) {
                return false;
            }
        }
        return true;
    }
    void pushAffine(
        llvm::SmallVector<Affine, 8> &affs, llvm::Value &initV,
        llvm::Value &stepV, llvm::Value &finalV,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
            4> const &outerLoops) {
	bool startInvariant = invariant(&initV, outerLoops);
	bool stepInvariant = invariant(&stepV, outerLoops);
	bool stopInvariant = invariant(&finalV, outerLoops);
        llvm::SmallVector<intptr_t, 4> aL(outerLoops.size() + 1, 0);
        llvm::SmallVector<intptr_t, 4> aU(outerLoops.size() + 1, 0);
        MPoly bL;
        MPoly bU;
        if (isa<llvm::ConstantInt>(stepV)) {
            llvm::ConstantInt &stepConst = llvm::cast<llvm::ConstantInt>(stepV);
            if (!(stepConst.isOne())) {
                // divide by step
                // TODO: do we want to actually set lower bound to 0?
                //       Not doing so may be able to preserve affine structure
                //       in some situations. For example,
                // for i in 1:2:N, j in i:2:M
                // we have a lower bound of `i`, which (given step of `2`) would
                // itself be a linear funct of the canonical induction variable.
            }
        } else {
        }
        // if (llvm::ConstantInt *const &stepConst =
        //         llvm::dyn_cast<llvm::ConstantInt *>(stepV)) {
        //     if (!(stepConst->isOne())){
        // 	// llvm::IRBuilder<> Builder;
        // 	// Builder.CreateSDiv
        //     }
        // } else {

        // }
        // if (llvm::ConstantInt *const &initConst =
        //         llvm::dyn_cast<llvm::ConstantInt *>(initV)) {

        // } else {

        // }
        // if (llvm::ConstantInt *const &finalConst =
        //         llvm::dyn_cast<llvm::ConstantInt *>(finalV)) {

        // } else {

        // }
    }
    void descend(
        Tree &tree,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>, 4>
            &outerLoops,
        llvm::SmallVector<Affine, 8> &affs, llvm::Loop *LP) {
        llvm::Optional<llvm::Loop::LoopBounds> boundsRoot = LP->getBounds(*SE);
        size_t numOuter = outerLoops.size();
        if (boundsRoot.hasValue()) {
            llvm::Loop::LoopBounds &bounds = boundsRoot.getValue();
            pushAffine(affs, bounds.getInitialIVValue(), *bounds.getStepValue(),
                       bounds.getFinalIVValue(), outerLoops);
            llvm::Value *step = bounds.getStepValue();
            llvm::Value *start = &bounds.getInitialIVValue();
	    llvm::Value *stop = &bounds.getFinalIVValue();

            if (!invariant(step, outerLoops)) {
                tree.emplace_back(LP, numOuter);
                return;
            }
            
            llvm::errs() << "\nloop bounds: " << start << " : " << *step
                         << " : " << stop << "\n";
	    
        } else {
            // TODO: check if reachable; if not we can safely ignore
            // TODO: insert unoptimizable op representing skipped loop?
            // The concern is we want some understanding of the dependencies
            // between the unoptimized block and optimized block, in case
            // we want to move loops around. Otherwise, this is basically
            // a volatile barrier.
            tree.emplace_back(LP, numOuter);
            return;
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
