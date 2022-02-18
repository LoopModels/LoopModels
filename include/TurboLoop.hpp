#pragma once

#include "integerMap.hpp"
#include "poset.hpp"
#include "llvm/IR/PassManager.h"

class TurboLoopPass
    : public llvm::PassInfoMixin<TurboLoopPass> {
public:
  llvm::PreservedAnalyses
  run(llvm::Function &F,
      llvm::FunctionAnalysisManager &AM);
    ValueToPosetMap valueToPosetMap;
    PartiallyOrderedSet poset;
};

