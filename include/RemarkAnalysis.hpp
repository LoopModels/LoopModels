#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/DiagnosticInfo.h>
[[maybe_unused, nodiscard]] static auto
remarkAnalysis(const llvm::StringRef remarkName, llvm::Loop *L,
               llvm::Instruction *I = nullptr)
  -> llvm::OptimizationRemarkAnalysis {
  llvm::Value *codeRegion = L->getHeader();
  llvm::DebugLoc DL = L->getStartLoc();

  if (I) {
    codeRegion = I->getParent();
    // If there is no debug location attached to the instruction, revert
    // back to using the loop's.
    if (I->getDebugLoc())
      DL = I->getDebugLoc();
  }

  return {"turbo-loop", remarkName, DL, codeRegion};
}
