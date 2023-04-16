#pragma once

#include "./Address.hpp"
#include "./Predicate.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseMapInfo.h>
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
#include <tuple>
#include <utility>
#include <variant>

auto containsCycle(const llvm::Instruction *,
                   llvm::SmallPtrSet<llvm::Instruction const *, 8> &,
                   const llvm::Value *) -> bool;
inline auto
containsCycleCore(const llvm::Instruction *J,
                  llvm::SmallPtrSet<llvm::Instruction const *, 8> &visited,
                  const llvm::Instruction *K) -> bool {
  for (const llvm::Use &op : K->operands())
    if (containsCycle(J, visited, op.get())) return true;
  return false;
}
inline auto
containsCycle(const llvm::Instruction *J,
              llvm::SmallPtrSet<llvm::Instruction const *, 8> &visited,
              const llvm::Value *V) -> bool {
  const auto *S = llvm::dyn_cast<llvm::Instruction>(V);
  if (S == J) return true;
  if ((!S) || (visited.count(S))) return false;
  visited.insert(S);
  return containsCycleCore(J, visited, S);
}

inline auto containsCycle(llvm::Instruction const *S) -> bool {
  // don't get trapped in a different cycle
  llvm::SmallPtrSet<llvm::Instruction const *, 8> visited;
  return containsCycleCore(S, visited, S);
}

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
      llvm::Intrinsic::ID id; //{llvm::Intrinsic::not_intrinsic};
      constexpr auto operator==(const OpCode &other) const -> bool {
        return id == other.id;
      }
    };
    struct Intrin {
      llvm::Intrinsic::ID id; //{llvm::Intrinsic::not_intrinsic};
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
    constexpr Intrinsic(OpCode op, Intrin intr) : opcode(op), intrin(intr) {}
    constexpr Intrinsic(OpCode op) : opcode(op) {}
    constexpr Intrinsic() = default;
    [[nodiscard]] constexpr auto isInstruction(OpCode opCode) const -> bool {
      return opcode == opCode;
    }
    [[nodiscard]] constexpr auto isInstruction(unsigned opCode) const -> bool {
      return isInstruction(OpCode{opCode});
    }
    [[nodiscard]] constexpr auto isIntrinsicInstruction(Intrin opCode) const
      -> bool {
      return intrin == opCode;
    }
    [[nodiscard]] constexpr auto isIntrinsicInstruction(unsigned opCode) const
      -> bool {
      return isIntrinsicInstruction(Intrin{opCode});
    }
    [[nodiscard]] constexpr auto operator==(const Intrinsic &other) const
      -> bool {
      return opcode == other.opcode && intrin == other.intrin;
    }
  };
  struct IdentifierEmpty {
    constexpr auto operator==(const IdentifierEmpty &) const -> bool {
      return true;
    }
  };
  struct IdentifierTombstone {
    constexpr auto operator==(const IdentifierTombstone &) const -> bool {
      return true;
    }
  };
  using Identifier = std::variant<Intrinsic, llvm::Function *, int64_t, double,
                                  IdentifierEmpty, IdentifierTombstone>;
  using UniqueIdentifier =
    std::pair<Identifier, llvm::MutableArrayRef<Instruction *>>;

  [[no_unique_address]] Identifier idtf;
  // Intrinsic id;
  [[no_unique_address]] llvm::Type *type;
  [[no_unique_address]] std::variant<std::monostate, llvm::Instruction *,
                                     llvm::ConstantInt *, llvm::ConstantFP *,
                                     Address *>
    ptr;
  [[no_unique_address]] Predicate::Set predicates;
  [[no_unique_address]] llvm::MutableArrayRef<Instruction *> operands;
  // [[no_unique_address]] llvm::SmallVector<Instruction *> users;
  [[no_unique_address]] llvm::SmallPtrSet<Instruction *, 8> users;
  /// costs[i] == cost for vector-width 2^i
  [[no_unique_address]] llvm::SmallVector<RecipThroughputLatency> costs;

  void setOperands(llvm::MutableArrayRef<Instruction *> ops) {
    operands = ops;
    for (auto op : ops) op->users.insert(this);
  }

  static auto getIdentifier(llvm::Instruction *S) -> Identifier {
    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(S))
      if (auto *F = CB->getCalledFunction()) return F;
    return Intrinsic(S);
  }
  static auto getIdentifier(llvm::ConstantInt *S) -> Identifier {
    return S->getSExtValue();
  }
  static auto getIdentifier(llvm::ConstantFP *S) -> Identifier {
    return S->getValueAPF().convertToDouble();
  }
  static auto getIdentifier(llvm::Value *v) -> std::optional<Identifier> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return getIdentifier(i);
    else if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(v))
      return getIdentifier(ci);
    else if (auto *cfp = llvm::dyn_cast<llvm::ConstantFP>(v))
      return getIdentifier(cfp);
    else return std::nullopt;
  }
  [[nodiscard]] auto getOpType() const -> std::pair<Intrinsic, llvm::Type *> {
    if (auto i = getIntrinsic()) return std::make_pair(*i, type);
    return std::make_pair(Intrinsic(), type);
  }
  [[nodiscard]] auto isIntrinsic() const -> bool {
    return std::holds_alternative<Intrinsic>(idtf);
  }
  [[nodiscard]] auto isFunction() const -> bool {
    return std::holds_alternative<llvm::Function *>(idtf);
  }
  [[nodiscard]] auto isConstantInt() const -> bool {
    return std::holds_alternative<int64_t>(idtf);
  }
  [[nodiscard]] auto isConstantFP() const -> bool {
    return std::holds_alternative<double>(idtf);
  }
  [[nodiscard]] auto isConstant() const -> bool {
    return isConstantInt() || isConstantFP();
  }

  /// Check if the ptr is a load or store, without an ArrayRef
  [[nodiscard]] auto isValueLoadOrStore() const -> bool {
    if (llvm::Instruction *const *J = std::get_if<llvm::Instruction *>(&ptr))
      return llvm::isa<llvm::LoadInst>(*J) || llvm::isa<llvm::StoreInst>(*J);
    return false;
  }
  [[nodiscard]] auto getFunction() const -> llvm::Function * {
    if (llvm::Function *const *F = std::get_if<llvm::Function *>(&idtf))
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
  [[nodiscard]] auto getUsers() -> llvm::SmallPtrSetImpl<Instruction *> & {
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
    auto operator()(Address *v) const -> llvm::Value * {
      return v->getInstruction();
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
      if (auto *J = llvm::dyn_cast<llvm::Instruction>(v)) return J->getParent();
      return nullptr;
    }
    auto operator()(Address *v) const -> llvm::BasicBlock * {
      return v->getInstruction()->getParent();
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
  Instruction(Intrinsic idt, llvm::Type *typ) : idtf(idt), type(typ) {}
  // Instruction(UniqueIdentifier uid)
  // : id(std::get<0>(uid)), operands(std::get<1>(uid)) {}
  Instruction(UniqueIdentifier uid, llvm::Type *typ)
    : idtf(std::get<0>(uid)), type(typ), operands(std::get<1>(uid)) {}
  struct Cache {
    [[no_unique_address]] llvm::DenseMap<llvm::Value *, Instruction *>
      llvmToInternalMap;
    [[no_unique_address]] llvm::DenseMap<UniqueIdentifier, Instruction *>
      argMap;
    [[no_unique_address]] llvm::SmallVector<Instruction *> predicates;
    // tmp is used in case we don't need an allocation
    // [[no_unique_address]] Instruction *tmp{nullptr};
    // auto allocate(BumpAlloc<> &alloc, Intrinsic id,
    //               llvm::Type *type) -> Instruction * {
    //     if (tmp) {
    //         tmp->id = id;
    //         tmp->type = type;
    //         Instruction *I = tmp;
    //         tmp = nullptr;
    //         return I;
    //     } else {
    //         return new (alloc) Instruction(id, type);
    //     }
    // }
    auto operator[](llvm::Value *v) -> Instruction * {
      auto f = llvmToInternalMap.find(v);
      if (f != llvmToInternalMap.end()) return f->second;
      return nullptr;
    }
    auto operator[](UniqueIdentifier uid) -> Instruction * {
      auto f = argMap.find(uid);
      if (f != argMap.end()) return f->second;
      return nullptr;
    }
    auto argMapLoopup(Identifier idt) -> Instruction * {
      UniqueIdentifier uid{idt, {}};
      return (*this)[uid];
    }
    auto argMapLoopup(Identifier idt, Instruction *op) -> Instruction * {
      std::array<Instruction *, 1> ops;
      ops[0] = op;
      llvm::MutableArrayRef<Instruction *> opsRef(ops);
      UniqueIdentifier uid{idt, opsRef};
      return (*this)[uid];
    }
    template <size_t N>
    auto argMapLoopup(Identifier idt, std::array<Instruction *, N> ops)
      -> Instruction * {
      llvm::MutableArrayRef<Instruction *> opsRef(ops);
      UniqueIdentifier uid{idt, opsRef};
      return (*this)[uid];
    }
    auto argMapLoopup(Identifier idt, Instruction *op0, Instruction *op1)
      -> Instruction * {
      return argMapLoopup<2>(idt, {op0, op1});
    }
    auto argMapLoopup(Identifier idt, Instruction *op0, Instruction *op1,
                      Instruction *op2) -> Instruction * {
      return argMapLoopup<3>(idt, {op0, op1, op2});
    }
    auto createInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                           llvm::Type *typ) -> Instruction * {
      auto *i = new (alloc) Instruction(uid, typ);
      for (auto *op : i->operands) op->users.insert(i);
      argMap.insert({uid, i});
      return i;
    }
    auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                        llvm::Type *typ) {
      if (auto *i = (*this)[uid]) return i;
      return createInstruction(alloc, uid, typ);
    }
    auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                        llvm::Type *typ, Predicate::Set pred) {
      if (auto *i = (*this)[uid]) return i;
      auto *i = createInstruction(alloc, uid, typ);
      i->predicates = std::move(pred);
      return i;
    }
    auto getInstruction(BumpAlloc<> &alloc, Identifier idt, llvm::Type *typ) {
      UniqueIdentifier uid{idt, {}};
      return getInstruction(alloc, uid, typ);
    }
    auto getInstruction(BumpAlloc<> &alloc, Identifier idt, Instruction *op0,
                        llvm::Type *typ) {
      // stack allocate for check
      if (auto *i = argMapLoopup(idt, op0)) return i;
      auto **opptr = alloc.allocate<Instruction *>(1);
      opptr[0] = op0;
      llvm::MutableArrayRef<Instruction *> ops(opptr, 1);
      UniqueIdentifier uid{idt, ops};
      return createInstruction(alloc, uid, typ);
    }
    template <size_t N>
    auto getInstruction(BumpAlloc<> &alloc, Identifier idt,
                        std::array<Instruction *, N> ops, llvm::Type *typ) {
      // stack allocate for check
      if (auto *i = argMapLoopup(idt, ops)) return i;
      auto **opptr = alloc.allocate<Instruction *>(2);
      for (size_t n = 0; n < N; n++) opptr[n] = ops[n];
      llvm::MutableArrayRef<Instruction *> mops(opptr, N);
      UniqueIdentifier uid{idt, mops};
      return createInstruction(alloc, uid, typ);
    }
    auto getInstruction(BumpAlloc<> &alloc, Identifier idt, Instruction *op0,
                        Instruction *op1, llvm::Type *typ) {
      return getInstruction<2>(alloc, idt, {op0, op1}, typ);
    }
    auto getInstruction(BumpAlloc<> &alloc, Identifier idt, Instruction *op0,
                        Instruction *op1, Instruction *op2, llvm::Type *typ) {
      return getInstruction<3>(alloc, idt, {op0, op1, op2}, typ);
    }

    /// This is the API for creating new instructions
    auto getInstruction(BumpAlloc<> &alloc, llvm::Instruction *instr)
      -> Instruction * {
      if (Instruction *i = (*this)[instr]) return i;
      UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, instr)};
      auto *i = getInstruction(alloc, uid, instr->getType());
      llvmToInternalMap[instr] = i;
      return i;
    }
    auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                        llvm::Instruction *instr) -> Instruction *;
    auto getInstruction(BumpAlloc<> &alloc, llvm::Value *v) -> Instruction * {
      if (Instruction *i = (*this)[v]) return i;
      UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, v)};
      auto *i = getInstruction(alloc, uid, v->getType());
      llvmToInternalMap[v] = i;
      return i;
    }
    // if not in predMap, then operands don't get added, and
    // it won't be added to the argMap
    auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                        llvm::Value *v) -> Instruction *;
    [[nodiscard]] auto contains(llvm::Value *v) const -> bool {
      return llvmToInternalMap.count(v);
    }
    auto createConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
      -> Instruction * {
      UniqueIdentifier uid{Identifier(c), {}};
      auto argMatch = argMap.find(uid);
      if (argMatch != argMap.end()) return argMatch->second;
      return new (alloc) Instruction(uid, typ);
    }
    auto getConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
      -> Instruction * {
      UniqueIdentifier uid{Identifier(c), {}};
      if (auto *i = (*this)[uid]) return i;
      return createConstant(alloc, typ, c);
    }
    auto createCondition(BumpAlloc<> &alloc, Predicate::Relation rel,
                         Instruction *instr, bool swap = false)
      -> Instruction * {
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
    auto createCondition(BumpAlloc<> &alloc, Predicate::Intersection pred,
                         bool swap) -> Instruction * {
      size_t popCount = pred.popCount();
      if (popCount == 0) {
        // everything is true
        return getConstant(alloc, predicates[0]->getType(), 1);
      } else if (popCount == 1) {
        size_t ind = pred.getFirstIndex();
        Instruction *J = predicates[ind];
        return swap ? J->negate(alloc, *this) : J;
      }
      // we have more than one instruction
      auto And = Intrinsic(Intrinsic::OpCode{llvm::Instruction::And});
      size_t ind = pred.getFirstIndex();
      Instruction *J = predicates[ind];
      ind = pred.getNextIndex(ind);
      // we keep I &= predicates[ind] until ind is invalid
      // ind will be >= 32 when it is invalid
      // getNextIndex will return a valid answer at least once, because
      // popCount > 1
      // there may be a better order than folding from the left
      // e.g. a binary tree could allow for more out of order execution
      // but I think a later pass should handle that sort of associativity
      do {
        J = getInstruction(alloc, And, J, predicates[ind], J->getType());
        ind = pred.getNextIndex(ind);
      } while (ind < 32);
      return J;
    }
    auto createSelect(BumpAlloc<> &alloc, Instruction *A, Instruction *B)
      -> Instruction * {
      auto idt = Intrinsic(Intrinsic::OpCode{llvm::Instruction::Select});
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
      Predicate::Intersection P = A->predicates.getConflict(B->predicates);
      assert(!P.isEmpty() && "No conflict between predicates");
      bool swap = P.countFalse() <= P.countTrue();
      Instruction *cond = createCondition(alloc, P, swap);
      Instruction *op0 = swap ? B : A;
      Instruction *op1 = swap ? A : B;
      Instruction *S = getInstruction(alloc, idt, cond, op0, op1, A->getType());
      S->predicates |= A->predicates;
      S->predicates |= B->predicates;
      return S;
    }
    /// completeInstruction
    /// when filling a predMap, we may initially not complete an instruction
    /// if it didn't appear inside the predMap if it is added later, we then
    /// need to finish adding its operands.
    auto completeInstruction(BumpAlloc<> &, Predicate::Map &,
                             llvm::Instruction *) -> Instruction *;
  };
  [[nodiscard]] auto static getUniqueIdentifier(BumpAlloc<> &alloc,
                                                Cache &cache,
                                                llvm::Instruction *v)
    -> UniqueIdentifier {
    return std::make_pair(Intrinsic(v), getOperands(alloc, cache, v));
  }
  [[nodiscard]] auto getUniqueIdentifier(BumpAlloc<> &alloc, Cache &cache)
    -> UniqueIdentifier {
    llvm::Instruction *J = getInstruction();
    return std::make_pair(idtf, getOperands(alloc, cache, J));
  }
  [[nodiscard]] auto static getUniqueIdentifier(BumpAlloc<> &alloc,
                                                Cache &cache, llvm::Value *v)
    -> UniqueIdentifier {
    if (auto *J = llvm::dyn_cast<llvm::Instruction>(v))
      return getUniqueIdentifier(alloc, cache, J);
    return {Intrinsic(v), {}};
  }
  [[nodiscard]] static auto
  getUniqueIdentifier(BumpAlloc<> &alloc, Predicate::Map &predMap, Cache &cache,
                      llvm::Instruction *J) -> UniqueIdentifier {
    return std::make_pair(Intrinsic(J), getOperands(alloc, predMap, cache, J));
  }
  [[nodiscard]] auto getUniqueIdentifier(BumpAlloc<> &alloc,
                                         Predicate::Map &predMap, Cache &cache)
    -> UniqueIdentifier {
    llvm::Instruction *J = getInstruction();
    return std::make_pair(idtf, getOperands(alloc, predMap, cache, J));
  }
  [[nodiscard]] static auto getOperands(BumpAlloc<> &alloc, Cache &cache,
                                        llvm::Instruction *instr)
    -> llvm::MutableArrayRef<Instruction *> {
    if (llvm::isa<llvm::LoadInst>(instr)) return {nullptr, size_t(0)};
    auto ops{instr->operands()};
    auto OI = ops.begin();
    // NOTE: operand 0 is the value operand of a store
    bool isStore = llvm::isa<llvm::StoreInst>(instr);
    auto OE = isStore ? (OI + 1) : ops.end();
    size_t numOps = isStore ? 1 : instr->getNumOperands();
    auto **operands = alloc.allocate<Instruction *>(numOps);
    Instruction **p = operands;
    for (; OI != OE; ++OI, ++p) *p = cache.getInstruction(alloc, *OI);
    return {operands, numOps};
  }
  [[nodiscard]] static auto getOperands(BumpAlloc<> &alloc,
                                        Predicate::Map &BBpreds, Cache &cache,
                                        llvm::Instruction *instr)
    -> llvm::MutableArrayRef<Instruction *> {
    if (llvm::isa<llvm::LoadInst>(instr)) return {nullptr, size_t(0)};
    auto ops{instr->operands()};
    auto OI = ops.begin();
    // NOTE: operand 0 is the value operand of a store
    bool isStore = llvm::isa<llvm::StoreInst>(instr);
    auto OE = isStore ? (OI + 1) : ops.end();
    size_t Nops = isStore ? 1 : instr->getNumOperands();
    auto **operands = alloc.allocate<Instruction *>(Nops);
    Instruction **p = operands;
    for (; OI != OE; ++OI, ++p) *p = cache.getInstruction(alloc, BBpreds, *OI);
    return {operands, Nops};
  }
  static auto createIsolated(BumpAlloc<> &alloc, llvm::Instruction *instr)
    -> Instruction * {
    Intrinsic id{instr};
    auto *i = new (alloc) Instruction(id, instr->getType());
    return i;
  }

  auto negate(BumpAlloc<> &alloc, Cache &cache) -> Instruction * {
    // first, check if its parent is a negation
    if (isInstruction(llvm::Instruction::Xor) && (getNumOperands() == 2)) {
      // !x where `x isa bool` is represented as `x ^ true`
      auto *op0 = getOperand(0);
      auto *op1 = getOperand(1);
      if (op1->isConstantOneInt()) return op0;
      else if (op0->isConstantOneInt()) return op1;
    }
    Instruction *one = cache.getConstant(alloc, getType(), 1);
    Identifier Xor = Intrinsic(Intrinsic::OpCode{llvm::Instruction::Xor});
    return cache.getInstruction(alloc, Xor, this, one, getType());
  }
  [[nodiscard]] auto isInstruction(llvm::Intrinsic::ID op) const -> bool {
    const Intrinsic *intrin = std::get_if<Intrinsic>(&idtf);
    if (!intrin) return false;
    return intrin->isInstruction(op);
  }
  [[nodiscard]] auto isIntrinsic(Intrinsic op) const -> bool {
    const Intrinsic *intrin = std::get_if<Intrinsic>(&idtf);
    if (!intrin) return false;
    return *intrin == op;
  }
  [[nodiscard]] auto isIntrinsic(llvm::Intrinsic::ID op) const -> bool {
    const Intrinsic *intrin = std::get_if<Intrinsic>(&idtf);
    if (!intrin) return false;
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
    if (auto m = getInstruction()) return m->getFastMathFlags().allowContract();
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
      costs.resize(log2VectorWidth + 1, RecipThroughputLatency::getInvalid());
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
  static auto getType(llvm::Type *T, unsigned int vectorWidth) -> llvm::Type * {
    if (vectorWidth == 1) return T;
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
    if (auto i = std::get_if<Intrinsic>(&idtf)) return i;
    return {};
  }
#if LLVM_VERSION_MAJOR >= 16
  llvm::TargetTransformInfo::OperandValueInfo
  getOperandInfo(llvm::TargetTransformInfo &TTI, unsigned int i) const {
    if (llvm::Value *v = operands[i]->getValue()) return TTI.getOperandInfo(v);
    return llvm::TargetTransformInfo::OperandValueInfo{};
  }
  RecipThroughputLatency calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                                 Intrinsic::OpCode id,
                                                 unsigned int vectorWidth) {
    auto op0info = getOperandInfo(TTI, 0);
    llvm::Type *T = getType(vectorWidth);
    return {
      TTI.getArithmeticInstrCost(
        id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
      TTI.getArithmeticInstrCost(
        id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)};
  }
  RecipThroughputLatency
  calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode id,
                           unsigned int vectorWidth) {
    auto op0info = getOperandInfo(TTI, 0);
    auto op1info = getOperandInfo(TTI, 1);
    llvm::Type *T = getType(vectorWidth);
    return {
      TTI.getArithmeticInstrCost(id.id, T,
                                 llvm::TargetTransformInfo::TCK_RecipThroughput,
                                 op0info, op1info),
      TTI.getArithmeticInstrCost(
        id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info, op1info)};
  }
