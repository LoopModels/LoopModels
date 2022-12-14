#pragma once

#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./Predicate.hpp"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/InstructionCost.h>

void buildInstructionGraph(llvm::BumpPtrAllocator &alloc,
                           Instruction::Cache &cache,
                           LinearProgramLoopBlock &LB) {
    for (auto mem : LB.getMemoryAccesses()) {
        if (mem->nodeIndex.isEmpty())
            continue;
        // TODO: add a means for cache.get to stop adding operands
        // that are outside of LB, as we don't care about that part of the
        // graph.
        auto inst = cache.get(alloc, mem->getInstruction());
        inst->id.ptr.ref = &mem->ref;
    }
}

// merge all instructions from toMerge into merged
inline void merge(llvm::SmallPtrSetImpl<Instruction *> &merged,
                  llvm::SmallPtrSetImpl<Instruction *> &toMerge) {
    merged.insert(toMerge.begin(), toMerge.end());
}

// represents the cost of merging key=>values; cost is hopefully negative.
// cost is measured in reciprocal throughput
struct MergingCost {
    llvm::DenseMap<Instruction *, Instruction *> merge;
    llvm::DenseMap<Instruction *, llvm::SmallPtrSet<Instruction *, 16> *>
        ancesors;
    llvm::InstructionCost cost;
};

/// mergeInstructions(
///    llvm::BumpPtrAllocator &alloc,
///    llvm::BumpPtrAllocator &tmpAlloc,
///    Instruction::Cache &cache,
///    Predicate::Map &predMap
/// )
/// merges instructions from predMap what have disparate control flow.
/// NOTE: calls tmpAlloc.Reset(); this should be an allocator specific for
/// merging as it allocates a lot of memory that it can free when it is done.
void mergeInstructions(llvm::BumpPtrAllocator &alloc,
                       llvm::BumpPtrAllocator &tmpAlloc,
                       Instruction::Cache &cache, Predicate::Map &predMap) {
    if (predMap.size() < 2)
        return;

    tmpAlloc.Reset();
}
