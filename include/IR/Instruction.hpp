#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/InstructionCost.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
#include <Containers/UnrolledList.hpp>
#include <Math/Array.hpp>
#include <Utilities/Allocators.hpp>
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h>
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
#include <llvm/Support/MathExtras.h>
#include <tuple>
#include <utility>

namespace poly {
using math::PtrVector, math::MutPtrVector, utils::Arena, utils::invariant,
  utils::NotNull;
}; // namespace poly

namespace poly::IR {

using dict::aset, dict::amap, containers::UList, utils::Optional,
  cost::RecipThroughputLatency, cost::VectorWidth, cost::VectorizationCosts;

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

inline auto containsCycle(Arena<> alloc, llvm::Instruction const *S) -> bool {
  // don't get trapped in a different cycle
  aset<llvm::Instruction const *> visited{&alloc};
  return containsCycleCore(S, visited, S);
}

class Compute : public Instruction {

protected:
  llvm::Instruction *inst{nullptr};
  llvm::Type *type;
  llvm::Intrinsic::ID opId;          // unsigned
  int numOperands;                   // negative means incomplete
  llvm::FastMathFlags fastMathFlags; // holds unsigned
  VectorizationCosts costs;
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  Value *operands[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

public:
  Compute(const Compute &) = delete;
  constexpr Compute(ValKind k, llvm::Instruction *i, llvm::Intrinsic::ID id,
                    int numOps)
    : Instruction(k), inst(i), type(i->getType()), opId(id),
      numOperands(numOps), fastMathFlags(i->getFastMathFlags()) {}
  constexpr Compute(ValKind k, llvm::Intrinsic::ID id, int numOps,
                    llvm::Type *t, llvm::FastMathFlags fmf)
    : Instruction(k), type(t), opId(id), numOperands(numOps),
      fastMathFlags(fmf) {}

  // static constexpr auto construct(Arena<> *alloc, ValKind k,
  //                                 llvm::Instruction *i, llvm::Intrinsic::ID
  //                                 id)
  //   -> Inst * {
  //   unsigned nOps = i->getNumOperands();
  //   auto *p = static_cast<Inst *>(
  //     alloc.allocate(sizeof(Inst) + sizeof(Node *) * nOps, alignof(Inst)));
  //   return std::construct_at(p, k, i, id);
  // }

  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_Func;
  }
  [[nodiscard]] constexpr auto getLLVMInstruction() const
    -> llvm::Instruction * {
    return inst;
  }
  static auto getIDKind(llvm::Instruction *I)
    -> std::pair<llvm::Intrinsic::ID, ValKind> {
    if (auto *c = llvm::dyn_cast<llvm::CallInst>(I)) {
      if (auto *J = llvm::dyn_cast<llvm::IntrinsicInst>(c))
        return {J->getIntrinsicID(), VK_Call};
      return {llvm::Intrinsic::not_intrinsic, VK_Func};
    }
    return {I->getOpcode(), VK_Oprn};
  }
  constexpr void setNumOps(int n) { numOperands = n; }
  // called when incomplete; flips sign
  constexpr auto numCompleteOps() -> unsigned {
    invariant(numOperands <= 0); // we'll allow 0 for now
    return numOperands = -numOperands;
  }
  constexpr void makeIncomplete() { numOperands = -numOperands; }
  // constexpr auto getPredicate() -> UList<Node *> * { return predicates; }
  // constexpr auto getPredicate() const -> UList<Node *> const * {
  //   return predicates;
  // }
  [[nodiscard]] constexpr auto getNumOperands() const -> unsigned {
    return unsigned(numOperands);
  }
  [[nodiscard]] constexpr auto getType() const -> llvm::Type * { return type; }
  [[nodiscard]] constexpr auto getOpId() const -> llvm::Intrinsic::ID {
    return opId;
  }
  constexpr auto getOperands() -> MutPtrVector<Value *> {
    return {operands, numOperands};
  }
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return {const_cast<Value **>(operands), unsigned(numOperands)};
  }
  [[nodiscard]] constexpr auto getOperand(size_t i) const -> Value * {
    return operands[i];
  }
  constexpr void setOperands(Arena<> *alloc, PtrVector<Value *> ops) {
    getOperands() << ops;
    for (auto *op : ops) op->addUser(alloc, this);
  }

