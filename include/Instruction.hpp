#pragma once

#include "./ArrayReference.hpp"
#include "./LoopForest.hpp"
#include "./Predicate.hpp"
#include <cstdint>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
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
    [[nodiscard]] auto isValid() const -> bool {
        return recipThroughput.isValid() && latency.isValid();
    }
    static auto getInvalid() -> RecipThroughputLatency {
        return {llvm::InstructionCost::getInvalid(),
                llvm::InstructionCost::getInvalid()};
    }
};

// struct Instruction{
//     struct Identifer;
// };
// struct Instruction::Identifier;

struct Instruction {
    struct Identifier {
        /// Instruction ID
        /// if not Load or Store, then check val for whether it is a call
        /// and ID corresponds to the instruction or to the intrinsic call
        llvm::Intrinsic::ID op; // getOpCode()
        llvm::Intrinsic::ID intrin{llvm::Intrinsic::not_intrinsic};
        [[nodiscard]] auto getOpCode() const -> llvm::Intrinsic::ID {
            return op;
        }
        [[nodiscard]] auto getIntrinsicID() const -> llvm::Intrinsic::ID {
            return intrin;
        }
        static auto getOpCode(llvm::Value *v) -> llvm::Intrinsic::ID {
            if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
                return i->getOpcode();
            return llvm::Intrinsic::not_intrinsic;
        }
        static auto getIntrinsicID(llvm::Value *v) -> llvm::Intrinsic::ID {
            if (auto *i = llvm::dyn_cast<llvm::IntrinsicInst>(v))
                return i->getIntrinsicID();
            return llvm::Intrinsic::not_intrinsic;
        }

        /// Data we may need
        union {
            llvm::Value *val;    // other
            ArrayReference *ref; // load or store
        } ptr{nullptr};

        static auto isCall(llvm::Value *v) -> bool {
            return getOpCode(v) == llvm::Instruction::Call;
        }
        static auto isIntrinsicCall(llvm::Value *v) -> bool {
            return llvm::isa<llvm::IntrinsicInst>(v);
        }

        Identifier(llvm::Value *v)
            : op(getOpCode(v)), intrin(getIntrinsicID(v)), ptr{v} {}
        Identifier(llvm::Intrinsic::ID op, llvm::Intrinsic::ID intrin,
                   llvm::Value *v)
            : op(op), intrin(intrin), ptr{v} {}
        [[nodiscard]] constexpr auto isValue() const -> bool {
            return op == llvm::Intrinsic::not_intrinsic;
        }
        [[nodiscard]] constexpr auto isCall() const -> bool {
            return op == llvm::Instruction::Call;
        }
        [[nodiscard]] constexpr auto isIntrinsicCall() const -> bool {
            return intrin != llvm::Intrinsic::not_intrinsic;
        }
        [[nodiscard]] constexpr auto isInstruction(unsigned opCode) const
            -> bool {
            return op == opCode;
        }
        [[nodiscard]] constexpr auto
        isIntrinsicInstruction(unsigned opCode) const -> bool {
            return intrin == opCode;
        }
        [[nodiscard]] auto getFunction() const -> llvm::Function * {
            if (auto *i = llvm::dyn_cast<llvm::CallBase>(ptr.val))
                return i->getCalledFunction();
            return nullptr;
        }
        auto operator==(const Identifier &other) const -> bool {
            return op == other.op && intrin == other.intrin &&
                   ptr.val == other.ptr.val;
        }
    };

    Identifier id;
    llvm::Type *type;
    llvm::ArrayRef<Instruction *> predicates;
    llvm::ArrayRef<Instruction *> operands;
    llvm::SmallVector<Instruction *> users;
    /// costs[i] == cost for vector-width 2^i
    llvm::SmallVector<RecipThroughputLatency> costs;
    // llvm::TargetTransformInfo &TTI;

