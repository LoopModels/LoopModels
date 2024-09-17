#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallBitVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
// #include <llvm/CodeGen/MachineValueType.h>
#ifndef USE_MODULE
#include "Containers/TinyVector.cxx"
#include "Math/MultiplicativeInverse.cxx"
#include "Utilities/Invariant.cxx"
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#else
export module TargetMachine;
import Invariant;
import MultiplicativeInverse;
import STL;
import TinyVector;
#endif

#ifdef USE_MODULE
export namespace target {
#else
namespace target {
#endif

struct CoreWidth {
  math::MultiplicativeInverse<double> load_, stow_, comp_, total_;
};

struct MachineCore {
  enum Arch : uint8_t {
    SandyBridge,
    Haswell,
    Broadwell,
    SkylakeClient,
    SkylakeServer,
    IceLakeClient,
    TigerLake,
    IceLakeServer,
    AlderLake,
    SapphireRapids,
    Zen1,
    Zen2,
    Zen3,
    Zen4,
    Zen5,
    AppleM1,
    AppleM2,
    AppleM3,
    AppleM4,
  };
  Arch arch_;

  static constexpr int64_t KiB = 1024z;
  static constexpr int64_t MiB = 1024z * KiB;
  static constexpr int64_t GiB = 1024z * MiB;
  static constexpr int64_t TiB = 1024z * GiB;
  // Note: LLVM `ClassID = 0` means GPR
  // `ClassID = 1` means vector
  enum class RegisterKind : uint8_t { GPR, Vector, Matrix, Mask };

  // constexpr Machine(Arch arch_) : arch(arch_) {}
  // returns `true` if succesful
  constexpr auto demoteArch() -> bool {
    switch (arch_) {
    case AppleM1:
    case AppleM2:
    case AppleM3:
    case SandyBridge: return false;
    case Haswell:
    case Broadwell:
    case SkylakeClient:
    case AlderLake:
    case Zen1:
    case Zen2:
    case Zen3: arch_ = SandyBridge; return true;
    case SkylakeServer:
    case IceLakeClient:
    case TigerLake:
    case IceLakeServer:
    case SapphireRapids:
    case Zen4:
    case Zen5: arch_ = SkylakeClient; return true;
    case AppleM4: arch_ = AppleM3; return true;
    }
  }

  // Gather is in AVX2 and AVX512
  [[nodiscard]] constexpr auto supportsGather() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return true;
    case SandyBridge: [[fallthrough]];
    default: return false;
    }
  }

