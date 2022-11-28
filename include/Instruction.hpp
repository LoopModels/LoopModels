#pragma once

#include "ArrayReference.hpp"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/InstructionCost.h>

struct RecipThroughputLatency {
    llvm::InstructionCost recipThroughput;
    llvm::InstructionCost latency;
    bool isValid() const {
        return recipThroughput.isValid() && latency.isValid();
    }
};

struct Instruction {
    /// Instruction ID
    llvm::Intrinsic::ID id; // getOpcode()
    /// Data we may need
    union {
        ArrayReference *ref;  // load or store
        llvm::Function *func; // call
        llvm::Value *val;     // other
    } ptr{nullptr};
    llvm::Type *type;
    llvm::SmallVector<Instruction *> operands;
    llvm::SmallVector<Instruction *> users;
    /// costs[i] == cost for vector-width 2^i
    llvm::SmallVector<RecipThroughputLatency> costs;
    llvm::TargetTransformInfo &TTI;
    bool isCall() const {
        assert((id != llvm::Instruction::Call) || (ptr.func != nullptr));
        return id == llvm::Instruction::Call;
    }
    bool isLoad() const {
        assert((id != llvm::Instruction::Load) || (ptr.ref != nullptr));
        return id == llvm::Instruction::Load;
    }
    bool isStore() const {
        assert((id != llvm::Instruction::Load) || (ptr.ref != nullptr));
        return id == llvm::Instruction::Store;
    }
    /// fall back in case we need value operand
    bool isValue() const {
        unsigned int x = llvm::Instruction::OtherOpsEnd + 1;
        assert((id != x) || (ptr.val != nullptr));
        return id == x;
    }
    RecipThroughputLatency getCost(unsigned int vectorWidth) const {
        assert(vectorWidth > 0);
        unsigned int i = 0;
        while (vectorWidth >>= 1) {
            ++i;
        }
        assert(i < costs.size());
        return costs[i];
    }

    RecipThroughputLatency
    getCostLog2VectorWidth(unsigned int log2VectorWidth) const {
        return costs[log2VectorWidth];
    }
#if LLVM_VERSION_MAJOR >= 16
    llvm::TargetTransformInfo::OperandValueInfo
    getOperandInfo(unsigned int i) const {
        Instruction *opi = operands[i];
        if (opi->isValue())
            return TTI.getOperandInfo(opi->ptr.val);
        return TTI::OK_AnyValue;
    }
    RecipThroughputLatency unnaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
                TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)
        }
    }
    RecipThroughputLatency binaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info,
                op1info),
                TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_Latency, op0info,
                    op1info)
        }
    }
#else
    std::pair<llvm::TargetTransformInfo::OperandValueKind,
              llvm::TargetTransformInfo::OperandValueProperties>
    getOperandInfo(unsigned int i) const {
        Instruction *opi = operands[i];
        if (opi->isValue()) {
            if (auto c = llvm::dyn_cast<llvm::ConstantInt>(opi->ptr.val)) {
                llvm::APInt v = c->getValue();
                if (v.isPowerOf2())
                    return std::make_pair(
                        llvm::TargetTransformInfo::OK_UniformConstantValue,
                        llvm::TargetTransformInfo::OP_PowerOf2);
                return std::make_pair(

                    llvm::TargetTransformInfo::OK_UniformConstantValue,
                    llvm::TargetTransformInfo::OP_None);
                // if (v.isNegative()){
                //     v.negate();
                //     if (v.isPowerOf2())
                // 	return llvm::TargetTransformInfo::OP_NegatedPowerOf@;
                // }
            }
        }
        return std::make_pair(llvm::TargetTransformInfo::OK_AnyValue,
                              llvm::TargetTransformInfo::OP_None);
    }
    RecipThroughputLatency unaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second),
                TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_Latency,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second)};
    }
    RecipThroughputLatency binaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info.first, op1info.first, op0info.second, op1info.second),
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
                op1info.first, op0info.second, op1info.second)};
    }
#endif
    RecipThroughputLatency calculateCost(unsigned int vectorWidth) {
        switch (id) {
        case llvm::Instruction::Add:
        case llvm::Instruction::Sub:
        case llvm::Instruction::Mul:
        case llvm::Instruction::FAdd:
        case llvm::Instruction::FSub:
        case llvm::Instruction::FMul:
        case llvm::Instruction::FDiv:
        case llvm::Instruction::Shl:
        case llvm::Instruction::LShr:
        case llvm::Instruction::AShr:
        case llvm::Instruction::And:
        case llvm::Instruction::Or:
        case llvm::Instruction::Xor:
        case llvm::Instruction::SDiv:
        case llvm::Instruction::SRem:
        case llvm::Instruction::UDiv:
        case llvm::Instruction::URem:
        case llvm::Instruction::FRem: // TODO: check if frem is supported?
                                      // two arg arithmetic cost
                                      // TTI.getOperandInfo() to get op info;
            return binaryArithmeticCost(vectorWidth);
        case llvm::Instruction::FNeg:
            // one arg arithmetic cost
            return unaryArithmeticCost(vectorWidth);
        case llvm::Instruction::ICmp:
        case llvm::Instruction::FCmp:

        case llvm::Instruction::Load:
        case llvm::Instruction::Store:
            return calculateCostLoadStore(vectorWidth);
        }
    }
};

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
