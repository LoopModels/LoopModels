#pragma once

#include "./ArrayReference.hpp"
#include "./Predicate.hpp"
#include <bit>
#include <concepts>
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
#include <llvm/IR/Constant.h>
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
#include <variant>

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
    struct Intrinsic {
        struct OpCode {
            llvm::Intrinsic::ID id;
            constexpr auto operator==(const OpCode &other) const -> bool {
                return id == other.id;
            }
        };
        struct Intrin {
            llvm::Intrinsic::ID id{llvm::Intrinsic::not_intrinsic};
            constexpr auto operator==(const Intrin &other) const -> bool {
                return id == other.id;
            }
        };
        OpCode opcode;
        Intrin intrin;
        [[nodiscard]] auto getOpCode() const -> OpCode { return opcode; }
        [[nodiscard]] auto getIntrinsicID() const -> Intrin { return intrin; }
        static auto getOpCode(llvm::Value *v) -> OpCode {
            if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
                return OpCode{i->getOpcode()};
            return OpCode{llvm::Intrinsic::not_intrinsic};
        }
        static auto getIntrinsicID(llvm::Value *v) -> Intrin {
            if (auto *i = llvm::dyn_cast<llvm::IntrinsicInst>(v))
                return Intrin{i->getIntrinsicID()};
            return Intrin{llvm::Intrinsic::not_intrinsic};
        }

        /// Instruction ID
        /// if not Load or Store, then check val for whether it is a call
        /// and ID corresponds to the instruction or to the intrinsic call

        /// Data we may need

        Intrinsic(llvm::Value *v)
            : opcode(getOpCode(v)), intrin(getIntrinsicID(v)) {}
        Intrinsic(OpCode op, Intrin intrin) : opcode(op), intrin(intrin) {}
        Intrinsic(OpCode op) : opcode(op) {}
        [[nodiscard]] constexpr auto isInstruction(OpCode opCode) const
            -> bool {
            return opcode == opCode;
        }
        [[nodiscard]] constexpr auto isInstruction(unsigned opCode) const
            -> bool {
            return isInstruction(OpCode{opCode});
        }
        [[nodiscard]] constexpr auto isIntrinsicInstruction(Intrin opCode) const
            -> bool {
            return intrin == opCode;
        }
        [[nodiscard]] constexpr auto
        isIntrinsicInstruction(unsigned opCode) const -> bool {
            return isIntrinsicInstruction(Intrin{opCode});
        }
        [[nodiscard]] constexpr auto operator==(const Intrinsic &other) const
            -> bool {
            return opcode == other.opcode && intrin == other.intrin;
        }
    };
    using Identifer =
        std::variant<Intrinsic, llvm::Function *, int64_t, double>;
    using UniqueIdentifier =
        std::pair<Identifer, llvm::MutableArrayRef<Instruction *>>;

    [[no_unique_address]] Identifer id;
    // Intrinsic id;
    [[no_unique_address]] llvm::Type *type;
    [[no_unique_address]] std::variant<std::monostate, llvm::Instruction *,
                                       llvm::ConstantInt *, llvm::ConstantFP *,
                                       ArrayReference *>
        ptr;
    [[no_unique_address]] Predicate::Set predicates;
    [[no_unique_address]] llvm::MutableArrayRef<Instruction *> operands;
    [[no_unique_address]] llvm::SmallVector<Instruction *> users;
    /// costs[i] == cost for vector-width 2^i
    [[no_unique_address]] llvm::SmallVector<RecipThroughputLatency> costs;

    static auto getIdentifier(llvm::Instruction *I) -> Identifer {
        if (auto *CB = llvm::dyn_cast<llvm::CallBase>(I))
            if (auto *F = CB->getCalledFunction())
                return F;
        return Intrinsic(I);
    }
    static auto getIdentifier(llvm::ConstantInt *I) -> Identifer {
        return I->getSExtValue();
    }
    static auto getIdentifier(llvm::ConstantFP *I) -> Identifer {
        return I->getValueAPF().convertToDouble();
    }
    static auto getIdentifier(llvm::Value *v) -> std::optional<Identifer> {
        if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) {
            return getIdentifier(i);
        } else if (auto *i = llvm::dyn_cast<llvm::ConstantInt>(v)) {
            return getIdentifier(i);
        } else if (auto *i = llvm::dyn_cast<llvm::ConstantFP>(v)) {
            return getIdentifier(i);
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] auto isIntrinsic() const -> bool {
        return std::holds_alternative<Intrinsic>(id);
    }
    [[nodiscard]] auto isFunction() const -> bool {
        return std::holds_alternative<llvm::Function *>(id);
    }
    [[nodiscard]] auto isConstantInt() const -> bool {
        return std::holds_alternative<int64_t>(id);
    }
    [[nodiscard]] auto isConstantFP() const -> bool {
        return std::holds_alternative<double>(id);
    }
    [[nodiscard]] auto isConstant() const -> bool {
        return isConstantInt() || isConstantFP();
    }

    /// Check if the ptr is a load or store, without an ArrayRef
    [[nodiscard]] auto isValueLoadOrStore() const -> bool {
        if (llvm::Instruction *const *I =
                std::get_if<llvm::Instruction *>(&ptr))
            return llvm::isa<llvm::LoadInst>(*I) ||
                   llvm::isa<llvm::StoreInst>(*I);
        return false;
    }
    [[nodiscard]] auto getFunction() const -> llvm::Function * {
        if (llvm::Function *const *F = std::get_if<llvm::Function *>(&id))
            return *F;
        return nullptr;
    }

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
    // [[nodiscard]] auto getOpTripple() const
    //     -> std::tuple<llvm::Intrinsic::ID, llvm::Intrinsic::ID, llvm::Type *>
    //     { return std::make_tuple(id.op, id.intrin, getType());
    // }
    struct ExtractValue {
        auto operator()(auto) const -> llvm::Value * { return nullptr; }
        auto operator()(llvm::Value *v) const -> llvm::Value * { return v; }
        auto operator()(ArrayReference *v) const -> llvm::Value * {
            return v->loadOrStore;
        }
    };
    [[nodiscard]] auto getValue() -> llvm::Value * {
        return std::visit<llvm::Value *>(ExtractValue{}, ptr);
    }
    [[nodiscard]] auto getInstruction() -> llvm::Instruction * {
        return llvm::dyn_cast_or_null<llvm::Instruction>(getValue());
    }
    [[nodiscard]] auto getValue() const -> llvm::Value * {
        return std::visit<llvm::Value *>(ExtractValue{}, ptr);
    }
    [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
        return llvm::dyn_cast_or_null<llvm::Instruction>(getValue());
    }

    struct ExtractBasicBlock {
        auto operator()(auto) const -> llvm::BasicBlock * { return nullptr; }
        auto operator()(llvm::Value *v) const -> llvm::BasicBlock * {
            if (auto *I = llvm::dyn_cast<llvm::Instruction>(v))
                return I->getParent();
            return nullptr;
        }
        auto operator()(ArrayReference *v) const -> llvm::BasicBlock * {
            return v->loadOrStore->getParent();
        }
    };
    [[nodiscard]] auto getBasicBlock() -> llvm::BasicBlock * {
        return std::visit<llvm::BasicBlock *>(ExtractBasicBlock{}, ptr);
    }

    struct Predicates {
        [[no_unique_address]] Predicate::Set predicates;
        [[no_unique_address]] llvm::MutableArrayRef<Instruction *> instr;
    };
    // llvm::TargetTransformInfo &TTI;

    // Instruction(llvm::Intrinsic::ID id, llvm::Type *type) : id(id),
    // type(type) {
    //     // this->TTI = TTI;
    // }
    Instruction(Intrinsic id, llvm::Type *type) : id(id), type(type) {}
    // Instruction(UniqueIdentifier uid)
    // : id(std::get<0>(uid)), operands(std::get<1>(uid)) {}
    Instruction(UniqueIdentifier uid, llvm::Type *type)
        : id(std::get<0>(uid)), type(type), operands(std::get<1>(uid)) {}
    struct Cache {
        [[no_unique_address]] llvm::DenseMap<llvm::Value *, Instruction *>
            llvmToInternalMap;
        [[no_unique_address]] llvm::DenseMap<UniqueIdentifier, Instruction *>
            argMap;
        [[no_unique_address]] llvm::SmallVector<Instruction *> predicates;
        auto operator[](llvm::Value *v) -> Instruction * {
            auto f = llvmToInternalMap.find(v);
            if (f != llvmToInternalMap.end())
                return f->second;
            return nullptr;
        }
        auto operator[](UniqueIdentifier uid) -> Instruction * {
            auto f = argMap.find(uid);
            if (f != argMap.end())
                return f->second;
            return nullptr;
        }
        auto argMapLoopup(Identifer id) -> Instruction * {
            UniqueIdentifier uid{id, {}};
            return (*this)[uid];
        }
        auto argMapLoopup(Identifer id, Instruction *op) -> Instruction * {
            std::array<Instruction *, 1> ops;
            ops[0] = op;
            llvm::MutableArrayRef<Instruction *> opsRef(ops);
            UniqueIdentifier uid{id, opsRef};
            return (*this)[uid];
        }
        template <size_t N>
        auto argMapLoopup(Identifer id, std::array<Instruction *, N> ops)
            -> Instruction * {
            llvm::MutableArrayRef<Instruction *> opsRef(ops);
            UniqueIdentifier uid{id, opsRef};
            return (*this)[uid];
        }
        auto argMapLoopup(Identifer id, Instruction *op0, Instruction *op1,
                          Instruction *op2) -> Instruction * {
            return argMapLoopup<3>(id, {op0, op1, op2});
        }
        auto createInstruction(llvm::BumpPtrAllocator &alloc,
                               UniqueIdentifier uid, llvm::Type *type)
            -> Instruction * {
            auto *i = new (alloc) Instruction(uid, type);
            for (auto *op : i->operands)
                op->users.push_back(i);
            argMap.insert({uid, i});
            return i;
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc, UniqueIdentifier uid,
                            llvm::Type *type) {
            if (auto *i = (*this)[uid])
                return i;
            return createInstruction(alloc, uid, type);
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc, UniqueIdentifier uid,
                            llvm::Type *type, Predicate::Set pred) {
            if (auto *i = (*this)[uid])
                return i;
            auto *i = createInstruction(alloc, uid, type);
            i->predicates = std::move(pred);
            return i;
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc, Identifer id,
                            llvm::Type *type) {
            UniqueIdentifier uid{id, {}};
            return getInstruction(alloc, uid, type);
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc, Identifer id,
                            Instruction *op0, llvm::Type *type) {
            // stack allocate for check
            if (auto *i = argMapLoopup(id, op0))
                return i;
            auto **operands = alloc.Allocate<Instruction *>(1);
            operands[0] = op0;
            llvm::MutableArrayRef<Instruction *> ops(operands, 1);
            UniqueIdentifier uid{id, ops};
            return createInstruction(alloc, uid, type);
        }
        template <size_t N>
        auto getInstruction(llvm::BumpPtrAllocator &alloc, Identifer id,
                            std::array<Instruction *, N> ops,
                            llvm::Type *type) {
            // stack allocate for check
            if (auto *i = argMapLoopup(id, ops))
                return i;
            auto **operands = alloc.Allocate<Instruction *>(2);
            for (size_t n = 0; n < N; n++)
                operands[n] = ops[n];
            llvm::MutableArrayRef<Instruction *> mops(operands, N);
            UniqueIdentifier uid{id, mops};
            return createInstruction(alloc, uid, type);
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc, Identifer id,
                            Instruction *op0, Instruction *op1,
                            Instruction *op2, llvm::Type *type) {
            // stack allocate for check
            if (auto *i = argMapLoopup(id, op0, op1, op2))
                return i;
            auto **operands = alloc.Allocate<Instruction *>(2);
            operands[0] = op0;
            operands[1] = op1;
            operands[2] = op2;
            llvm::MutableArrayRef<Instruction *> ops(operands, 3);
            UniqueIdentifier uid{id, ops};
            return createInstruction(alloc, uid, type);
        }

        /// This is the API for creating new instructions
        auto getInstruction(llvm::BumpPtrAllocator &alloc,
                            llvm::Instruction *instr) -> Instruction * {
            if (Instruction *i = (*this)[instr])
                return i;
            UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, instr)};
            auto *i = getInstruction(alloc, uid, instr->getType());
            llvmToInternalMap[instr] = i;
            return i;
        }
        auto getInstruction(llvm::BumpPtrAllocator &alloc,
                            Predicate::Map &predMap, llvm::Instruction *instr)
            -> Instruction *;
        auto getInstruction(llvm::BumpPtrAllocator &alloc, llvm::Value *v)
            -> Instruction * {
            if (Instruction *i = (*this)[v])
                return i;
            UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, v)};
            auto *i = getInstruction(alloc, uid, v->getType());
            llvmToInternalMap[v] = i;
            return i;
        }
        // if not in predMap, then operands don't get added, and
        // it won't be added to the argMap
        auto getInstruction(llvm::BumpPtrAllocator &alloc,
                            Predicate::Map &predMap, llvm::Value *v)
            -> Instruction *;
        [[nodiscard]] auto contains(llvm::Value *v) const -> bool {
            return llvmToInternalMap.count(v);
        }
        auto createConstant(llvm::BumpPtrAllocator &alloc, llvm::Type *type,
                            int64_t c) -> Instruction * {
            UniqueIdentifier uid{Identifer(c), {}};
            auto argMatch = argMap.find(uid);
            if (argMatch != argMap.end())
                return argMatch->second;
            return new (alloc) Instruction(uid, type);
        }
        auto getConstant(llvm::BumpPtrAllocator &alloc, llvm::Type *type,
                         int64_t c) -> Instruction * {
            UniqueIdentifier uid{Identifer(c), {}};
            if (auto *i = (*this)[uid])
                return i;
            return createConstant(alloc, type, c);
        }
        auto createCondition(llvm::BumpPtrAllocator &alloc,
                             Predicate::Relation rel, Instruction *instr,
                             bool swap = false) -> Instruction * {
            switch (rel) {
            case Predicate::Relation::Any:
                return getConstant(alloc, instr->getType(), 1);
            case Predicate::Relation::Empty:
                return getConstant(alloc, instr->getType(), 0);
            case Predicate::Relation::False:
                swap = !swap;
                [[fallthrough]];
            case Predicate::Relation::True:
                return swap ? instr->negate(alloc, *this) : instr;
            }
        }
        auto createCondition(llvm::BumpPtrAllocator &alloc,
                             Predicate::Intersection pred, bool swap)
            -> Instruction * {
            size_t popCount = pred.popCount();
            if (popCount == 0) {
                // everything is true
                return getConstant(alloc, predicates[0]->getType(), 1);
            } else if (popCount == 1) {
                size_t ind = pred.getFirstIndex();
                Instruction *I = predicates[ind];
                return swap ? I->negate(alloc, *this) : I;
            }
            // we have more than one instruction
            auto And = Intrinsic(Intrinsic::OpCode{llvm::Instruction::And});
            size_t ind = pred.getFirstIndex();
            Instruction *I = predicates[ind];
            ind = pred.getNextIndex(ind);
            // we keep I &= predicates[ind] until ind is invalid
            // ind will be >= 32 when it is invalid
            // getNextIndex will return a valid answer at least once, because
            // popCount > 1
            // there may be a better order than folding from the left
            // e.g. a binary tree could allow for more out of order execution
            // but I think a later pass should handle that sort of associativity
            do {
                I = getInstruction(alloc, And, I, predicates[ind],
                                   I->getType());
                ind = pred.getNextIndex(ind);
            } while (ind < 32);
            return I;
        }
        auto createSelect(llvm::BumpPtrAllocator &alloc, Instruction *A,
                          Instruction *B) -> Instruction * {
            auto id = Intrinsic(Intrinsic::OpCode{llvm::Instruction::Select});
            // TODO: make predicate's instruction vector shared among all in
            // LoopTree?
            // What I need here is to take the union of the predicates to form
            // the predicates of the new select instruction. Then, for the
            // select's `cond` instruction, I need something to indicate when to
            // take one path and not the other. We know the intersection is
            // empty, so -- why is it empty? We need something to slice that.
            // E.g.
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
            /// Then use the simpler of these two to determine the direction of
            /// the select.
            Predicate::Intersection P =
                A->predicates.getConflict(B->predicates);
            assert(!P.isEmpty() && "No conflict between predicates");
            bool swap = P.countFalse() <= P.countTrue();
            Instruction *cond = createCondition(alloc, P, swap);
            Instruction *op0 = swap ? B : A;
            Instruction *op1 = swap ? A : B;
            Instruction *S =
                getInstruction(alloc, id, cond, op0, op1, A->getType());
            S->predicates |= A->predicates;
            S->predicates |= B->predicates;
            return S;
        }
        /// completeInstruction
        /// when filling a predMap, we may initially not complete an instruction
        /// if it didn't appear inside the predMap if it is added later, we then
        /// need to finish adding its operands.
        auto completeInstruction(llvm::BumpPtrAllocator &, Predicate::Map &,
                                 llvm::Instruction *) -> Instruction *;
    };
    [[nodiscard]] auto static getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                                  Cache &cache,
                                                  llvm::Instruction *v)
        -> UniqueIdentifier {
        return std::make_pair(Intrinsic(v), getOperands(alloc, cache, v));
    }
    [[nodiscard]] auto getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                           Cache &cache) -> UniqueIdentifier {
        llvm::Instruction *I = getInstruction();
        return std::make_pair(id, getOperands(alloc, cache, I));
    }
    [[nodiscard]] auto static getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                                  Cache &cache, llvm::Value *v)
        -> UniqueIdentifier {
        if (auto *I = llvm::dyn_cast<llvm::Instruction>(v))
            return getUniqueIdentifier(alloc, cache, I);
        return {Intrinsic(v), {}};
    }
    [[nodiscard]] static auto
    getUniqueIdentifier(llvm::BumpPtrAllocator &alloc, Predicate::Map &predMap,
                        Cache &cache, llvm::Instruction *I)
        -> UniqueIdentifier {
        return std::make_pair(Intrinsic(I),
                              getOperands(alloc, predMap, cache, I));
    }
    [[nodiscard]] auto getUniqueIdentifier(llvm::BumpPtrAllocator &alloc,
                                           Predicate::Map &predMap,
                                           Cache &cache) -> UniqueIdentifier {
        llvm::Instruction *I = getInstruction();
        return std::make_pair(id, getOperands(alloc, predMap, cache, I));
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
            *p = cache.getInstruction(alloc, *OI);
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
            *p = cache.getInstruction(alloc, BBpreds, *OI);
        }
        return {operands, Nops};
    }
    static auto createIsolated(llvm::BumpPtrAllocator &alloc,
                               llvm::Instruction *instr) -> Instruction * {
        Intrinsic id{instr};
        auto *i = new (alloc) Instruction(id, instr->getType());
        return i;
    }

    auto negate(llvm::BumpPtrAllocator &alloc, Cache &cache) -> Instruction * {
        // first, check if its parent is a negation
        if (isInstruction(llvm::Instruction::Xor) && (getNumOperands() == 2)) {
            // !x where `x isa bool` is represented as `x ^ true`
            auto *op0 = getOperand(0);
            auto *op1 = getOperand(1);
            if (op1->isConstantOneInt()) {
                return op0;
            } else if (op0->isConstantOneInt()) {
                return op1;
            }
        }
        Instruction *one = cache.getConstant(alloc, getType(), 1);
        Identifer Xor = Intrinsic(Intrinsic::OpCode{llvm::Instruction::Xor});
        return cache.getInstruction(alloc, Xor, this, one, getType());
    }
    [[nodiscard]] auto isInstruction(llvm::Intrinsic::ID op) const -> bool {
        const Intrinsic *intrin = std::get_if<Intrinsic>(&id);
        if (!intrin)
            return false;
        return intrin->isInstruction(op);
    }
    [[nodiscard]] auto isIntrinsic(Intrinsic op) const -> bool {
        const Intrinsic *intrin = std::get_if<Intrinsic>(&id);
        if (!intrin)
            return false;
        return *intrin == op;
    }
    [[nodiscard]] auto isIntrinsic(llvm::Intrinsic::ID op) const -> bool {
        const Intrinsic *intrin = std::get_if<Intrinsic>(&id);
        if (!intrin)
            return false;
        return intrin->isIntrinsicInstruction(op);
    }

    [[nodiscard]] auto isLoad() const -> bool {
        return isInstruction(llvm::Instruction::Load);
    }
    [[nodiscard]] auto isStore() const -> bool {
        return isInstruction(llvm::Instruction::Store);
    }
    [[nodiscard]] auto isLoadOrStore() const -> bool {
        return isLoad() || isStore();
    }
    /// fall back in case we need value operand
    // [[nodiscard]] auto isValue() const -> bool { return id.isValue(); }
    [[nodiscard]] auto isShuffle() const -> bool {
        return isInstruction(llvm::Instruction::ShuffleVector);
    }
    [[nodiscard]] auto isFcmp() const -> bool {
        return isInstruction(llvm::Instruction::FCmp);
    }
    [[nodiscard]] auto isIcmp() const -> bool {
        return isInstruction(llvm::Instruction::ICmp);
    }
    [[nodiscard]] auto isCmp() const -> bool { return isFcmp() || isIcmp(); }
    [[nodiscard]] auto isSelect() const -> bool {
        return isInstruction(llvm::Instruction::Select);
    }
    [[nodiscard]] auto isExtract() const -> bool {
        return isInstruction(llvm::Instruction::ExtractElement);
    }
    [[nodiscard]] auto isInsert() const -> bool {
        return isInstruction(llvm::Instruction::InsertElement);
    }
    [[nodiscard]] auto isExtractValue() const -> bool {
        return isInstruction(llvm::Instruction::ExtractValue);
    }
    [[nodiscard]] auto isInsertValue() const -> bool {
        return isInstruction(llvm::Instruction::InsertValue);
    }
    [[nodiscard]] auto isFMul() const -> bool {
        return isInstruction(llvm::Instruction::FMul);
    }
    [[nodiscard]] auto isFNeg() const -> bool {
        return isInstruction(llvm::Instruction::FNeg);
    }
    [[nodiscard]] auto isFMulOrFNegOfFMul() const -> bool {
        return isFMul() || (isFNeg() && operands.front()->isFMul());
    }
    [[nodiscard]] auto isFAdd() const -> bool {
        return isInstruction(llvm::Instruction::FAdd);
    }
    [[nodiscard]] auto isFSub() const -> bool {
        return isInstruction(llvm::Instruction::FSub);
    }
    [[nodiscard]] auto allowsContract() const -> bool {
        if (auto m = getInstruction())
            return m->getFastMathFlags().allowContract();
        return false;
    }
    [[nodiscard]] auto isMulAdd() const -> bool {
        return isIntrinsic(llvm::Intrinsic::fmuladd) ||
               isIntrinsic(llvm::Intrinsic::fma);
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
    [[nodiscard]] auto getIntrinsic() const -> Optional<const Intrinsic *> {
        if (auto i = std::get_if<Intrinsic>(&id))
            return i;
        return {};
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
                            Intrinsic::OpCode id, unsigned int vectorWidth) {
        auto op0info = getOperandInfo(TTI, 0);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info),
                TTI.getArithmeticInstrCost(
                    id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)
        }
    }
    RecipThroughputLatency
    calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                             Intrinsic::OpCode id, unsigned int vectorWidth) {
        auto op0info = getOperandInfo(TTI, 0);
        auto op1info = getOperandInfo(TTI, 1);
        llvm::Type *T = getType(vectorWidth);
        return {
            TTI.getArithmeticInstrCost(
                id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
                op0info, op1info),
                TTI.getArithmeticInstrCost(
                    id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info,
                    op1info)
        }
    }
