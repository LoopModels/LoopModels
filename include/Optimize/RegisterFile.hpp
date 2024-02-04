#pragma once
#ifndef RegisterFile_hpp_INCLUDED
#define RegisterFile_hpp_INCLUDED

#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DerivedTypes.h>

namespace poly::RegisterFile {
// returns vector width in bytes, ignoring mprefer-vector-width
inline auto estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                       const llvm::TargetTransformInfo &TTI)
  -> uint8_t {
  uint8_t twiceMaxVectorWidth = 2;
  auto *f32 = llvm::Type::getFloatTy(C);
  llvm::InstructionCost prevCost = TTI.getArithmeticInstrCost(
    llvm::Instruction::FAdd,
    llvm::FixedVectorType::get(f32, twiceMaxVectorWidth));
  while (true) {
    llvm::InstructionCost nextCost = TTI.getArithmeticInstrCost(
      llvm::Instruction::FAdd,
      llvm::FixedVectorType::get(f32, twiceMaxVectorWidth *= 2));
    if (nextCost > prevCost) break;
    prevCost = nextCost;
  }
  return 2 * twiceMaxVectorWidth;
}

class CPURegisterFile {
  uint8_t maximumVectorWidth;
  uint8_t numVectorRegisters;
  uint8_t numGeneralPurposeRegisters;
  uint8_t numPredicateRegisters;

#if defined(__x86_64__)
  // hacky check for has AVX512
  static inline auto hasAVX512(llvm::LLVMContext &C,
                               const llvm::TargetTransformInfo &TTI) -> bool {
    return TTI.isLegalMaskedExpandLoad(
      llvm::FixedVectorType::get(llvm::Type::getDoubleTy(C), 8));
  }
#else
  // assume we're not cross-compiling to x64 from some other arch to reduce the
  // risk of false positives
  static constexpr hasAVX512(llvm::LLVMContext &,
                             const llvm::TargetTransformInfo &)
    ->bool {
    return false;
  }
#endif

  static auto estimateNumPredicateRegisters(
    llvm::LLVMContext &C, const llvm::TargetTransformInfo &TTI) -> uint8_t {
    if (TTI.supportsScalableVectors()) return 8;
    // hacky check for AVX512
    if (hasAVX512(C, TTI)) return 7; // 7, because k0 is reserved for unmasked
    return 0;
  }

public:
  CPURegisterFile(llvm::LLVMContext &C, const llvm::TargetTransformInfo &TTI) {
    maximumVectorWidth = estimateMaximumVectorWidth(C, TTI);
    numVectorRegisters = TTI.getNumberOfRegisters(true);
    numGeneralPurposeRegisters = TTI.getNumberOfRegisters(false);
    numPredicateRegisters = estimateNumPredicateRegisters(C, TTI);
  }
  [[nodiscard]] constexpr auto getNumVectorBits() const -> uint8_t {
    return maximumVectorWidth;
  }
  [[nodiscard]] constexpr auto getNumVector() const -> uint8_t {
    return numVectorRegisters;
  }
  [[nodiscard]] constexpr auto getNumScalar() const -> uint8_t {
    return numGeneralPurposeRegisters;
  }
  [[nodiscard]] constexpr auto getNumPredicate() const -> uint8_t {
    return numPredicateRegisters;
  }
};

} // namespace poly::RegisterFile
#endif // RegisterFile_hpp_INCLUDED
