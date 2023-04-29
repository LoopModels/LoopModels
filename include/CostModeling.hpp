#pragma once

#include "./ControlFlowMerging.hpp"
#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./LoopForest.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include <algorithm>
#include <any>
#include <cassert>
#include <cstddef>
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

namespace CostModeling {

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
    if (TTI.supportsScalableVectors()) return 8;
    // hacky check for AVX512
    if (hasAVX512(C, TTI)) return 7; // 7, because k0 is reserved for unmasked
    return 0;
  }
  // returns vector width in bits
  static auto estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                         llvm::TargetTransformInfo &TTI)
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
    return 16 * twiceMaxVectorWidth;
  }
  constexpr CPURegisterFile(llvm::LLVMContext &C,
                            llvm::TargetTransformInfo &TTI) {
    maximumVectorWidth = estimateMaximumVectorWidth(C, TTI);
    numVectorRegisters = TTI.getNumberOfRegisters(true);
    numGeneralPurposeRegisters = TTI.getNumberOfRegisters(false);
    numPredicateRegisters = estimateNumPredicateRegisters(C, TTI);
  }
};
// struct CPUExecutionModel {};

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

/// Initialized from a LoopBlock
/// First, all memory accesses are placed.
///  - Topologically sort at the same level
///  - Hoist out as far as posible
/// Then, merge eligible loads.
///  - I.e., merge loads that are in the same block with same address and not
///  aliasing stores in between
/// Finally, place instructions, seeded by stores, hoisted as far out as
/// possible. With this, we can begin cost modeling.
class LoopTreeSchedule {
  template <typename T> using Vec = LinAlg::ResizeableView<T *, unsigned>;
  struct InstructionBlock {
    [[no_unique_address]] Vec<Address *> memAccesses;
  };
  struct LoopAndExit {
    [[no_unique_address]] LoopTreeSchedule *subTree;
    [[no_unique_address]] InstructionBlock exit;
  };
  /// Header of the loop.
  [[no_unique_address]] InstructionBlock header;
  /// Variable number of sub loops and their associated exits.
  /// For the inner most loop, `subTrees.empty()`.
  [[no_unique_address]] Vec<LoopAndExit> subTrees;
  [[no_unique_address]] uint8_t depth;
  [[no_unique_address]] uint8_t vectorizationFactor{1};
  [[no_unique_address]] uint8_t unrollFactor{1};
  [[no_unique_address]] uint8_t unrollPredcedence{1};
  [[nodiscard]] constexpr auto getNumSubTrees() const -> size_t {
    return subTrees.size();
  }
  constexpr auto getLoopAndExit(BumpAlloc<> &tAlloc, size_t i)
    -> LoopAndExit * {
    if (LoopAndExit *ret = subTrees[i]) return ret;
    return subTrees[i] = tAlloc.allocate<LoopAndExit>();
  }
  auto getLoopTripple(BumpAlloc<> &tAlloc, size_t i)
    -> std::tuple<InstructionBlock *, LoopTreeSchedule *, InstructionBlock *> {
    InstructionBlock *H;
    if (i) {
      auto *loopAndExit = getLoopAndExit(tAlloc, i - 1);
      H = &loopAndExit->exit;
    } else H = &header;
    auto *loopAndExit = getLoopAndExit(tAlloc, i);
    LoopTreeSchedule *L = loopAndExit->subTree;
    InstructionBlock *E = &loopAndExit->exit;
    return {H, L, E};
  }
  auto getLoop(BumpAlloc<> &tAlloc, size_t i) -> LoopTreeSchedule * {
    return getLoopAndExit(tAlloc, i)->subTree;
  }
  [[nodiscard]] auto getDepth() const -> size_t { return depth; }
  /// Adds the schedule corresponding for the innermost loop.
  void addInnermostSchedule(BumpAlloc<> &alloc, Instruction::Cache &cache,
                            BumpAlloc<> &tAlloc, LoopTree *loopForest,
                            LinearProgramLoopBlock &LB, ScheduledNode &node,
                            llvm::TargetTransformInfo &TTI,
                            unsigned int vectorBits, AffineSchedule sch,
                            size_t depth_) {
    // TODO: emplace all memory accesses that occur here
    assert(subTrees.empty());
  }
  // this method descends
  // NOLINTNEXTLINE(misc-no-recursion)
  void addMemory(BumpAlloc<> &alloc, Instruction::Cache &cache,
                 BumpAlloc<> &tAlloc, LoopTree *loopForest,
                 LinearProgramLoopBlock &LB, ScheduledNode &node,
                 llvm::TargetTransformInfo &TTI, unsigned int vectorBits,
                 AffineSchedule sch, size_t d) {
    depth = d++;
    if (d == sch.getNumLoops()) {
      assert(sch.getFusionOmega(d) == 0);
      return addInnermostSchedule(alloc, cache, tAlloc, loopForest, LB, node,
                                  TTI, vectorBits, sch, d);
    }
    size_t i = sch.getFusionOmega(d);
    assert(i < subTrees.size());
    // if (i >= subTrees.size()) subTrees.resize(i + 1);
    // TODO: emplace all memory access that occur here in either H or E.
    // what we need is to first check:
    // 1. can we hoist the memory access out of the remaining inner loops?
    // 2. if so, do we place before or after the loop?
    // To hoist out, we need the intersection polyheda between the memory access
    // and all accesses explicitly dependent on inner loops to be empty.
    getLoop(tAlloc, i)->addMemory(alloc, cache, tAlloc, loopForest, LB, node,
                                  TTI, vectorBits, sch, d);
  }
  void init(BumpAlloc<> &alloc, Instruction::Cache &cache, BumpAlloc<> &tAlloc,
            LoopTree *loopForest, LinearProgramLoopBlock &LB,
            llvm::TargetTransformInfo &TTI, unsigned int vectorBits) {
    // TODO: can we shorten the life span of the instructions we
    // allocate here to `lalloc`? I.e., do we need them to live on after
    // this forest is scheduled?

    // we first add all memory operands
    // then, we licm
    for (auto &node : LB.getNodes()) {
      // now we walk the scheduled nodes to build the loop tree.
      AffineSchedule sch = node.getSchedule();
      // TODO: preprocess all memory accesses to compute their rotated indMats
      addMemory(alloc, cache, tAlloc, loopForest, LB, node, TTI, vectorBits,
                sch, 0);
      // addSchedule(alloc, cache, tAlloc, loopForest, LB, node, TTI,
      // vectorBits, sch, 0);
    }
    // buidInstructionGraph(alloc, cache);
    mergeInstructions(alloc, cache, loopForest, TTI, tAlloc, vectorBits);
  }
};

struct LoopForestSchedule : LoopTreeSchedule {
  [[no_unique_address]] BumpAlloc<> &allocator;
};
} // namespace CostModeling
