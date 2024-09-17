#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/DiagnosticInfo.h>

#ifdef USE_MODULE
export module Remark;
#endif

#ifdef USE_MODULE
export namespace utils {
#else
namespace utils {
#endif
[[maybe_unused, nodiscard]] inline auto remarkAnalysis(
  const llvm::StringRef remarkName, llvm::Loop *L,
  llvm::Instruction *Inst = nullptr) -> llvm::OptimizationRemarkAnalysis {
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
} // namespace utils