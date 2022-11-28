#pragma once

#include "./ArrayReference.hpp"
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
    static inline bool hasAVX512(llvm::LLVMContext &C,
                                 llvm::TargetTransformInfo &TTI) {
        return TTI.isLegalMaskedExpandLoad(
            llvm::FixedVectorType::get(llvm::Type::getDoubleTy(C), 8));
    }

    static uint8_t
    estimateNumPredicateRegisters(llvm::LLVMContext &C,
                                  llvm::TargetTransformInfo &TTI) {
        if (TTI.supportsScalableVectors())
            return 8;
        // hacky check for AVX512
        if (hasAVX512(C, TTI))
            return 7; // 7, because k0 is reserved for unmasked
        return 0;
    }
    static uint8_t estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                              llvm::TargetTransformInfo &TTI) {
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

struct PredicatedInstruction {
    [[no_unique_address]] Predicates predicates;
    [[no_unique_address]] llvm::Instruction *instruction;
    [[no_unique_address]] ArrayReference *ref;

    [[no_unique_address]] union {
        llvm::Instruction *instruction;
        MemoryAccess *memoryAccess;
    } instructionOrMemoryAccessPtr;
    [[no_unique_address]] llvm::SmallVector<PredicatedInstruction *> args;
    [[no_unique_address]] llvm::SmallVector<PredicatedInstruction *> uses;
    [[no_unique_address]] InstructionBlock *instrBlock{nullptr};
    [[no_unique_address]] bool isMemAccess;
    llvm::Instruction *getInstruction() {
        if (isMemoryAccess())
            return instructionOrMemoryAccessPtr.memoryAccess->getInstruction();
        return instructionOrMemoryAccessPtr.instruction;
    }
    MemoryAccess *getMemoryAccess() {
        if (isMemoryAccess())
            return instructionOrMemoryAccessPtr.memoryAccess;
        return nullptr;
    }
    bool isMemoryAccess() const { return isMemAccess; }
    bool isInstruction() const { return !isMemAccess; }
};

struct InstructionBlock {
    [[no_unique_address]] llvm::SmallVector<PredicatedInstruction *>
        instructions;
    [[no_unique_address]] LoopTreeSchedule *loopTree;

    void pushBlock(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
                   llvm::SmallPtrSet<llvm::BasicBlock *, 32> &chainBBs,
                   Predicates &pred, llvm::BasicBlock *BB) {
        assert(chainBBs.contains(block));
        chainBBs.erase(BB);
        // we only want to extract relevant instructions, i.e. parents of stores
        for (llvm::Instruction &instr : *BB) {
            if (trackInstr.contains(&instr))
                instructions.emplace_back(pred, instr);
        }
        llvm::Instruction *term = BB->getTerminator();
        if (!term)
            return;
        switch (term->getNumSuccessors()) {
        case 0:
            return;
        case 1:
            BB = term->getSuccessor(0);
            if (chainBBs.contains(BB))
                pushBlock(trackInstr, chainBBs, pred, BB);
            return;
        case 2:
            break;
        default:
            assert(false);
        }
        auto succ0 = term->getSuccessor(0);
        auto succ1 = term->getSuccessor(1);
        if (chainBBs.contains(succ0) && chainBBs.contains(succ1)) {
            // we need to fuse these blocks.
            auto newPred = pred;
            newPred.add(term);
            pushBlock(trackInstr, chainBBs, newPred, succ0);
            pushBlock(trackInstr, chainBBs, pred, succ1);
        } else if (chainBBs.contains(succ0)) {
            pushBlock(trackInstr, chainBBs, pred, succ0);
        } else if (chainBBs.contains(succ1)) {
            pushBlock(trackInstr, chainBBs, pred, succ1);
        }
    }
    /// Construct a single instruction block from a chain of
    /// predicated basic blocks. This applies the predicates to
    /// individual instructions, and attempts to fuse.
    /// We use bipartite matching to merge divergences.
    InstructionBlock(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
                     PredicatedChain &chain, LoopTreeSchedule *loopTree)
        : loopTree(loopTree) {
        llvm::SmallPtrSet<llvm::BasicBlock *, 32> chainBBs;
        for (auto &block : chain)
            chainBBs.insert(block.basicBlock);
    }
    static std::pair<InstructionBlock, size_t>
    instrBlockCost(InstructionBlock &instrBlock,
                   llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
                   PtrVector<PredicatedBasicBlock> chain) {
        auto PBB = chain.front();
        auto tail = chain(_(1, end));
        auto pred = PBB.predicates;
        if (pred.empty()) {
            for (auto &instr : PBB)
                instrBlock.instructions.emplace_back(pred, instr);
        } else {
            for (auto &instr : PBB) {
            }
        }
    }
    static std::pair<InstructionBlock, size_t>
    instrBlockCost(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
                   PredicatedChain &chain, LoopTreeSchedule *loopTree) {
        // inserts from the predicated chain linearly

        llvm::SmallPtrSet<llvm::BasicBlock *, 32> chainBBs;
        for (auto &block : chain)
            chainBBs.insert(block.basicBlock);
    }
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
    size_t getNumSubTrees() const { return subTrees.size(); }
    size_t getDepth() const { return depth; }
};

struct LoopForestSchedule {
    [[no_unique_address]] InstructionBlock enteringBlock;
    [[no_unique_address]] llvm::SmallVector<LoopAndExit> loopNests;
    [[no_unique_address]] llvm::SmallVector<
        llvm::SmallVector<LoopTreeSchedule *>>
        depthTrees;
    [[no_unique_address]] llvm::BumpPtrAllocator &allocator;
};
