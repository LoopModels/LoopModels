#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "Dicts/BumpVector.hpp"
#include "Dicts/MapVector.hpp"
#include "IR/Address.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
#include <Containers/UnrolledList.hpp>
#include <IR/Operands.hpp>
#include <Math/Array.hpp>
#include <Utilities/Allocators.hpp>
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/FMF.h>
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

// NOLINTNEXTLINE(cert-dcl58-cpp)
template <> struct std::hash<poly::IR::Operands> {
  auto operator()(const poly::IR::Operands &s) const noexcept -> size_t {
    if (s.empty()) return 0;
    std::size_t h = 0;
    return s.reduce(h, [](std::size_t h, const poly::IR::Node *t) {
      return llvm::detail::combineHashValue(
        h, std::hash<const poly::IR::Node *>{}(t));
    });
  }
};

namespace poly {
using math::PtrVector, math::MutPtrVector, utils::BumpAlloc, utils::invariant,
  utils::NotNull;

};

namespace poly::IR {

using dict::aset, dict::amap, containers::UList;

struct UniqueIdentifier {
  Operands ops;
  Operands preds;
  Node::ValKind kind;
  llvm::Intrinsic::ID op{llvm::Intrinsic::not_intrinsic};
  llvm::FastMathFlags fastMathFlags{};
};

auto containsCycle(const llvm::Instruction *, aset<llvm::Instruction const *> &,
                   const llvm::Value *) -> bool;
// NOLINTNEXTLINE(misc-no-recursion)
inline auto containsCycleCore(const llvm::Instruction *J,
                              aset<llvm::Instruction const *> &visited,
                              const llvm::Instruction *K) -> bool {
  for (const llvm::Use &op : K->operands())
    if (containsCycle(J, visited, op.get())) return true;
  return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
inline auto containsCycle(const llvm::Instruction *J,
                          aset<llvm::Instruction const *> &visited,
                          const llvm::Value *V) -> bool {
  const auto *S = llvm::dyn_cast<llvm::Instruction>(V);
  if (S == J) return true;
  if ((!S) || (visited.count(S))) return false;
  visited.insert(S);
  return containsCycleCore(J, visited, S);
}

inline auto containsCycle(BumpAlloc<> &alloc, llvm::Instruction const *S)
  -> bool {
  // don't get trapped in a different cycle
  auto p = alloc.scope();
  aset<llvm::Instruction const *> visited{alloc};
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

class Inst : public Node {

protected:
  UList<Node *> *operands;
  UList<Node *> *predicates{nullptr};
  llvm::FastMathFlags fastMathFlags;

  constexpr Inst(ValKind k) : Node(k) {}
  constexpr Inst(ValKind k, UList<Node *> *op, UList<Node *> *pred)
    : Node(k), operands(op), predicates(pred) {}

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_Intr;
  }
  constexpr auto getPredicate() -> UList<Node *> * { return predicates; }
  constexpr auto getPredicate() const -> UList<Node *> const * {
    return predicates;
  }
  auto getOperands() -> UList<Node *> * { return operands; }
  constexpr auto getOperands() const -> UList<Node *> const * {
    return operands;
  }
  [[nodiscard]] auto allowsContract() const -> bool {
    return fastMathFlags.allowContract();
  }
};

class Func : public Inst {
  llvm::Function *func;

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Func;
  }
  constexpr Func(llvm::Function *f) : Inst(VK_Func), func(f) {}
};
class Op : public Inst {
  llvm::Intrinsic::ID opcode;
  struct Identifier {
    llvm::Intrinsic::ID op;
    UList<Node *> *operands;
    UList<Node *> *predicates;
  };
  [[nodiscard]] auto getOpCode() const -> llvm::Intrinsic::ID { return opcode; }
  static auto getOpCode(llvm::Value *v) -> std::optional<llvm::Intrinsic::ID> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return i->getOpcode();
    return {};
  }
  [[nodiscard]] constexpr auto isInstruction(llvm::Intrinsic::ID opCode) const
    -> bool {
    return opcode == opCode;
  }
  static auto isFMul(Node *n) -> bool {
    if (auto *op = llvm::dyn_cast<Op>(n)) return op->isFMul();
    return false;
  }
  [[nodiscard]] auto isFMul() const -> bool {
    return isInstruction(llvm::Instruction::FMul);
  }
  [[nodiscard]] auto isFNeg() const -> bool {
    return isInstruction(llvm::Instruction::FNeg);
  }
  [[nodiscard]] auto isFMulOrFNegOfFMul() const -> bool {
    return isFMul() || (isFNeg() && isFMul(getOperands()->only()));
  }
  [[nodiscard]] auto isFAdd() const -> bool {
    return isInstruction(llvm::Instruction::FAdd);
  }
  [[nodiscard]] auto isFSub() const -> bool {
    return isInstruction(llvm::Instruction::FSub);
  }
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

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Oprn;
  }
};
class Intr : public Inst {

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Intr;
  }
  [[nodiscard]] auto getIntrinsicID() const -> llvm::Intrinsic::ID {
    return intrin;
  }
  static auto getIntrinsicID(llvm::Value *v)
    -> std::optional<llvm::Intrinsic::ID> {
    if (auto *i = llvm::dyn_cast<llvm::IntrinsicInst>(v))
      return Intrin{i->getIntrinsicID()};
    return {};
  }
  [[nodiscard]] constexpr auto isIntrinsic(llvm::Intrinsic::ID opCode) const
    -> bool {
    return intrin == opCode;
  }
  struct Identifier {
    llvm::Intrinsic::ID id;
    UList<Node *> *operands;
    UList<Node *> *predicates;
  };

  llvm::Intrinsic::ID intrin;
  llvm::Type *type;
  // aset<Intr *> users;
  RecipThroughputLatency costs[2];
  // math::BumpPtrVector<RecipThroughputLatency> costs;

  void setOperands(MutPtrVector<Intr *> ops) {
    operands << ops;
    for (auto *op : ops) op->users.insert(this);
  }
  [[nodiscard]] auto isMulAdd() const -> bool {
    return isIntrinsic(llvm::Intrinsic::fmuladd) ||
           isIntrinsic(llvm::Intrinsic::fma);
  }
  [[nodiscard]] auto getType() const -> llvm::Type * { return type; }
  [[nodiscard]] auto getOperands() -> MutPtrVector<Intr *> { return operands; }
  [[nodiscard]] auto getOperands() const -> PtrVector<Intr *> {
    return operands;
  }
  [[nodiscard]] auto getOperand(size_t i) -> Intr * { return operands[i]; }
  [[nodiscard]] auto getOperand(size_t i) const -> Intr * {
    return operands[i];
  }
  [[nodiscard]] auto getUsers() -> aset<Intr *> & { return users; }
  [[nodiscard]] auto getNumOperands() const -> size_t {
    return operands->size();
  }
  explicit Intr(BumpAlloc<> &alloc, llvm::Intrinsic::ID idt, llvm::Type *typ)
    : Inst(VK_Intr), idtf(idt), type(typ) {}
  // Instruction(UniqueIdentifier uid)
  // : id(std::get<0>(uid)), operands(std::get<1>(uid)) {}
  explicit Intr(BumpAlloc<> &alloc, Identifier uid, llvm::Type *typ)
    : Inst(VK_Intr, uid.operands, uid.predicates), idtf(uid.id), type(typ) {}

  [[nodiscard]] static auto // NOLINTNEXTLINE(misc-no-recursion)
  getUniqueIdentifier(BumpAlloc<> &alloc, Cache &cache, llvm::Instruction *v)
    -> UniqueIdentifier {
    return {Intrinsic(v), getOperands(alloc, cache, v)};
  }
  [[nodiscard]] auto getUniqueIdentifier(BumpAlloc<> &alloc, Cache &cache)
    -> UniqueIdentifier {
    llvm::Instruction *J = getInstruction();
    return {idtf, getOperands(alloc, cache, J)};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto static getUniqueIdentifier(BumpAlloc<> &alloc,
                                                Cache &cache, llvm::Value *v)
    -> UniqueIdentifier {
    if (auto *J = llvm::dyn_cast<llvm::Instruction>(v))
      return getUniqueIdentifier(alloc, cache, J);
    return {Intrinsic(v), {nullptr, unsigned(0)}};
  }
  [[nodiscard]] static auto // NOLINTNEXTLINE(misc-no-recursion)
  getUniqueIdentifier(BumpAlloc<> &alloc, Predicate::Map &predMap, Cache &cache,
                      llvm::Instruction *J) -> UniqueIdentifier {
    return {Intrinsic(J), getOperands(alloc, predMap, cache, J)};
  }
  [[nodiscard]] auto getUniqueIdentifier(BumpAlloc<> &alloc,
                                         Predicate::Map &predMap, Cache &cache)
    -> UniqueIdentifier {
    llvm::Instruction *J = getInstruction();
    return {idtf, getOperands(alloc, predMap, cache, J)};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] static auto getOperands(BumpAlloc<> &alloc, Cache &cache,
                                        llvm::Instruction *instr)
    -> MutPtrVector<Intr *> {
    if (llvm::isa<llvm::LoadInst>(instr)) return {nullptr, size_t(0)};
    auto ops{instr->operands()};
    auto *OI = ops.begin();
    // NOTE: operand 0 is the value operand of a store
    bool isStore = llvm::isa<llvm::StoreInst>(instr);
    // getFastMathFlags()
    auto *OE = isStore ? (OI + 1) : ops.end();
    size_t numOps = isStore ? 1 : instr->getNumOperands();
    auto **operands = alloc.allocate<Intr *>(numOps);
    Intr **p = operands;
    for (; OI != OE; ++OI, ++p) *p = cache.getInstruction(alloc, *OI);
    return {operands, numOps};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] static auto getOperands(BumpAlloc<> &alloc,
                                        Predicate::Map &BBpreds, Cache &cache,
                                        llvm::Instruction *instr)
    -> MutPtrVector<Intr *> {
    if (llvm::isa<llvm::LoadInst>(instr)) return {nullptr, size_t(0)};
    auto ops{instr->operands()};
    auto *OI = ops.begin();
    // NOTE: operand 0 is the value operand of a store
    bool isStore = llvm::isa<llvm::StoreInst>(instr);
    auto *OE = isStore ? (OI + 1) : ops.end();
    size_t nOps = isStore ? 1 : instr->getNumOperands();
    auto **operands = alloc.allocate<Intr *>(nOps);
    Intr **p = operands;
    for (; OI != OE; ++OI, ++p) *p = cache.getInstruction(alloc, BBpreds, *OI);
    return {operands, nOps};
  }
  static auto createIsolated(BumpAlloc<> &alloc, llvm::Instruction *instr)
    -> Intr * {
    Intrinsic id{instr};
    auto *i = new (alloc) Intr(alloc, id, instr->getType());
    return i;
  }

  auto negate(BumpAlloc<> &alloc, Cache &cache) -> Intr * {
    // first, check if its parent is a negation
    if (isInstruction(llvm::Instruction::Xor) && (getNumOperands() == 2)) {
      // !x where `x isa bool` is represented as `x ^ true`
      auto *op0 = getOperand(0);
      auto *op1 = getOperand(1);
      if (op1->isConstantOneInt()) return op0;
      if (op0->isConstantOneInt()) return op1;
    }
    Intr *one = cache.getConstant(alloc, getType(), 1);
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

  /// fall back in case we need value operand
  // [[nodiscard]] auto isValue() const -> bool { return id.isValue(); }
  auto getCost(llvm::TargetTransformInfo &TTI, unsigned W, unsigned l2W)
    -> RecipThroughputLatency {
    if (l2W >= costs.size()) {
      costs.resize(l2W + 1, RecipThroughputLatency::getInvalid());
      return costs[l2W] = calculateCost(TTI, W);
    }
    RecipThroughputLatency c = costs[l2W];
    // TODO: differentiate between uninitialized and invalid
    if (!c.isValid()) costs[l2W] = c = calculateCost(TTI, W);
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
    if (const auto *i = std::get_if<Intrinsic>(&idtf)) return i;
    return {};
  }
#if LLVM_VERSION_MAJOR >= 16
  auto getOperandInfo(llvm::TargetTransformInfo & /*TTI*/, unsigned int i) const
    -> llvm::TargetTransformInfo::OperandValueInfo {
    if (llvm::Value *v = operands[i]->getValue())
      return llvm::TargetTransformInfo::getOperandInfo(v);
    return llvm::TargetTransformInfo::OperandValueInfo{};
  }
  auto calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                               Intrinsic::OpCode id,
                               unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    auto op0info = getOperandInfo(TTI, 0);
    llvm::Type *T = getType(vectorWidth);
    return {
      TTI.getArithmeticInstrCost(
        id.id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
      TTI.getArithmeticInstrCost(
        id.id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)};
  }
  auto calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                Intrinsic::OpCode id,
                                unsigned int vectorWidth) const
    -> RecipThroughputLatency {
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
    return std::ranges::any_of(users,
                               [](const auto &u) { return u->isStore(); });
  }
  auto getCastContext(llvm::TargetTransformInfo & /*TTI*/) const
    -> llvm::TargetTransformInfo::CastContextHint {
    if (operandIsLoad() || userIsStore())
      return llvm::TargetTransformInfo::CastContextHint::Normal;
    if (auto *cast = llvm::dyn_cast_or_null<llvm::CastInst>(getValue()))
      return llvm::TargetTransformInfo::getCastContextHint(cast);
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
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto getPredicate() const -> llvm::CmpInst::Predicate {
    if (isSelect()) return operands.front()->getPredicate();
    assert(isCmp());
    if (auto *cmp = llvm::dyn_cast_or_null<llvm::CmpInst>(getValue()))
      return cmp->getPredicate();
    return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                    : llvm::CmpInst::BAD_ICMP_PREDICATE;
  }
  auto calcCmpSelectCost(llvm::TargetTransformInfo &TTI, Intrinsic::OpCode idt,
                         unsigned int vectorWidth) const
    -> RecipThroughputLatency {
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
  auto selectCost(llvm::TargetTransformInfo &TTI,
                  unsigned int vectorWidth) const -> llvm::InstructionCost {
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
    for (auto *op : operands) argTypes.push_back(op->getType(vectorWidth));
    if (intrin.id == llvm::Intrinsic::not_intrinsic) {
      // we shouldn't be hitting here
      return {
        TTI.getCallInstrCost(getFunction(), T, argTypes,
                             llvm::TargetTransformInfo::TCK_RecipThroughput),
        TTI.getCallInstrCost(getFunction(), T, argTypes,
                             llvm::TargetTransformInfo::TCK_Latency)};
    }
    llvm::IntrinsicCostAttributes attr(intrin.id, T, argTypes);
    return {
      TTI.getIntrinsicInstrCost(attr,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getIntrinsicInstrCost(attr, llvm::TargetTransformInfo::TCK_Latency)};
  }
  auto calcCallCost(llvm::TargetTransformInfo &TTI, llvm::Function *F,
                    unsigned int vectorWidth) -> RecipThroughputLatency {
    llvm::Type *T = getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> argTypes;
    for (auto *op : operands) argTypes.push_back(op->getType(vectorWidth));
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
      if (auto *load = llvm::dyn_cast_or_null<llvm::LoadInst>(v))
        return load->getAlign();
      if (auto *store = llvm::dyn_cast_or_null<llvm::StoreInst>(v))
        return store->getAlign();
      return {};
    }
    auto operator()(Addr *ref) const -> llvm::Align { return ref->getAlign(); }
  };
  auto calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                        Intrinsic::OpCode idt,
                                        unsigned int vectorWidth)
    -> RecipThroughputLatency {
    constexpr unsigned int addrSpace = 0;
    llvm::Type *T = getType(vectorWidth);
    llvm::Align alignment = std::visit(ExtractAlignment{}, ptr);
    if (predicates.size() == 0) {
      return {
        TTI.getMemoryOpCost(idt.id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_RecipThroughput),
        TTI.getMemoryOpCost(idt.id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_Latency)};
    }
    return {
      TTI.getMaskedMemoryOpCost(idt.id, T, alignment, addrSpace,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getMaskedMemoryOpCost(idt.id, T, alignment, addrSpace,
                                llvm::TargetTransformInfo::TCK_Latency)};
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
    return std::ranges::all_of(users, [](Intr *u) {
      return (((u->isFAdd()) || (u->isFSub())) && (u->allowsContract()));
    });
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
    if (auto *F = getFunction()) return calcCallCost(TTI, F, vectorWidth);
    return {};
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
    default: return RecipThroughputLatency::getInvalid();
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
    case llvm::Instruction::Xor: return 0x3;
    default: return 0;
    }
  }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void replaceOperand(Intr *old, Intr *new_) {
    for (auto &&op : operands)
      if (op == old) op = new_;
  }
  /// replace all uses of `*this` with `*I`.
  /// Assumes that `*I` does not depend on `*this`.
  void replaceAllUsesWith(Intr *J) {
    for (auto *u : users) {
      assert(u != J);
      u->replaceOperand(this, J);
      J->users.insert(u);
    }
  }
  /// replace all uses of `*this` with `*I`, except for `*I` itself.
  /// This is useful when replacing `*this` with `*I = f(*this)`
  /// E.g., when merging control flow branches, where `f` may be a select
  void replaceAllOtherUsesWith(Intr *J) {
    for (auto *u : users) {
      if (u != J) {
        u->replaceOperand(this, J);
        J->users.insert(u);
      }
    }
  }
  auto replaceAllUsesOf(Intr *J) -> Intr * {
    for (auto *u : J->users) {
      assert(u != this);
      u->replaceOperand(J, this);
      users.insert(u);
    }
    return this;
  }
  auto replaceAllOtherUsesOf(Intr *J) -> Intr * {
    for (auto *u : J->users) {
      if (u != this) {
        u->replaceOperand(J, this);
        users.insert(u);
      }
    }
    return this;
  }
};

inline auto getIdentifier(llvm::Instruction *S) -> Intrinsic {
  if (auto *CB = llvm::dyn_cast<llvm::CallBase>(S))
    if (auto *F = CB->getCalledFunction()) return F;
  return Intrinsic(S);
}
inline auto getIdentifier(llvm::ConstantInt *S) -> Intrinsic {
  return S->getSExtValue();
}
inline auto getIdentifier(llvm::ConstantFP *S) -> Intrinsic {
  auto x = S->getValueAPF();
  return S->getValueAPF().convertToDouble();
}
inline auto getIdentifier(llvm::Value *v) -> std::optional<Intrinsic> {
  if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return getIdentifier(i);
  if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(v)) return getIdentifier(ci);
  if (auto *cfp = llvm::dyn_cast<llvm::ConstantFP>(v))
    return getIdentifier(cfp);
  return std::nullopt;
}

