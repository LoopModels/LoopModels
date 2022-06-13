#pragma once

#include "IntegerMap.hpp"
#include "Loops.hpp"
#include "POSet.hpp"
#include "llvm/IR/BasicBlock.h"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/MemorySSA.h>
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
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>

// requires `isLCSSAForm`
// UnitStepPass is a LoopPass
class UnitStepPass : public llvm::PassInfoMixin<UnitStepPass> {
  public:
    llvm::PreservedAnalyses run(llvm::Loop &L, llvm::LoopAnalysisManager &,
                                llvm::LoopStandardAnalysisResults &AR,
                                llvm::LPMUpdater &) {

        if (toUnitStep(L, AR)) {
            // taken from llvm/Transforms/Scalar/IndVarsimplify.h
            auto PA = llvm::getLoopPassPreservedAnalyses();
            PA.preserveSet<llvm::CFGAnalyses>();
            if (AR.MSSA)
                PA.preserve<llvm::MemorySSAAnalysis>();
            return PA;
        } else {
            return llvm::PreservedAnalyses::all();
        }
    }

  private:
    static bool isConstantIntZero(llvm::Value *x) {
        if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(x)) {
            return c->isZero();
        }
        return false;
    }
    bool toUnitStep(llvm::Loop &L, llvm::LoopStandardAnalysisResults &AR) {
        if (!L.isLoopSimplifyForm()) {
            return false;
        }
        llvm::errs() << "Before replacement:\n"
                     << L << "\nPreHeader:\n"
                     << *L.getLoopPreheader() << "\nHeader:" << *L.getHeader()
                     << "\n\n";
        // llvm::Function *F = L.getHeader()->getParent();
        // const llvm::DataLayout &DL = F->getParent()->getDataLayout();

        auto oldIV = L.getInductionVariable(AR.SE);
        // auto oldCmp = L.getLatchCmpInst();
        // we check isLoopSimplifyForm, so there is only one latch
        llvm::BasicBlock *latch = L.getLoopLatch();
        llvm::BranchInst *oldBI =
            llvm::cast<llvm::BranchInst>(latch->getTerminator());
        llvm::Optional<llvm::Loop::LoopBounds> boundsRoot =
            llvm::Loop::LoopBounds::getBounds(L, *oldIV, AR.SE);
        if (boundsRoot.hasValue()) {
            llvm::Loop::LoopBounds &bounds = boundsRoot.getValue();
            // auto pred
            llvm::Value *step = bounds.getStepValue();
            if (llvm::ConstantInt *stepConst =
                    llvm::dyn_cast<llvm::ConstantInt>(step)) {
                if (stepConst->isOne()) {
                    return false;
                }
            }
            // If we're here, then we have non-unit step.
            llvm::Value *init = &bounds.getInitialIVValue();
            llvm::Value *finl = &bounds.getFinalIVValue();
            llvm::BasicBlock *preHeader = L.getLoopPreheader();
            llvm::IRBuilder<> preHeaderBuilder(preHeader->getTerminator());
            // we now want to transform the loop to
            // init = 0;
            // step = 1;
            // final = (oldFinal - oldInit) / oldStep + 1
            // oldIV = newIV * oldStep + oldInit
            llvm::Value *exitCount = preHeaderBuilder.CreateSDiv(
                preHeaderBuilder.CreateNSWSub(finl, init), step);
            // llvm::Value *tripCount = preHeaderBuilder.CreateNSWAdd(
            //     exitCount, llvm::ConstantInt::get(exitCount->getType(), 1));
            //  our new loop will be
            //  for (auto newIV = 0; newIV != tripCount; ++newIV){
            //    oldIV = newIV*oldStep + oldInit;
            //    ...
            //  }
            //  llvm::BasicBlock *header = L.getHeader();
            llvm::IRBuilder<> headerBuilder(oldIV);
            auto *newIV =
                headerBuilder.CreatePHI(exitCount->getType(), 2, "newIndVar");
            // newIV->setIncomingValue(
            //     0, llvm::ConstantInt::get(exitCount->getType(), 0));
            // newIV->setIncomingBlock(0, preHeader);
            newIV->addIncoming(llvm::ConstantInt::get(exitCount->getType(), 0),
                               preHeader);
            // 3 + x - 3
            // 3 - 3 + x
            llvm::Value *replacementIV;
            if (isConstantIntZero(init)) {
                replacementIV = headerBuilder.CreateNSWMul(newIV, step);
            } else {
                replacementIV = headerBuilder.CreateNSWAdd(
                    headerBuilder.CreateNSWMul(newIV, step), init);
            }
            // from IndVarSimplify.cpp
            llvm::ICmpInst::Predicate P;
            if (L.contains(oldBI->getSuccessor(0)))
                P = llvm::ICmpInst::ICMP_NE;
            else
                P = llvm::ICmpInst::ICMP_EQ;
            llvm::IRBuilder<> latchBuilder(oldBI);
            auto toCmp = latchBuilder.CreateNSWAdd(
                newIV, llvm::ConstantInt::get(newIV->getType(), 1),
                "incIndVar");
            newIV->addIncoming(toCmp, latch);
            auto newCmp = latchBuilder.CreateICmp(P, newIV, exitCount);
            // Now, we must delete clean up some old cold.
            // 1. Remove the old branch instruction.
            // 2. Remove the old compare instruction, if it has no remaining
            // uses.
            // 3. replace all uses of oldIV with replacementIV
            oldBI->setCondition(newCmp);
            oldIV->replaceAllUsesWith(replacementIV);
            oldIV->eraseFromParent();
            llvm::errs() << "After replacement PreHeader:\n"
                         << *preHeader << "\nHeader:\n"
                         << *L.getHeader();
            // oldCmp->replaceAllUsesWith(newCmp);
        }
        return false;
    }
};
