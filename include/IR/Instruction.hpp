
#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/InstructionCost.hpp"
#include "IR/Node.hpp"
#include "IR/Operands.hpp"
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
#include <llvm/Support/MathExtras.h>
#include <tuple>
#include <utility>

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

using dict::aset, dict::amap, containers::UList, utils::Optional,
  cost::RecipThroughputLatency, cost::VectorWidth, cost::VectorizationCosts;

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

class Inst : public Node {

protected:
  llvm::Instruction *inst{nullptr};
  llvm::Type *type;
  llvm::Intrinsic::ID op;            // unsigned
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
  Node *operands[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

public:
  constexpr Inst(ValKind k, llvm::Instruction *i, llvm::Intrinsic::ID id,
                 int numOps)
    : Node(k), inst(i), type(i->getType()), op(id), numOperands(numOps),
      fastMathFlags(i->getFastMathFlags()) {}
  constexpr Inst(ValKind k, llvm::Intrinsic::ID id, int numOps, llvm::Type *t,
                 llvm::FastMathFlags fmf)
    : Node(k), inst(nullptr), type(t), op(id), numOperands(numOps),
      fastMathFlags(fmf) {}

  // static constexpr auto construct(BumpAlloc<> &alloc, ValKind k,
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
  constexpr auto getLLVMInstruction() const -> llvm::Instruction * {
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
  constexpr auto getUsers() -> UList<Node *> * {
    invariant(kind >= VK_Func);
    return unionPtr.users;
  }
  constexpr void setUsers(UList<Node *> *newUsers) {
    invariant(kind >= VK_Func);
    unionPtr.users = newUsers;
  }
  constexpr void setNumOps(unsigned n) { numOperands = n; }
  // called when incomplete; flips sign
  constexpr auto numCompleteOps() -> unsigned {
    invariant(numOperands <= 0); // we'll allow 0 for now
    return numOperands = unsigned(-numOperands);
  }
  constexpr void makeIncomplete() { numOperands = -numOperands; }
  // constexpr auto getPredicate() -> UList<Node *> * { return predicates; }
  // constexpr auto getPredicate() const -> UList<Node *> const * {
  //   return predicates;
  // }
  constexpr auto getNumOperands() const -> unsigned {
    return unsigned(numOperands);
  }
  constexpr auto getType() const -> llvm::Type * { return type; }
  constexpr auto getOpId() const -> llvm::Intrinsic::ID { return op; }
  constexpr auto getOperands() -> MutPtrVector<Node *> {
    return {operands, numOperands};
  }
  constexpr auto getOperands() const -> PtrVector<Node *> {
    return {const_cast<Node **>(operands), unsigned(numOperands)};
  }
  constexpr auto getOperand(size_t i) const -> Node * { return operands[i]; }
  constexpr void setOperands(BumpAlloc<> &alloc, MutPtrVector<Node *> ops) {
    getOperands() << ops;
    for (auto *op : ops) op->addUser(alloc, this);
  }

  constexpr auto getFastMathFlags() const -> llvm::FastMathFlags {
    return fastMathFlags;
  }
  [[nodiscard]] auto allowsContract() const -> bool {
    return fastMathFlags.allowContract();
  }
  // Incomplete stores the correct number of ops it was allocated with as a
  // negative number. The primary reason for being able to check
  // completeness is for `==` checks and hashing.
  auto isComplete() const -> bool { return numOperands >= 0; }
  auto isIncomplete() const -> bool { return numOperands < 0; }
  [[nodiscard]] auto isCommutativeCall() const -> bool {
    if (auto *intrin = llvm::dyn_cast_or_null<llvm::IntrinsicInst>(inst))
      return intrin->isCommutative();
    return false;
  }
  [[nodiscard]] auto isMulAdd() const -> bool {
    return (getKind() == VK_Call) &&
           ((op == llvm::Intrinsic::fmuladd) || (op == llvm::Intrinsic::fma));
  }
  [[nodiscard]] auto associativeOperandsFlag() const -> uint8_t {
    switch (getKind()) {
    case VK_Call: return (isMulAdd() || isCommutativeCall()) ? 0x3 : 0;
    case VK_Oprn:
      switch (op) {
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
  auto operator==(Inst const &other) const -> bool {
    if (this == &other) return true;
    if ((getKind() != other.getKind()) || (op != other.op) ||
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
    if (c.notYetComputed()) costs[W] = c = calculateCost(TTI, W);
    return c;
  }
  [[nodiscard]] auto getType(unsigned int vectorWidth) const -> llvm::Type * {
    return cost::getType(type, vectorWidth);
  }
  [[nodiscard]] auto getNumScalarBits() const -> unsigned int {
    return type->getScalarSizeInBits();
  }
  [[nodiscard]] auto getNumScalarBytes() const -> unsigned int {
    return getNumScalarBits() / 8;
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
  [[nodiscard]] auto calculateCost(llvm::TargetTransformInfo &TTI,
                                   unsigned int vectorWidth)
    -> RecipThroughputLatency {
    if (Optional<const Intrinsic *> idt = getIntrinsic())
      return calcCost(*idt, TTI, vectorWidth);
    if (auto *F = getFunction()) return calcCallCost(TTI, F, vectorWidth);
    return {};
  }
  [[nodiscard]] static auto calcOperationCost(llvm::Intrinsic::ID id,
                                              llvm::TargetTransformInfo &TTI,
                                              unsigned int vectorWidth)
    -> RecipThroughputLatency {
    switch (id) {
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
      return calcBinaryArithmeticCost(TTI, id, vectorWidth);
    case llvm::Instruction::FNeg:
      // one arg arithmetic cost
      return calculateFNegCost(TTI, id, vectorWidth);
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
      return calcCastCost(TTI, id, vectorWidth);
    case llvm::Instruction::ICmp:
    case llvm::Instruction::FCmp:
    case llvm::Instruction::Select:
      return calcCmpSelectCost(TTI, id, vectorWidth);
    case llvm::Instruction::Load:
    case llvm::Instruction::Store:
      return calculateCostContiguousLoadStore(TTI, idt.opcode, vectorWidth);
    default: return RecipThroughputLatency::getInvalid();
    }
  }
  // [[nodiscard]] auto calcCallCost(llvm::Intrinsic::ID id,
  //                                 llvm::TargetTransformInfo &TTI,
  //                                 unsigned int vectorWidth)
  //   -> RecipThroughputLatency {
  //   return calcCallCost(TTI, id, vectorWidth);
  // }
  [[nodiscard]] auto calcCost(llvm::TargetTransformInfo &TTI,
                              unsigned vectorWidth) -> RecipThroughputLatency {
    if (getKind() == Node::VK_Oprn)
      return calcOperationCost(getOpId(), TTI, vectorWidth);
    return calcCallCost(TTI, getOpId(), vectorWidth);
  }

}; // class Inst

struct InstByValue {
  Inst *inst;
  auto operator==(InstByValue const &other) const -> bool {
    return *inst == *other.inst;
  }
};

// some opaque function
class OpaqueFunc {
  Inst *const ins;

public:
  constexpr operator Inst *() const { return ins; }
  constexpr OpaqueFunc(Inst *I) : ins(I) {
    invariant(ins->getKind(), Node::VK_Func);
  }
};
// a non-call
class Operation {
  Inst *const ins;

public:
  constexpr operator Inst *() const { return ins; }
  constexpr Operation(Inst *I)
    : ins(I->getKind() == Node::VK_Oprn ? I : nullptr) {}
  constexpr explicit operator bool() const { return ins; }
  [[nodiscard]] auto getOpCode() const -> llvm::Intrinsic::ID {
    return ins->getOpId();
  }
  static auto getOpCode(llvm::Value *v) -> std::optional<llvm::Intrinsic::ID> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return i->getOpcode();
    return {};
  }
  constexpr auto getOperands() const -> PtrVector<Node *> {
    return ins->getOperands();
  }
  constexpr auto getOperand(size_t i) const -> Node * {
    return ins->getOperand(i);
  }
  constexpr auto getNumOperands() const -> unsigned {
    return ins->getNumOperands();
  }
  [[nodiscard]] constexpr auto isInstruction(llvm::Intrinsic::ID opCode) const
    -> bool {
    return getOpCode() == opCode;
  }
  static auto isFMul(Node *n) -> bool {
    if (auto *op = llvm::dyn_cast<Operation>(n)) return op->isFMul();
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
};
// a call, e.g. fmuladd, sqrt, sin
class Call {
  Inst *ins;

public:
  constexpr operator Inst *() const { return ins; }
  constexpr Call(Inst *I) : ins(I) { invariant(ins->getKind(), Node::VK_Call); }

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

  [[nodiscard]] auto getOperands() -> MutPtrVector<Node *> {
    return ins->getOperands();
  }
  [[nodiscard]] auto getOperands() const -> PtrVector<Node *> {
    return ins->getOperands();
  }
  [[nodiscard]] auto getOperand(size_t i) -> Node * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getOperand(size_t i) const -> Node * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getNumOperands() const -> size_t {
    return ins->getNumOperands();
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