  [[nodiscard]] constexpr auto getFastMathFlags() const -> llvm::FastMathFlags {
    return fastMathFlags;
  }
  [[nodiscard]] auto allowsContract() const -> bool {
    return fastMathFlags.allowContract();
  }
  // Incomplete stores the correct number of ops it was allocated with as a
  // negative number. The primary reason for being able to check
  // completeness is for `==` checks and hashing.
  [[nodiscard]] auto isComplete() const -> bool { return numOperands >= 0; }
  [[nodiscard]] auto isIncomplete() const -> bool { return numOperands < 0; }
  [[nodiscard]] auto isCommutativeCall() const -> bool {
    if (auto *intrin = llvm::dyn_cast_or_null<llvm::IntrinsicInst>(inst))
      return intrin->isCommutative();
    return false;
  }
  [[nodiscard]] auto isMulAdd() const -> bool {
    return (getKind() == VK_Call) && ((opId == llvm::Intrinsic::fmuladd) ||
                                      (opId == llvm::Intrinsic::fma));
  }
  [[nodiscard]] auto associativeOperandsFlag() const -> uint8_t {
    switch (getKind()) {
    case VK_Call: return (isMulAdd() || isCommutativeCall()) ? 0x3 : 0;
    case VK_Oprn:
      switch (opId) {
      case llvm::Instruction::FAdd:
      case llvm::Instruction::Add:
      case llvm::Instruction::FMul:
      case llvm::Instruction::Mul:
      case llvm::Instruction::And:
      case llvm::Instruction::Or:
      case llvm::Instruction::Xor: return 0x3;
      default: break;
      }
    default: break;
    }
    return 0;
  }
  auto operator==(Compute const &other) const -> bool {
    if (this == &other) return true;
    if ((getKind() != other.getKind()) || (opId != other.opId) ||
        (getType() != other.getType()) || (isComplete() != other.isComplete()))
      return false;
    if (isIncomplete())
      return getLLVMInstruction() == other.getLLVMInstruction();
    if (getNumOperands() != other.getNumOperands()) return false;
    size_t offset = 0;
    auto opst = getOperands();
    auto opso = other.getOperands();
    if (uint8_t flag = associativeOperandsFlag()) {
      invariant(flag, uint8_t(3));
      auto *ot0 = opst[0];
      auto *oo0 = opso[0];
      auto *ot1 = opst[1];
      auto *oo1 = opso[1];
      if (((ot0 != oo0) || (ot1 != oo1)) && ((ot0 != oo1) || (ot1 != oo0)))
        return false;
      offset = 2;
    }
    for (size_t i = offset, N = getNumOperands(); i < N; ++i)
      if (opst[i] != opso[i]) return false;
    return true;
  }

  /// fall back in case we need value operand
  // [[nodiscard]] auto isValue() const -> bool { return id.isValue(); }
  auto getCost(llvm::TargetTransformInfo &TTI, VectorWidth W)
    -> RecipThroughputLatency {
    RecipThroughputLatency c = costs[W];
    if (c.notYetComputed()) costs[W] = c = calcCost(TTI, W.getWidth());
    return c;
  }
  [[nodiscard]] inline auto calcCost(llvm::TargetTransformInfo &TTI,
                                     unsigned vectorWidth)
    -> RecipThroughputLatency;
  [[nodiscard]] auto getType(unsigned int vectorWidth) const -> llvm::Type * {
    return cost::getType(type, vectorWidth);
  }
  [[nodiscard]] auto getOperandInfo(unsigned i) const
    -> llvm::TargetTransformInfo::OperandValueInfo {
    if (llvm::Value *v = operands[i]->getValue())
      return llvm::TargetTransformInfo::getOperandInfo(v);
    return llvm::TargetTransformInfo::OperandValueInfo{};
  }
  [[nodiscard]] auto getCmpPredicate() const -> llvm::CmpInst::Predicate {
    invariant(getKind() == VK_Oprn);
    return llvm::cast<llvm::CmpInst>(inst)->getPredicate();
  }
  [[nodiscard]] auto operandIsLoad(unsigned i = 0) const -> bool {
    return getOperand(i)->isLoad();
  }
  [[nodiscard]] auto userIsStore() const -> bool {
    return std::ranges::any_of(getUsers(),
                               [](auto *u) { return u->isStore(); });
  }
  // used to check if fmul can be folded with a `-`, in
  // which case it is free
  [[nodiscard]] inline auto allUsersAdditiveContract() const -> bool;

}; // class Inst

struct InstByValue {
  Compute *inst;
  auto operator==(InstByValue const &other) const -> bool {
    return *inst == *other.inst;
  }
};

