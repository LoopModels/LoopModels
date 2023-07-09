#include "../include/TurboLoop.hpp"
#include <cstdio>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopNestAnalysis.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
// #include <llvm/Passes/OptimizationLevel.h>
// #include <llvm/Support/Casting.h>
// #include <llvm/Transforms/Scalar/IndVarSimplify.h>
// #include <llvm/Transforms/Scalar/LoopRotation.h>
// #include <llvm/Transforms/Utils/LCSSA.h>
// #include <llvm/Transforms/Utils/LoopSimplify.h>
// #include <llvm/Transforms/Utils/LoopUtils.h>
// #include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>

// The TurboLoopPass represents each loop in function `F` using its own loop
// representation, suitable for more aggressive analysis. However, the remaining
// aspects of the function are still represented with `F`, which can answer
// queries, e.g. control flow graph queries like whether exiting one loop
// directly leads to another, which would be important for whether two loops may
// be fused.

class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
public:
  TurboLoopPass() = default;
  TurboLoopPass(const TurboLoopPass &) = delete;
  TurboLoopPass(TurboLoopPass &&) = default;

  static auto __attribute__((visibility("default")))
  run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM)
    -> llvm::PreservedAnalyses {
    poly::TurboLoop tl{F, FAM};
    return tl.run();
  }
};
auto __attribute__((visibility("default")))
PipelineParsingCB(llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                  llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) -> bool {
  if (Name == "turbo-loop") {
    // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopSimplifyPass()));
    // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));
    FPM.addPass(TurboLoopPass());
    return true;
  }
  return false;
}

void __attribute__((visibility("default"))) RegisterCB(llvm::PassBuilder &PB) {
  PB.registerVectorizerStartEPCallback(
    [](llvm::FunctionPassManager &PM, llvm::OptimizationLevel) {
      PM.addPass(TurboLoopPass());
    });
  PB.registerPipelineParsingCallback(PipelineParsingCB);
}

extern "C" auto __attribute__((visibility("default"))) LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() -> ::llvm::PassPluginLibraryInfo {
  return {LLVM_PLUGIN_API_VERSION, "TurboLoop", "v0.1", RegisterCB};
}
