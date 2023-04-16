#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/DiagnosticInfo.h>
[[maybe_unused, nodiscard]] static auto
remarkAnalysis(const llvm::StringRef remarkName, llvm::Loop *L,
               llvm::Instruction *Inst = nullptr)
  -> llvm::OptimizationRemarkAnalysis {
  llvm::Value *codeRegion = L->getHeader();
  llvm::DebugLoc DL = L->getStartLoc();

  if (Inst) {
    codeRegion = Inst->getParent();
    // If there is no debug location attached to the instruction, revert
    // back to using the loop's.
    if (Inst->getDebugLoc()) DL = Inst->getDebugLoc();
  }

  return {"turbo-loop", remarkName, DL, codeRegion};
}