    // Instruction(llvm::Intrinsic::ID id, llvm::Type *type) : id(id),
    // type(type) {
    //     // this->TTI = TTI;
    // }
    Instruction(Identifier id, llvm::Type *type) : id(id), type(type) {}
    using UniqueIdentifier =
        std::pair<Identifier, llvm::ArrayRef<Instruction *>>;
    struct Cache {
        llvm::DenseMap<llvm::Value *, Instruction *> llvmToInternalMap;
        llvm::DenseMap<UniqueIdentifier, Instruction *> argMap;
        auto operator[](llvm::Value *v) -> Instruction * {
            auto f = llvmToInternalMap.find(v);
            if (f != llvmToInternalMap.end())
                return f->second;
            return nullptr;
        }
        /// This is the API for creating new instructions
        auto get(llvm::BumpPtrAllocator &alloc, llvm::Value *v)
            -> Instruction * {
            if (Instruction *i = (*this)[v])
                return i;
            auto id = Identifier(v);
            Instruction *i = nullptr;
            if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v)) {
                if (auto *load = llvm::dyn_cast<llvm::LoadInst>(instr)) {

                } else if (auto *store =
                               llvm::dyn_cast<llvm::StoreInst>(instr)) {
                }
                UniqueIdentifier uid{id, getOperands(alloc, *this, instr)};
                auto argMatch = argMap.find(uid);
                if (argMatch != argMap.end()) {
                    return argMatch->second;
                }
                i = new (alloc) Instruction(id, v->getType());
                auto insertIter = argMap.insert({uid, i});
                assert(insertIter.second);
                assert(insertIter.first->second == i);
                i->operands = insertIter.first->first.second;
                for (auto *op : i->operands) {
                    op->users.push_back(i);
                }
            } else {
                i = new (alloc) Instruction(id, v->getType());
            }
            llvmToInternalMap[v] = i;
            return i;
        }
    };
    static auto getOperands(llvm::BumpPtrAllocator &alloc, Cache &cache,
                            llvm::Instruction *instr)
        -> llvm::ArrayRef<Instruction *> {
        auto ops{instr->operands()};
        auto OI = ops.begin();
        auto OE = ops.end();
        size_t Nops = instr->getNumOperands();
        auto **operands = alloc.Allocate<Instruction *>(Nops);
        Instruction **p = operands;
        for (; OI != OE; ++OI, ++p) {
            *p = cache.get(alloc, *OI);
        }
        return {operands, Nops};
    }

    static auto createIsolated(llvm::BumpPtrAllocator &alloc,
                               llvm::Instruction *instr) -> Instruction * {
        Identifier id{instr};
        auto *i = new (alloc) Instruction(id, instr->getType());
        return i;
    }
    [[nodiscard]] auto isCall() const -> bool {
        assert(!id.isIntrinsicCall() || id.isCall());
        return id.isCall();
    }
    [[nodiscard]] auto isLoad() const -> bool {
        return id.isInstruction(llvm::Instruction::Load);
    }
    [[nodiscard]] auto isStore() const -> bool {
        return id.isInstruction(llvm::Instruction::Store);
    }
    /// fall back in case we need value operand
    [[nodiscard]] auto isValue() const -> bool { return id.isValue(); }
    [[nodiscard]] auto isShuffle() const -> bool {
        return id.isInstruction(llvm::Instruction::ShuffleVector);
    }
    [[nodiscard]] auto isFcmp() const -> bool {
        return id.isInstruction(llvm::Instruction::FCmp);
    }
    [[nodiscard]] auto isIcmp() const -> bool {
        return id.isInstruction(llvm::Instruction::ICmp);
    }
    [[nodiscard]] auto isCmp() const -> bool { return isFcmp() || isIcmp(); }
    [[nodiscard]] auto isSelect() const -> bool {
        return id.isInstruction(llvm::Instruction::Select);
    }
    [[nodiscard]] auto isExtract() const -> bool {
        return id.isInstruction(llvm::Instruction::ExtractElement);
    }
    [[nodiscard]] auto isInsert() const -> bool {
        return id.isInstruction(llvm::Instruction::InsertElement);
    }
    [[nodiscard]] auto isExtractValue() const -> bool {
        return id.isInstruction(llvm::Instruction::ExtractValue);
    }
    [[nodiscard]] auto isInsertValue() const -> bool {
        return id.isInstruction(llvm::Instruction::InsertValue);
    }
    [[nodiscard]] auto isFMul() const -> bool {
        return id.isInstruction(llvm::Instruction::FMul);
    }
    [[nodiscard]] auto isFNeg() const -> bool {
        return id.isInstruction(llvm::Instruction::FNeg);
    }
    [[nodiscard]] auto isFMulOrFNegOfFMul() const -> bool {
        return isFMul() || (isFNeg() && operands.front()->isFMul());
    }
    [[nodiscard]] auto isFAdd() const -> bool {
        return id.isInstruction(llvm::Instruction::FAdd);
    }
    [[nodiscard]] auto isFSub() const -> bool {
        return id.isInstruction(llvm::Instruction::FSub);
    }
    [[nodiscard]] auto allowsContract() const -> bool {
        if (auto m = llvm::dyn_cast<llvm::Instruction>(id.ptr.val))
            return m->getFastMathFlags().allowContract();
        return false;
    }
    [[nodiscard]] auto isMulAdd() const -> bool {
        return id.isIntrinsicInstruction(llvm::Intrinsic::fmuladd) ||
               id.isIntrinsicInstruction(llvm::Intrinsic::fma);
    }
    auto getCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth,
                 unsigned int log2VectorWidth) -> RecipThroughputLatency {
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
    auto getCost(llvm::TargetTransformInfo &TTI, uint32_t vectorWidth)
        -> RecipThroughputLatency {
        return getCost(TTI, vectorWidth, llvm::Log2_32(vectorWidth));
    }
    auto getCost(llvm::TargetTransformInfo &TTI, uint64_t vectorWidth)
        -> RecipThroughputLatency {
        return getCost(TTI, vectorWidth, llvm::Log2_64(vectorWidth));
    }
    auto getCostLog2VectorWidth(llvm::TargetTransformInfo &TTI,
                                unsigned int log2VectorWidth)
        -> RecipThroughputLatency {
        return getCost(TTI, 1 << log2VectorWidth, log2VectorWidth);
    }
    static auto getType(llvm::Type *T, unsigned int vectorWidth)
        -> llvm::Type * {
        if (vectorWidth == 1)
            return T;
        return llvm::FixedVectorType::get(T, vectorWidth);
    }
    [[nodiscard]] auto getType(unsigned int vectorWidth) const -> llvm::Type * {
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
    [[nodiscard]] auto getOperandInfo(unsigned int i) const
        -> std::pair<llvm::TargetTransformInfo::OperandValueKind,
                     llvm::TargetTransformInfo::OperandValueProperties> {
        Instruction *opi = (operands)[i];
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
    auto calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                 unsigned int vectorWidth)
        -> RecipThroughputLatency {
        auto op0info = getOperandInfo(0);
        llvm::Type *T = type;
        if (vectorWidth > 1)
            T = llvm::FixedVectorType::get(T, vectorWidth);
        return {TTI.getArithmeticInstrCost(
                    id.op, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second),
                TTI.getArithmeticInstrCost(
                    id.op, T, llvm::TargetTransformInfo::TCK_Latency,
                    op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
                    op0info.second)};
    }
    auto calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                  unsigned int vectorWidth)
        -> RecipThroughputLatency {
        auto op0info = getOperandInfo(0);
        auto op1info = getOperandInfo(1);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id.op, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info.first, op1info.first, op0info.second, op1info.second),
            TTI.getArithmeticInstrCost(
                id.op, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
                op1info.first, op0info.second, op1info.second)};
    }
