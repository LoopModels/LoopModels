#include "../include/TurboLoop.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Transforms/Utils/LCSSA.h>

// The TurboLoopPass represents each loop in function `F` using its own loop
// representation, suitable for more aggressive analysis. However, the remaining
// aspects of the function are still represented with `F`, which can answer
// queries, e.g. control flow graph queries like whether exiting one loop
// directly leads to another, which would be important for whether two loops may
// be fused.

llvm::PreservedAnalyses TurboLoopPass::run(llvm::Function &F,
                                           llvm::FunctionAnalysisManager &FAM) {
    llvm::AssumptionCache AC = FAM.getResult<llvm::AssumptionAnalysis>(F);
    std::cout << "Assumptions:" << std::endl;
    for (auto &a : AC.assumptions()) {
        llvm::errs() << *a << "\n";
        llvm::CallInst *Call = llvm::cast<llvm::CallInst>(a);
        llvm::Value *val = (Call->arg_begin()->get());
        llvm::errs() << *val << "\n";
        llvm::errs() << "Value id: " << val->getValueID() << "\n";
        llvm::errs() << "Value name: " << val->getValueName() << "\n";
        llvm::errs() << "name: " << val->getName() << "\n";
        llvm::ICmpInst *icmp = llvm::dyn_cast<llvm::ICmpInst>(val);
        if (icmp) {
            llvm::errs() << "icmp: " << *icmp << "\n";
            llvm::Value *op0 = icmp->getOperand(0);
            llvm::Value *op1 = icmp->getOperand(1);
            llvm::errs() << "op0: " << *op0 << "\nop1: " << *op1 << "\n";
            llvm::errs() << "op0 valueID: " << op0->getValueID()
                         << "\nop1 valueID: " << op1->getValueID() << "\n";
            llvm::errs() << "op0 valueName: " << op0->getValueName()
                         << "\nop1 valueName: " << op1->getValueName() << "\n";
            size_t op0posID = valueToPosetMap.push(op0);
            size_t op1posID = valueToPosetMap.push(op1);
            llvm::errs() << "op0posID: " << op0posID
                         << "\nop1posID: " << op1posID << "\n";
            switch (icmp->getPredicate()) {
            case llvm::CmpInst::ICMP_ULT:
                // op0 < op1
                // 1 - 0
                poset.push(0, op0posID, Interval::nonNegative());
                poset.push(0, op1posID, Interval::nonNegative());
            case llvm::CmpInst::ICMP_SLT:
                poset.push(op0posID, op1posID, Interval::positive());
                break;
            case llvm::CmpInst::ICMP_ULE:
                poset.push(0, op0posID, Interval::nonNegative());
                poset.push(0, op1posID, Interval::nonNegative());
            case llvm::CmpInst::ICMP_SLE:
                poset.push(op0posID, op1posID, Interval::nonNegative());
                break;
            case llvm::CmpInst::ICMP_EQ:
                poset.push(op0posID, op1posID, Interval::zero());
                break;
            case llvm::CmpInst::ICMP_UGT:
                poset.push(0, op0posID, Interval::nonNegative());
                poset.push(0, op1posID, Interval::nonNegative());
            case llvm::CmpInst::ICMP_SGT:
                poset.push(op0posID, op1posID, Interval::negative());
                break;
            case llvm::CmpInst::ICMP_UGE:
                poset.push(0, op0posID, Interval::nonNegative());
                poset.push(0, op1posID, Interval::nonNegative());
            case llvm::CmpInst::ICMP_SGE:
                poset.push(op0posID, op1posID, Interval::nonPositive());
                break;
            case llvm::CmpInst::ICMP_NE:
                // we don't have a representation of this.
                break;
            default:
                // this is icmp, not fcmp!!!
                break;
            }
            if (icmp->isEquality()) {
                llvm::errs() << *op0 << "\nand\n" << *op1 << "\nare equal!\n";
            }
        } else {
            llvm::errs() << "not an icmp.\n";
        }
        llvm::errs() << *Call << "\n";
    }
    llvm::TargetLibraryInfoImpl TLII;
    llvm::TargetLibraryInfo TLI(TLII);
    llvm::DominatorTree DT(F);
    llvm::LoopInfo &LI = FAM.getResult<llvm::LoopAnalysis>(F);
    llvm::ScalarEvolution SE(F, TLI, AC, DT, LI);
    for (llvm::Loop *LP : LI) {
        auto boundsRoot = LP->getBounds(SE);
        if (boundsRoot.hasValue()) {
            auto bounds = boundsRoot.getValue();

        } else {
            // TODO: insert unoptimizable op representing skipped loop?
            continue;
        }
        llvm::LoopNest LN = llvm::LoopNest(*LP, SE);
        size_t nestDepth = LN.getNestDepth();

        

    }

    llvm::InductionDescriptor ID;
    for (llvm::Loop *LP : LI) {
        auto *inductOuter = LP->getInductionVariable(SE);
        const llvm::SCEV *backEdgeTaken = nullptr;
        if (inductOuter) {
            llvm::errs() << "Outer InductionVariable: " << *inductOuter << "\n";
            backEdgeTaken = SE.getBackedgeTakenCount(LP);
            if (backEdgeTaken) {
                llvm::errs()
                    << "Back edge taken count: " << *backEdgeTaken
                    << "\n\ttrip count: "
                    << *SE.getAddExpr(backEdgeTaken,
                                      SE.getOne(backEdgeTaken->getType()))
                    << "\n";
            } else {
                std::cout << "couldn't find backedge taken?\n";
            }
        } else {
            std::cout << "no outer induction variable" << std::endl;
        }
        auto obouter = LP->getBounds(SE);
        if (obouter.hasValue()) {
            auto b = obouter.getValue();
            llvm::errs() << "\nOuter loop bounds: " << b.getInitialIVValue()
                         << " : " << *b.getStepValue() << " : "
                         << b.getFinalIVValue() << "\n";
        } else {
            std::cout << "Could not find outer loop bounds. =(" << std::endl;
        }
        int i = 0;
        for (llvm::Loop *SubLP : depth_first(LP)) {
            auto *induct = SubLP->getInductionVariable(SE);
            if (induct) {
                if (inductOuter) {
                    llvm::errs()
                        << "Loop " << i++
                        << " in outer InductionVariable: " << *induct << "\n";
                    llvm::errs()
                        << "innerInduct > outerInduct: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SGT,
                               SE.getSCEV(induct), SE.getSCEV(inductOuter))
                        << "\n";
                    llvm::errs()
                        << "innerInduct == outerInduct: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_EQ,
                               SE.getSCEV(induct), SE.getSCEV(inductOuter))
                        << "\n";
                    llvm::errs()
                        << "innerInduct < outerInduct: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SLT,
                               SE.getSCEV(induct), SE.getSCEV(inductOuter))
                        << "\n";
                }
            } else {
                std::cout << "no inner induction variable?" << std::endl;
            }
            if (SubLP->getInductionDescriptor(SE, ID)) {
                std::cout << "Found induction descriptor" << std::endl;
            } else {
                std::cout << "no induction description" << std::endl;
            }

            auto ob = SubLP->getBounds(SE);
            if (ob.hasValue()) {
                auto b = ob.getValue();
                auto &inner_LB = b.getInitialIVValue();
                auto &inner_UB = b.getFinalIVValue();

                llvm::errs() << "\nLoop Bounds: " << inner_LB << " : "
                             << *b.getStepValue() << " : " << inner_UB << "\n";
                if (obouter.hasValue()) {
                    auto ob = obouter.getValue();
                    auto oLB = SE.getSCEV(&ob.getInitialIVValue());
                    auto oUB = SE.getSCEV(&ob.getFinalIVValue());
                    auto iLB = SE.getSCEV(&inner_LB);
                    auto iUB = SE.getSCEV(&inner_UB);

                    // both ob and ib have values
                    llvm::errs() << "Loop " << i++
                                 << " in bounds cmp: " << *induct << "\n";
                    llvm::errs()
                        << "inner_LB > outer_UB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SGT, iLB, oUB)
                        << "\n";
                    llvm::errs()
                        << "inner_LB == outer_UB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_EQ, iLB, oUB)
                        << "\n";
                    llvm::errs()
                        << "inner_LB < outer_UB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SLT, iLB, oUB)
                        << "\n";
                    llvm::errs()
                        << "inner_UB > outer_LB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SGT, iUB, oLB)
                        << "\n";
                    llvm::errs()
                        << "inner_UB == outer_LB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_EQ, iUB, oLB)
                        << "\n";
                    llvm::errs()
                        << "inner_UB < outer_LB: "
                        << SE.isKnownPredicate(
                               llvm::CmpInst::Predicate::ICMP_SLT, iUB, oLB)
                        << "\n";
                }
            } else {
                std::cout << "loop bound didn't have value!?" << std::endl;
            }
            std::cout << "\n";
        }
    }

    return llvm::PreservedAnalyses::all();
}

bool PipelineParsingCB(llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                       llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
    if (Name == "turbo-loop") {
        FPM.addPass(TurboLoopPass());
        return true;
    }
    return false;
}

void RegisterCB(llvm::PassBuilder &PB) {
    PB.registerPipelineParsingCallback(PipelineParsingCB);
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "TurboLoop", "v0.1", RegisterCB};
}