  /// The standard for fast is an 1/throughput of at most 1 + numElements cycles
  [[nodiscard]] constexpr auto fastGather() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: return true;
    case Zen2: [[fallthrough]]; // slow
    case Zen1: [[fallthrough]];
    case Haswell: [[fallthrough]]; // 8
    case SandyBridge: [[fallthrough]];
    default: return false;
    }
  }
  [[nodiscard]] constexpr auto hasNEON() const -> bool {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return true;
    default: return false;
    }
  }
  [[nodiscard]] constexpr auto cachelineBytes() const -> int {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 128;
    default: return 64;
    }
  }
  [[nodiscard]] constexpr auto cachelineBits() const -> int {
    return cachelineBytes() << 3;
  }
  [[nodiscard]] constexpr auto hasFMA() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return true;
    case SandyBridge: [[fallthrough]];
    default: return false;
    }
  }
  [[nodiscard]] constexpr auto hasSSE1() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto hasSSE2() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto hasSSE3() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto hasSSE4A() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto hasSSE41() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto hasAVX() const -> bool { return !hasNEON(); }
  [[nodiscard]] constexpr auto
  getL0DSize(RegisterKind kind = RegisterKind::Vector) const -> int64_t {
    return getNumberOfRegisters(kind) * getRegisterByteWidth(kind);
  }
  [[nodiscard]] constexpr auto hasCLFLUSHOPT() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: return true;
    default: return false;
    }
  }
  [[nodiscard]] constexpr auto getL1DSize() const -> int64_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: return 48z * KiB;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 128z * KiB;
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 32z * KiB;
    }
  }
  [[nodiscard]] constexpr auto getL2DSize() const -> int64_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: return MiB;
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: return 512z * KiB;
    case SapphireRapids: return 2z * MiB;
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: return (5z * MiB) / 4z;
    case IceLakeClient: return 512z * KiB;
    case SkylakeServer: return MiB; // 1 MiB
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 3 * MiB;
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 256z * KiB;
    }
  }
  [[nodiscard]] constexpr auto getL3DSize() const -> int64_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: return 4z * MiB;
    case Zen1: return 2z * MiB;
    case SapphireRapids: return (15z * MiB) / 8z;
    case AlderLake: return 3z * MiB;
    case IceLakeServer: return (3z * MiB) / 2z;
    case TigerLake: return 3z * MiB;
    case IceLakeClient: return 2z * MiB;
    case SkylakeServer: return (11z * MiB) / 8z;
    case SkylakeClient: return 2z * MiB;
    case Broadwell: return (3z * MiB) / 2z;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 0;
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 2z * MiB;
    }
  }
  // ignoring that Broadwell may have actual L4
  [[nodiscard]] static constexpr auto getRAMSize() -> int64_t { return TiB; }
  // L0 is registers
  // Final level is RAM
  [[nodiscard]] constexpr auto getMemSize(int Level) const -> int64_t {
    switch (Level) {
    case 0: return getL0DSize();
    case 1: return getL1DSize();
    case 2: return getL2DSize();
    case 3: return getL3DSize();
    default: return getRAMSize();
    }
  }
  // strides and sizes are per core...
  // stride = # sets * linesize
  [[nodiscard]] constexpr auto getL1DStride() const -> int64_t {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 16z * KiB;
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 4z * KiB;
    }
  }
  [[nodiscard]] constexpr auto getL2DStride() const -> int64_t {
    switch (arch_) {
    case Zen4: return 128z * KiB;
    case Zen5: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 64z * KiB;
    case SapphireRapids: return 128z * KiB;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return MiB;
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 32z * KiB;
    }
  }
  // per core
  [[nodiscard]] constexpr auto getL3DStride() const -> int64_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: return 2z * MiB / 16;
    case Zen1: return MiB / 16;
    case SapphireRapids: return 128z * KiB;
    case AlderLake: return MiB / 4;
    case IceLakeServer: return MiB / 8z;
    case TigerLake: return MiB / 4;
    case IceLakeClient: return MiB / 8;
    case SkylakeServer: return MiB / 8z;
    case SkylakeClient: return MiB / 8;
    case Broadwell: return MiB / 8z;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 0;
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return MiB / 8;
    }
  }
  [[nodiscard]] constexpr auto getL4DStride() const -> int64_t { return 0; }
  [[nodiscard]] constexpr auto getL1DAssociativity() const -> uint32_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: return 12;
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]];
    default: return 8;
    }
  }
  [[nodiscard]] constexpr auto getL2DAssociativity() const -> uint32_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SapphireRapids: return 16;
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: return 20;
    case SkylakeClient: return 4;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 12;
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    default: return 8;
    }
  }
  [[nodiscard]] constexpr auto getL3DAssociativity() const -> uint32_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeClient: return 16;
    case SapphireRapids: return 15;
    case AlderLake:
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case Broadwell: return 12;
    case SkylakeServer: return 11;
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 16;
    }
  }
  [[nodiscard]] constexpr auto getL4DAssociativity() const -> uint32_t {
    return 0;
  }

  // Index into caches with 0-based indexing
  // Set bit indicates to count the cache as a victim cache,
  // subtracting the previous cache's size from the size-contribution.
  // In the future, perhaps consider that loads bipass it, so it only
  // experiences input bandwidth from evictions?
  // The meaning of a victim cache on a hardware level is either:
  // 1. Exclusive cache: does not contain any cachelines within a lower level
  //    cache.
  // 2. A cache filled only by evictions from lower level caches, e.g.
  //    Skylake-X's L3.
  // We may have to refine the model for case `2.`, i.e. loading from L3
  // will then result in copies within both L2 and L3. Is it implemented
  // as moving the data to a least recently used position, so the next
  // time we get an addition to this set, it gets evicted?
  // With different numbers of sets between L2 and L3, it may be some time
  // before we get an eviction of the 2nd copy from L3.
  // Would require some creative tests to figure out the behavior.
  [[nodiscard]] constexpr auto getVictimCacheFlag() const -> uint32_t {
    switch (arch_) {
    case SkylakeServer: return 4;
    default: return 0;
    }
  }
  [[nodiscard]] constexpr auto getuOpCacheSize() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: return 6912;
    case Zen3: [[fallthrough]];
    case Zen2: return 4096;
    case Zen1: return 2048;
    case SapphireRapids: [[fallthrough]];
    case AlderLake: return 4096;
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: return 2304;
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: [[fallthrough]];
    default: return 1536;
    }
  }
  [[nodiscard]] constexpr auto getTotalCoreWidth() const -> int {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 8;
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]]; // return 6;
    case SapphireRapids: [[fallthrough]];
    case AlderLake: return 6;
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: return 5;
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    default: return 4;
    }
  }
  [[nodiscard]] constexpr auto getLoadThroughput() const -> int {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 4;
    default: return 2;
    }
  }
  [[nodiscard]] constexpr auto getStowThroughput() const -> int {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 2;
    default: return 1;
    }
  }
  [[nodiscard]] constexpr auto getExecutionThroughput() const -> int {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 4;
    case SandyBridge: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen5: [[fallthrough]];
    default: return 2;
    }
  }
  /// cld( getExecutionThroughput(), cld( bytes, getExecutionWidth() ));
  [[nodiscard]] constexpr auto getExecutionThroughput(int64_t bytes) const
    -> int64_t {
    int64_t t = getExecutionThroughput(), p = executionPenalty(bytes);
    return p <= 1 ? t : (t + p - 1) / p;
  }
  [[nodiscard]] constexpr auto getExecutionThroughput(llvm::Type *T) const
    -> int64_t {
    return getExecutionThroughput(
      static_cast<int64_t>(T->getPrimitiveSizeInBits()) >> 3z);
  }
  [[nodiscard]] constexpr auto getCoreWidth() const -> CoreWidth {
    return {getLoadThroughput(), getStowThroughput(), getExecutionThroughput(),
            getTotalCoreWidth()};
  }
  // returns (cycle / bytes_loaded) + (cycle / bytes_stored)
  // unit is type
  [[nodiscard]] constexpr auto getLoadStowCycles() const -> double {
    int w = getVectorRegisterByteWidth();
    double l = getLoadThroughput() * w, s = getStowThroughput() * w;
    return (1.0 / l) + (1.0 / s);
  }
  // returns (cycle / elements_loaded) + (cycle / elements_stored)
  [[nodiscard]] constexpr auto getLoadStowCycles(llvm::Type *T) const
    -> double {
    int w = getVectorRegisterBitWidth() / T->getPrimitiveSizeInBits();
    double l = getLoadThroughput() * w, s = getStowThroughput() * w;
    return (1.0 / l) + (1.0 / s);
  }
  [[nodiscard]] constexpr auto getuOpDispatch() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]]; // return 6;
    case SapphireRapids: [[fallthrough]];
    case AlderLake: [[fallthrough]]; // return 6;
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient: return 6;
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    default: return 4;
    }
  }
  [[nodiscard]] constexpr auto getCacheAssociativity(int Level) const -> int {
    utils::invariant((Level > 0) && (Level < 4));
    switch (Level) {
    case 1: return getL1DAssociativity();
    case 2: return getL2DAssociativity();
    default: return getL3DAssociativity();
    }
  }
  [[nodiscard]] constexpr auto getL1DLatency() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: return 4; // 4-5, 7-8 for fp
    case SapphireRapids:
    case AlderLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: return 5;
    case SkylakeServer: [[fallthrough]];
    case SkylakeClient:
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    default: return 4;
    }
  }
  [[nodiscard]] constexpr auto getL2DLatency() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: return 13; // 4.57 * 3.655
    case Zen2: [[fallthrough]];
    case Zen1: return 12;
    case SapphireRapids: return 16;
    case AlderLake: return 15;
    case IceLakeServer: [[fallthrough]]; // return 14;
    case TigerLake: return 14;
    case IceLakeClient: return 13;
    case SkylakeServer: return 18;       // 4.1 * 4.585
    case SkylakeClient: [[fallthrough]]; // return 12;
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    default: return 12;
    }
  }
  [[nodiscard]] constexpr auto getL3DLatency() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: return 50;
    case Zen3: return 54; // 14.9 * 3.655
    case Zen2: [[fallthrough]];
    case Zen1: return 39;
    case SapphireRapids: return 124; // 33 ns, vs 4.27 ns for l2
    case AlderLake:
    case IceLakeServer: [[fallthrough]];
    case TigerLake: return 45;
    case IceLakeClient: return 36;
    case SkylakeServer: return 96; // 20.89 * 4.585
    case SkylakeClient: return 37;
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    default: return 30;
    }
  }
  /// This is RAM for many architectures
  [[nodiscard]] constexpr auto getL4DLatency() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]]; // DDR5
    case Zen4: return 500;      // DDR5
    case Zen3: return 376;      // 103 * 3.655
    case Zen2: [[fallthrough]];
    case Zen1: return 360;           // made up
    case SapphireRapids: return 500; // 33 ns, vs 4.27 ns for l2
    case AlderLake:
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 513; // 112 * 4.585
    case SkylakeClient: [[fallthrough]];
    case Broadwell: return 400; // made up
    case Haswell: [[fallthrough]];
    default: return 300; // made up
    }
  }
  [[nodiscard]] auto getMemLatency(int Level) const -> int {
    utils::invariant((Level > 0) && (Level < 5));
    switch (Level) {
    case 1: return getL1DLatency();
    case 2: return getL2DLatency();
    case 3: return getL3DLatency();
    default: return getL4DLatency();
    }
  }
  // Bandwidth is in average B/cycle
  [[nodiscard]] constexpr auto getL2DBandwidth() const -> double {
    // case SkylakeServer: return 43.3; // 168.8 / 3.9; opt manual says 52
    switch (arch_) {
    case Zen5: return 32; // 2800 / 5.5 / 16
    case Zen4: [[fallthrough]];
    case Zen3: return 32; // 114.15 / 3.642
    case Zen2: [[fallthrough]];
    case Zen1: return 30; // made up
    case SapphireRapids:
    case AlderLake:
    case IceLakeServer:
    case TigerLake: return 32.3;     // 155 / 4.8
    case IceLakeClient: return 34.5; // 135 / 3.9; opt manual says 48
    case SkylakeServer: return 52; // opt manual; 64 = peak; 45 more realistic?
    case SkylakeClient:
    case Broadwell: [[fallthrough]];
    case Haswell:
    default: return 25; // optimization manual for Broadwell; 32 peak
    }
  }
  // For shared caches, we benchmark multithreaded with private caches,
  // and divide by the number of cores.
  // Given multiple core counts, we'd ideally pick the largest, for the
  // most conservative per-core estimate.
  // We do not assume that a core has access to more than it's share of
  // memory bandwidth; real use cases should put all threads to work; a
  // goal is scalability.
  // Benchmarked systems:
  // Skylake-X/Cascadelake (10980XE)
  [[nodiscard]] constexpr auto getL3DBandwidth() const -> double {
    // case SkylakeServer: return 7.7;  // 30 / 3.9
    switch (arch_) {
    case Zen5: [[fallthrough]]; // 9950x: 2208 / 5.5 / 16
    case Zen4: return 25;
    case Zen3: return 18.7; // 68.174 / 3.642
    case Zen2: [[fallthrough]];
    case Zen1: return 18; // made up
    case SapphireRapids:
    case AlderLake:
    case IceLakeServer:
    case TigerLake: return 20.9;     // 100 / 4.8
    case IceLakeClient: return 21.0; // 85 / 3.9
    case SkylakeServer: return 3;    // opt manual; 32 = peak, 15 sustained
    case SkylakeClient:
    case Broadwell: [[fallthrough]];
    case Haswell:
    default: return 14; // optimization manual for Broadwell; 16 peak
    }
  }
  // Actually RAM if it exceeds number of cache levels
  [[nodiscard]] constexpr auto getL4DBandwidth() const -> double {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: return 0.7; // 9950x: 64 / 5.5 / 6.6
    case Zen2: [[fallthrough]];
    case Zen1: return 0.8; // made up
    case SapphireRapids:
    case AlderLake:
    case IceLakeServer:
    case TigerLake: return 7.3;      // 35 / 4.8
    case IceLakeClient: return 7.67; // 30 / 3.9
    case SkylakeServer: return 1;    // 3.33; // 13 / 3.9
    case SkylakeClient:
    case Broadwell:
    case Haswell: [[fallthrough]];
    default: return 1;
    }
  }
  // Actually RAM if it exceeds number of cache levels
  [[nodiscard]] constexpr auto getL5DBandwidth() const -> double { return 0.0; }
  [[nodiscard]] auto getCacheBandwidth(int Level) const -> double {
    // L1 is assumed to be governed by loads/stores executed/cycle
    utils::invariant((Level >= 2) && (Level <= 4));
    switch (Level) {
    // case 1: return getL1DBandwidth();
    case 2: return getL2DBandwidth();
    case 3: return getL3DBandwidth();
    default: return getL4DBandwidth();
    }
  }
  [[nodiscard]] auto getNumberOfVectorRegisters() const -> int64_t {
    switch (arch_) {
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: [[fallthrough]];
    case SandyBridge: return 16;
    default: return 32;
    }
  }
  [[nodiscard]] auto getNumberOfMaskRegisters() const -> int64_t {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 7;
    default: return 0;
    }
  }
  [[nodiscard]] auto getNumberOfMatrixRegisters() const -> int64_t {
    switch (arch_) {
    case SapphireRapids: return 8;
    default: return 0;
    }
  }
  [[nodiscard]] auto getNumberOfGPRegisters() const -> int64_t {
    switch (arch_) {
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: return 32;
    default: return 16;
    }
  }
  [[nodiscard]] auto getNumberOfRegisters(RegisterKind kind) const -> int64_t {
    switch (kind) {
    case RegisterKind::GPR: return getNumberOfGPRegisters();
    case RegisterKind::Vector: return getNumberOfVectorRegisters();
    case RegisterKind::Matrix: return getNumberOfMatrixRegisters();
    case RegisterKind::Mask: return getNumberOfMaskRegisters();
    }
    std::unreachable();
  }
  [[nodiscard]] constexpr auto getVectorRegisterByteWidth() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 64;
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return 32;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]];
    default: return 16;
    }
  }
  [[nodiscard]] constexpr auto getLog2VectorRegisterByteWidth() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 6;
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return 5;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]];
    default: return 4;
    }
  }
  [[nodiscard]] constexpr auto getExecutionByteWidth() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 64;
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return 32;
    case Zen1: [[fallthrough]];
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]];
    default: return 16;
    }
  }
  [[nodiscard]] constexpr auto getLog2ExecutionByteWidth() const -> int {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return 6;
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return 5;
    case Zen1: [[fallthrough]];
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]];
    default: return 4;
    }
  }
  /// cld(bytes, executionWidth())
  [[nodiscard]] constexpr auto executionPenalty(int64_t bytes) const
    -> int64_t {
    int64_t w = getLog2ExecutionByteWidth();
    return (bytes + (1 << w) - 1) >> w;
  }
  [[nodiscard]] constexpr auto executionPenalty(llvm::Type *T) const
    -> int64_t {
    int64_t bytes = static_cast<int64_t>(T->getPrimitiveSizeInBits()) >> 3z;
    return executionPenalty(bytes);
  }
  [[nodiscard]] constexpr auto getVectorRegisterBitWidth() const -> int {
    return 8 * getVectorRegisterByteWidth();
  }
  [[nodiscard]] constexpr auto hasAMX() const -> bool {
    return arch_ == SapphireRapids;
  }
  [[nodiscard]] constexpr auto hasAVX512() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: return true;
    default: return false;
    }
  }
  /// No Xeon-Phi support for now
  [[nodiscard]] constexpr auto hasBWI() const -> bool { return hasAVX512(); }
  [[nodiscard]] constexpr auto hasBF16() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case SapphireRapids: return true;
    default: return false;
    }
  }
  [[nodiscard]] constexpr auto hasAVX2() const -> bool {
    switch (arch_) {
    case Zen5: [[fallthrough]];
    case Zen4: [[fallthrough]];
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case Haswell: return true;
    default: return false;
    }
  }
  [[nodiscard]] auto getRegisterByteWidth(RegisterKind K) const -> int {
    switch (K) {
    case RegisterKind::GPR: return 8;
    case RegisterKind::Vector: return getVectorRegisterByteWidth();
    case RegisterKind::Matrix: return hasAMX() ? 16 * 64 : 0;
    case RegisterKind::Mask: return hasAVX512() ? 8 : 0;
    }
    std::unreachable();
  }
  [[nodiscard]] auto getLog2RegisterByteWidth(RegisterKind K) const -> int {
    switch (K) {
    case RegisterKind::GPR: return 3;
    case RegisterKind::Vector: return getLog2VectorRegisterByteWidth();
    case RegisterKind::Matrix: return hasAMX() ? 10 : -1;
    case RegisterKind::Mask: return hasAVX512() ? 3 : -1;
    }
    std::unreachable();
  }
  [[nodiscard]] auto getRegisterBitWidth(RegisterKind K) const -> int {
    return 8 * getRegisterByteWidth(K);
  }
  static constexpr auto is64Bit() -> bool { return true; }
  static constexpr auto hasMacroFusion() -> bool { return true; }
  static constexpr auto hasBranchFusion() -> bool { return true; }

  struct Cache {
    // linesize * # of sets
    [[no_unique_address]] math::MultiplicativeInverse<int64_t> stride_;
    uint32_t victim_ : 1;
    uint32_t associativty_ : 31;
    // bandwidth of the next cache (or RAM) to this cache
    // e.g., for L2, it is L3->L2 bandwidth.
    // Unit is cycles/element.
    double inv_next_bandwidth_;
  };
  static_assert(sizeof(Cache) == 32);
  // NOTE: sizes are in bits
  constexpr auto cacheSummary() const -> containers::TinyVector<Cache, 4> {
    uint32_t victim_flag = getVictimCacheFlag();
    containers::TinyVector<Cache, 4> ret{
      {.stride_ = 8 * getL1DStride(),
       .victim_ = victim_flag & 1,
       .associativty_ = getL1DAssociativity(),
       .inv_next_bandwidth_ = 0.125 / getL2DBandwidth()},
      {.stride_ = 8 * getL2DStride(),
       .victim_ = (victim_flag >> 1) & 1,
       .associativty_ = getL2DAssociativity(),
       .inv_next_bandwidth_ = 0.125 / getL3DBandwidth()}};
    if (int x = getL3DStride()) {
      ret.push_back({.stride_ = 8 * x,
                     .victim_ = (victim_flag >> 2) & 1,
                     .associativty_ = getL3DAssociativity(),
                     .inv_next_bandwidth_ = 0.125 / getL4DBandwidth()});
      if (int y = getL4DStride()) {
        ret.push_back({.stride_ = 8 * y,
                       .victim_ = (victim_flag >> 3) & 1,
                       .associativty_ = getL4DAssociativity(),
                       .inv_next_bandwidth_ = 0.125 / getL5DBandwidth()});
      }
    }
    return ret;
  }
};

