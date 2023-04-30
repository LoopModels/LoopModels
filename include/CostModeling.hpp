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
  template <typename T> using Vec = LinAlg::ResizeableView<T, unsigned>;

  template <typename T>
  static constexpr auto realloc(BumpAlloc<> &alloc, Vec<T> vec, unsigned nc)
    -> Vec<T> {
    return Vec<T>(alloc.reallocate<false>(vec.data(), vec.getCapacity(), nc),
                  vec.size(), nc);
  }
  template <typename T>
  static constexpr auto grow(BumpAlloc<> &alloc, Vec<T> vec, unsigned sz)
    -> Vec<T> {
    if (unsigned C = vec.getCapacity(); C < sz) {
      T *p = alloc.allocate<T>(sz + sz);
      for (unsigned i = 0; i < vec.size(); ++i) p[i] = std::move(vec[i]);
      return Vec<T>(p, sz, sz + sz);
    }
    vec.resizeForOverwrite(sz);
    return vec;
  }

  struct InstructionBlock {
    [[no_unique_address]] Vec<Address *> memAccesses{};
    [[nodiscard]] constexpr auto size() const -> unsigned {
      return memAccesses.size();
    }
    [[nodiscard]] constexpr auto getCapacity() const -> unsigned {
      return memAccesses.getCapacity();
    }
    [[nodiscard]] constexpr auto operator[](unsigned i) const -> Address * {
      return memAccesses[i];
    }
    constexpr auto reserveExtra(BumpAlloc<> &alloc, unsigned i)
      -> MutPtrVector<Address *> {
      memAccesses = grow(alloc, memAccesses, size() + i);
      return memAccesses[_(end - i, end)];
    }
  };
  struct LoopAndExit {
    [[no_unique_address]] LoopTreeSchedule *subTree;
    [[no_unique_address]] InstructionBlock exit{};
    constexpr LoopAndExit(LoopTreeSchedule *subTree) : subTree(subTree) {}
    static constexpr auto construct(BumpAlloc<> &alloc, uint8_t depth) {
      return LoopAndExit(alloc.create<LoopTreeSchedule>(depth));
    }
  };
  /// Header of the loop.
  [[no_unique_address]] InstructionBlock header{};
  /// Variable number of sub loops and their associated exits.
  /// For the inner most loop, `subTrees.empty()`.
  [[no_unique_address]] Vec<LoopAndExit> subTrees{};
  [[no_unique_address]] uint8_t depth;
  [[no_unique_address]] uint8_t vectorizationFactor{1};
  [[no_unique_address]] uint8_t unrollFactor{1};
  [[no_unique_address]] uint8_t unrollPredcedence{1};
  [[nodiscard]] constexpr auto getNumSubTrees() const -> size_t {
    return subTrees.size();
  }
  constexpr auto getLoopAndExit(BumpAlloc<> &alloc, size_t i, size_t d)
    -> LoopAndExit & {
    if (i >= subTrees.size()) {
      subTrees = grow(alloc, subTrees, i + 1);
      for (size_t j = subTrees.size(); j <= i; ++j)
        subTrees[j] = LoopAndExit::construct(alloc, d);
    }
    return subTrees[i];
  }
  auto getLoopTripple(size_t i)
    -> std::tuple<InstructionBlock &, LoopTreeSchedule *, InstructionBlock &> {
    auto loopAndExit = subTrees[i];
    if (i) return {subTrees[i - 1].exit, loopAndExit.subTree, loopAndExit.exit};
    return {header, loopAndExit.subTree, loopAndExit.exit};
  }
  auto getLoop(BumpAlloc<> &alloc, size_t i, uint8_t d) -> LoopTreeSchedule * {
    return getLoopAndExit(alloc, i, d).subTree;
  }
  [[nodiscard]] auto getDepth() const -> size_t { return depth; }
  /// Adds the schedule corresponding for the innermost loop.

  // this method descends
  // NOLINTNEXTLINE(misc-no-recursion)
  void addMemory(BumpAlloc<> &alloc, LinearProgramLoopBlock &LB,
                 ScheduledNode &node, AffineSchedule sch) {
    auto fO = sch.getFusionOmega();
    unsigned numLoops = sch.getNumLoops();
    invariant(fO.size() - 1, numLoops);
    LoopTreeSchedule *L = this;
    for (size_t i = 0; i < numLoops; ++i) L = L->getLoop(alloc, fO[i], i + 1);
    node.insertMemAccesses(alloc, LB.getMemoryAccesses(),
                           L->header.reserveExtra(alloc, node.getNumMem()));
  }
  void topologicalSortCore() { // NOLINT(misc-no-recursion)
    for (size_t i = 0; i < getNumSubTrees(); ++i) {
      auto [H, L, E] = getLoopTripple(i);
      L->topologicalSort(H, E);
    }
    // TODO: sort the memory accesses in the header and place in correct block.
    // On entry, all InstructionBlock in the `AndExit`s will be empty
    // as we only insert into  the header on construction.
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void topologicalSort(InstructionBlock &H, InstructionBlock &E) {
    topologicalSortCore();
    // TODO: hoist loads and stores, probably respectively into `H` and `E`
    // (seems unlikely that a store doesn't depend on anything in the loop,
    // or that a legal-to-hoist load does not)
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
      addMemory(alloc, LB, node, sch);
      // addSchedule(alloc, cache, tAlloc, loopForest, LB, node, TTI,
      // vectorBits, sch, 0);
    }
    topologicalSortCore();
    // buidInstructionGraph(alloc, cache);
    mergeInstructions(alloc, cache, loopForest, TTI, tAlloc, vectorBits);
  }

public:
  constexpr LoopTreeSchedule(uint8_t depth) : depth(depth) {}
};

// class LoopForestSchedule : LoopTreeSchedule {
//   [[no_unique_address]] BumpAlloc<> &allocator;
// };
} // namespace CostModeling
