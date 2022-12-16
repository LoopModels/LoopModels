#pragma once

#include "./ArrayReference.hpp"
#include "./Predicate.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
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
#include <utility>

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

struct Instruction {
    using UniqueIdentifier =
        std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Value *,
                   llvm::MutableArrayRef<Instruction *>>;
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
    struct Predicates {
        [[no_unique_address]] Predicate::Set predicates;
        // `SmallVector` to copy, as `llvm::ArrayRef` wouldn't be safe in
        // case of realloc
        [[no_unique_address]] llvm::SmallVector<Instruction *, 2> instr;
        // TODO: constexpr once llvm::SmallVector supports it
        auto size() -> size_t { return instr.size(); }
        auto begin() { return instr.begin(); }
        auto end() { return instr.end(); }
    };

    Identifier id;
    llvm::Type *type;
    Predicates predicates;
    llvm::MutableArrayRef<Instruction *> operands;
    llvm::SmallVector<Instruction *> users;
    /// costs[i] == cost for vector-width 2^i
    llvm::SmallVector<RecipThroughputLatency> costs;
    [[nodiscard]] auto getType() const -> llvm::Type * { return type; }
    [[nodiscard]] auto getOperands() -> llvm::MutableArrayRef<Instruction *> {
        return operands;
    }
    [[nodiscard]] auto getOperands() const -> llvm::ArrayRef<Instruction *> {
        return operands;
    }
    [[nodiscard]] auto getOperand(size_t i) -> Instruction * {
        return operands[i];
    }
    [[nodiscard]] auto getOperand(size_t i) const -> Instruction * {
        return operands[i];
    }
    [[nodiscard]] auto getUsers() -> llvm::ArrayRef<Instruction *> {
        return users;
    }
    [[nodiscard]] auto getNumOperands() const -> size_t {
        return operands.size();
    }
    [[nodiscard]] auto getOpTripple() const
        -> std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Type *> {
        return std::make_tuple(id.op, id.intrin, getType());
    }
    [[nodiscard]] auto getBasicBlock() -> llvm::BasicBlock * {
        if (isLoadOrStore())
            return id.ptr.ref->loadOrStore->getParent();
        else if (auto *I = llvm::dyn_cast<llvm::Instruction>(id.ptr.val))
            return I->getParent();
        else
            return nullptr;
    }
    static auto createSelect(llvm::BumpPtrAllocator &alloc, Instruction *A,
                             Instruction *B) -> Instruction * {
        Identifier id = Identifier(llvm::Instruction::Select,
                                   llvm::Intrinsic::not_intrinsic, nullptr);
        auto *S = new (alloc) Instruction(id, A->getType()); //, {A, B});
        // TODO: make predicate's instruction vector shared among all in
        // LoopTree?
        // What I need here is to take the union of the predicates to form the
        // predicates of the new select instruction.
        // Then, for the select's `cond` instruction, I need something
        // to indicate when to take one path and not the other.
        // We know the intersection is empty, so -- why is it empty?
        // We need something to slice that. E.g.
        /// if *A = [(a & b) | (c & d)]
        ///    *B = [(e & f) | (g & h)]
        /// then
        /// [(a & b) | (c & d)] & [(e & f) | (g & h)] =
        ///   [(a & b) & (e & f)] |
        ///   [(a & b) & (g & h)] |
        ///   [(c & d) & (e & f)] |
        ///   [(c & d) & (g & h)]
        /// for this to be empty, we need to have
        ///   [(a & b) & (e & f)] =
        ///   [(a & b) & (g & h)] =
        ///   [(c & d) & (e & f)] =
        ///   [(c & d) & (g & h)] = 0
        /// Suggestion: loop over union elements,
        /// and take the set of all of the conditions for
        /// each side.
        /// Then use the simpler of these two to determine the direction of the
        /// select.
        auto [L, R] = A->predicates.predicates.cut(B->predicates.predicates);
        S->predicates.predicates =
            A->predicates.predicates | B->predicates.predicates;
        return S;
    }
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
                               Predicate::Map &predMap,
                               llvm::Instruction *instr) -> Instruction *;
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
        auto get(llvm::BumpPtrAllocator &alloc, Predicate::Map &predMap,
                 llvm::Value *v) -> Instruction *;
        [[nodiscard]] auto contains(llvm::Value *v) const -> bool {
            return llvmToInternalMap.count(v);
        }
    };
    [[nodiscard]] auto static getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                                  Cache &cache,
                                                  llvm::Instruction *v)
        -> UniqueIdentifier {
        return std::make_tuple(Identifier::getOpCode(v),
                               Identifier::getIntrinsicID(v), v,
                               getOperands(alloc, cache, v));
    }
    [[nodiscard]] auto getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                           Cache &cache) -> UniqueIdentifier {
        return std::make_tuple(
            id.op, id.intrin, id.ptr.val,
            getOperands(alloc, cache,
                        llvm::cast<llvm::Instruction>(id.ptr.val)));
    }
    [[nodiscard]] static auto
    getUniqueIdentifier(llvm::BumpPtrAllocator &alloc, Predicate::Map &predMap,
                        Cache &cache, llvm::Instruction *v)
        -> UniqueIdentifier {
        return std::make_tuple(Identifier::getOpCode(v),
                               Identifier::getIntrinsicID(v), v,
                               getOperands(alloc, predMap, cache, v));
    }
    [[nodiscard]] auto getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                           Predicate::Map &predMap,
                                           Cache &cache) -> UniqueIdentifier {
        return std::make_tuple(
            id.op, id.intrin, id.ptr.val,
            getOperands(alloc, predMap, cache,
                        llvm::cast<llvm::Instruction>(id.ptr.val)));
    }
    [[nodiscard]] static auto getOperands(llvm::BumpPtrAllocator &alloc,
                                          Cache &cache,
                                          llvm::Instruction *instr)
        -> llvm::MutableArrayRef<Instruction *> {
        if (llvm::isa<llvm::LoadInst>(instr))
            return {nullptr, unsigned(0)};
        auto ops{instr->operands()};
        auto OI = ops.begin();
        // NOTE: operand 0 is the value operand of a store
        bool isStore = llvm::isa<llvm::StoreInst>(instr);
        auto OE = isStore ? (OI + 1) : ops.end();
        size_t numOps = isStore ? 1 : instr->getNumOperands();
        auto **operands = alloc.Allocate<Instruction *>(numOps);
        Instruction **p = operands;
        for (; OI != OE; ++OI, ++p) {
            *p = cache.get(alloc, *OI);
        }
        return {operands, numOps};
    }
    [[nodiscard]] static auto getOperands(llvm::BumpPtrAllocator &alloc,
                                          Predicate::Map &BBpreds, Cache &cache,
                                          llvm::Instruction *instr)
        -> llvm::MutableArrayRef<Instruction *> {
        if (llvm::isa<llvm::LoadInst>(instr))
            return {nullptr, unsigned(0)};
        auto ops{instr->operands()};
        auto OI = ops.begin();
        // NOTE: operand 0 is the value operand of a store
        bool isStore = llvm::isa<llvm::StoreInst>(instr);
        auto OE = isStore ? (OI + 1) : ops.end();
        size_t Nops = isStore ? 1 : instr->getNumOperands();
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
    [[nodiscard]] auto getNumScalarBits() const -> unsigned int {
        return type->getScalarSizeInBits();
    }
    [[nodiscard]] auto getNumScalarBytes() const -> unsigned int {
        return getNumScalarBits() / 8;
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

    /// for calculating the cost of a select when merging this instruction with
    /// another one.
    auto selectCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
        -> llvm::InstructionCost {
        llvm::Type *T = getType(vectorWidth);
        llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
        // llvm::CmpInst::Predicate pred =
        // TODO: extract from difference in predicates
        // between this and other (which would have to be passed in).
        // However, X86TargetTransformInfo doesn't use this for selects,
        // so doesn't seem like we need to bother with it.
        llvm::CmpInst::Predicate pred = T->isFPOrFPVectorTy()
                                            ? llvm::CmpInst::BAD_FCMP_PREDICATE
                                            : llvm::CmpInst::BAD_ICMP_PREDICATE;
        return TTI.getCmpSelInstrCost(
            llvm::Instruction::Select, T, cmpT, pred,
            llvm::TargetTransformInfo::TCK_RecipThroughput);
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

namespace Predicate {
struct Map {
    llvm::MapVector<llvm::BasicBlock *, Set> map;
    llvm::SmallVector<Instruction *> predicates;
    [[nodiscard]] auto size() const -> size_t { return map.size(); }
    [[nodiscard]] auto isEmpty() const -> bool { return map.empty(); }
    [[nodiscard]] auto isDivergent() const -> bool {
        if (size() < 2)
            return false;
        for (auto I = map.begin(), E = map.end(); I != E; ++I) {
            if (I->second.isEmpty())
                continue;
            for (auto J = std::next(I); J != E; ++J) {
                // NOTE: we don't need to check`isEmpty()`
                // because `emptyIntersection()` returns `false`
                // when isEmpty() is true.
                if (I->second.intersectionIsEmpty(J->second))
                    return true;
            }
        }
        return false;
    }
    [[nodiscard]] auto getEntry() const -> llvm::BasicBlock * {
        return map.back().first;
    }
    [[nodiscard]] auto get(llvm::BasicBlock *bb) -> Set & { return map[bb]; }
    [[nodiscard]] auto find(llvm::BasicBlock *bb)
        -> llvm::MapVector<llvm::BasicBlock *, Set>::iterator {
        return map.find(bb);
    }
    [[nodiscard]] auto find(llvm::Instruction *inst)
        -> llvm::MapVector<llvm::BasicBlock *, Set>::iterator {
        return map.find(inst->getParent());
    }
    // we insert into map in reverse order, so our iterators reverse
    [[nodiscard]] auto begin() -> decltype(map.rbegin()) {
        return map.rbegin();
    }
    [[nodiscard]] auto end() -> decltype(map.rend()) { return map.rend(); }
    [[nodiscard]] auto rbegin() -> decltype(map.begin()) { return map.begin(); }
    [[nodiscard]] auto rend() -> decltype(map.end()) { return map.end(); }
    [[nodiscard]] auto operator[](llvm::BasicBlock *bb)
        -> std::optional<Instruction::Predicates> {
        auto it = map.find(bb);
        if (it == map.end())
            return std::nullopt;
        return Instruction::Predicates{it->second, predicates};
    }
    [[nodiscard]] auto operator[](llvm::Instruction *inst)
        -> std::optional<Instruction::Predicates> {
        return (*this)[inst->getParent()];
    }
    void insert(std::pair<llvm::BasicBlock *, Set> &&pair) {
        map.insert(std::move(pair));
    }
    [[nodiscard]] auto contains(llvm::BasicBlock *BB) -> bool {
        return map.count(BB);
    }
    [[nodiscard]] auto isInPath(llvm::BasicBlock *BB) -> bool {
        auto f = find(BB);
        if (f == rend())
            return false;
        return !f->second.isEmpty();
    }
    [[nodiscard]] auto isInPath(llvm::Instruction *I) -> bool {
        return isInPath(I->getParent());
    }
    void clear() {
        map.clear();
        predicates.clear();
    }
    // void visit(llvm::BasicBlock *BB) { map.insert(std::make_pair(BB, Set()));
    // } void visit(llvm::Instruction *inst) { visit(inst->getParent()); }
    [[nodiscard]] auto addPredicate(llvm::BumpPtrAllocator &alloc,
                                    Instruction::Cache &cache,
                                    llvm::Value *value) -> size_t {
        auto *I = cache.get(alloc, *this, value);
        assert(predicates.size() <= 32 && "too many predicates");
        for (size_t i = 0; i < predicates.size(); ++i)
            if (predicates[i] == I)
                return i;
        size_t i = predicates.size();
        assert(predicates.size() != 32 && "too many predicates");
        predicates.emplace_back(I);
        return i;
    }
    void reach(llvm::BasicBlock *BB, Intersection predicate) {
        // because we may have inserted into predMap, we need to look up
        // again rather than being able to reuse anything from the
        // `visit`.
        if (auto f = find(BB); f != rend()) {
            f->second |= predicate;
        } else {
            map.insert({BB, predicate});
            // map.insert(std::make_pair(BB, Set(predicate)));
        }
    }
    void assume(Intersection predicate) {
        for (auto &&pair : map)
            pair.second &= predicate;
    };
    enum class Destination { Reached, Unreachable, Returned, Unknown };
    // TODO:
    // 1. see why L->contains(BBsrc) does not work; does it only contain BBs in
    // it directly, and not nested another loop deeper?
    // 2. We are ignoring cycles for now; we must ensure this is done correctly
    [[nodiscard]] static auto
    descendBlock(llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
                 llvm::SmallPtrSet<llvm::BasicBlock *, 16> &visited,
                 Predicate::Map &predMap, llvm::BasicBlock *BBsrc,
                 llvm::BasicBlock *BBdst, Predicate::Intersection predicate,
                 llvm::BasicBlock *BBhead, llvm::Loop *L) -> Destination {
        if (BBsrc == BBdst) {
            assert(!predMap.contains(BBsrc));
            predMap.insert({BBsrc, predicate});
            return Destination::Reached;
        } else if (L && (!(L->contains(BBsrc)))) {
            // oops, we seem to have skipped the preheader and escaped the loop.
            return Destination::Returned;
        } else if (visited.contains(BBsrc)) {
            // FIXME: This is terribly hacky.
            // if `BBsrc == BBhead`, then we assume we hit a path that
            // bypasses the following loop, e.g. there was a loop guard.
            //
            // Thus, we return `Returned`, indicating that it was a non-fatal
            // dead-end.
            // Otherwise, we check if it seems to have led to a live, non-empty
            // path.
            // TODO: should we union the predicates in case of returned?
            if (BBsrc == BBhead) {
                return Destination::Returned;
            } else if (auto f = predMap.find(BBsrc);
                       f != predMap.rend() && !f->second.isEmpty()) {
                return Destination::Returned;
            } else {
                return Destination::Reached;
            }
        }
        // Inserts a tombstone to indicate that we have visited BBsrc, but not
        // actually reached a destination.
        visited.insert(BBsrc);
        const llvm::Instruction *I = BBsrc->getTerminator();
        if (!I)
            return Destination::Unknown;
        else if (llvm::isa<llvm::ReturnInst>(I))
            return Destination::Returned;
        else if (llvm::isa<llvm::UnreachableInst>(I))
            return Destination::Unreachable;
        auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
        if (!BI)
            return Destination::Unknown;
        if (BI->isUnconditional()) {
            auto rc =
                descendBlock(alloc, cache, visited, predMap,
                             BI->getSuccessor(0), BBdst, predicate, BBhead, L);
            if (rc == Destination::Reached)
                predMap.reach(BBsrc, predicate);
            return rc;
        }
        // We have a conditional branch.
        llvm::Value *cond = BI->getCondition();
        // We need to check both sides of the branch and add a predicate.
        size_t predInd = predMap.addPredicate(alloc, cache, cond);
        auto rc0 = descendBlock(
            alloc, cache, visited, predMap, BI->getSuccessor(0), BBdst,
            predicate.intersect(predInd, Predicate::Relation::True), BBhead, L);
        if (rc0 == Destination::Unknown) // bail
            return Destination::Unknown;
        auto rc1 = descendBlock(
            alloc, cache, visited, predMap, BI->getSuccessor(1), BBdst,
            predicate.intersect(predInd, Predicate::Relation::False), BBhead,
            L);
        if ((rc0 == Destination::Returned) ||
            (rc0 == Destination::Unreachable)) {
            if (rc1 == Destination::Reached) {
                //  we're now assuming that !cond
                predMap.assume(Predicate::Intersection(
                    predInd, Predicate::Relation::False));
                predMap.reach(BBsrc, predicate);
            }
            return rc1;
        } else if ((rc1 == Destination::Returned) ||
                   (rc1 == Destination::Unreachable)) {
            if (rc0 == Destination::Reached) {
                //  we're now assuming that cond
                predMap.assume(Predicate::Intersection(
                    predInd, Predicate::Relation::True));
                predMap.reach(BBsrc, predicate);
            }
            return rc0;
        } else if (rc0 == rc1) {
            if (rc0 == Destination::Reached)
                predMap.reach(BBsrc, predicate);
            return rc0;
        } else
            return Destination::Unknown;
    }
    /// We bail if there are more than 32 conditions; control flow that
    /// branchy is probably not worth trying to vectorize.
    [[nodiscard]] static auto
    descend(llvm::BumpPtrAllocator &alloc, Instruction::Cache &cache,
            llvm::BasicBlock *start, llvm::BasicBlock *stop, llvm::Loop *L)
        -> std::optional<Map> {
        Predicate::Map pm;
        llvm::SmallPtrSet<llvm::BasicBlock *, 16> visited;
        if (descendBlock(alloc, cache, visited, pm, start, stop, {}, start,
                         L) == Destination::Reached)
            return pm;
        return std::nullopt;
    }

}; // struct Map
} // namespace Predicate

auto Instruction::Cache::createInstruction(llvm::BumpPtrAllocator &alloc,
                                           Predicate::Map &predMap,
                                           llvm::Instruction *instr)
    -> Instruction * {
    auto i = new (alloc) Instruction(Identifier(instr), instr->getType());
    // allocate and store first to avoid cycles
    llvmToInternalMap[instr] = i;
    auto pred = predMap[instr];
    if (!pred) {
        // return an incomplete instruction
        // it is not added to the argmap
        return i;
    }
    UniqueIdentifier uid{i->getUniqueIdentifier(alloc, predMap, *this)};
    auto argMatch = argMap.find(uid);
    if (argMatch != argMap.end()) {
        llvmToInternalMap[instr] = argMatch->second;
        return argMatch->second;
    }
    // auto i = new (alloc) Instruction(uid);
    auto insertIter = argMap.insert({uid, i});
    assert(insertIter.second);
    assert(insertIter.first->second == i);
    i->predicates = *pred;
    i->operands = std::get<3>(insertIter.first->first);
    for (auto *op : i->operands) {
        op->users.push_back(i);
    }
    llvmToInternalMap[instr] = i;
    return i;
}
auto Instruction::Cache::get(llvm::BumpPtrAllocator &alloc,
                             Predicate::Map &predMap, llvm::Value *v)
    -> Instruction * {
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
            SHOWLN(instr);
            SHOWLN(*instr);
            i->predicates = std::move(*pred);
            // we use dummy operands to avoid infinite recursion
            i->operands = llvm::MutableArrayRef<Instruction *>{nullptr, 1};
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