// some opaque function
class OpaqueFunc {
  Compute *const ins;

public:
  constexpr operator Compute *() const { return ins; }
  constexpr OpaqueFunc(Compute *I) : ins(I) {
    invariant(ins->getKind(), Node::VK_Func);
  }
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return ins->getOperands();
  }
  auto getFunction() -> llvm::Function * {
    return ins->getLLVMInstruction()->getFunction();
  }
  auto calcCallCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
    -> RecipThroughputLatency {
    llvm::Type *T = ins->getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> argTypes;
    for (auto *op : getOperands()) argTypes.push_back(op->getType(vectorWidth));
    // we shouldn't be hitting here
    return {
      TTI.getCallInstrCost(getFunction(), T, argTypes,
                           llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getCallInstrCost(getFunction(), T, argTypes,
                           llvm::TargetTransformInfo::TCK_Latency)};
  }
  auto calcCallCost(llvm::TargetTransformInfo &TTI, llvm::Function *F,
                    unsigned int vectorWidth) -> RecipThroughputLatency {
    llvm::Type *T = ins->getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> argTypes;
    for (auto *op : getOperands()) argTypes.push_back(op->getType(vectorWidth));
    return {TTI.getCallInstrCost(
              F, T, argTypes, llvm::TargetTransformInfo::TCK_RecipThroughput),
            TTI.getCallInstrCost(F, T, argTypes,
                                 llvm::TargetTransformInfo::TCK_Latency)};
  }
};
// a non-call
class Operation {
  Compute *const ins;

public:
  constexpr operator Compute *() const { return ins; }
  constexpr Operation(Compute *I)
    : ins(I->getKind() == Node::VK_Oprn ? I : nullptr) {}
  constexpr Operation(Node *n)
    : ins(n->getKind() == Node::VK_Oprn ? static_cast<Compute *>(n) : nullptr) {
  }
  constexpr explicit operator bool() const { return ins; }
  [[nodiscard]] auto getOpCode() const -> llvm::Intrinsic::ID {
    return ins->getOpId();
  }
  static auto getOpCode(llvm::Value *v) -> std::optional<llvm::Intrinsic::ID> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return i->getOpcode();
    return {};
  }
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return ins->getOperands();
  }
  [[nodiscard]] constexpr auto getOperand(size_t i) const -> Value * {
    return ins->getOperand(i);
  }
  [[nodiscard]] constexpr auto getNumOperands() const -> unsigned {
    return ins->getNumOperands();
  }
  [[nodiscard]] constexpr auto isInstruction(llvm::Intrinsic::ID opCode) const
    -> bool {
    return getOpCode() == opCode;
  }
  static auto isFMul(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFMul();
    return false;
  }
  static auto isFNeg(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFNeg();
    return false;
  }
  static auto isFMulOrFNegOfFMul(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFMulOrFNegOfFMul();
    return false;
  }
  static auto isFAdd(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFAdd();
    return false;
  }
  static auto isFSub(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFSub();
    return false;
  }
  static auto isShuffle(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isShuffle();
    return false;
  }
  static auto isFcmp(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isFcmp();
    return false;
  }
  static auto isIcmp(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isIcmp();
    return false;
  }
  static auto isCmp(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isCmp();
    return false;
  }
  static auto isSelect(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isSelect();
    return false;
  }
  static auto isExtract(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isExtract();
    return false;
  }
  static auto isInsert(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isInsert();
    return false;
  }
  static auto isExtractValue(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isExtractValue();
    return false;
  }
  static auto isInsertValue(Node *n) -> bool {
    if (auto op = Operation(n)) return op.isInsertValue();
    return false;
  }
  [[nodiscard]] auto isFMul() const -> bool {
    return isInstruction(llvm::Instruction::FMul);
  }
  [[nodiscard]] auto isFNeg() const -> bool {
    return isInstruction(llvm::Instruction::FNeg);
  }
  [[nodiscard]] auto isFMulOrFNegOfFMul() const -> bool {
    return isFMul() || (isFNeg() && isFMul(getOperands()[0]));
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

  [[nodiscard]] auto getType() const -> llvm::Type * { return ins->getType(); }
  [[nodiscard]] auto getType(unsigned w) const -> llvm::Type * {
    return ins->getType(w);
  }
  auto calcUnaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                               unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    auto op0info = ins->getOperandInfo(0);
    llvm::Type *T = getType(vectorWidth);
    llvm::Intrinsic::ID id = getOpCode();
    return {TTI.getArithmeticInstrCost(
              id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info),
            TTI.getArithmeticInstrCost(
              id, T, llvm::TargetTransformInfo::TCK_Latency, op0info)};
  }
  [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
    return ins->getLLVMInstruction();
  }
  auto calcBinaryArithmeticCost(llvm::TargetTransformInfo &TTI,
                                unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    auto op0info = ins->getOperandInfo(0);
    auto op1info = ins->getOperandInfo(1);
    llvm::Type *T = getType(vectorWidth);
    llvm::Intrinsic::ID id = getOpCode();
    return {TTI.getArithmeticInstrCost(
              id, T, llvm::TargetTransformInfo::TCK_RecipThroughput, op0info,
              op1info),
            TTI.getArithmeticInstrCost(
              id, T, llvm::TargetTransformInfo::TCK_Latency, op0info, op1info)};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto getPredicate() const -> llvm::CmpInst::Predicate {
    if (isSelect())
      return llvm::cast<Compute>(getOperand(0))->getCmpPredicate();
    assert(isCmp());
    if (auto *cmp = llvm::dyn_cast_or_null<llvm::CmpInst>(getInstruction()))
      return cmp->getPredicate();
    return isFcmp() ? llvm::CmpInst::BAD_FCMP_PREDICATE
                    : llvm::CmpInst::BAD_ICMP_PREDICATE;
  }
  auto calcCmpSelectCost(llvm::TargetTransformInfo &TTI,
                         unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    llvm::Type *T = getType(vectorWidth),
               *cmpT = llvm::CmpInst::makeCmpResultType(T);
    llvm::CmpInst::Predicate pred = getPredicate();
    llvm::Intrinsic::ID idt = getOpCode();
    return {
      TTI.getCmpSelInstrCost(idt, T, cmpT, pred,
                             llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getCmpSelInstrCost(idt, T, cmpT, pred,
                             llvm::TargetTransformInfo::TCK_Latency)};
  }

  /// for calculating the cost of a select when merging this instruction with
  /// another one.
  auto selectCost(llvm::TargetTransformInfo &TTI,
                  unsigned int vectorWidth) const -> llvm::InstructionCost {
    return selectCost(TTI, getType(vectorWidth));
  }
  static auto selectCost(llvm::TargetTransformInfo &TTI, llvm::Type *T)
    -> llvm::InstructionCost {
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
  auto getCastContext(llvm::TargetTransformInfo & /*TTI*/) const
    -> llvm::TargetTransformInfo::CastContextHint {
    if (ins->operandIsLoad() || ins->userIsStore())
      return llvm::TargetTransformInfo::CastContextHint::Normal;
    if (auto *cast = llvm::dyn_cast_or_null<llvm::CastInst>(getInstruction()))
      return llvm::TargetTransformInfo::getCastContextHint(cast);
    // TODO: check for whether mask, interleave, or reversed is likely.
    return llvm::TargetTransformInfo::CastContextHint::None;
  }
  auto calcCastCost(llvm::TargetTransformInfo &TTI,
                    unsigned int vectorWidth) const -> RecipThroughputLatency {
    llvm::Type *srcT = cost::getType(getOperand(0)->getType(), vectorWidth),
               *dstT = getType(vectorWidth);
    llvm::TargetTransformInfo::CastContextHint ctx = getCastContext(TTI);
    llvm::Intrinsic::ID idt = getOpCode();
    return {
      TTI.getCastInstrCost(idt, dstT, srcT, ctx,
                           llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getCastInstrCost(idt, dstT, srcT, ctx,
                           llvm::TargetTransformInfo::TCK_Latency)};
  }
  auto calculateCostFAddFSub(llvm::TargetTransformInfo &TTI,
                             unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    // TODO: allow not assuming hardware FMA support
    if ((isFMulOrFNegOfFMul(getOperand(0)) ||
         isFMulOrFNegOfFMul(getOperand(1))) &&
        ins->allowsContract())
      return {};
    return calcBinaryArithmeticCost(TTI, vectorWidth);
  }
  auto calculateFNegCost(llvm::TargetTransformInfo &TTI,
                         unsigned int vectorWidth) const
    -> RecipThroughputLatency {

    if (isFMul(getOperand(0)) && ins->allUsersAdditiveContract()) return {};
    return calcUnaryArithmeticCost(TTI, vectorWidth);
  }

  [[nodiscard]] auto calcCost(llvm::TargetTransformInfo &TTI,
                              unsigned int vectorWidth) const
    -> RecipThroughputLatency {
    switch (getOpCode()) {
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
    case llvm::Instruction::Select: return calcCmpSelectCost(TTI, vectorWidth);
    default: return RecipThroughputLatency::getInvalid();
    }
  }
};
// a call, e.g. fmuladd, sqrt, sin
class Call {
  Compute *ins;

public:
  constexpr operator Compute *() const { return ins; }
  constexpr Call(Compute *I) : ins(I) {
    invariant(ins->getKind(), Node::VK_Call);
  }

  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == Node::VK_Call;
  }
  [[nodiscard]] auto getIntrinsicID() const -> llvm::Intrinsic::ID {
    return ins->getOpId();
  }
  static auto getIntrinsicID(llvm::Value *v) -> llvm::Intrinsic::ID {
    if (auto *i = llvm::dyn_cast<llvm::IntrinsicInst>(v))
      return i->getIntrinsicID();
    return llvm::Intrinsic::not_intrinsic;
  }
  [[nodiscard]] constexpr auto isIntrinsic(llvm::Intrinsic::ID opCode) const
    -> bool {
    return ins->getOpId() == opCode;
  }

  [[nodiscard]] auto isMulAdd() const -> bool {
    return isIntrinsic(llvm::Intrinsic::fmuladd) ||
           isIntrinsic(llvm::Intrinsic::fma);
  }

  [[nodiscard]] auto getOperands() -> MutPtrVector<Value *> {
    return ins->getOperands();
  }
  [[nodiscard]] auto getOperands() const -> PtrVector<Value *> {
    return ins->getOperands();
  }
  [[nodiscard]] auto getOperand(size_t i) -> Value * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getOperand(size_t i) const -> Value * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getNumOperands() const -> size_t {
    return ins->getNumOperands();
  }
  auto calcCallCost(llvm::TargetTransformInfo &TTI, unsigned int vectorWidth)
    -> RecipThroughputLatency {
    llvm::Type *T = ins->getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> argTypes;
    for (auto *op : ins->getOperands())
      argTypes.push_back(op->getType(vectorWidth));
    llvm::Intrinsic::ID intrin = ins->getOpId();
    invariant(intrin != llvm::Intrinsic::not_intrinsic);
    llvm::IntrinsicCostAttributes attr(intrin, T, argTypes);
    return {
      TTI.getIntrinsicInstrCost(attr,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getIntrinsicInstrCost(attr, llvm::TargetTransformInfo::TCK_Latency)};
  }
};

inline auto Value::getCost(llvm::TargetTransformInfo &TTI, cost::VectorWidth W)
  -> cost::RecipThroughputLatency {
  if (auto *a = llvm::dyn_cast<Addr>(this)) return a->getCost(TTI, W);
  invariant(getKind() >= VK_Func);
  return static_cast<Compute *>(this)->getCost(TTI, W);
}
inline auto Value::getValue() -> llvm::Value * {
  if (auto *a = llvm::dyn_cast<Addr>(this)) return a->getInstruction();
  if (auto *I = llvm::dyn_cast<Compute>(this)) return I->getLLVMInstruction();
  invariant(getKind() == VK_CVal);
  return static_cast<CVal *>(this)->getValue();
}
inline auto Value::getValue() const -> const llvm::Value * {
  if (const auto *a = llvm::dyn_cast<Addr>(this)) return a->getInstruction();
  if (const auto *I = llvm::dyn_cast<Compute>(this))
    return I->getLLVMInstruction();
  invariant(getKind() == VK_CVal);
  return static_cast<const CVal *>(this)->getValue();
}
inline auto Value::getInstruction() -> llvm::Instruction * {
  if (auto *a = llvm::dyn_cast<Addr>(this)) return a->getInstruction();
  invariant(getKind() >= VK_Func);
  return static_cast<Compute *>(this)->getLLVMInstruction();
}
inline auto Value::getInstruction() const -> const llvm::Instruction * {
  if (const auto *a = llvm::dyn_cast<Addr>(this)) return a->getInstruction();
  invariant(getKind() >= VK_Func);
  return static_cast<const Compute *>(this)->getLLVMInstruction();
}
inline auto Value::getType() const -> llvm::Type * {
  if (const auto *a = llvm::dyn_cast<Addr>(this)) return a->getType();
  if (const auto *I = llvm::dyn_cast<Compute>(this)) return I->getType();
  if (const auto *C = llvm::dyn_cast<Cnst>(this)) return C->getType();
  invariant(getKind() == VK_CVal);
  return static_cast<const CVal *>(this)->getValue()->getType();
}
inline auto Value::getType(unsigned w) const -> llvm::Type * {
  return cost::getType(getType(), w);
}
[[nodiscard]] inline auto Compute::calcCost(llvm::TargetTransformInfo &TTI,
                                            unsigned vectorWidth)
  -> RecipThroughputLatency {
  if (auto op = Operation(this)) return op.calcCost(TTI, vectorWidth);
  if (auto call = Call(this)) return call.calcCallCost(TTI, vectorWidth);
  auto f = OpaqueFunc(this);
  invariant(f);
  return f.calcCallCost(TTI, vectorWidth);
}
[[nodiscard]] inline auto Compute::allUsersAdditiveContract() const -> bool {
  // NOLINTNEXTLINE(readability-use-anyofallof)
  for (auto *u : getUsers()) {
    auto *I = llvm::dyn_cast<Compute>(u);
    if (!I) return false;
    if (!I->allowsContract()) return false;
    Operation op = I;
    if (!op) return false;
    if (!(op.isFAdd() || op.isFSub())) return false;
  }
  return true;
}
[[nodiscard]] inline auto Value::getFastMathFlags() const
  -> llvm::FastMathFlags {
  if (const auto *I = llvm::dyn_cast<Compute>(this))
    return I->getFastMathFlags();
  return {};
}
[[nodiscard]] inline auto Value::getOperands() -> math::PtrVector<Value *> {
  if (const auto *I = llvm::dyn_cast<Compute>(this)) return I->getOperands();
  if (Stow stow{this}) return {stow.getStoredValPtr(), unsigned(1)};
  return {};
}
[[nodiscard]] inline auto Value::getOperand(unsigned i) -> Value * {
  if (const auto *I = llvm::dyn_cast<Compute>(this)) return I->getOperand(i);
  invariant(getKind() == VK_Stow);
  invariant(i == 0);
  return static_cast<Addr *>(this)->getStoredVal();
}
[[nodiscard]] inline auto Value::getOperand(unsigned i) const -> const Value * {
  if (const auto *I = llvm::dyn_cast<Compute>(this)) return I->getOperand(i);
  invariant(getKind() == VK_Stow);
  invariant(i == 0);
  return static_cast<const Addr *>(this)->getStoredVal();
}
[[nodiscard]] inline auto Value::getNumOperands() const -> unsigned {
  if (const auto *I = llvm::dyn_cast<Compute>(this)) return I->getNumOperands();
  return getKind() == VK_Stow;
}
[[nodiscard]] inline auto Value::associativeOperandsFlag() const -> uint8_t {
  if (const auto *I = llvm::dyn_cast<Compute>(this))
    return I->associativeOperandsFlag();
  return 0;
}
[[nodiscard]] inline auto Value::getNumScalarBits() const -> unsigned int {
  return getType()->getScalarSizeInBits();
}
[[nodiscard]] inline auto Value::getNumScalarBytes() const -> unsigned int {
  return getNumScalarBits() / 8;
}
[[nodiscard]] inline auto Value::getBasicBlock() const
  -> const llvm::BasicBlock * {
  if (const auto *I = getInstruction()) return I->getParent();
  return nullptr;
}
[[nodiscard]] inline auto Value::getBasicBlock() -> llvm::BasicBlock * {
  if (auto *I = getInstruction()) return I->getParent();
  return nullptr;
}
[[nodiscard]] constexpr auto Instruction::getIdentifier() const
  -> Instruction::Identifier {
  llvm::Intrinsic::ID id;
  if (const auto *I = llvm::dyn_cast<Compute>(this)) id = I->getOpId();
  switch (getKind()) {
  case Node::VK_Load: {
    id = llvm::Instruction::Load;
    break;
  }
  case Node::VK_Stow: {
    id = llvm::Instruction::Store;
    break;
  }
  default: id = llvm::Intrinsic::not_intrinsic;
  };
  return {id, getKind(), getType()};
}

inline void Instruction::setOperands(Arena<> *alloc,
                                     math::PtrVector<Value *> x) {
  if (auto *I = llvm::dyn_cast<Compute>(this)) return I->setOperands(alloc, x);
  invariant(getKind() == VK_Stow);
  static_cast<Addr *>(this)->setVal(x[0]);
}

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;

} // namespace poly::IR
