#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <cstdint>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Support/Casting.h>
#include <optional>

#ifdef USE_MODULE
export module LLVMUtils;
#endif

#ifdef USE_MODULE
export namespace utils {
#else
namespace utils {
#endif
inline auto getConstantInt(const llvm::SCEV *v) -> std::optional<int64_t> {
  if (const auto *sc = llvm::dyn_cast<const llvm::SCEVConstant>(v)) {
    llvm::ConstantInt *c = sc->getValue();
    // we need bit width of 64, for sake of negative numbers
    if (c->getBitWidth() <= 64) return c->getSExtValue();
  }
  return {};
}
} // namespace utils