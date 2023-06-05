#pragma once

#include "Containers/UnrolledList.hpp"
#include "Dicts/BumpMapSet.hpp"
#include "Dicts/BumpVector.hpp"
#include "Dicts/MapVector.hpp"
#include "IR/Address.hpp"
#include "IR/Node.hpp"
#include "Math/Array.hpp"
#include "Predicate.hpp"
#include "Utilities/Allocators.hpp"
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
template <typename T> struct std::hash<poly::math::MutPtrVector<T>> {
  auto operator()(const MutPtrVector<T> &s) const noexcept -> size_t {
    if (s.empty()) return 0;
    std::size_t h = std::hash<T>{}(*s.begin());
    for (const auto *it = std::next(s.begin()); it != s.end(); ++it)
      h = llvm::detail::combineHashValue(h, std::hash<T>{}(*it));
    // h ^= std::hash<T>{}(*it) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

namespace poly {
using math::PtrVector, math::MutPtrVector, utils::BumpAlloc, utils::invariant,
  utils::NotNull;

};

namespace poly::IR {

using dict::aset, dict::amap, containers::UList;

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
  explicit Intr(BumpAlloc<> &alloc, Intrin idt, llvm::Type *typ)
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

struct Cache {
  map<llvm::Value *, Intr *> llvmToInternalMap;
  map<UniqueIdentifier, Intr *> argMap;
  llvm::SmallVector<Intr *> predicates;
  // tmp is used in case we don't need an allocation
  // Instruction *tmp{nullptr};
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
  auto operator[](llvm::Value *v) -> Intr * {
    auto f = llvmToInternalMap.find(v);
    if (f != llvmToInternalMap.end()) return f->second;
    return nullptr;
  }
  auto operator[](UniqueIdentifier uid) -> Intr * {
    if (auto f = argMap.find(uid); f != argMap.end()) return f->second;
    return nullptr;
  }
  auto argMapLoopup(Intrinsic idt) -> Intr * {
    UniqueIdentifier uid{idt, {nullptr, unsigned(0)}};
    return (*this)[uid];
  }
  auto argMapLoopup(Intrinsic idt, Intr *op) -> Intr * {
    std::array<Intr *, 1> ops;
    ops[0] = op;
    MutPtrVector<Intr *> opsRef(ops);
    UniqueIdentifier uid{idt, opsRef};
    return (*this)[uid];
  }
  template <size_t N>
  auto argMapLoopup(Intrinsic idt, std::array<Intr *, N> ops) -> Intr * {
    MutPtrVector<Intr *> opsRef(ops);
    UniqueIdentifier uid{idt, opsRef};
    return (*this)[uid];
  }
  auto argMapLoopup(Intrinsic idt, Intr *op0, Intr *op1) -> Intr * {
    return argMapLoopup<2>(idt, {op0, op1});
  }
  auto argMapLoopup(Intrinsic idt, Intr *op0, Intr *op1, Intr *op2) -> Intr * {
    return argMapLoopup<3>(idt, {op0, op1, op2});
  }
  auto createInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                         llvm::Type *typ) -> Intr * {
    auto *i = new (alloc) Intr(alloc, uid, typ);
    if (!i->operands.empty())
      for (auto *op : i->operands) op->users.insert(i);
    argMap.insert({uid, i});
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                      llvm::Type *typ) {
    if (auto *i = (*this)[uid]) return i;
    return createInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid, llvm::Type *typ,
                      Predicate::Set pred) {
    if (auto *i = (*this)[uid]) return i;
    auto *i = createInstruction(alloc, uid, typ);
    i->predicates = std::move(pred);
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, llvm::Type *typ) {
    UniqueIdentifier uid{idt, {nullptr, unsigned(0)}};
    return getInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0,
                      llvm::Type *typ) {
    // stack allocate for check
    if (auto *i = argMapLoopup(idt, op0)) return i;
    auto **opptr = alloc.allocate<Intr *>(1);
    opptr[0] = op0;
    MutPtrVector<Intr *> ops(opptr, 1);
    UniqueIdentifier uid{idt, ops};
    return createInstruction(alloc, uid, typ);
  }
  template <size_t N>
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt,
                      std::array<Intr *, N> ops, llvm::Type *typ) {
    // stack allocate for check
    if (auto *i = argMapLoopup(idt, ops)) return i;
    auto **opptr = alloc.allocate<Intr *>(2);
    for (size_t n = 0; n < N; n++) opptr[n] = ops[n];
    MutPtrVector<Intr *> mops(opptr, N);
    UniqueIdentifier uid{idt, mops};
    return createInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0, Intr *op1,
                      llvm::Type *typ) {
    return getInstruction<2>(alloc, idt, {op0, op1}, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0, Intr *op1,
                      Intr *op2, llvm::Type *typ) {
    return getInstruction<3>(alloc, idt, {op0, op1, op2}, typ);
  }

  /// This is the API for creating new instructions
  auto getInstruction(BumpAlloc<> &alloc, llvm::Instruction *instr) -> Intr * {
    if (Intr *i = (*this)[instr]) return i;
    UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, instr)};
    auto *i = getInstruction(alloc, uid, instr->getType());
    llvmToInternalMap[instr] = i;
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                      llvm::Instruction *instr) -> Intr *;
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getInstruction(BumpAlloc<> &alloc, llvm::Value *v) -> Intr * {
    if (Intr *i = (*this)[v]) return i;
    UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, v)};
    auto *i = getInstruction(alloc, uid, v->getType());
    llvmToInternalMap[v] = i;
    return i;
  }
  // if not in predMap, then operands don't get added, and
  // it won't be added to the argMap
  auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                      llvm::Value *v) -> Intr *;
  [[nodiscard]] auto contains(llvm::Value *v) const -> bool {
    return llvmToInternalMap.count(v);
  }
  auto createConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
    -> Intr * {
    UniqueIdentifier uid{Identifier(c), {nullptr, unsigned(0)}};
    auto argMatch = argMap.find(uid);
    if (argMatch != argMap.end()) return argMatch->second;
    return new (alloc) Intr(alloc, uid, typ);
  }
  auto eslgetConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
    -> Intr * {
    UniqueIdentifier uid{Identifier(c), {nullptr, unsigned(0)}};
    if (auto *i = (*this)[uid]) return i;
    return createConstant(alloc, typ, c);
  }
  auto createCondition(BumpAlloc<> &alloc, Predicate::Relation rel, Intr *instr,
                       bool swap = false) -> Node * {
    switch (rel) {
    case Predicate::Relation::Any:
      return Cint::create(alloc, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(alloc, 0, instr->getType());
    case Predicate::Relation::False:
      swap = !swap;
      [[fallthrough]];
    case Predicate::Relation::True:
      return swap ? instr->negate(alloc, *this) : instr;
    }
  }
  auto createCondition(BumpAlloc<> &alloc, Predicate::Intersection pred,
                       bool swap) -> Intr * {
    size_t popCount = pred.popCount();

    if (popCount == 0) return getConstant(alloc, predicates[0]->getType(), 1);
    if (popCount == 1) {
      size_t ind = pred.getFirstIndex();
      Intr *J = predicates[ind];
      return swap ? J->negate(alloc, *this) : J;
    }
    // we have more than one instruction
    auto And = Intrinsic(Intrinsic::OpCode{llvm::Instruction::And});
    size_t ind = pred.getFirstIndex();
    Intr *J = predicates[ind];
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
  auto createSelect(BumpAlloc<> &alloc, Intr *A, Intr *B) -> Intr * {
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
    Intr *cond = createCondition(alloc, P, swap);
    Intr *op1 = swap ? B : A;
    Intr *op2 = swap ? A : B;
    Intr *S = getInstruction(alloc, idt, cond, op1, op2, A->getType());
    S->predicates |= A->predicates;
    S->predicates |= B->predicates;
    return S;
  }
  /// completeInstruction
  /// when filling a predMap, we may initially not complete an instruction
  /// if it didn't appear inside the predMap if it is added later, we then
  /// need to finish adding its operands.
  auto completeInstruction(BumpAlloc<> &, Predicate::Map &, llvm::Instruction *)
    -> Intr *;
  Node *addParents(BumpAlloc<> &alloc, Stow *a, llvm::Loop *L) {
    llvm::StoreInst *J = a->getInstruction();
    if (!L->contains(J->getParent())) return;
    auto ops{instr->operands()};
    auto *OI = *ops.begin();
    Instr *p = cache.getInstruction(alloc, *OI);
  }
  Node *addParents(BumpAlloc<> &alloc, Inst *a, llvm::Loop *L) {
    llvm::StoreInst *J = a->getInstruction();
    if (!L->contains(J->getParent())) return;
    llvm::Use *U = J->getOperandList();
    unsigned numOperands = J->getNumOperands();
    for (unsigned i = 0; i < numOperands; ++i) {
      llvm::Value *V = U[i].get();
      if (!L->contains(V->getParent())) continue;
      addValue(alloc, V, L);
    }
  }
  Node *addParents(BumpAlloc<> &alloc, Node *a, llvm::Loop *L,
                   Node *outOfLoop) {
    if (auto *S = llvm::dyn_cast<Stow>(a))
      return addParents(alloc, S, L, outOfLoop);
    if (auto *I = llvm::dyn_cast<Inst>(a))
      return addParents(alloc, I, L, outOfLoop);
    return outOfLoop;
  }
  void addValue(BumpAlloc<> &alloc, llvm::Value *V, llvm::Loop *L) {}
};

namespace Predicate {
struct Map {
  MapVector<llvm::BasicBlock *, Set> map;
  constexpr Map(BumpAlloc<> &alloc) : map(alloc) {}
  Map(const Map &x) = default;
  Map(Map &&x) noexcept : map{std::move(x.map)} {}
  auto operator=(const Map &) -> Map & = default;
  auto operator=(Map &&) -> Map & = default;
  [[nodiscard]] auto size() const -> size_t { return map.size(); }
  [[nodiscard]] auto isEmpty() const -> bool { return map.empty(); }
  [[nodiscard]] auto isDivergent() const -> bool {
    if (size() < 2) return false;
    for (auto I = map.begin(), E = map.end(); I != E; ++I) {
      if (I->second.isEmpty()) continue;
      for (const auto *J = std::next(I); J != E; ++J) {
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
  // [[nodiscard]] auto get(llvm::BasicBlock *bb) -> Set & { return map[bb]; }
  [[nodiscard]] auto find(llvm::BasicBlock *bb) { return map.find(bb); }
  [[nodiscard]] auto find(llvm::Instruction *inst) {
    return map.find(inst->getParent());
  }
  // we insert into map in reverse order, so our iterators reverse
  [[nodiscard]] auto begin() { return std::reverse_iterator(map.end()); }
  [[nodiscard]] auto end() { return std::reverse_iterator(map.begin()); }
  [[nodiscard]] auto rbegin() { return map.begin(); }
  [[nodiscard]] auto rend() { return map.end(); }
  [[nodiscard]] auto operator[](llvm::BasicBlock *bb) -> std::optional<Set> {
    auto *it = map.find(bb);
    if (it == map.end()) return std::nullopt;
    return it->second;
  }
  [[nodiscard]] auto operator[](llvm::Instruction *inst) -> std::optional<Set> {
    return (*this)[inst->getParent()];
  }
  void insert(std::pair<llvm::BasicBlock *, Set> &&pair) {
    map.insert(std::move(pair));
  }
  [[nodiscard]] auto contains(llvm::BasicBlock *BB) const -> bool {
    return map.count(BB);
  }
  [[nodiscard]] auto isInPath(llvm::BasicBlock *BB) -> bool {
    auto *f = find(BB);
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
  [[nodiscard]] auto addPredicate(BumpAlloc<> &alloc, Intr::Cache &cache,
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
  void reach(BumpAlloc<> &alloc, llvm::BasicBlock *BB, Intersection predicate) {
    // because we may have inserted into predMap, we need to look up
    // again rather than being able to reuse anything from the
    // `visit`.
    if (auto *f = find(BB); f != rend()) f->second |= predicate;
    else map.insert({BB, Set{alloc, predicate}});
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
  [[nodiscard]] static auto // NOLINTNEXTLINE(misc-no-recursion)
  descendBlock(BumpAlloc<> &alloc, Intr::Cache &cache,
               aset<llvm::BasicBlock *> &visited, Predicate::Map &predMap,
               llvm::BasicBlock *BBsrc, llvm::BasicBlock *BBdst,
               Predicate::Intersection predicate, llvm::BasicBlock *BBhead,
               llvm::Loop *L) -> Destination {
    if (BBsrc == BBdst) {
      assert(!predMap.contains(BBsrc));
      predMap.insert({BBsrc, Set{alloc, predicate}});
      return Destination::Reached;
    }
    if (L && (!(L->contains(BBsrc)))) {
      // oops, we seem to have skipped the preheader and escaped the
      // loop.
      return Destination::Returned;
    }
    if (visited.contains(BBsrc)) {
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
    if (llvm::isa<llvm::ReturnInst>(I)) return Destination::Returned;
    if (llvm::isa<llvm::UnreachableInst>(I)) return Destination::Unreachable;
    const auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
    if (!BI) return Destination::Unknown;
    if (BI->isUnconditional()) {
      auto rc = descendBlock(alloc, cache, visited, predMap,
                             BI->getSuccessor(0), BBdst, predicate, BBhead, L);
      if (rc == Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
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
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc1;
    }
    if ((rc1 == Destination::Returned) || (rc1 == Destination::Unreachable)) {
      if (rc0 == Destination::Reached) {
        //  we're now assuming that cond
        predMap.assume(
          Predicate::Intersection(predInd, Predicate::Relation::True));
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc0;
    }
    if (rc0 != rc1) return Destination::Unknown;
    if (rc0 == Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
    return rc0;
  }
  /// We bail if there are more than 32 conditions; control flow that
  /// branchy is probably not worth trying to vectorize.
  [[nodiscard]] static auto descend(BumpAlloc<> &alloc, Intr::Cache &cache,
                                    llvm::BasicBlock *start,
                                    llvm::BasicBlock *stop, llvm::Loop *L)
    -> std::optional<Map> {
    auto p = alloc.checkpoint();
    Map pm{alloc};
    aset<llvm::BasicBlock *> visited{alloc};
    if (descendBlock(alloc, cache, visited, pm, start, stop, {}, start, L) ==
        Destination::Reached)
      return pm;
    alloc.rollback(p);
    return std::nullopt;
  }

}; // struct Map

} // namespace Predicate

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
    if (containsCycle(alloc, instr)) {
    }
    return getInstruction(alloc, predMap, instr);
  }
  return getInstruction(alloc, v);
}
static_assert(std::is_trivially_destructible_v<
              std::variant<std::monostate, llvm::Instruction *,
                           llvm::ConstantInt *, llvm::ConstantFP *, Addr *>>);
static_assert(std::is_trivially_destructible_v<Predicate::Set>);
static_assert(std::is_trivially_destructible_v<LinAlg::BumpPtrVector<Intr *>>);

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;
} // namespace poly::IR