#endif
    [[nodiscard]] auto operandIsLoad(unsigned int i = 0) const -> bool {
        return (operands)[i]->isLoad();
    }
    [[nodiscard]] auto userIsStore(unsigned int i) const -> bool {
        return users[i]->isLoad();
    }
    [[nodiscard]] auto userIsStore() const -> bool {
        for (auto u : users)
            if (u->isStore())
                return true;
        return false;
    }
    auto getCastContext(llvm::TargetTransformInfo &TTI) const
        -> llvm::TargetTransformInfo::CastContextHint {
        if (auto cast = llvm::dyn_cast<llvm::CastInst>(id.ptr.val))
            return TTI.getCastContextHint(cast);
        if (operandIsLoad() || userIsStore())
            return llvm::TargetTransformInfo::CastContextHint::Normal;
        // TODO: check for whether mask, interleave, or reversed is likely.
        return llvm::TargetTransformInfo::CastContextHint::None;
    }
    auto calcCastCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
        -> RecipThroughputLatency {
        llvm::Type *srcT = getType(operands.front()->type, vectorWidth);
        llvm::Type *dstT = getType(vectorWidth);
        llvm::TargetTransformInfo::CastContextHint ctx = getCastContext(TTI);
        return {TTI.getCastInstrCost(
                    id.op, dstT, srcT, ctx,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCastInstrCost(id.op, dstT, srcT, ctx,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    [[nodiscard]] auto getPredicate() const -> llvm::CmpInst::Predicate {
        if (isSelect())
            return operands.front()->getPredicate();
        assert(isCmp());
        if (auto cmp = llvm::dyn_cast<llvm::CmpInst>(id.ptr.val))
            return cmp->getPredicate();
        return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                        : llvm::CmpInst::BAD_ICMP_PREDICATE;
    }
    auto calcCmpSelectCost(llvm::TargetTransformInfo &TTI,
                           unsigned int vectorWidth) -> RecipThroughputLatency {
        llvm::Type *T = getType(vectorWidth);
        llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
        llvm::CmpInst::Predicate pred = getPredicate();
        return {TTI.getCmpSelInstrCost(
                    id.op, T, cmpT, pred,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCmpSelInstrCost(id.op, T, cmpT, pred,
                                       llvm::TargetTransformInfo::TCK_Latency)};
    }
    auto calcCallCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
        -> RecipThroughputLatency {
        llvm::Type *T = getType(vectorWidth);
        llvm::SmallVector<llvm::Type *, 4> argTypes;
        for (auto op : operands)
            argTypes.push_back(op->getType(vectorWidth));
        if (id.intrin == llvm::Intrinsic::not_intrinsic) {
            return {
                TTI.getCallInstrCost(
                    id.getFunction(), T, argTypes,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCallInstrCost(id.getFunction(), T, argTypes,
                                     llvm::TargetTransformInfo::TCK_Latency)};
        } else {
            llvm::IntrinsicCostAttributes attr(id.intrin, T, argTypes);
            return {TTI.getIntrinsicInstrCost(
                        attr, llvm::TargetTransformInfo::TCK_RecipThroughput),
                    TTI.getIntrinsicInstrCost(
                        attr, llvm::TargetTransformInfo::TCK_Latency)};
        }
    }
    auto calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                          unsigned int vectorWidth)
        -> RecipThroughputLatency {
        constexpr unsigned int AddressSpace = 0;
        llvm::Type *T = getType(vectorWidth);
        llvm::Align alignment = id.ptr.ref->getAlignment();
        return {
            TTI.getMemoryOpCost(id.op, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getMemoryOpCost(id.op, T, alignment, AddressSpace,
                                llvm::TargetTransformInfo::TCK_Latency)};
    }
    auto calculateCostFAddFSub(llvm::TargetTransformInfo &TTI,
                               unsigned int vectorWidth)
        -> RecipThroughputLatency {
        // TODO: allow not assuming hardware FMA support
        if (((operands)[0]->isFMulOrFNegOfFMul() ||
             (operands)[1]->isFMulOrFNegOfFMul()) &&
            allowsContract())
            return {};
        return calcBinaryArithmeticCost(TTI, vectorWidth);
    }
    auto allUsersAdditiveContract() -> bool {
        for (auto u : users)
            if (!(((u->isFAdd()) || (u->isFSub())) && (u->allowsContract())))
                return false;
        return true;
    }
    auto calculateFNegCost(llvm::TargetTransformInfo &TTI,
                           unsigned int vectorWidth) -> RecipThroughputLatency {

        if (operands.front()->isFMul() && allUsersAdditiveContract())
            return {};
        return calcUnaryArithmeticCost(TTI, vectorWidth);
    }
    auto calculateCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
        -> RecipThroughputLatency {
        switch (id.op) {
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
    [[nodiscard]] auto isCommutativeCall() const -> bool {
        if (id.intrin != llvm::Intrinsic::not_intrinsic) {
            if (auto *intrin = llvm::dyn_cast<llvm::IntrinsicInst>(id.ptr.val))
                return intrin->isCommutative();
        }
        return false;
    }
    [[nodiscard]] auto associativeOperandsFlag() const -> uint8_t {
        switch (id.op) {
        case llvm::Instruction::Call:
            if (!(isMulAdd() || isCommutativeCall()))
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

/// Provide DenseMapInfo for Identifier.
template <> struct llvm::DenseMapInfo<Instruction::Identifier, void> {
    static inline auto getEmptyKey() -> ::Instruction::Identifier {
        auto K = llvm::DenseMapInfo<llvm::Intrinsic::ID>::getEmptyKey();
        auto P = llvm::DenseMapInfo<llvm::Value *>::getEmptyKey();
        return ::Instruction::Identifier{K, K, P};
    }

    static inline auto getTombstoneKey() -> ::Instruction::Identifier {
        auto K = llvm::DenseMapInfo<llvm::Intrinsic::ID>::getTombstoneKey();
        auto P = llvm::DenseMapInfo<llvm::Value *>::getTombstoneKey();
        return ::Instruction::Identifier{K, K, P};
    }

    static auto getHashValue(const ::Instruction::Identifier &Key) -> unsigned;

    static auto isEqual(const ::Instruction::Identifier &LHS,
                        const ::Instruction::Identifier &RHS) -> bool {
        return LHS == RHS;
    }
};

enum struct PredicateRelation : uint8_t {
    Any = 0,
    True = 1,
    False = 2,
    Empty = 3,
};

[[maybe_unused]] static constexpr auto operator&(PredicateRelation a,
                                                 PredicateRelation b)
    -> PredicateRelation {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) |
                                          static_cast<uint8_t>(b));
}
[[maybe_unused]] static constexpr auto operator|(PredicateRelation a,
                                                 PredicateRelation b)
    -> PredicateRelation {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) &
                                          static_cast<uint8_t>(b));
}

/// PredicateRelations
/// A type for performing set algebra on predicates, representing sets
/// Note:
/// Commutative:
///     a | b == b | a
///     a & b == b & a
/// Distributive:
///     a | (b & c) == (a | b) & (a | c)
///     a & (b | c) == (a & b) | (a & c)
/// Associative:
///    a | (b | c) == (a | b) | c
///    a & (b & c) == (a & b) & c
/// Idempotent:
///    a | a == a
///    a & a == a
/// The internal representation can be interpreted as the intersection
/// of a vector of predicates.
/// This makes intersection operations efficient, but means we
/// may need to allocate new instructions to represent unions.
/// Unions are needed for merging divergent control flow branches.
/// For union calculation, we'd simplify:
/// (a & b) | (a & c) == a & (b | c)
/// If c == !b, then
/// (a & b) | (a & !b) == a & (b | !b) == a & True == a
/// Generically:
/// (a & b) | (c & d) == ((a & b) | c) & ((a & b) | d)
/// == (a | c) & (b | c) & (a | d) & (b | d)
struct PredicateRelations {
    [[no_unique_address]] llvm::SmallVector<uint64_t, 1> relations;
    auto operator[](size_t index) const -> PredicateRelation {
        return static_cast<PredicateRelation>(
            (relations[index / 32] >> (2 * (index % 32))) & 3);
    }
    void set(size_t index, PredicateRelation value) {
        auto d = index / 32;
        if (d >= relations.size())
            relations.resize(d + 1);
        auto r2 = 2 * (index % 32);
        uint64_t maskedOff = relations[d] & ~(3ULL << (r2));
        relations[d] = maskedOff | static_cast<uint64_t>(value) << (r2);
    }
    struct Reference {
        [[no_unique_address]] uint64_t *rp;
        [[no_unique_address]] size_t index;
        operator PredicateRelation() const {
            return static_cast<PredicateRelation>((*rp) >> index);
        }
        auto operator=(PredicateRelation relation) -> Reference & {
            *this->rp = (*this->rp & ~(3 << index)) |
                        (static_cast<uint64_t>(relation) << index);
            return *this;
        }
    };

    auto operator[](size_t index) -> Reference {
        auto i = index / 32;
        if (i >= relations.size())
            relations.resize(i + 1);
        return {&relations[i], 2 * (index % 32)};
    }
    [[nodiscard]] auto size() const -> size_t { return relations.size() * 32; }
    [[nodiscard]] auto relationSize() const -> size_t {
        return relations.size();
    }
    // FIXME: over-optimistic
    // (!a & !b) U (a & b) = a == b
    // (!a & b) U a = b
    [[nodiscard]] auto predUnion(const PredicateRelations &other) const
        -> PredicateRelations {
        if (relationSize() < other.relationSize())
            return other.predUnion(*this);
        // other.relationSize() <= relationSize()
        PredicateRelations result;
        result.relations.resize(other.relationSize());
        // `&` because `0` is `Any`
        // and `Any` is the preferred default initialization
        for (size_t i = 0; i < other.relationSize(); i++)
            result.relations[i] = relations[i] & other.relations[i];
        return result;
    }
    static void intersectImpl(PredicateRelations &c,
                              const PredicateRelations &a,
                              const PredicateRelations &b) {
        assert(a.relationSize() >= b.relationSize());
        c.relations.resize(a.relationSize());
        // `&` because `0` is `Any`
        // and `Any` is the preferred default initialization
        for (size_t i = 0; i < b.relationSize(); i++)
            c.relations[i] = a.relations[i] | b.relations[i];
        for (size_t i = b.relationSize(); i < a.relationSize(); i++)
            c.relations[i] = a.relations[i];
    }
    [[nodiscard]] auto predIntersect(const PredicateRelations &other) const
        -> PredicateRelations {
        PredicateRelations result;
        if (relationSize() < other.relationSize()) {
            intersectImpl(result, other, *this);
        } else {
            intersectImpl(result, *this, other);
        }
        return result;
    }

    static constexpr auto isEmpty(uint64_t x) -> bool {
        return ((x & (x >> 1)) & 0x5555555555555555) != 0;
    }
    [[nodiscard]] auto isEmpty() const -> bool {
        for (uint64_t x : relations)
            if (isEmpty(x))
                return true;
        return false;
    }
    [[nodiscard]] auto emptyIntersection(const PredicateRelations &other) const
        -> bool {
        if (relationSize() < other.relationSize())
            return other.emptyIntersection(*this);
        // other.relationSize() <= relationSize()
        for (size_t i = 0; i < other.relationSize(); i++)
            if (isEmpty(relations[i] | other.relations[i]))
                return true;
        for (size_t i = other.relationSize(); i < relations.size(); i++)
            if (isEmpty(relations[i]))
                return true;
        return false;
    }
    static auto getIndex(llvm::SmallVectorImpl<Instruction *> &instructions,
                         Instruction *instruction) -> size_t {
        size_t I = instructions.size();
        for (size_t i = 0; i < I; i++)
            if (instructions[i] == instruction)
                return i;
        instructions.push_back(instruction);
        return I;
    }
    PredicateRelations() = default;
    PredicateRelations(llvm::BumpPtrAllocator &alloc, Instruction::Cache &ic,
                       llvm::SmallVector<Instruction *> &predicates,
                       Predicates &pred) {
        for (Predicate &p : pred) {
            Instruction *i = ic.get(alloc, p.condition);
            size_t index = getIndex(predicates, i);
            PredicateRelation val =
                p.flip ? PredicateRelation::False : PredicateRelation::True;
            set(index, val);
        }
    }
};

struct BlockPredicates {
    // TODO: use internal IR for predicates
    // the purpose of this would be to allow for union calculations.
    llvm::SmallVector<Instruction *> predicates;
    llvm::DenseMap<llvm::BasicBlock *, PredicateRelations> blockPredicates;
    void add(llvm::BumpPtrAllocator &alloc, Instruction::Cache &ic,
             llvm::BasicBlock *B, Predicates &pred) {
        blockPredicates[B] = PredicateRelations(alloc, ic, predicates, pred);
    }
};

void buildInstructionGraph(llvm::BumpPtrAllocator &alloc,
                           Instruction::Cache &ic, BlockPredicates &bp,
                           LoopTree &loopTree) {
    for (auto &L : loopTree) {
    }
}

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;