template <> struct std::hash<Intr::Intrinsic> {
  auto operator()(const Intr::Intrinsic &s) const noexcept -> size_t {
    return llvm::detail::combineHashValue(std::hash<unsigned>{}(s.opcode.id),
                                          std::hash<unsigned>{}(s.intrin.id));
  }
};

template <> struct std::hash<Intr::UniqueIdentifier> {
  auto operator()(const Intr::UniqueIdentifier &s) const noexcept -> size_t {
    return llvm::detail::combineHashValue(
      std::hash<Intr::Identifier>{}(s.idtf),
      std::hash<MutPtrVector<Intr *>>{}(s.operands));
  }
};

// NOLINTNEXTLINE(misc-no-recursion)
inline auto Intr::Cache::getInstruction(BumpAlloc<> &alloc,
                                        Predicate::Map &predMap,
                                        llvm::Instruction *instr) -> Intr * {
  if (Intr *i = completeInstruction(alloc, predMap, instr)) return i;
  if (containsCycle(alloc, instr)) {
    auto *i = new (alloc) Intr(alloc, Intr::Intrinsic(instr), instr->getType());
    llvmToInternalMap[instr] = i;
    return i;
  }
  UniqueIdentifier uid{getUniqueIdentifier(alloc, predMap, *this, instr)};
  auto *i = getInstruction(alloc, uid, instr->getType());
  llvmToInternalMap[instr] = i;
  return i;
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto Intr::Cache::completeInstruction(BumpAlloc<> &alloc,
                                             Predicate::Map &predMap,
                                             llvm::Instruction *J) -> Intr * {
  Intr *i = (*this)[J];
  if (!i) return nullptr;
  // if `i` has operands, or if it isn't supposed to, it's been completed
  if ((!i->operands.empty()) || (J->getNumOperands() == 0)) return i;
  // instr is non-null and has operands
  // maybe instr isn't in BBpreds?
  if (std::optional<Predicate::Set> pred = predMap[J]) {
    // instr is in BBpreds, therefore, we now complete `i`.
    i->predicates = std::move(*pred);
    // we use dummy operands to avoid infinite recursion
    // the i->operands.size() > 0 check above will block this
    i->operands = MutPtrVector<Intr *>{nullptr, 1};
    i->operands = getOperands(alloc, predMap, *this, J);
    for (auto *op : i->operands) op->users.insert(i);
  }
  return i;
}
// NOLINTNEXTLINE(misc-no-recursion)
inline auto Intr::Cache::getInstruction(BumpAlloc<> &alloc,
                                        Predicate::Map &predMap, llvm::Value *v)
  -> Intr * {

  if (auto *instr = llvm::dyn_cast<llvm::Instruction>(v)) {
    if (containsCycle(alloc, instr)) {}
    return getInstruction(alloc, predMap, instr);
  }
  return getInstruction(alloc, v);
}
static_assert(std::is_trivially_destructible_v<
              std::variant<std::monostate, llvm::Instruction *,
                           llvm::ConstantInt *, llvm::ConstantFP *, Addr *>>);
static_assert(std::is_trivially_destructible_v<Predicate::Set>);
static_assert(std::is_trivially_destructible_v<math::BumpPtrVector<Intr *>>);

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;
} // namespace poly::IR
