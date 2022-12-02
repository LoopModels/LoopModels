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
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <llvm/Support/MathExtras.h>

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
    struct Identifier {
        /// Instruction ID
        llvm::Intrinsic::ID id; // getOpcode()
        /// Data we may need
        union {
            ArrayReference *ref;  // load or store
            llvm::Function *func; // call
            llvm::Value *val;     // other
        } ptr{nullptr};
    };
    Identifier id;
    llvm::Type *type;
    llvm::SmallVector<Instruction *> operands;
    llvm::SmallVector<Instruction *> users;
    /// costs[i] == cost for vector-width 2^i
    llvm::SmallVector<RecipThroughputLatency> costs;
    // llvm::TargetTransformInfo &TTI;

    // Instruction(llvm::Intrinsic::ID id, llvm::Type *type) : id(id),
    // type(type) {
    //     // this->TTI = TTI;
    // }
    struct InstructionCache {
        llvm::DenseMap<llvm::Instruction *, Instruction *> llvmToInternalMap;
        llvm::DenseMap<std::pair<Identifier, llvm::SmallVector<Instruction *>>,
                       Instruction *>
            argMap;
    };
    // static Instruction *create(llvm::BumpPtrAllocator &alloc,
    //                            llvm::Instruction *instr) {
    //     llvm::Intrinsic::ID id = instr->getOpcode();
    //     Instruction *i = new (alloc) Instruction(id, instr->getType(), TTI);
    //     return new Instruction(id, type, TTI);
    // }
    bool isCall() const {
        assert((id.id != llvm::Instruction::Call) || (id.ptr.func != nullptr));
        return id.id == llvm::Instruction::Call;
    }
    bool isLoad() const {
        assert((id.id != llvm::Instruction::Load) || (id.ptr.ref != nullptr));
        return id.id == llvm::Instruction::Load;
    }
    bool isStore() const {
        assert((id.id != llvm::Instruction::Load) || (id.ptr.ref != nullptr));
        return id.id == llvm::Instruction::Store;
    }
    /// fall back in case we need value operand
    bool isValue() const {
        unsigned int x = llvm::Instruction::OtherOpsEnd + 1;
        assert((id.id != x) || (id.ptr.val != nullptr));
        return id.id == x;
    }
    bool isInstruction(unsigned opCode) const { return id.id == opCode; }
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
    bool isFMul() const { return isInstruction(llvm::Instruction::FMul); }
    bool isFNeg() const { return isInstruction(llvm::Instruction::FNeg); }
    bool isFMulOrFNegOfFMul() const {
        return isFMul() || (isFNeg() && operands[0]->isFMul());
    }
    bool isFAdd() const { return isInstruction(llvm::Instruction::FAdd); }
    bool isFSub() const { return isInstruction(llvm::Instruction::FSub); }
    bool allowsContract() const {
        if (auto m = llvm::dyn_cast<llvm::Instruction>(id.ptr.val))
            return m->getFastMathFlags().allowContract();
        return false;
    }
    bool isMulAdd() const {
        if (!isCall())
            return false;
        llvm::Intrinsic::ID intrinID = id.ptr.func->getIntrinsicID();
        return intrinID == llvm::Intrinsic::fma ||
               intrinID == llvm::Intrinsic::fmuladd;
    }
    RecipThroughputLatency getCost(llvm::TargetTransformInfo &TTI,
                                   unsigned int vectorWidth,
                                   unsigned int log2VectorWidth) {
        RecipThroughputLatency c;
        if (log2VectorWidth >= costs.size()) {
            costs.resize(log2VectorWidth + 1,
                         RecipThroughputLatency::getInvalid());
            costs[log2VectorWidth] = c = calculateCost(TTI, vectorWidth);
        } else {
            c = costs[log2VectorWidth];
            // TODO: differentiate between uninitialized and invalid
            if (!c.isValid())
                costs[log2VectorWidth] = c = calculateCost(TTI, vectorWidth);
        }
        return c;
    }
    RecipThroughputLatency getCost(llvm::TargetTransformInfo &TTI,
                                   uint32_t vectorWidth) {
        return getCost(TTI, vectorWidth, llvm::Log2_32(vectorWidth));
    }
    RecipThroughputLatency getCost(llvm::TargetTransformInfo &TTI,
                                   uint64_t vectorWidth) {
        return getCost(TTI, vectorWidth, llvm::Log2_64(vectorWidth));
    }
    RecipThroughputLatency
    getCostLog2VectorWidth(llvm::TargetTransformInfo &TTI,
                           unsigned int log2VectorWidth) {
        return getCost(TTI, 1 << log2VectorWidth, log2VectorWidth);
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
    getOperandInfo(llvm::TargetTransformInfo &TTI, unsigned int i) const {
        Instruction *opi = operands[i];
        if (opi->isValue())
            return TTI.getOperandInfo(opi->ptr.val);
        return TTI::OK_AnyValue;
    }
    RecipThroughputLatency
    calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                            unsigned int vectorWidth) {
        auto op0info = getOperandInfo(TTI, 0);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
                TTI.getArithmeticInstrCost(
                    id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)
        }
    }
    RecipThroughputLatency
    calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                             unsigned int vectorWidth) {
        auto op0info = getOperandInfo(TTI, 0);
        auto op1info = getOperandInfo(TTI, 1);
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
            if (auto c = llvm::dyn_cast<llvm::ConstantInt>(opi->id.ptr.val)) {
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
    RecipThroughputLatency
    calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                            unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {TTI.getArithmeticInstrCost(
                    id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second),
                TTI.getArithmeticInstrCost(
                    id.id, T, llvm::TargetTransformInfo::TCK_Latency,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second)};
    }
    RecipThroughputLatency
    calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                             unsigned int vectorWidth) {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info.first, op1info.first, op0info.second, op1info.second),
            TTI.getArithmeticInstrCost(
                id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
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
    llvm::TargetTransformInfo::CastContextHint
    getCastContext(llvm::TargetTransformInfo &TTI) const {
        if (auto cast = llvm::dyn_cast<llvm::CastInst>(id.ptr.val))
            return TTI.getCastContextHint(cast);
        if (operandIsLoad() || userIsStore())
            return llvm::TargetTransformInfo::CastContextHint::Normal;
        // TODO: check for whether mask, interleave, or reversed is likely.
        return llvm::TargetTransformInfo::CastContextHint::None;
    }
    RecipThroughputLatency calcCastCost(llvm::TargetTransformInfo &TTI,
                                        unsigned int vectorWidth) {
        llvm::Type *srcT = getType(operands.front()->type, vectorWidth);
        llvm::Type *dstT = getType(vectorWidth);
        llvm::TargetTransformInfo::CastContextHint ctx = getCastContext(TTI);
        return {TTI.getCastInstrCost(
                    id.id, dstT, srcT, ctx,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCastInstrCost(id.id, dstT, srcT, ctx,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    llvm::CmpInst::Predicate getPredicate() const {
        if (isSelect())
            return operands.front()->getPredicate();
        assert(isCmp());
        if (auto cmp = llvm::dyn_cast<llvm::CmpInst>(id.ptr.val))
            return cmp->getPredicate();
        return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                        : llvm::CmpInst::BAD_ICMP_PREDICATE;
    }
    RecipThroughputLatency calcCmpSelectCost(llvm::TargetTransformInfo &TTI,
                                             unsigned int vectorWidth) {
        llvm::Type *T = getType(vectorWidth);
        llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
        llvm::CmpInst::Predicate pred = getPredicate();
        return {TTI.getCmpSelInstrCost(
                    id.id, T, cmpT, pred,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCmpSelInstrCost(id.id, T, cmpT, pred,
                                       llvm::TargetTransformInfo::TCK_Latency)};
    }
    RecipThroughputLatency calcCallCost(llvm::TargetTransformInfo &TTI,
                                        unsigned int vectorWidth) {
        llvm::Type *T = getType(vectorWidth);
        llvm::SmallVector<llvm::Type *, 4> argTypes;
        for (auto op : operands)
            argTypes.push_back(op->getType(vectorWidth));
        return {TTI.getCallInstrCost(
                    id.ptr.func, T, argTypes,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCallInstrCost(id.ptr.func, T, argTypes,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    RecipThroughputLatency
    calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                     unsigned int vectorWidth) {
        constexpr unsigned int AddressSpace = 0;
        llvm::Type *T = getType(vectorWidth);
        llvm::Align alignment = id.ptr.ref->getAlignment();
        return {
            TTI.getMemoryOpCost(id.id, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getMemoryOpCost(id.id, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_Latency)};
    }
    RecipThroughputLatency calculateCostFAddFSub(llvm::TargetTransformInfo &TTI,
                                                 unsigned int vectorWidth) {
        // TODO: allow not assuming hardware FMA support
        if ((operands[0]->isFMulOrFNegOfFMul() ||
             operands[1]->isFMulOrFNegOfFMul()) &&
            allowsContract())
            return {};
        return calcBinaryArithmeticCost(TTI, vectorWidth);
    }
    bool allUsersAdditiveContract() {
        for (auto u : users)
            if (!(((u->isFAdd()) || (u->isFSub())) && (u->allowsContract())))
                return false;
        return true;
    }
    RecipThroughputLatency calculateFNegCost(llvm::TargetTransformInfo &TTI,
                                             unsigned int vectorWidth) {

        if (operands[0]->isFMul() && allUsersAdditiveContract())
            return {};
        return calcUnaryArithmeticCost(TTI, vectorWidth);
    }
    RecipThroughputLatency calculateCost(llvm::TargetTransformInfo &TTI,
                                         unsigned int vectorWidth) {
        switch (id.id) {
        case llvm::Instruction::FAdd:
        case llvm::Instruction::FSub:
            return calculateCostFAddFSub(TTI, vectorWidth);
        case llvm::Instruction::Add:
        case llvm::Instruction::Sub:
        case llvm::Instruction::FMul:
        case llvm::Instruction::Mul:
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
            return calcBinaryArithmeticCost(TTI, vectorWidth);
        case llvm::Instruction::FNeg:
            // one arg arithmetic cost
            return calculateFNegCost(TTI, vectorWidth);
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
            return calcCastCost(TTI, vectorWidth);
        case llvm::Instruction::ICmp:
        case llvm::Instruction::FCmp:
        case llvm::Instruction::Select:
            return calcCmpSelectCost(TTI, vectorWidth);
        case llvm::Instruction::Call:
            return calcCallCost(TTI, vectorWidth);
        case llvm::Instruction::Load:
        case llvm::Instruction::Store:
            return calculateCostContiguousLoadStore(TTI, vectorWidth);
        default:
            return RecipThroughputLatency::getInvalid();
        }
    }
    uint8_t associativeOperandsFlag() const {
        switch (id.id) {
        case llvm::Instruction::Call:
            if (!isMulAdd())
                return 0;
            // fall through
        case llvm::Instruction::FAdd:
        case llvm::Instruction::Add:
        case llvm::Instruction::FMul:
        case llvm::Instruction::Mul:
        case llvm::Instruction::And:
        case llvm::Instruction::Or:
        case llvm::Instruction::Xor:
            return 0x3;
        default:
            return 0;
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
