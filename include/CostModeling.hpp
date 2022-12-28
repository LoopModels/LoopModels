#pragma once

#include "./Instruction.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "ControlFlowMerging.hpp"
#include "LoopBlock.hpp"
#include "LoopForest.hpp"
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>

struct CPURegisterFile {
  [[no_unique_address]] uint8_t maximumVectorWidth;
  [[no_unique_address]] uint8_t numVectorRegisters;
  [[no_unique_address]] uint8_t numGeneralPurposeRegisters;
  [[no_unique_address]] uint8_t numPredicateRegisters;

  // hacky check for has AVX512
  static inline auto hasAVX512(llvm::LLVMContext &C,
                               llvm::TargetTransformInfo &TTI) -> bool {
    return TTI.isLegalMaskedExpandLoad(
      llvm::FixedVectorType::get(llvm::Type::getDoubleTy(C), 8));
  }

  static auto estimateNumPredicateRegisters(llvm::LLVMContext &C,
                                            llvm::TargetTransformInfo &TTI)
    -> uint8_t {
    if (TTI.supportsScalableVectors())
      return 8;
    // hacky check for AVX512
    if (hasAVX512(C, TTI))
      return 7; // 7, because k0 is reserved for unmasked
    return 0;
  }
  // returns vector width in bits
  static auto estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                         llvm::TargetTransformInfo &TTI)
    -> uint8_t {
    uint8_t twiceMaxVectorWidth = 2;
    auto f32 = llvm::Type::getFloatTy(C);
    llvm::InstructionCost prevCost = TTI.getArithmeticInstrCost(
      llvm::Instruction::FAdd,
      llvm::FixedVectorType::get(f32, twiceMaxVectorWidth));
    while (true) {
      llvm::InstructionCost nextCost = TTI.getArithmeticInstrCost(
        llvm::Instruction::FAdd,
        llvm::FixedVectorType::get(f32, twiceMaxVectorWidth *= 2));
      if (nextCost > prevCost)
        break;
      prevCost = nextCost;
    }
    return 16 * twiceMaxVectorWidth;
  }
  CPURegisterFile(llvm::LLVMContext &C, llvm::TargetTransformInfo &TTI) {
    maximumVectorWidth = estimateMaximumVectorWidth(C, TTI);
    numVectorRegisters = TTI.getNumberOfRegisters(true);
    numGeneralPurposeRegisters = TTI.getNumberOfRegisters(false);
    numPredicateRegisters = estimateNumPredicateRegisters(C, TTI);
  }
};
struct CPUExecutionModel {};

struct LoopTreeSchedule;

// Plan for cost modeling:
// 1. Build Instruction graph
// 2. Iterate over all PredicatedChains, merging instructions across branches
// where possible
// 3. Create a loop tree structure for optimization
// 4. Create InstructionBlocks at each level.

// void pushBlock(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
//                llvm::SmallPtrSet<llvm::BasicBlock *, 32> &chainBBs,
//                Predicates &pred, llvm::BasicBlock *BB) {
//     assert(chainBBs.contains(block));
//     chainBBs.erase(BB);
//     // we only want to extract relevant instructions, i.e. parents of
//     stores for (llvm::Instruction &instr : *BB) {
//         if (trackInstr.contains(&instr))
//             instructions.emplace_back(pred, instr);
//     }
//     llvm::Instruction *term = BB->getTerminator();
//     if (!term)
//         return;
//     switch (term->getNumSuccessors()) {
//     case 0:
//         return;
//     case 1:
//         BB = term->getSuccessor(0);
//         if (chainBBs.contains(BB))
//             pushBlock(trackInstr, chainBBs, pred, BB);
//         return;
//     case 2:
//         break;
//     default:
//         assert(false);
//     }
//     auto succ0 = term->getSuccessor(0);
//     auto succ1 = term->getSuccessor(1);
//     if (chainBBs.contains(succ0) && chainBBs.contains(succ1)) {
//         // TODO: we need to fuse these blocks.

//     } else if (chainBBs.contains(succ0)) {
//         pushBlock(trackInstr, chainBBs, pred, succ0);
//     } else if (chainBBs.contains(succ1)) {
//         pushBlock(trackInstr, chainBBs, pred, succ1);
//     }
// }

/// Given: llvm::SmallVector<LoopAndExit> subTrees;
/// subTrees[i].second is the preheader for
/// subTrees[i+1].first, which has exit block
/// subTrees[i+1].second
struct ScheduledMemoryAccess {
  MemoryAccess *access;
};
struct InstructionBlock {
  [[no_unique_address]] llvm::SmallVector<ScheduledMemoryAccess> memAccesses;
};
struct LoopTreeSchedule;
using LoopAndExit = std::pair<LoopTreeSchedule *, InstructionBlock *>;

struct LoopTreeSchedule {
  /// Header of the loop.
  [[no_unique_address]] InstructionBlock header;
  /// Variable number of sub loops and their associated exits.
  /// For the inner most loop, `subTrees.empty()`.
  [[no_unique_address]] llvm::SmallVector<LoopAndExit> subTrees;
  [[no_unique_address]] uint8_t depth;
  [[no_unique_address]] uint8_t vectorizationFactor{1};
  [[no_unique_address]] uint8_t unrollFactor{1};
  [[no_unique_address]] uint8_t unrollPredcedence{1};
  [[nodiscard]] auto getNumSubTrees() const -> size_t {
    return subTrees.size();
  }
  [[nodiscard]] auto getDepth() const -> size_t { return depth; }

  void init(llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
            llvm::BumpPtrAllocator &tAlloc, LoopTree *loopForest,
            LinearProgramLoopBlock &LB, llvm::TargetTransformInfo &TTI,
            unsigned int vectorBits) {
    // TODO: can we shorten the life span of the instructions we
    // allocate here to `lalloc`? I.e., do we need them to live on after
    // this forest is scheduled?
    buildInstructionGraph(alloc, cache, LB);
    mergeInstructions(alloc, cache, loopForest, TTI, tAlloc, vectorBits);
    for (auto &node : LB.getNodes()) {
      // now we walk the scheduled nodes to build the loop tree.
    }
  }
};

struct LoopForestSchedule {
  [[no_unique_address]] InstructionBlock enteringBlock;
  [[no_unique_address]] llvm::SmallVector<LoopAndExit> loopNests;
  [[no_unique_address]] llvm::SmallVector<llvm::SmallVector<LoopTreeSchedule *>>
    depthTrees;
  [[no_unique_address]] llvm::BumpPtrAllocator &allocator;
};
