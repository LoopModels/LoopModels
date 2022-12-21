#include "../include/TurboLoop.hpp"
#include "../include/LoopBlock.hpp"
#include "../include/LoopForest.hpp"
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
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include <llvm/Transforms/Scalar/LoopRotation.h>
#include <llvm/Transforms/Utils/LCSSA.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>

// The TurboLoopPass represents each loop in function `F` using its own loop
// representation, suitable for more aggressive analysis. However, the remaining
// aspects of the function are still represented with `F`, which can answer
// queries, e.g. control flow graph queries like whether exiting one loop
// directly leads to another, which would be important for whether two loops may
// be fused.

auto TurboLoopPass::run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM)
  -> llvm::PreservedAnalyses {
  // llvm::LoopNest LA = FAM.getResult<llvm::LoopNestAnalysis>(F);
  // llvm::AssumptionCache &AC = FAM.getResult<llvm::AssumptionAnalysis>(F);
  // llvm::DominatorTree &DT = FAM.getResult<llvm::DominatorTreeAnalysis>(F);
  // ClassID 0: ScalarRC
  // ClassID 1: RegisterRC
  // TLI = &FAM.getResult<llvm::TargetLibraryAnalysis>(F);
  TTI = &FAM.getResult<llvm::TargetIRAnalysis>(F);
  // llvm::errs() << "DataLayout: "
  //              << F.getParent()->getDataLayout().getStringRepresentation()
  //              << "\n";

  // for (size_t i = 0; i < 5; ++i) {
  //   size_t w = 1 << i;
  //   llvm::errs() << "Vector width: " << w << "\nfadd cost: "
  //                << TTI->getArithmeticInstrCost(
  //                     llvm::Instruction::FAdd,
  //                     llvm::FixedVectorType::get(
  //                       llvm::Type::getDoubleTy(F.getContext()), w))
  //                << "\n";
  // }

  LI = &FAM.getResult<llvm::LoopAnalysis>(F);
  SE = &FAM.getResult<llvm::ScalarEvolutionAnalysis>(F);
  ORE = &FAM.getResult<llvm::OptimizationRemarkEmitterAnalysis>(F);
  if (!ORE->enabled())
    ORE = nullptr; // cheaper check
  if (ORE) {
    // llvm::OptimizationRemarkAnalysis analysis{remarkAnalysis("RegisterCount",
    // *LI->begin())}; ORE->emit(analysis << "There are
    // "<<TTI->getNumberOfRegisters(0)<<" scalar registers");
    llvm::SmallString<32> str = llvm::formatv("there are {0} scalar registers",
                                              TTI->getNumberOfRegisters(0));

    remark("ScalarRegisterCount", *LI->begin(), str);
    str = llvm::formatv("there are {0} vector registers",
                        TTI->getNumberOfRegisters(1));
    remark("VectorRegisterCount", *LI->begin(), str);
  }
  // llvm::errs() << "Scalar registers: " << TTI->getNumberOfRegisters(0) <<
  // "\n"; llvm::errs() << "Vector registers: " << TTI->getNumberOfRegisters(1)
  // << "\n";

  // Builds the loopForest, constructing predicate chains and loop nests
  initializeLoopForest();
  if (loopForests.empty())
    return llvm::PreservedAnalyses::all();

  // first, we try and parse the function to find sets of loop nests
  // then we search for sets of fusile loops

  // fills array refs
  parseNest();

  // TODO: fill schedules
  for (auto forest : loopForests) {
    fillLoopBlock(*forest);
    std::optional<BitSet<>> optDeps = loopBlock.optimize();
    if (ORE) {
      if (optDeps) {
        llvm::SmallVector<char, 512> str;
        llvm::raw_svector_ostream os(str);
        os << "Solved linear program:" << loopBlock << "\n";
        remark("LinearProgramSuccess", forest->getOuterLoop(), os.str());
      } else {
        remark("LinearProgramFailure", forest->getOuterLoop(),
               "Failed to solve linear program");
      }
    }
    loopBlock.clear();
  }
  return llvm::PreservedAnalyses::none();
}
auto PipelineParsingCB(llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                       llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
  -> bool {
  if (Name == "turbo-loop") {
    // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LoopSimplifyPass()));
    // FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));
    FPM.addPass(TurboLoopPass());
    return true;
  }
  return false;
}

void RegisterCB(llvm::PassBuilder &PB) {
  PB.registerVectorizerStartEPCallback(
    [](llvm::FunctionPassManager &PM, llvm::OptimizationLevel) {
      PM.addPass(TurboLoopPass());
    });
  PB.registerPipelineParsingCallback(PipelineParsingCB);
}

extern "C" auto LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo()
  -> ::llvm::PassPluginLibraryInfo {
  return {LLVM_PLUGIN_API_VERSION, "TurboLoop", "v0.1", RegisterCB};
}
