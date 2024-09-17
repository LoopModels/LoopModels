#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Analysis/VectorUtils.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Alignment.h>
#include <llvm/TargetParser/Host.h>

#ifndef USE_MODULE
#include "Target/Machine.cxx"
#else
export module Host;
import TargetMachine;
#endif

#ifdef USE_MODULE
export namespace target {
#else
namespace target {
#endif

inline auto host() -> MachineCore {
  llvm::StringRef s = llvm::sys::getHostCPUName();

  if (s == "sapphirerapids" || s == "graniterapids" || s == "graniterapids-d")
    return {MachineCore::Arch::SapphireRapids};
  if (s == "alderlake" || s == "arrowlake" || s == "arrowlake-s" ||
      s == "pantherlake")
    return {MachineCore::Arch::AlderLake};
  if (s == "tigerlake") return {MachineCore::Arch::TigerLake};
  if (s == "rocketlake" || s == "icelake-client")
    return {MachineCore::Arch::IceLakeClient};

  if (s == "skylake-avx512" || s == "cascadelake" || s == "cooperlake" ||
      s == "cannonlake")
    return {MachineCore::Arch::SkylakeServer};
  if (s == "skylake") return {MachineCore::Arch::SkylakeClient};
  if (s == "broadwell") return {MachineCore::Arch::Broadwell};
  if (s == "haswell") return {MachineCore::Arch::Haswell};
  if (s == "sandybridge" || s == "ivybridge")
    return {MachineCore::Arch::SandyBridge};
  if (s == "znver5") return {MachineCore::Arch::Zen5};
  if (s == "znver4") return {MachineCore::Arch::Zen4};
  if (s == "znver3") return {MachineCore::Arch::Zen3};
  if (s == "znver2") return {MachineCore::Arch::Zen2};
  if (s == "znver1") return {MachineCore::Arch::Zen1};
  if (s == "apple-m4") return {MachineCore::Arch::AppleM4};
  if (s == "apple-m3") return {MachineCore::Arch::AppleM3};
  if (s == "apple-m2") return {MachineCore::Arch::AppleM2};
  if (s == "apple-m1") return {MachineCore::Arch::AppleM1};

  if (s == "i386" || s == "i486" || s == "pentium-mmx" || s == "pentium-m" ||
      s == "pentium2" || s == "pentium3" || s == "pentium4" || s == "nocona" ||
      s == "prescott" || s == "pentiumpro" || s == "pentium" || s == "core2" ||
      s == "yonah" || s == "penryn" || s == "nehalem" || s == "westmere")
    __builtin_trap();
  if (s == "bonnel" || s == "silvermont" || s == "goldmont" ||
      s == "goldmont-plus" || s == "tremont" || s == "sierraforest" ||
      s == "grandridge" || s == "clearwaterforest")
    __builtin_trap();
  if (s == "knl" || s == "knm") __builtin_trap();
  __builtin_trap();
}

inline auto machine(const llvm::TargetTransformInfo &TTI,
                    llvm::LLVMContext &ctx) -> Machine<true> {
  MachineCore mc = host();
  // we demote the host until we find something that seems to match `TTI`
#if LLVM_VERSION_MAJOR >= 19
  if (mc.hasAVX512() && !TTI.isLegalMaskedExpandLoad(llvm::FixedVectorType::get(
                          llvm::Type::getDoubleTy(ctx), 8), llvm::Align::Constant<64>()))
    #else
  if (mc.hasAVX512() && !TTI.isLegalMaskedExpandLoad(llvm::FixedVectorType::get(
                          llvm::Type::getDoubleTy(ctx), 8)))
    #endif
    mc.demoteArch();
  if (mc.hasAVX2() && !TTI.isLegalNTLoad(llvm::FixedVectorType::get(
                                           llvm::Type::getDoubleTy(ctx), 32),
                                         llvm::Align::Constant<64>()))
    mc.demoteArch();
  if (mc.hasAVX() && !TTI.isLegalMaskedLoad(llvm::Type::getDoubleTy(ctx),
                                            llvm::Align::Constant<64>()))
    mc.demoteArch();

  return {mc, &TTI};
}

} // namespace target