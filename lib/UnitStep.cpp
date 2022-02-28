#include "../include/UnitStep.hpp"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopNestAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include <llvm/Transforms/Scalar/LoopRotation.h>
#include <llvm/Transforms/Utils/LCSSA.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>

bool PipelineParsingCB(llvm::StringRef Name, llvm::LoopPassManager &LPM,
                       llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
    if (Name == "unit-step") {
        // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopSimplifyPass()));
        // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));
        LPM.addPass(UnitStepPass());
        return true;
    }
    return false;
}

void RegisterCB(llvm::PassBuilder &PB) {
    PB.registerPipelineParsingCallback(PipelineParsingCB);
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "UnitStep", "v0.1", RegisterCB};
}
