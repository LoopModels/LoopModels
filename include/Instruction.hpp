#pragma once

#include "./ArrayReference.hpp"
#include "./LoopForest.hpp"
#include "./Predicate.hpp"
#include <cstdint>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DenseMap.h>
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
    using UniqueIdentifier =
        std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Value *,
                   llvm::ArrayRef<Instruction *>>;
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

        Identifier(UniqueIdentifier id)
            : op(std::get<0>(id)),
              intrin(std::get<1>(id)), ptr{std::get<2>(id)} {}

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
        /// Check if the ptr is a load or store
        [[nodiscard]] auto isValueLoadOrStore() const -> bool {
            return isValue() && (llvm::isa<llvm::LoadInst>(ptr.val) ||
                                 llvm::isa<llvm::StoreInst>(ptr.val));
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
        [[nodiscard]] constexpr auto operator==(const Identifier &other) const
            -> bool {
            return op == other.op && intrin == other.intrin &&
                   ptr.val == other.ptr.val;
        }
    };

    Identifier id;
    llvm::Type *type;
    Predicates predicates;
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
    Instruction(UniqueIdentifier uid)
        : id(uid), type(std::get<2>(uid)->getType()) {}
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
        auto createInstruction(llvm::BumpPtrAllocator &alloc,
                               llvm::Instruction *instr) -> Instruction * {
            UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, instr)};
            auto argMatch = argMap.find(uid);
            if (argMatch != argMap.end())
                return argMatch->second;
            // if load or store, we replace ptr with ArrayReference
            auto i = new (alloc) Instruction(uid);
            auto insertIter = argMap.insert({uid, i});
            assert(insertIter.second);
            assert(insertIter.first->second == i);
            i->operands = std::get<3>(insertIter.first->first);
            for (auto *op : i->operands) {
                op->users.push_back(i);
            }
            llvmToInternalMap[instr] = i;
            return i;
        }
        auto createInstruction(llvm::BumpPtrAllocator &alloc,
                               PredicateMap &predMap, llvm::Instruction *instr)
            -> Instruction * {
            auto pred = predMap[instr];
            if (!pred) {
                // return an incomplete instruction
                // it is not added to the argMap
                auto i = new (alloc)
                    Instruction(Identifier(instr), instr->getType());
                llvmToInternalMap[instr] = i;
                return i;
            }
            UniqueIdentifier uid{
                getUniqueIdentifier(alloc, predMap, *this, instr)};
            auto argMatch = argMap.find(uid);
            if (argMatch != argMap.end())
                return argMatch->second;
            auto i = new (alloc) Instruction(uid);
            auto insertIter = argMap.insert({uid, i});
            assert(insertIter.second);
            assert(insertIter.first->second == i);
            i->predicates = std::move(*pred);
            i->operands = std::get<3>(insertIter.first->first);
            for (auto *op : i->operands) {
                op->users.push_back(i);
            }
            llvmToInternalMap[instr] = i;
            return i;
        }
        auto get(llvm::BumpPtrAllocator &alloc, llvm::Value *v)
            -> Instruction * {
            if (Instruction *i = (*this)[v])
                return i;
            if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v))
                return createInstruction(alloc, instr);
            auto *i = new (alloc) Instruction(Identifier(v), v->getType());
            llvmToInternalMap[v] = i;
            return i;
        }
        // if not in predMap, then operands don't get added, and
        // it won't be added to the argMap
        auto get(llvm::BumpPtrAllocator &alloc, PredicateMap &predMap,
                 llvm::Value *v) -> Instruction * {
            if (Instruction *i = (*this)[v]) {
                // if `i` has operands, it's been completed
                if (i->operands.size() > 0)
                    return i;
                // maybe `i` legitimately has no operands? If so, we also return
                auto instr = llvm::dyn_cast<llvm::Instruction>(v);
                if (!instr || instr->getNumOperands() == 0)
                    return i;
                // instr is non-null and has operands
                // maybe instr isn't in BBpreds?
                if (auto pred = predMap[instr]) {
                    // instr is in BBpreds, therefore, we now complete `i`.
                    i->predicates = std::move(*pred);
                    i->operands = getOperands(alloc, predMap, *this, instr);
                    for (auto *op : i->operands) {
                        op->users.push_back(i);
                    }
                }
                return i;
            }
            if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v))
                return createInstruction(alloc, predMap, instr);
            auto *i = new (alloc) Instruction(Identifier(v), v->getType());
            llvmToInternalMap[v] = i;
            return i;
        }
    };
    [[nodiscard]] static auto getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                                  Cache &cache,
                                                  llvm::Instruction *v)
        -> UniqueIdentifier {
        return std::make_tuple(Identifier::getOpCode(v),
                               Identifier::getIntrinsicID(v), v,
                               getOperands(alloc, cache, v));
    }
    [[nodiscard]] static auto
    getUniqueIdentifier(llvm::BumpPtrAllocator &alloc, PredicateMap &predMap,
                        Cache &cache, llvm::Instruction *v)
        -> UniqueIdentifier {
        return std::make_tuple(Identifier::getOpCode(v),
                               Identifier::getIntrinsicID(v), v,
                               getOperands(alloc, predMap, cache, v));
    }
    [[nodiscard]] static auto getOperands(llvm::BumpPtrAllocator &alloc,
                                          Cache &cache,
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
    [[nodiscard]] static auto getOperands(llvm::BumpPtrAllocator &alloc,
                                          PredicateMap &BBpreds, Cache &cache,
                                          llvm::Instruction *instr)
        -> llvm::ArrayRef<Instruction *> {
        auto ops{instr->operands()};
        auto OI = ops.begin();
        auto OE = ops.end();
        size_t Nops = instr->getNumOperands();
        auto **operands = alloc.Allocate<Instruction *>(Nops);
        Instruction **p = operands;
        for (; OI != OE; ++OI, ++p) {
            *p = cache.get(alloc, BBpreds, *OI);
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
    [[nodiscard]] auto isLoadOrStore() const -> bool {
        return isLoad() || isStore();
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
        if (predicates.size() == 0) {
            return {
                TTI.getMemoryOpCost(
                    id.op, T, alignment, AddressSpace,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getMemoryOpCost(id.op, T, alignment, AddressSpace,
                                    llvm::TargetTransformInfo::TCK_Latency)};
        } else {
            return {TTI.getMaskedMemoryOpCost(
                        id.op, T, alignment, AddressSpace,
                        llvm::TargetTransformInfo::TCK_RecipThroughput),
                    TTI.getMaskedMemoryOpCost(
                        id.op, T, alignment, AddressSpace,
                        llvm::TargetTransformInfo::TCK_Latency)};
        }
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
};

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;
