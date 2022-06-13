#pragma once

#include "./IntegerMap.hpp"
#include "./Loops.hpp"
#include "./POSet.hpp"
// #include "Tree.hpp"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>

static bool isKnownOne(llvm::Value *x) {
    if (llvm::ConstantInt *constInt = llvm::dyn_cast<llvm::ConstantInt>(x)) {
        return constInt->isOne();
    } else if (llvm::Constant *constVal = llvm::dyn_cast<llvm::Constant>(x)) {
        return constVal->isOneValue();
    }
    return false;
}

// requires `isRecursivelyLCSSAForm`
class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
  public:
    llvm::PreservedAnalyses run(llvm::Function &F,
                                llvm::FunctionAnalysisManager &AM);
    ValueToPosetMap valueToPosetMap;
    PartiallyOrderedSet poset;
    // Tree tree;
    // llvm::AssumptionCache *AC;
    const llvm::TargetLibraryInfo *TLI;
    const llvm::TargetTransformInfo *TTI;
    llvm::LoopInfo *LI;
    llvm::ScalarEvolution *SE;
    // const llvm::DataLayout *DL;
    unsigned registerCount;

    // returns index to the loop whose preheader we place it in.
    // if it equals depth, then we must place it into the inner most loop
    // header..
    static size_t invariant(
        llvm::Value &V,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
            4> const &LPS) {
        size_t depth = LPS.size();
        for (auto LP = LPS.rbegin(); LP != LPS.rend(); ++LP) {
            bool changed = false;
            bool invariant = LP->first->makeLoopInvariant(&V, changed);
            if (!(changed | invariant)) {
                return depth;
            }
            depth--;
        }
        return 0;
    }

    // // // we need unit step size
    // // void canonicalizeStep(llvm::Value *initV,
    // //     llvm::Value *stepV, llvm::Value *finalV) {

    // // 	return;
    // // }
    // // this can result in unfavorable rotations in canonicalizing the
    // starting
    // // index to 0 so we rely on orthogonalizing indices later. Supporting
    // // orthogonalization is needed anyway, as loops may have originally been
    // // written in an unfavorable way.
    // // returns `true` if it failed.
    // void pushAffine(
    //     llvm::SmallVector<AffineCmp, 8> &affs, llvm::Value &initV,
    //     llvm::Value &finalV,
    //     llvm::SmallVector<
    //         std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
    //         4> const &outerLoops,
    //     llvm::Loop *LP) {

    //     size_t startInvariant = invariant(initV, outerLoops);
    //     size_t stopInvariant = invariant(finalV, outerLoops);

    //     llvm::SmallVector<int64_t, 4> aL(outerLoops.size() + 1, 0);
    //     llvm::SmallVector<int64_t, 4> aU(outerLoops.size() + 1, 0);
    //     MPoly bL;
    //     MPoly bU;
    //     /*
    //     if (llvm::ConstantInt *stepConst =
    //             llvm::dyn_cast<llvm::ConstantInt>(stepV)) {
    //         if (!(stepConst->isOne())) {
    //             // stepConst->getValue();
    //             // divide by const
    //             size_t defLevel = std::max(startInvariant, stopInvariant);
    //             llvm::BasicBlock *startStopPre;
    //             if (defLevel == outerLoops.size()) {
    //                 startStopPre = LP->getLoopPreheader();
    //             } else {
    //                 startStopPre =
    //                     outerLoops[defLevel].first->getLoopPreheader();
    //             }
    //             // auto stopSCEV = SE->getSCEV(finalV);
    //             // llvm::Value *len = rewriter.expandCodeFor(
    //             //     SE->getAddExpr(
    //             //         SE->getUDivExpr(SE->getMinusSCEV(stopSCEV,
    //             // SE->getSCEV(initV),
    //             // llvm::SCEV::FlagNUW),
    //             //                         SE->getSCEV(stepV)),
    //             //         SE->getOne(stopSCEV->getType())),
    //             //     stopSCEV->getType(), startStopPre->getTerminator());

    //             llvm::IRBuilder<> Builder(startStopPre);
    //             llvm::Value *len = Builder.CreateNSWAdd(
    //                 Builder.CreateSDiv(Builder.CreateNSWSub(finalV, initV),
    //                                    stepV),
    //                 llvm::ConstantInt::get(finalV->getType(), 1));
    //             // Now that we have the length
    //             // we must create a new phi initialized at 0
    //             // then insert a new break/latch, replacing old
    //             // then replace all uses of old phi with mul/step

    //             // initV),
    //         }
    //     } else {
    //     }
    //     */
    // 	if (llvm::ConstantInt *initConst =
    // llvm::dyn_cast<llvm::Constantint64_t>(&initV)){

    // 	} else {

    // 	}
    // 	if (llvm::ConstantInt *finalConst =
    // llvm::dyn_cast<llvm::Constantint64_t>(&finalV)){

    // 	} else {

    // 	}
    // }
    // void descend(
    //     Tree &tree,
    //     llvm::SmallVector<
    //         std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
    //         4> &outerLoops,
    //     llvm::SmallVector<AffineCmp, 8> &affs, llvm::Loop *LP,
    //     llvm::DominatorTree &DT) {
    //     size_t numOuter = outerLoops.size();
    //     if (LP->isLCSSAForm(DT)) {
    //         // we check for LCSSA form as we'd like to assume it
    //         llvm::Optional<llvm::Loop::LoopBounds> boundsRoot =
    //             LP->getBounds(*SE);
    //         if (boundsRoot.hasValue()) {
    //             llvm::Loop::LoopBounds &bounds = boundsRoot.getValue();
    //             // TODO: write separate pass for canonicalizing steps to 1
    //             if (isKnownOne(bounds.getStepValue())) {
    //                 pushAffine(affs, bounds.getInitialIVValue(),
    //                            bounds.getFinalIVValue(),
    //                            outerLoops, LP);
    //                 llvm::Value *start = &bounds.getInitialIVValue();
    //                 llvm::Value *stop = &bounds.getFinalIVValue();

    //                 llvm::errs()
    //                     << "\nloop bounds: " << *start << " : " << *stop <<
    //                     "\n";
    //             }
    //         }
    //     }
    //     // TODO: check if reachable; if not we can safely ignore
    //     // TODO: insert unoptimizable op representing skipped loop?
    //     // The concern is we want some understanding of the dependencies
    //     // between the unoptimized block and optimized block, in case
    //     // we want to move loops around. Otherwise, this is basically
    //     // a volatile barrier.
    //     tree.emplace_back(LP, numOuter);
    //     return;
    //     // Alt TODO: insert a remark
    //     // return llvm::PreservedAnalyses::all();
    //     // llvm::LoopNest LN = llvm::LoopNest(*LP, SE);
    //     // size_t nestDepth = LN.getNestDepth();

    //     for (auto *B : LP->getBlocks()) {
    //         std::cout << "Basic block:\n";
    //         for (auto &I : *B) {
    //             llvm::errs() << I << "\n";
    //         }
    //     }
    // }
};
