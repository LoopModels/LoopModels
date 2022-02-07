#pragma once

#include "llvm/IR/PassManager.h"

class TurboLoopPass
    : public llvm::PassInfoMixin<TurboLoopPass> {
public:
  llvm::PreservedAnalyses
  run(llvm::Function &F,
      llvm::FunctionAnalysisManager &AM);
};