#else
    [[nodiscard]] auto getOperandInfo(unsigned int i) const
        -> std::pair<llvm::TargetTransformInfo::OperandValueKind,
                     llvm::TargetTransformInfo::OperandValueProperties> {
        Instruction *opi = (operands)[i];
        if (auto c =
                llvm::dyn_cast_or_null<llvm::ConstantInt>(opi->getValue())) {
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
        return std::make_pair(llvm::TargetTransformInfo::OK_AnyValue,
                              llvm::TargetTransformInfo::OP_None);
    }
    auto calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                 Intrinsic::OpCode id, unsigned int vectorWidth)
        -> RecipThroughputLatency {
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
    auto calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                  Intrinsic::OpCode id,
                                  unsigned int vectorWidth)
        -> RecipThroughputLatency {
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
        if (operandIsLoad() || userIsStore())
            return llvm::TargetTransformInfo::CastContextHint::Normal;
        if (auto cast = llvm::dyn_cast_or_null<llvm::CastInst>(getValue()))
            return TTI.getCastContextHint(cast);
        // TODO: check for whether mask, interleave, or reversed is likely.
        return llvm::TargetTransformInfo::CastContextHint::None;
    }
    auto calcCastCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode id,
                      unsigned int vectorWidth) -> RecipThroughputLatency {
        llvm::Type *srcT = getType(operands.front()->type, vectorWidth);
        llvm::Type *dstT = getType(vectorWidth);
        llvm::TargetTransformInfo::CastContextHint ctx = getCastContext(TTI);
        return {TTI.getCastInstrCost(
                    id.id, dstT, srcT, ctx,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCastInstrCost(id.id, dstT, srcT, ctx,
                                     llvm::TargetTransformInfo::TCK_Latency)};
    }
    [[nodiscard]] auto getPredicate() const -> llvm::CmpInst::Predicate {
        if (isSelect())
            return operands.front()->getPredicate();
        assert(isCmp());
        if (auto cmp = llvm::dyn_cast_or_null<llvm::CmpInst>(getValue()))
            return cmp->getPredicate();
        return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                        : llvm::CmpInst::BAD_ICMP_PREDICATE;
    }
    auto calcCmpSelectCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode id,
                           unsigned int vectorWidth) -> RecipThroughputLatency {
        llvm::Type *T = getType(vectorWidth);
        llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
        llvm::CmpInst::Predicate pred = getPredicate();
        return {TTI.getCmpSelInstrCost(
                    id.id, T, cmpT, pred,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCmpSelInstrCost(id.id, T, cmpT, pred,
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
    auto calcCallCost(llvm::TargetTransformInfo &TTI, Intrinsic::Intrin intrin,
                      unsigned int vectorWidth) -> RecipThroughputLatency {
        llvm::Type *T = getType(vectorWidth);
        llvm::SmallVector<llvm::Type *, 4> argTypes;
        for (auto op : operands)
            argTypes.push_back(op->getType(vectorWidth));
        if (intrin.id == llvm::Intrinsic::not_intrinsic) {
            // we shouldn't be hitting here
            return {
                TTI.getCallInstrCost(
                    getFunction(), T, argTypes,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getCallInstrCost(getFunction(), T, argTypes,
                                     llvm::TargetTransformInfo::TCK_Latency)};
        } else {
            llvm::IntrinsicCostAttributes attr(intrin.id, T, argTypes);
            return {TTI.getIntrinsicInstrCost(
                        attr, llvm::TargetTransformInfo::TCK_RecipThroughput),
                    TTI.getIntrinsicInstrCost(
                        attr, llvm::TargetTransformInfo::TCK_Latency)};
        }
    }
    auto calcCallCost(llvm::TargetTransformInfo &TTI, llvm::Function *F,
                      unsigned int vectorWidth) -> RecipThroughputLatency {
        llvm::Type *T = getType(vectorWidth);
        llvm::SmallVector<llvm::Type *, 4> argTypes;
        for (auto op : operands)
            argTypes.push_back(op->getType(vectorWidth));
        return {
            TTI.getCallInstrCost(
                F, T, argTypes, llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getCallInstrCost(getFunction(), T, argTypes,
                                 llvm::TargetTransformInfo::TCK_Latency)};
    }
    struct ExtractAlignment {
        constexpr auto operator()(std::monostate) -> llvm::Align {
            return llvm::Align{};
        }
        auto operator()(llvm::Value *v) -> llvm::Align {
            if (auto load = llvm::dyn_cast_or_null<llvm::LoadInst>(v)) {
                return load->getAlign();
            } else if (auto store =
                           llvm::dyn_cast_or_null<llvm::StoreInst>(v)) {
                return store->getAlign();
            } else {
                return {};
            }
        }
        auto operator()(ArrayReference *ref) const -> llvm::Align {
            return ref->getAlign();
        }
    };
    auto calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                          Intrinsic::OpCode id,
                                          unsigned int vectorWidth)
        -> RecipThroughputLatency {
        constexpr unsigned int AddressSpace = 0;
        llvm::Type *T = getType(vectorWidth);
        llvm::Align alignment = std::visit(ExtractAlignment{}, ptr);
        if (predicates.size() == 0) {
            return {
                TTI.getMemoryOpCost(
                    id.id, T, alignment, AddressSpace,
                    llvm::TargetTransformInfo::TCK_RecipThroughput),
                TTI.getMemoryOpCost(id.id, T, alignment, AddressSpace,
                                    llvm::TargetTransformInfo::TCK_Latency)};
        } else {
            return {TTI.getMaskedMemoryOpCost(
                        id.id, T, alignment, AddressSpace,
                        llvm::TargetTransformInfo::TCK_RecipThroughput),
                    TTI.getMaskedMemoryOpCost(
                        id.id, T, alignment, AddressSpace,
                        llvm::TargetTransformInfo::TCK_Latency)};
        }
    }
    auto calculateCostFAddFSub(llvm::TargetTransformInfo &TTI,
                               Intrinsic::OpCode id, unsigned int vectorWidth)
        -> RecipThroughputLatency {
        // TODO: allow not assuming hardware FMA support
        if (((operands)[0]->isFMulOrFNegOfFMul() ||
             (operands)[1]->isFMulOrFNegOfFMul()) &&
            allowsContract())
            return {};
        return calcBinaryArithmeticCost(TTI, id, vectorWidth);
    }
    auto allUsersAdditiveContract() -> bool {
        for (auto u : users)
            if (!(((u->isFAdd()) || (u->isFSub())) && (u->allowsContract())))
                return false;
        return true;
    }
    auto calculateFNegCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode id,
                           unsigned int vectorWidth) -> RecipThroughputLatency {

        if (operands.front()->isFMul() && allUsersAdditiveContract())
            return {};
        return calcUnaryArithmeticCost(TTI, id, vectorWidth);
    }
    [[nodiscard]] auto isConstantOneInt() const -> bool {
        if (const long *c = std::get_if<int64_t>(&id))
            return *c == 1;
        return false;
    }
    [[nodiscard]] auto calculateCost(llvm::TargetTransformInfo &TTI,
                                     unsigned int vectorWidth)
        -> RecipThroughputLatency {
        if (Optional<const Intrinsic *> id = getIntrinsic()) {
            return calculateCost(*id, TTI, vectorWidth);
        } else if (auto *F = getFunction()) {
            return calcCallCost(TTI, F, vectorWidth);
        } else {
            return {};
        }
    }
    [[nodiscard]] auto calculateCost(Intrinsic id,
                                     llvm::TargetTransformInfo &TTI,
                                     unsigned int vectorWidth)
        -> RecipThroughputLatency {
        switch (id.opcode.id) {
        case llvm::Instruction::FAdd:
        case llvm::Instruction::FSub:
            return calculateCostFAddFSub(TTI, id.opcode, vectorWidth);
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
            return calcBinaryArithmeticCost(TTI, id.opcode, vectorWidth);
        case llvm::Instruction::FNeg:
            // one arg arithmetic cost
            return calculateFNegCost(TTI, id.opcode, vectorWidth);
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
            return calcCastCost(TTI, id.opcode, vectorWidth);
        case llvm::Instruction::ICmp:
        case llvm::Instruction::FCmp:
        case llvm::Instruction::Select:
            return calcCmpSelectCost(TTI, id.opcode, vectorWidth);
        case llvm::Instruction::Call:
            return calcCallCost(TTI, id.intrin, vectorWidth);
        case llvm::Instruction::Load:
        case llvm::Instruction::Store:
            return calculateCostContiguousLoadStore(TTI, id.opcode,
                                                    vectorWidth);
        default:
            return RecipThroughputLatency::getInvalid();
        }
    }
    [[nodiscard]] auto isCommutativeCall() const -> bool {
        if (auto *intrin =
                llvm::dyn_cast_or_null<llvm::IntrinsicInst>(getInstruction()))
            return intrin->isCommutative();
        return false;
    }
    [[nodiscard]] auto associativeOperandsFlag() const -> uint8_t {
        Optional<const Intrinsic *> idop = getIntrinsic();
        if (!idop)
            return 0;
        switch (idop->opcode.id) {
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
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void replaceOperand(Instruction *old, Instruction *new_) {
        for (auto &&op : operands)
            if (op == old)
                op = new_;
    }
    /// replace all uses of `*this` with `*I`.
    /// Assumes that `*I` does not depend on `*this`.
    void replaceAllUsesWith(Instruction *I) {
        for (auto u : users) {
            assert(u != I);
            u->replaceOperand(this, I);
            I->users.push_back(u);
        }
    }
    /// replace all uses of `*this` with `*I`, except for `*I` itself.
    /// This is useful when replacing `*this` with `*I = f(*this)`
    /// E.g., when merging control flow branches, where `f` may be a select
    void replaceAllOtherUsesWith(Instruction *I) {
        for (auto u : users) {
            if (u != I) {
                u->replaceOperand(this, I);
                I->users.push_back(u);
            }
        }
    }
    auto replaceAllUsesOf(Instruction *I) -> Instruction * {
        for (auto u : I->users) {
            assert(u != this);
            u->replaceOperand(I, this);
            users.push_back(u);
        }
        return this;
    }
    auto replaceAllOtherUsesOf(Instruction *I) -> Instruction * {
        for (auto u : I->users) {
            if (u != this) {
                u->replaceOperand(I, this);
                users.push_back(u);
            }
        }
        return this;
    }
};

/// Provide DenseMapInfo for Identifier.
template <> struct llvm::DenseMapInfo<Instruction::Intrinsic, void> {
    static inline auto getEmptyKey() -> ::Instruction::Intrinsic {
        auto K = llvm::DenseMapInfo<llvm::Intrinsic::ID>::getEmptyKey();
        return ::Instruction::Intrinsic{::Instruction::Intrinsic::OpCode{K},
                                        ::Instruction::Intrinsic::Intrin{K}};
    }

    static inline auto getTombstoneKey() -> ::Instruction::Intrinsic {
        auto K = llvm::DenseMapInfo<llvm::Intrinsic::ID>::getTombstoneKey();
        return ::Instruction::Intrinsic{::Instruction::Intrinsic::OpCode{K},
                                        ::Instruction::Intrinsic::Intrin{K}};
    }

    static auto getHashValue(const ::Instruction::Intrinsic &Key) -> unsigned;

    static auto isEqual(const ::Instruction::Intrinsic &LHS,
                        const ::Instruction::Intrinsic &RHS) -> bool {
        return LHS == RHS;
    }
};

namespace Predicate {
struct Map {
    llvm::MapVector<llvm::BasicBlock *, Set> map;
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
    [[nodiscard]] auto operator[](llvm::BasicBlock *bb) -> std::optional<Set> {
        auto it = map.find(bb);
        if (it == map.end())
            return std::nullopt;
        return it->second;
    }
    [[nodiscard]] auto operator[](llvm::Instruction *inst)
        -> std::optional<Set> {
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
    void clear() { map.clear(); }
    // void visit(llvm::BasicBlock *BB) { map.insert(std::make_pair(BB,
    // Set())); } void visit(llvm::Instruction *inst) {
    // visit(inst->getParent()); }
    [[nodiscard]] auto addPredicate(llvm::BumpPtrAllocator &alloc,
                                    Instruction::Cache &cache,
                                    llvm::Value *value) -> size_t {
        auto *I = cache.getInstruction(alloc, *this, value);
        assert(cache.predicates.size() <= 32 && "too many predicates");
        for (size_t i = 0; i < cache.predicates.size(); ++i)
            if (cache.predicates[i] == I)
                return i;
        size_t i = cache.predicates.size();
        assert(cache.predicates.size() != 32 && "too many predicates");
        cache.predicates.emplace_back(I);
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
    // 1. see why L->contains(BBsrc) does not work; does it only contain BBs
    // in it directly, and not nested another loop deeper?
    // 2. We are ignoring cycles for now; we must ensure this is done
    // correctly
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
            // oops, we seem to have skipped the preheader and escaped the
            // loop.
            return Destination::Returned;
        } else if (visited.contains(BBsrc)) {
            // FIXME: This is terribly hacky.
            // if `BBsrc == BBhead`, then we assume we hit a path that
            // bypasses the following loop, e.g. there was a loop guard.
            //
            // Thus, we return `Returned`, indicating that it was a
            // non-fatal dead-end. Otherwise, we check if it seems to have
            // led to a live, non-empty path.
            // TODO: should we union the predicates in case of returned?
            if ((BBsrc != BBhead) && predMap.find(BBsrc) != predMap.rend())
                return Destination::Reached;
            return Destination::Returned;
        }
        // Inserts a tombstone to indicate that we have visited BBsrc, but
        // not actually reached a destination.
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

auto Instruction::Cache::getInstruction(llvm::BumpPtrAllocator &alloc,
                                        Predicate::Map &predMap,
                                        llvm::Instruction *instr)
    -> Instruction * {
    if (Instruction *i = completeInstruction(alloc, predMap, instr))
        return i;
    UniqueIdentifier uid{getUniqueIdentifier(alloc, predMap, *this, instr)};
    auto *i = getInstruction(alloc, uid, instr->getType());
    llvmToInternalMap[instr] = i;
    return i;
}
auto Instruction::Cache::completeInstruction(llvm::BumpPtrAllocator &alloc,
                                             Predicate::Map &predMap,
                                             llvm::Instruction *I)
    -> Instruction * {
    Instruction *i = (*this)[I];
    if (!i)
        return nullptr;
    // if `i` has operands, or if it isn't supposed to, it's been completed
    if ((i->operands.size() > 0) || (I->getNumOperands() == 0))
        return i;
    // instr is non-null and has operands
    // maybe instr isn't in BBpreds?
    if (std::optional<Predicate::Set> pred = predMap[I]) {
        // instr is in BBpreds, therefore, we now complete `i`.
        i->predicates = std::move(*pred);
        // we use dummy operands to avoid infinite recursion
        // the i->operands.size() > 0 check above will block this
        i->operands = llvm::MutableArrayRef<Instruction *>{nullptr, 1};
        i->operands = getOperands(alloc, predMap, *this, I);
        for (auto *op : i->operands)
            op->users.push_back(i);
    }
    return i;
}
auto Instruction::Cache::getInstruction(llvm::BumpPtrAllocator &alloc,
                                        Predicate::Map &predMap, llvm::Value *v)
    -> Instruction * {

    if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v))
        return getInstruction(alloc, predMap, instr);
    return getInstruction(alloc, v);
}
/*
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
*/
// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;
