#pragma once

#include "ArrayReference.hpp"
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>

struct RecipThroughputLatency {
    llvm::InstructionCost recipThroughput;
    llvm::InstructionCost latency;
    bool isValid() const {
        return recipThroughput.isValid() && latency.isValid();
    }
    static RecipThroughputLatency getInvalid() {
        return {llvm::InstructionCost::getInvalid(),
                llvm::InstructionCost::getInvalid()};
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
    bool isInstruction(unsigned opCode) const { return id == opCode; }
    bool isShuffle() const {
        return isInstruction(llvm::Instruction::ShuffleVector);
    }
    bool isFcmp() const { return isInstruction(llvm::Instruction::FCmp); }
    bool isIcmp() const { return isInstruction(llvm::Instruction::ICmp); }
    bool isCmp() const { return isFcmp() || isIcmp(); }
    bool isSelect() const { return isInstruction(llvm::Instruction::Select); }
    bool isExtract() const {
        return isInstruction(llvm::Instruction::ExtractElement);
    }
    bool isInsert() const {
        return isInstruction(llvm::Instruction::InsertElement);
    }
    bool isExtractValue() const {
        return isInstruction(llvm::Instruction::ExtractValue);
    }
    bool isInsertValue() const {
        return isInstruction(llvm::Instruction::InsertValue);
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
    static llvm::Type *getType(llvm::Type *T, unsigned int vectorWidth) {
        if (vectorWidth == 1)
            return T;
        return llvm::FixedVectorType::get(T, vectorWidth);
    }
    llvm::Type *getType(unsigned int vectorWidth) const {
        return getType(type, vectorWidth);
    }
#if LLVM_VERSION_MAJOR >= 16
    llvm::TargetTransformInfo::OperandValueInfo
    getOperandInfo(unsigned int i) const {
        Instruction *opi = operands[i];
        if (opi->isValue())
            return TTI.getOperandInfo(opi->ptr.val);
        return TTI::OK_AnyValue;
    }
    RecipThroughputLatency calcUnaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
                TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)
        }
    }
    RecipThroughputLatency calcBinaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = getType(vectorWidth);
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
    RecipThroughputLatency calcUnaryArithmeticCost(unsigned int vectorWidth) {
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
    RecipThroughputLatency calcBinaryArithmeticCost(unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info.first, op1info.first, op0info.second, op1info.second),
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
                op1info.first, op0info.second, op1info.second)};
    }
#endif
    bool operandIsLoad(unsigned int i = 0) const {
        return operands[i]->isLoad();
    }
    bool userIsStore(unsigned int i) const { return users[i]->isLoad(); }
    bool userIsStore() const {
        for (auto u : users)
            if (u->isStore())
                return true;
        return false;
    }
    llvm::TargetTransformInfo::CastContextHint getCastContext() const {
        if (operandIsLoad() || userIsStore())
            return llvm::TargetTransformInfo::CastContextHint::Normal;
        // TODO: check for whether mask, interleave, or reversed is likely.
        return llvm::TargetTransformInfo::CastContextHint::None;
    }
    RecipThroughputLatency calcCastCost(unsigned int vectorWidth) {
        llvm::Type *srcT = getType(operands.front()->type, vectorWidth);
        llvm::Type *dstT = getType(vectorWidth);
        llvm::TargetTransformInfo::CastContextHint ctx = getCastContext();
        return {TTI.getCastInstrCost(
                    id, dstT, srcT, ctx,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCastInstrCost(id, dstT, srcT, ctx,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    llvm::CmpInst::Predicate getPredicate() const {
        if (isSelect())
            return operands.front()->getPredicate();
        assert(isCmp());
        if (auto cmp = llvm::dyn_cast<llvm::CmpInst>(ptr.val))
            return cmp->getPredicate();
        return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                        : llvm::CmpInst::BAD_ICMP_PREDICATE;
    }
    RecipThroughputLatency calcCmpSelectCost(unsigned int vectorWidth) {
        llvm::Type *T = getType(vectorWidth);
        llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
        llvm::CmpInst::Predicate pred = getPredicate();
        return {TTI.getCmpSelInstrCost(
                    id, T, cmpT, pred,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCmpSelInstrCost(id, T, cmpT, pred,
                                       llvm::TargetTransformInfo::TCK_Latency)};
    }
    RecipThroughputLatency calcCallCost(unsigned int vectorWidth) {
        llvm::Type *T = getType(vectorWidth);
        llvm::SmallVector<llvm::Type *, 4> argTypes;
        for (auto op : operands)
            argTypes.push_back(op->getType(vectorWidth));
        return {TTI.getCallInstrCost(
                    ptr.func, T, argTypes,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCallInstrCost(ptr.func, T, argTypes,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    RecipThroughputLatency
    calculateCostContiguousLoadStore(unsigned int vectorWidth) {
        constexpr unsigned int AddressSpace = 0;
        llvm::Type *T = getType(vectorWidth);
        llvm::Align alignment = ptr.ref->getAlignment();
        return {
            TTI.getMemoryOpCost(id, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getMemoryOpCost(id, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_Latency)};
    }
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
        case llvm::Instruction::FRem: // TODO: check if frem is supported?
        case llvm::Instruction::URem:
            // two arg arithmetic cost
            return calcBinaryArithmeticCost(vectorWidth);
        case llvm::Instruction::FNeg:
            // one arg arithmetic cost
            return calcUnaryArithmeticCost(vectorWidth);
        case llvm::Instruction::Trunc:
        case llvm::Instruction::ZExt:
        case llvm::Instruction::SExt:
        case llvm::Instruction::FPTrunc:
        case llvm::Instruction::FPExt:
        case llvm::Instruction::FPToUI:
        case llvm::Instruction::FPToSI:
        case llvm::Instruction::UIToFP:
        case llvm::Instruction::SIToFP:
        case llvm::Instruction::IntToPtr:
        case llvm::Instruction::PtrToInt:
        case llvm::Instruction::BitCast:
        case llvm::Instruction::AddrSpaceCast:
            // one arg cast cost
            return calcCastCost(vectorWidth);
        case llvm::Instruction::ICmp:
        case llvm::Instruction::FCmp:
        case llvm::Instruction::Select:
            return calcCmpSelectCost(vectorWidth);
        case llvm::Instruction::Call:
            return calcCallCost(vectorWidth);
        case llvm::Instruction::Load:
        case llvm::Instruction::Store:
            return calculateCostContiguousLoadStore(vectorWidth);
        default:
            return RecipThroughputLatency::getInvalid();
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
