#include "../include/TurboLoopPass.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include <llvm/Transforms/Utils/LCSSA.h>

// The TurboLoopPass represents each loop in function `F` using its own loop
// representation, suitable for more aggressive analysis. However, the remaining
// aspects of the function are still represented with `F`, which can answer
// queries, e.g. control flow graph queries like whether exiting one loop
// directly leads to another, which would be important for whether two loops may
// be fused.
llvm::PreservedAnalyses TurboLoopPass::run(llvm::Function &F,
                                           llvm::FunctionAnalysisManager &FAM) {

    llvm::LoopInfo &LI = FAM.getResult<llvm::LoopAnalysis>(F);
    for (llvm::Loop *LP : LI){
	
    }
    //
    // FunctionAnalysisManager &FAM =
    // FAM.getResult<LoopAnalysisManagerFunctionProxy>(InitialC, CG)
    // .getManager();
    
    
    // TODO: what analysis are preserved?
    // For now, this pass is a no-op, so we preserve `all()`. Eventually, switch
    // to `none()` or find a correct minimal list of analyses to invalidate.
    return llvm::PreservedAnalyses::all();
}
#define STRICT_OPT_USE_PIPELINE_PARSER
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "StrictOpt", "v0.1",
            [](llvm::PassBuilder &PB) {
#ifdef STRICT_OPT_USE_PIPELINE_PARSER
                // Use opt's `--passes` textual pipeline description to trigger
                // StrictOpt
                using PipelineElement = typename llvm::PassBuilder::PipelineElement;
                PB.registerPipelineParsingCallback(
                    [](llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                       llvm::ArrayRef<PipelineElement>) {
                        if (Name == "strict-opt") {
			    FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopSimplifyPass()));
			    FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LCSSAPass()));
			    FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopRotatePass()));
                            FPM.addPass(TurboLoopPass());
                            return true;
                        }
                        return false;
                    });
#else
                // Run StrictOpt before other optimizations when the
                // optimization level is at least -O2
                using OptimizationLevel =
                    typename llvm::PassBuilder::OptimizationLevel;
                PB.registerVectorizerStartEPCallback(
                    [](llvm::FunctionPassManager &FPM, OptimizationLevel OL) {
                        if (OL.getSpeedupLevel() >= 2)
                            FPM.addPass(TurboLoopPass());
                    });
#endif
            }};
}