struct NoTTI {};
template <bool HasTTI = true> struct Machine : public MachineCore {
  using TTITy =
    std::conditional_t<HasTTI, const llvm::TargetTransformInfo *, NoTTI>;
  using CostKind = llvm::TargetTransformInfo::TargetCostKind;
  // const llvm::TargetTransformInfo &TTI;
  [[no_unique_address]] TTITy tti_{};

  [[nodiscard]] auto getCallInstrCost(llvm::Function *F, llvm::Type *T,
                                      llvm::ArrayRef<llvm::Type *> argTyps,
                                      CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) return executionPenalty(T);
    else return tti_->getCallInstrCost(F, T, argTyps, ck);
  }
  [[nodiscard]] auto getArithmeticInstrCost(llvm::Intrinsic::ID id,
                                            llvm::Type *T, CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) {
      int64_t r = executionPenalty(T);
      switch (ck) {
      case CostKind::TCK_RecipThroughput: return r;
      case CostKind::TCK_Latency: return 3 + r;
      case CostKind::TCK_CodeSize: return r;
      case CostKind::TCK_SizeAndLatency: return 3 + 2 * r;
      }
      std::unreachable();
    } else return tti_->getArithmeticInstrCost(id, T, ck);
  }
  [[nodiscard]] auto
  getCmpSelInstrCost(llvm::Intrinsic::ID id, llvm::Type *T, llvm::Type *cmpT,
                     llvm::CmpInst::Predicate pred, CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) return executionPenalty(T);
    else return tti_->getCmpSelInstrCost(id, T, cmpT, pred, ck);
  }
  [[nodiscard]] auto
  getCastInstrCost(llvm::Intrinsic::ID id, llvm::Type *dstT, llvm::Type *srcT,
                   llvm::TargetTransformInfo::CastContextHint ctx,
                   CostKind ck) const -> llvm::InstructionCost {
    if constexpr (!HasTTI) return executionPenalty(dstT);
    else return tti_->getCastInstrCost(id, dstT, srcT, ctx, ck);
  }
  [[nodiscard]] auto getIntrinsicInstrCost(llvm::IntrinsicCostAttributes attr,
                                           CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) {
      int64_t r = executionPenalty(attr.getReturnType());
      // FIXME: I made up a bunch of numbers.
      switch (attr.getID()) {
      case llvm::Intrinsic::fmuladd: return hasFMA() ? r : 2 * r;
      case llvm::Intrinsic::fma: return hasFMA() ? r : 10 * r;
      case llvm::Intrinsic::sqrt: return 10 * r;
      case llvm::Intrinsic::sin: [[fallthrough]];
      case llvm::Intrinsic::cos: return 20 * r;
      case llvm::Intrinsic::exp: [[fallthrough]];
#if LLVM_VERSION_MAJOR >= 18
      case llvm::Intrinsic::exp10: [[fallthrough]];
#endif
      case llvm::Intrinsic::exp2: return 15 * r;
      case llvm::Intrinsic::log: [[fallthrough]];
      case llvm::Intrinsic::log2: [[fallthrough]];
      case llvm::Intrinsic::log10: return 17 * r;
      default: return 25 * r;
      }
    } else return tti_->getIntrinsicInstrCost(attr, ck);
  }
  [[nodiscard]] auto getMemoryOpCost(llvm::Intrinsic::ID id, llvm::Type *T,
                                     llvm::Align align, unsigned addrSpace,
                                     CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) return executionPenalty(T);
    else return tti_->getMemoryOpCost(id, T, align, addrSpace, ck);
  }
  [[nodiscard]] auto getMaskedLoadRT() const -> llvm::InstructionCost {
    switch (arch_) {
    case SandyBridge: return 2;
    case Haswell: [[fallthrough]];
    case Broadwell: return 4;
    case SkylakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case Zen5: [[fallthrough]];
    case Zen4: return 1;
    case Zen3: [[fallthrough]];
    case Zen2: return 1;
    case Zen1: return 20;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]]; // return 4;
    default: return 4;
    }
  }
  [[nodiscard]] auto getMaskedStoreRT() const -> llvm::InstructionCost {
    switch (arch_) {
    case SandyBridge: return 2;
    case Haswell: [[fallthrough]];
    case Broadwell: [[fallthrough]];
    case SkylakeClient: [[fallthrough]];
    case SkylakeServer: [[fallthrough]];
    case IceLakeClient: [[fallthrough]];
    case TigerLake: [[fallthrough]];
    case IceLakeServer: [[fallthrough]];
    case AlderLake: [[fallthrough]];
    case SapphireRapids: [[fallthrough]];
    case Zen5: [[fallthrough]];
    case Zen4: return 1;
    case Zen3: [[fallthrough]];
    case Zen2: [[fallthrough]];
    case Zen1: return 12;
    case AppleM4: [[fallthrough]];
    case AppleM3: [[fallthrough]];
    case AppleM2: [[fallthrough]];
    case AppleM1: [[fallthrough]]; // return 4;
    default: return 4;
    }
  }
  [[nodiscard]] auto getMaskedMemoryOpCost(llvm::Intrinsic::ID id,
                                           llvm::Type *T, llvm::Align align,
                                           unsigned addrSpace,
                                           CostKind ck) const
    -> llvm::InstructionCost {
    if constexpr (!HasTTI) {
      return executionPenalty(T) * ((id == llvm::Instruction::Load)
                                      ? getMaskedLoadRT()
                                      : getMaskedStoreRT());
    } else return tti_->getMaskedMemoryOpCost(id, T, align, addrSpace, ck);
  }
  [[nodiscard]] auto
  getGatherScatterOpCost(llvm::Intrinsic::ID id, llvm::FixedVectorType *VT,
                         bool varMask, llvm::Align align, CostKind ck) const
    -> llvm::InstructionCost {

    if constexpr (!HasTTI) {
      bool fast = (id == llvm::Instruction::Load) ? fastGather() : hasAVX512();
      unsigned width = VT->getNumElements();
      if (!fast) width *= 2;
      return width * getMemoryOpCost(id, VT->getElementType(), align, 0, ck);
    } else
      return tti_->getGatherScatterOpCost(id, VT, nullptr, varMask, align, ck);
  }

  auto isLegalAltInstr(llvm::VectorType *VecTy, unsigned Opcode0,
                       unsigned Opcode1, const llvm::SmallBitVector &OpcodeMask)
    -> bool {
    if constexpr (!HasTTI) {
      llvm::Type *el_ty = VecTy->getElementType();
      if (!(el_ty->isFloatTy() || el_ty->isDoubleTy())) return false;

      unsigned num_elements =
        llvm::cast<llvm::FixedVectorType>(VecTy)->getNumElements();
      assert(OpcodeMask.size() == num_elements &&
             "Mask and VecTy are incompatible");
      if (std::popcount(num_elements) != 1) return false;
      // Check the opcode pattern. We apply the mask on the opcode arguments and
      // then check if it is what we expect.
      for (ptrdiff_t lane = 0; lane < num_elements; ++lane) {
        unsigned Opc = OpcodeMask.test(lane) ? Opcode1 : Opcode0;
        // We expect FSub for even lanes and FAdd for odd lanes.
        if (lane % 2 == 0 && Opc != llvm::Instruction::FSub) return false;
        if (lane % 2 == 1 && Opc != llvm::Instruction::FAdd) return false;
      }
      // requies SSE3
      return (el_ty->isFloatTy() ? num_elements % 4 : num_elements % 2) == 0;
    } else return tti_->isLegalAltInstr(VecTy, Opcode0, Opcode1, OpcodeMask);
  }
};

constexpr auto machine(MachineCore::Arch arch) -> Machine<false> {
  return {{arch}};
}
constexpr auto machine(MachineCore::Arch arch,
                       const llvm::TargetTransformInfo &TTI) -> Machine<true> {
  return {{arch}, &TTI};
}

} // namespace target