#else
  [[nodiscard]] auto getOperandInfo(unsigned int i) const
    -> std::pair<llvm::TargetTransformInfo::OperandValueKind,
                 llvm::TargetTransformInfo::OperandValueProperties> {
    Instruction *opi = (operands)[i];
    if (auto c = llvm::dyn_cast_or_null<llvm::ConstantInt>(opi->getValue())) {
      llvm::APInt v = c->getValue();
      if (v.isPowerOf2())
        return std::make_pair(
          llvm::TargetTransformInfo::OK_UniformConstantValue,
          llvm::TargetTransformInfo::OP_PowerOf2);
      return std::make_pair(llvm::TargetTransformInfo::OK_UniformConstantValue,
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
                               Intrinsic::OpCode idt, unsigned int vectorWidth)
    -> RecipThroughputLatency {
    auto op0info = getOperandInfo(0);
    llvm::Type *T = type;
    if (vectorWidth > 1) T = llvm::FixedVectorType::get(T, vectorWidth);
    return {TTI.getArithmeticInstrCost(
              idt.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
              op0info.first, llvm::TargetTransformInfo::OK_AnyValue,
              op0info.second),
            TTI.getArithmeticInstrCost(
              idt.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
              llvm::TargetTransformInfo::OK_AnyValue, op0info.second)};
  }
  auto calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                Intrinsic::OpCode idt, unsigned int vectorWidth)
    -> RecipThroughputLatency {
    auto op0info = getOperandInfo(0);
    auto op1info = getOperandInfo(1);
    llvm::Type *T = getType(vectorWidth);
    return {TTI.getArithmeticInstrCost(
              idt.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput,
              op0info.first, op1info.first, op0info.second, op1info.second),
            TTI.getArithmeticInstrCost(
              idt.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info.first,
              op1info.first, op0info.second, op1info.second)};
  }
#endif
  [[nodiscard]] auto operandIsLoad(unsigned int i = 0) const -> bool {
    return (operands)[i]->isLoad();
  }
  [[nodiscard]] auto userIsStore() const -> bool {
    for (auto u : users)
      if (u->isStore()) return true;
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
  auto calcCastCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode idt,
                    unsigned int vectorWidth) -> RecipThroughputLatency {
    llvm::Type *srcT = getType(operands.front()->type, vectorWidth);
    llvm::Type *dstT = getType(vectorWidth);
    llvm::TargetTransformInfo::CastContextHint ctx = getCastContext(TTI);
    return {
      TTI.getCastInstrCost(idt.id, dstT, srcT, ctx,
                           llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getCastInstrCost(idt.id, dstT, srcT, ctx,
                           llvm::TargetTransformInfo::TCK_Latency)};
  }
  [[nodiscard]] auto getPredicate() const -> llvm::CmpInst::Predicate {
    if (isSelect()) return operands.front()->getPredicate();
    assert(isCmp());
    if (auto cmp = llvm::dyn_cast_or_null<llvm::CmpInst>(getValue()))
      return cmp->getPredicate();
    return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                    : llvm::CmpInst::BAD_ICMP_PREDICATE;
  }
  auto calcCmpSelectCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode idt,
                         unsigned int vectorWidth) -> RecipThroughputLatency {
    llvm::Type *T = getType(vectorWidth);
    llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
    llvm::CmpInst::Predicate pred = getPredicate();
    return {
      TTI.getCmpSelInstrCost(idt.id, T, cmpT, pred,
                             llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getCmpSelInstrCost(idt.id, T, cmpT, pred,
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
    for (auto op : operands) argTypes.push_back(op->getType(vectorWidth));
    if (intrin.id == llvm::Intrinsic::not_intrinsic) {
      // we shouldn't be hitting here
      return {
        TTI.getCallInstrCost(getFunction(), T, argTypes,
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
    for (auto op : operands) argTypes.push_back(op->getType(vectorWidth));
    return {TTI.getCallInstrCost(
              F, T, argTypes, llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getCallInstrCost(getFunction(), T, argTypes,
                                 llvm::TargetTransformInfo::TCK_Latency)};
  }
  struct ExtractAlignment {
    constexpr auto operator()(std::monostate) -> llvm::Align {
      return llvm::Align{};
    }
    auto operator()(llvm::Value *v) -> llvm::Align {
      if (auto load = llvm::dyn_cast_or_null<llvm::LoadInst>(v))
        return load->getAlign();
      else if (auto store = llvm::dyn_cast_or_null<llvm::StoreInst>(v))
        return store->getAlign();
      else return {};
    }
    auto operator()(Address *ref) const -> llvm::Align {
      return ref->getAlign();
    }
  };
  auto calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                        Intrinsic::OpCode idt,
                                        unsigned int vectorWidth)
    -> RecipThroughputLatency {
    constexpr unsigned int AddressSpace = 0;
    llvm::Type *T = getType(vectorWidth);
    llvm::Align alignment = std::visit(ExtractAlignment{}, ptr);
    if (predicates.size() == 0) {
      return {
        TTI.getMemoryOpCost(idt.id, T, alignment, AddressSpace,
                            llvm::TargetTransformInfo::TCK_RecipThroughput),
        TTI.getMemoryOpCost(idt.id, T, alignment, AddressSpace,
                            llvm::TargetTransformInfo::TCK_Latency)};
    } else {
      return {
        TTI.getMaskedMemoryOpCost(
          idt.id, T, alignment, AddressSpace,
          llvm::TargetTransformInfo::TCK_RecipThroughput),
        TTI.getMaskedMemoryOpCost(idt.id, T, alignment, AddressSpace,
                                  llvm::TargetTransformInfo::TCK_Latency)};
    }
  }
  auto calculateCostFAddFSub(llvm::TargetTransformInfo &TTI,
                             Intrinsic::OpCode idt, unsigned int vectorWidth)
    -> RecipThroughputLatency {
    // TODO: allow not assuming hardware FMA support
    if (((operands)[0]->isFMulOrFNegOfFMul() ||
         (operands)[1]->isFMulOrFNegOfFMul()) &&
        allowsContract())
      return {};
    return calcBinaryArithmeticCost(TTI, idt, vectorWidth);
  }
  auto allUsersAdditiveContract() -> bool {
    for (auto u : users)
      if (!(((u->isFAdd()) || (u->isFSub())) && (u->allowsContract())))
        return false;
    return true;
  }
  auto calculateFNegCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode idt,
                         unsigned int vectorWidth) -> RecipThroughputLatency {

    if (operands.front()->isFMul() && allUsersAdditiveContract()) return {};
    return calcUnaryArithmeticCost(TTI, idt, vectorWidth);
  }
  [[nodiscard]] auto isConstantOneInt() const -> bool {
    if (const int64_t *c = std::get_if<int64_t>(&idtf)) return *c == 1;
    return false;
  }
  [[nodiscard]] auto calculateCost(llvm::TargetTransformInfo &TTI,
                                   unsigned int vectorWidth)
    -> RecipThroughputLatency {
    if (Optional<const Intrinsic *> idt = getIntrinsic())
      return calcCost(*idt, TTI, vectorWidth);
    else if (auto *F = getFunction()) return calcCallCost(TTI, F, vectorWidth);
    else return {};
  }
  [[nodiscard]] auto calcCost(Intrinsic idt, llvm::TargetTransformInfo &TTI,
                              unsigned int vectorWidth)
    -> RecipThroughputLatency {
    switch (idt.opcode.id) {
    case llvm::Instruction::FAdd:
    case llvm::Instruction::FSub:
      return calculateCostFAddFSub(TTI, idt.opcode, vectorWidth);
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
      return calcBinaryArithmeticCost(TTI, idt.opcode, vectorWidth);
    case llvm::Instruction::FNeg:
      // one arg arithmetic cost
      return calculateFNegCost(TTI, idt.opcode, vectorWidth);
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
      return calcCastCost(TTI, idt.opcode, vectorWidth);
    case llvm::Instruction::ICmp:
    case llvm::Instruction::FCmp:
    case llvm::Instruction::Select:
      return calcCmpSelectCost(TTI, idt.opcode, vectorWidth);
    case llvm::Instruction::Call:
      return calcCallCost(TTI, idt.intrin, vectorWidth);
    case llvm::Instruction::Load:
    case llvm::Instruction::Store:
      return calculateCostContiguousLoadStore(TTI, idt.opcode, vectorWidth);
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
    if (!idop) return 0;
    switch (idop->opcode.id) {
    case llvm::Instruction::Call:
      if (!(isMulAdd() || isCommutativeCall())) return 0;
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
      if (op == old) op = new_;
  }
  /// replace all uses of `*this` with `*I`.
  /// Assumes that `*I` does not depend on `*this`.
  void replaceAllUsesWith(Instruction *J) {
    for (auto u : users) {
      assert(u != J);
      u->replaceOperand(this, J);
      J->users.insert(u);
    }
  }
  /// replace all uses of `*this` with `*I`, except for `*I` itself.
  /// This is useful when replacing `*this` with `*I = f(*this)`
  /// E.g., when merging control flow branches, where `f` may be a select
  void replaceAllOtherUsesWith(Instruction *J) {
    for (auto u : users) {
      if (u != J) {
        u->replaceOperand(this, J);
        J->users.insert(u);
      }
    }
  }
  auto replaceAllUsesOf(Instruction *J) -> Instruction * {
    for (auto u : J->users) {
      assert(u != this);
      u->replaceOperand(J, this);
      users.insert(u);
    }
    return this;
  }
  auto replaceAllOtherUsesOf(Instruction *J) -> Instruction * {
    for (auto u : J->users) {
      if (u != this) {
        u->replaceOperand(J, this);
        users.insert(u);
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

  static auto getHashValue(const ::Instruction::Intrinsic &Key) -> unsigned {
    return llvm::detail::combineHashValue(
      llvm::DenseMapInfo<llvm::Intrinsic::ID>::getHashValue(Key.opcode.id),
      llvm::DenseMapInfo<llvm::Intrinsic::ID>::getHashValue(Key.intrin.id));
  }

  static auto isEqual(const ::Instruction::Intrinsic &LHS,
                      const ::Instruction::Intrinsic &RHS) -> bool {
    return LHS == RHS;
  }
};
template <> struct llvm::DenseMapInfo<::Instruction::Identifier> {
  static inline auto getEmptyKey() -> ::Instruction::Identifier {
    return ::Instruction::IdentifierEmpty{};
  }

  static inline auto getTombstoneKey() -> ::Instruction::Identifier {
    return ::Instruction::IdentifierTombstone{};
  }

  struct Hash {
    template <typename T> auto operator()(T &Key) const -> unsigned {
      using CVT = std::remove_cvref_t<T>;
      if constexpr (std::same_as<CVT, double>) {
        return llvm::DenseMapInfo<uint64_t>::getHashValue(
          static_cast<uint64_t>(Key));
      } else if constexpr (std::same_as<CVT, ::Instruction::IdentifierEmpty>) {
        return 0;
      } else if constexpr (std::same_as<CVT,
                                        ::Instruction::IdentifierTombstone>) {
        return 1;
      } else {
        return llvm::DenseMapInfo<CVT>::getHashValue(Key);
      }
    }
  };

  static auto getHashValue(const ::Instruction::Identifier &Key) -> unsigned {
    return std::visit<unsigned>(Hash{}, Key);
  }
  struct VariantEqual {
    const ::Instruction::Identifier &rhs;
    template <typename T> auto operator()(T &Key) const -> bool {
      if (T *RHS = std::get_if<std::remove_cvref_t<T>>(&rhs))
        return *RHS == Key;
      return false;
    }
  };
  static auto isEqual(const ::Instruction::Identifier &LHS,
                      const ::Instruction::Identifier &RHS) -> bool {
    return std::visit(VariantEqual{RHS}, LHS);
  }
};
template <typename T> struct llvm::DenseMapInfo<llvm::MutableArrayRef<T>> {
  static inline auto getEmptyKey() -> llvm::MutableArrayRef<T> {
    return {llvm::DenseMapInfo<T *>::getEmptyKey(), size_t(0)};
  }

  static inline auto getTombstoneKey() -> llvm::MutableArrayRef<T> {
    return {llvm::DenseMapInfo<T *>::getTombstoneKey(), size_t(0)};
  }

  static auto getHashValue(llvm::MutableArrayRef<T> Key) -> unsigned {
    if (Key.empty()) return 0;
    unsigned hash = llvm::DenseMapInfo<T>::getHashValue(Key.front());
    for (size_t n = 1; n < Key.size(); ++n)
      hash = llvm::detail::combineHashValue(
        hash, llvm::DenseMapInfo<T>::getHashValue(Key[n]));
    return hash;
  }

  static auto isEqual(llvm::MutableArrayRef<T> LHS,
                      llvm::MutableArrayRef<T> RHS) -> bool {
    return LHS == RHS;
  }
};

namespace Predicate {
struct Map {
  llvm::MapVector<llvm::BasicBlock *, Set> map;
  [[nodiscard]] auto size() const -> size_t { return map.size(); }
  [[nodiscard]] auto isEmpty() const -> bool { return map.empty(); }
  [[nodiscard]] auto isDivergent() const -> bool {
    if (size() < 2) return false;
    for (auto I = map.begin(), E = map.end(); I != E; ++I) {
      if (I->second.isEmpty()) continue;
      for (auto J = std::next(I); J != E; ++J) {
        // NOTE: we don't need to check`isEmpty()`
        // because `emptyIntersection()` returns `false`
        // when isEmpty() is true.
        if (I->second.intersectionIsEmpty(J->second)) return true;
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
  [[nodiscard]] auto begin() -> decltype(map.rbegin()) { return map.rbegin(); }
  [[nodiscard]] auto end() -> decltype(map.rend()) { return map.rend(); }
  [[nodiscard]] auto rbegin() -> decltype(map.begin()) { return map.begin(); }
  [[nodiscard]] auto rend() -> decltype(map.end()) { return map.end(); }
  [[nodiscard]] auto operator[](llvm::BasicBlock *bb) -> std::optional<Set> {
    auto it = map.find(bb);
    if (it == map.end()) return std::nullopt;
    return it->second;
  }
  [[nodiscard]] auto operator[](llvm::Instruction *inst) -> std::optional<Set> {
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
    if (f == rend()) return false;
    return !f->second.isEmpty();
  }
  [[nodiscard]] auto isInPath(llvm::Instruction *I) -> bool {
    return isInPath(I->getParent());
  }
  void clear() { map.clear(); }
  // void visit(llvm::BasicBlock *BB) { map.insert(std::make_pair(BB,
  // Set())); } void visit(llvm::Instruction *inst) {
  // visit(inst->getParent()); }
  [[nodiscard]] auto addPredicate(BumpAlloc<> &alloc, Instruction::Cache &cache,
                                  llvm::Value *value) -> size_t {
    auto *I = cache.getInstruction(alloc, *this, value);
    assert(cache.predicates.size() <= 32 && "too many predicates");
    for (size_t i = 0; i < cache.predicates.size(); ++i)
      if (cache.predicates[i] == I) return i;
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
    for (auto &&pair : map) pair.second &= predicate;
  };
  enum class Destination { Reached, Unreachable, Returned, Unknown };
  // TODO:
  // 1. see why L->contains(BBsrc) does not work; does it only contain BBs
  // in it directly, and not nested another loop deeper?
  // 2. We are ignoring cycles for now; we must ensure this is done
  // correctly
  [[nodiscard]] static auto
  descendBlock(BumpAlloc<> &alloc, Instruction::Cache &cache,
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
    if (!I) return Destination::Unknown;
    else if (llvm::isa<llvm::ReturnInst>(I)) return Destination::Returned;
    else if (llvm::isa<llvm::UnreachableInst>(I))
      return Destination::Unreachable;
    auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
    if (!BI) return Destination::Unknown;
    if (BI->isUnconditional()) {
      auto rc = descendBlock(alloc, cache, visited, predMap,
                             BI->getSuccessor(0), BBdst, predicate, BBhead, L);
      if (rc == Destination::Reached) predMap.reach(BBsrc, predicate);
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
      predicate.intersect(predInd, Predicate::Relation::False), BBhead, L);
    if ((rc0 == Destination::Returned) || (rc0 == Destination::Unreachable)) {
      if (rc1 == Destination::Reached) {
        //  we're now assuming that !cond
        predMap.assume(
          Predicate::Intersection(predInd, Predicate::Relation::False));
        predMap.reach(BBsrc, predicate);
      }
      return rc1;
    } else if ((rc1 == Destination::Returned) ||
               (rc1 == Destination::Unreachable)) {
      if (rc0 == Destination::Reached) {
        //  we're now assuming that cond
        predMap.assume(
          Predicate::Intersection(predInd, Predicate::Relation::True));
        predMap.reach(BBsrc, predicate);
      }
      return rc0;
    } else if (rc0 == rc1) {
      if (rc0 == Destination::Reached) predMap.reach(BBsrc, predicate);
      return rc0;
    } else return Destination::Unknown;
  }
  /// We bail if there are more than 32 conditions; control flow that
  /// branchy is probably not worth trying to vectorize.
  [[nodiscard]] static auto
  descend(BumpAlloc<> &alloc, Instruction::Cache &cache,
          llvm::BasicBlock *start, llvm::BasicBlock *stop, llvm::Loop *L)
    -> std::optional<Map> {
    Predicate::Map pm;
    llvm::SmallPtrSet<llvm::BasicBlock *, 16> visited;
    if (descendBlock(alloc, cache, visited, pm, start, stop, {}, start, L) ==
        Destination::Reached)
      return pm;
    return std::nullopt;
  }

}; // struct Map
} // namespace Predicate

inline auto Instruction::Cache::getInstruction(BumpAlloc<> &alloc,
                                               Predicate::Map &predMap,
                                               llvm::Instruction *instr)
  -> Instruction * {
  if (Instruction *i = completeInstruction(alloc, predMap, instr)) return i;
  if (containsCycle(instr)) {
    auto *i =
      new (alloc) Instruction(Instruction::Intrinsic(instr), instr->getType());
    llvmToInternalMap[instr] = i;
    return i;
  }
  UniqueIdentifier uid{getUniqueIdentifier(alloc, predMap, *this, instr)};
  auto *i = getInstruction(alloc, uid, instr->getType());
  llvmToInternalMap[instr] = i;
  return i;
}
inline auto Instruction::Cache::completeInstruction(BumpAlloc<> &alloc,
                                                    Predicate::Map &predMap,
                                                    llvm::Instruction *J)
  -> Instruction * {
  Instruction *i = (*this)[J];
  if (!i) return nullptr;
  // if `i` has operands, or if it isn't supposed to, it's been completed
  if ((i->operands.size() > 0) || (J->getNumOperands() == 0)) return i;
  // instr is non-null and has operands
  // maybe instr isn't in BBpreds?
  if (std::optional<Predicate::Set> pred = predMap[J]) {
    // instr is in BBpreds, therefore, we now complete `i`.
    i->predicates = std::move(*pred);
    // we use dummy operands to avoid infinite recursion
    // the i->operands.size() > 0 check above will block this
    i->operands = llvm::MutableArrayRef<Instruction *>{nullptr, 1};
    i->operands = getOperands(alloc, predMap, *this, J);
    for (auto *op : i->operands) op->users.insert(i);
  }
  return i;
}
inline auto Instruction::Cache::getInstruction(BumpAlloc<> &alloc,
                                               Predicate::Map &predMap,
                                               llvm::Value *v)
  -> Instruction * {

  if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v)) {
    if (containsCycle(instr)) {
    }
    return getInstruction(alloc, predMap, instr);
  }
  return getInstruction(alloc, v);
}
/*
struct InstructionBlock {
    // we tend to heap allocate InstructionBlocks with a bump allocator,
    // so using 128 bytes seems reasonable.
    [[no_unique_address]] llvm::SmallVector<Instruction *, 14> instructions;
    // [[no_unique_address]] LoopTreeSchedule *loopTree{nullptr};

    InstructionBlock(BumpAlloc<> &alloc, Instruction::Cache &cache,
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
