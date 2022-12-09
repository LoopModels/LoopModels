#pragma once

#include "./ArrayReference.hpp"
#include "./Instruction.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
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

struct InstructionBlock;
struct LoopTreeSchedule;

// Plan for cost modeling:
// 1. Build Instruction graph
// 2. Iterate over all PredicatedChains, merging instructions across branches
// where possible
// 3. Create a loop tree structure for optimization
// 4. Create InstructionBlocks at each level.

struct PredicatedInstruction {
    [[no_unique_address]] Predicates predicates;
    [[no_unique_address]] Instruction *instruction;
    // [[no_unique_address]] llvm::SmallVector<PredicatedInstruction *> args;
    // [[no_unique_address]] llvm::SmallVector<PredicatedInstruction *> uses;
    [[no_unique_address]] InstructionBlock *instrBlock{nullptr};
    PredicatedInstruction(Instruction *instr) : instruction(instr) {}
    // PredicatedInstruction(llvm::Instruction *instr) :
    // instruction(Instruction(instr)) {}
    [[nodiscard]] auto isMemoryAccess() const -> bool {
        return instruction->isLoad() || instruction->isStore();
    }
    [[nodiscard]] auto isInstruction() const -> bool {
        return !isMemoryAccess();
    }
};

struct InstructionBlock {
    // we tend to heap allocate InstructionBlocks with a bump allocator,
    // so using 128 bytes seems reasonable.
    [[no_unique_address]] llvm::SmallVector<Instruction *, 14> instructions;
    // [[no_unique_address]] LoopTreeSchedule *loopTree{nullptr};

    InstructionBlock(llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
                     llvm::BasicBlock *BB) {
        for (auto &I : *BB) {
            instructions.push_back(cache.get(alloc, &I));
        }
    }
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
};

/// Given: llvm::SmallVector<LoopAndExit> subTrees;
/// subTrees[i].second is the preheader for
/// subTrees[i+1].first, which has exit block
/// subTrees[i+1].second
using LoopAndExit = std::pair<LoopTreeSchedule *, InstructionBlock>;

struct LoopTreeSchedule {
    /// First block of the loop body. Also, the preheader of
    /// subTrees.front().first;
    [[no_unique_address]] AffineLoopNest<false> *loopNest;
    // schedule commented out, as we're assuming we applied the rotation
    // to the array reference.
    // [[no_unique_address]] Schedule *schedule;
    [[no_unique_address]] InstructionBlock enteringBlock;
    [[no_unique_address]] llvm::SmallVector<LoopAndExit> subTrees;
    /// Loop pre-header block for this loop tree.
    [[no_unique_address]] InstructionBlock *preHeader;
    /// Loop exit block for this loop tree.
    [[no_unique_address]] InstructionBlock *exitBlock;
    [[no_unique_address]] LoopTreeSchedule *parentLoop{nullptr};
    [[no_unique_address]] uint8_t depth;
    [[no_unique_address]] uint8_t vectorizationFactor{1};
    [[no_unique_address]] uint8_t unrollFactor{1};
    [[no_unique_address]] uint8_t unrollPredcedence{1};
    [[nodiscard]] auto getNumSubTrees() const -> size_t {
        return subTrees.size();
    }
    [[nodiscard]] auto getDepth() const -> size_t { return depth; }
};

struct LoopForestSchedule {
    [[no_unique_address]] InstructionBlock enteringBlock;
    [[no_unique_address]] llvm::SmallVector<LoopAndExit> loopNests;
    [[no_unique_address]] llvm::SmallVector<
        llvm::SmallVector<LoopTreeSchedule *>>
        depthTrees;
    [[no_unique_address]] llvm::BumpPtrAllocator &allocator;
};
