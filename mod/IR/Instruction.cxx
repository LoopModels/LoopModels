#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <boost/container_hash/hash.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h>
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
#include <llvm/IR/Use.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <llvm/Support/MathExtras.h>
#include <optional>
#include <ostream>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Pair.cxx"
#include "Containers/UnrolledList.cxx"
#include "Dicts/Trie.cxx"
#include "IR/Address.cxx"
#include "IR/InstructionCost.cxx"
#include "IR/Node.cxx"
#include "IR/Predicate.cxx"
#include "Math/Array.cxx"
#include "Support/OStream.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Valid.cxx"
#else
export module IR:Instruction;
import Arena;
import Array;
import InstructionCost;
import Invariant;
import OStream;
import Pair;
import TargetMachine;
import Trie;
import UnrolledList;
import Valid;
import :Address;
import :Node;
import :Predicate;
#endif

namespace poly {
using math::PtrVector, math::MutPtrVector, alloc::Arena, utils::invariant,
  utils::Valid;
}; // namespace poly

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using containers::Pair;
using containers::UList, cost::VectorWidth, cost::VectorizationCosts;

auto containsCycle(Arena<> *alloc, const llvm::Instruction *,
                   dict::InlineTrie<llvm::Instruction const *> &,
                   const llvm::Value *) -> bool;

inline auto // NOLINTNEXTLINE(misc-no-recursion)
containsCycleCore(Arena<> *alloc, const llvm::Instruction *J,
                  dict::InlineTrie<llvm::Instruction const *> &visited,
                  const llvm::Instruction *K) -> bool {
  for (const llvm::Use &op : K->operands())
    if (containsCycle(alloc, J, visited, op.get())) return true;
  return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
inline auto containsCycle(Arena<> *alloc, const llvm::Instruction *J,
                          dict::InlineTrie<llvm::Instruction const *> &visited,
                          const llvm::Value *V) -> bool {
  const auto *S = llvm::dyn_cast<llvm::Instruction>(V);
  if (S == J) return true;
  // `insert` returns `true` if we do insert, i.e.
  // if have not yet visited `S`
  // `false` if we have already visited.
  // We return `false` in that case to avoid repeating work
  return S && visited.insert(alloc, S) &&
         containsCycleCore(alloc, J, visited, S);
}

inline auto containsCycle(Arena<> alloc, llvm::Instruction const *S) -> bool {
  // don't get trapped in a different cycle
  dict::InlineTrie<llvm::Instruction const *> visited;
  return containsCycleCore(&alloc, S, visited, S);
}

/// Represents an instruction.
/// May be an Operation or Call
class Compute : public Instruction {

protected:
  llvm::Instruction *inst{nullptr};
  llvm::Intrinsic::ID opId;          // unsigned
  llvm::FastMathFlags fastMathFlags; // holds unsigned
  // VectorizationCosts costs;
  // FIXME: we have `loopdep` flag...
  uint32_t loopIndepFlag;
  int numOperands; // negative means incomplete
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

  static constexpr auto diffMask(ptrdiff_t smaller,
                                 ptrdiff_t larger) -> uint32_t {
    invariant(smaller <= larger);
    invariant(larger < 32);
    // return ((uint32_t(1) << (larger - smaller)) - 1) << smaller;
    uint32_t umask = ((uint32_t(1) << larger) - 1),
             lmask = ((uint32_t(1) << smaller) - 1);
    return umask ^ lmask;
  }
  static constexpr auto diffMask(Value *v, ptrdiff_t depth1) -> uint32_t {
    ptrdiff_t vDepth = v->getCurrentDepth();
    return vDepth < depth1 ? diffMask(vDepth, depth1) : 0;
  }

public:
  using Value::getType;
  Compute(const Compute &) = delete;
  Compute(ValKind k, llvm::Instruction *i, llvm::Intrinsic::ID id, int numOps)
    : Instruction(k, i->getType()), inst(i), opId(id),
      fastMathFlags(i->getFastMathFlags()), numOperands(numOps) {}
  constexpr Compute(ValKind k, llvm::Intrinsic::ID id, int numOps,
                    llvm::Type *t, llvm::FastMathFlags fmf)
    : Instruction(k, t), opId(id), fastMathFlags(fmf), numOperands(numOps) {}

  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_Func;
  }
  [[nodiscard]] constexpr auto
  getLLVMInstruction() const -> llvm::Instruction * {
    return inst;
  }
  [[nodiscard]] auto getBasicBlock() -> llvm::BasicBlock * {
    return inst ? inst->getParent() : nullptr;
  }
  static auto
  getIDKind(llvm::Instruction *I) -> Pair<llvm::Intrinsic::ID, ValKind> {
    if (auto *c = llvm::dyn_cast<llvm::CallInst>(I)) {
      if (auto *J = llvm::dyn_cast<llvm::IntrinsicInst>(c))
        return {J->getIntrinsicID(), VK_Call};
      return {llvm::Intrinsic::not_intrinsic, VK_Func};
    }
    return {I->getOpcode(), VK_Oprn};
  }
  auto argTypes(unsigned vectorWidth) -> llvm::SmallVector<llvm::Type *, 4> {
    llvm::SmallVector<llvm::Type *, 4> ret{};
    ret.resize(size_t(numOperands));
    for (auto *op : getOperands())
      ret.push_back(cost::getType(op->getType(), vectorWidth));
    return ret;
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
  [[nodiscard]] constexpr auto getOpId() const -> llvm::Intrinsic::ID {
    return opId;
  }
  constexpr auto getOperands() -> MutPtrVector<Value *> {
    return {operands, math::length(numOperands)};
  }
  // recursive thanks to Compute calling on args
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto calcLoopMask() -> int {
    if (loopdeps != std::numeric_limits<uint16_t>::max()) return loopdeps;
    uint16_t ld = 0;
    for (Value *v : getOperands()) ld |= v->calcLoopMask();
    return loopdeps = ld;
  }

  [[nodiscard]] constexpr auto getLoopIndepFlag() const -> uint32_t {
    return loopIndepFlag;
  }
  // First currentDepth bits:
  // 1s mean independent, 0 dependent
  // Remaining (left) bits are 0
  constexpr auto calcLoopIndepFlag(ptrdiff_t depth1) -> uint32_t {
    return (~loopdeps) & ((1 << depth1) - 1);
  }
  /// Get the arguments to this function
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return {const_cast<Value **>(operands), math::length(numOperands)};
  }
  /// Get the `i`th argument of this function
  [[nodiscard]] constexpr auto getOperand(ptrdiff_t i) const -> Value * {
    return operands[i];
  }
  constexpr void setOperands(Arena<> *alloc, PtrVector<Value *> ops) {
    getOperands() << ops;
    for (auto *op : ops) op->addUser(alloc, this);
  }
  constexpr void
  setFast(llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast()) {
    fastMathFlags = fmf;
  }
  [[nodiscard]] constexpr auto getFastMathFlags() const -> llvm::FastMathFlags {
    return fastMathFlags;
  }
  [[nodiscard]] auto allowsContract() const -> bool {
    return fastMathFlags.allowContract();
  }
  [[nodiscard]] auto reassociableArgs() const -> uint32_t {
    if (!fastMathFlags.allowReassoc()) return 0;
    return isMulAdd() ? 0x4 : ((0x1 << numOperands) - 1);
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
  // [[nodiscard]] auto reducer()const->Compute*{
  //    isMulAdd() ?
  // }
  // Bitmask indicating which args are commutative
  // E.g. `muladd(a, b, c)` returns `0x3`
  // where the bitpattern is 11000000
  // indicating that the first two arguments are commutative.
  // That is, `muladd(a, b, c) == muladd(b, a, c)`.
  [[nodiscard]] auto commuatativeOperandsFlag() const -> uint8_t {
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
    if (uint8_t flag = commuatativeOperandsFlag()) {
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

  template <size_t N, bool TTI>
  auto getCost(target::Machine<TTI> target, unsigned width,
               std::array<CostKind, N> costKinds)
    -> std::array<llvm::InstructionCost, N> {
    // RecipThroughputLatency c = costs[W];
    // if (c.notYetComputed()) costs[W] = c = calcCost(TTI, W.getWidth());
    // return c;
    return calcCost(target, width, costKinds);
  }
  template <bool TTI>
  auto getCost(target::Machine<TTI> target, unsigned width,
               CostKind costKind = CostKind::TCK_RecipThroughput)
    -> llvm::InstructionCost {
    return calcCost<1, TTI>(target, width, {costKind})[0];
  }
  template <size_t N, bool TTI>
  [[nodiscard]] inline auto
  calcCost(target::Machine<TTI>, unsigned,
           std::array<CostKind, N>) -> std::array<llvm::InstructionCost, N>;
  template <bool TTI>
  [[nodiscard]] inline auto
  calcCost(target::Machine<TTI>, unsigned,
           CostKind = CostKind::TCK_RecipThroughput) -> llvm::InstructionCost;
  [[nodiscard]] auto getType(unsigned int vectorWidth) const -> llvm::Type * {
    return cost::getType(getType(), vectorWidth);
  }
  [[nodiscard]] auto getCmpPredicate() const -> llvm::CmpInst::Predicate {
    invariant(getKind() == VK_Oprn);
    // FIXME: need to remove `inst`
    return llvm::cast<llvm::CmpInst>(inst)->getPredicate();
  }
  [[nodiscard]] auto operandIsLoad(unsigned i = 0) const -> bool {
    return getOperand(i)->isLoad();
  }
  [[nodiscard]] auto userIsStore() const -> bool {
    return std::ranges::any_of(getUsers(),
                               [](auto *u) { return u->isStore(); });
  }
  // used to check if fmul can be folded with a `+`/`-`, in
  // which case it is free.
  // It peels through arbitrary numbers of `FNeg`.
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto allUsersAdditiveContract() const -> bool {
    return std::ranges::all_of(getUsers(), [](Instruction *U) -> bool {
      auto *C = llvm::dyn_cast<Compute>(U);
      return C && ((C->allowsContract() && C->isAddOrSub()) ||
                   (C->isFNeg() && C->allUsersAdditiveContract()));
    });
  }
  [[nodiscard]] constexpr auto isAddOrSub() const -> bool {
    llvm::Intrinsic::ID id = getOpId();
    return getKind() == VK_Oprn &&
           (id == llvm::Instruction::FAdd || id == llvm::Instruction::FSub);
  }
  [[nodiscard]] constexpr auto isFNeg() const -> bool {
    return getKind() == VK_Oprn && getOpId() == llvm::Instruction::FNeg;
  }
  [[nodiscard]] constexpr auto isFMul() const -> bool {
    return getKind() == VK_Oprn && getOpId() == llvm::Instruction::FMul;
  }
  [[nodiscard]] constexpr auto canContract() const -> bool {
    return allowsContract() && allUsersAdditiveContract();
  }
  static auto stripFNeg(Compute *C) -> Instruction * {
    for (; C->isFNeg() && C->getUsers().size() == 1;) {
      Instruction *I = *C->getUsers().begin();
      C = llvm::dyn_cast<Compute>(I);
      if (!C) return I;
    }
    return C;
  }

}; // class Compute

struct InstByValue {
  Compute *inst;

private:
  friend auto operator==(InstByValue a, InstByValue b) -> bool {
    if (a.inst == b.inst) return true;
    return *a.inst == *b.inst;
  }
  [[nodiscard]] friend auto hash_value(InstByValue x) noexcept -> size_t {
    auto seed = static_cast<size_t>(x.inst->getKind());
    boost::hash_combine(seed, x.inst->getType());
    boost::hash_combine(seed, x.inst->getOpId());
    if (x.inst->isIncomplete()) {
      boost::hash_combine(seed, x.inst->getLLVMInstruction());
      return seed;
    }
    uint8_t commute_flag = x.inst->commuatativeOperandsFlag(),
            commute_iter = commute_flag;
    // combine all operands
    PtrVector<Value *> operands = x.inst->getOperands();
    uint64_t commute_hash{};
    // all commutative operands have their hashes added, so that all
    // permutations of these operands hash the same way.
    // `hash_combine` isn't commutative
    // thus, `a + b` and `b + a` have the same hash, but
    // `a - b` and `b - a` do not.
    for (auto *op : operands) {
      if (commute_iter & 1) commute_hash += dict::fastHash(op);
      else boost::hash_combine(seed, op);
      commute_iter >>= 1;
    }
    if (commute_flag) boost::hash_combine(seed, commute_hash);
    return seed;
  }
};

// some opaque function
class OpaqueFunc {
  Compute *const ins_;
  using CostKind = Instruction::CostKind;

public:
  constexpr operator Compute *() const { return ins_; }
  constexpr OpaqueFunc(Compute *I) : ins_(I) {
    invariant(ins_->getKind(), Node::VK_Func);
  }
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return ins_->getOperands();
  }
  auto getFunction() -> llvm::Function * {
    return ins_->getLLVMInstruction()->getFunction();
  }
  template <size_t N, bool TTI>
  auto calcCallCost(target::Machine<TTI> target, unsigned int vectorWidth,
                    std::array<CostKind, N> costKinds)
    -> std::array<llvm::InstructionCost, N> {
    return calcCallCost(target, getFunction(), vectorWidth, costKinds);
  }
  template <size_t N, bool TTI>
  auto calcCallCost(target::Machine<TTI> target, llvm::Function *F,
                    unsigned int vectorWidth, std::array<CostKind, N> costKinds)
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *T = ins_->getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> arg_typs{ins_->argTypes(vectorWidth)};
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getCallInstrCost(F, T, arg_typs, costKinds[n]);
    return ret;
  }
};
// a non-call
class Operation {
  Compute *const ins_;
  using CostKind = Instruction::CostKind;

public:
  constexpr operator Compute *() const { return ins_; }
  constexpr Operation(Compute *I)
    : ins_(I->getKind() == Node::VK_Oprn ? I : nullptr) {}
  constexpr Operation(Node *n)
    : ins_(n->getKind() == Node::VK_Oprn ? static_cast<Compute *>(n)
                                         : nullptr) {}
  constexpr explicit operator bool() const { return ins_; }
  [[nodiscard]] auto getOpCode() const -> llvm::Intrinsic::ID {
    return ins_->getOpId();
  }
  static auto getOpCode(llvm::Value *v) -> std::optional<llvm::Intrinsic::ID> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v)) return i->getOpcode();
    return {};
  }
  [[nodiscard]] constexpr auto getOperands() const -> PtrVector<Value *> {
    return ins_->getOperands();
  }
  [[nodiscard]] constexpr auto getOperand(ptrdiff_t i) const -> Value * {
    return ins_->getOperand(i);
  }
  [[nodiscard]] constexpr auto getNumOperands() const -> unsigned {
    return ins_->getNumOperands();
  }
  [[nodiscard]] auto isInstruction(llvm::Intrinsic::ID opCode) const -> bool {
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

  [[nodiscard]] auto getType() const -> llvm::Type * { return ins_->getType(); }
  [[nodiscard]] auto getType(unsigned w) const -> llvm::Type * {
    return ins_->getType(w);
  }
  template <size_t N, bool TTI>
  [[nodiscard]] auto
  calcUnaryArithmeticCost(target::Machine<TTI> target, unsigned int vectorWidth,
                          std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *T = getType(vectorWidth);
    llvm::Intrinsic::ID id = getOpCode();
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getArithmeticInstrCost(id, T, costKinds[n]);
    return ret;
  }
  [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
    return ins_->getLLVMInstruction();
  }
  template <size_t N, bool TTI>
  [[nodiscard]] auto calcBinaryArithmeticCost(target::Machine<TTI> target,
                                              unsigned int vectorWidth,
                                              std::array<CostKind, N> costKinds)
    const -> std::array<llvm::InstructionCost, N> {
    llvm::Type *T = getType(vectorWidth);
    llvm::Intrinsic::ID id = getOpCode();
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getArithmeticInstrCost(id, T, costKinds[n]);
    return ret;
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
  template <size_t N, bool TTI>
  [[nodiscard]] auto calcCmpSelectCost(target::Machine<TTI> target,
                                       unsigned int vectorWidth,
                                       std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *T = getType(vectorWidth),
               *cmpT = llvm::CmpInst::makeCmpResultType(T);
    llvm::CmpInst::Predicate pred = getPredicate();
    llvm::Intrinsic::ID idt = getOpCode();
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getCmpSelInstrCost(idt, T, cmpT, pred, costKinds[n]);
    return ret;
  }

  /// for calculating the cost of a select when merging this instruction with
  /// another one.
  template <size_t N, bool TTI>
  [[nodiscard]] auto selectCost(target::Machine<TTI> target,
                                unsigned int vectorWidth,
                                std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    return selectCost(target, getType(vectorWidth), costKinds);
  }
  template <size_t N, bool TTI>
  static auto selectCost(target::Machine<TTI> target, llvm::Type *T,
                         std::array<CostKind, N> costKinds)
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *cmpT = llvm::CmpInst::makeCmpResultType(T);
    // llvm::CmpInst::Predicate pred =
    // TODO: extract from difference in predicates
    // between this and other (which would have to be passed in).
    // However, X86TargetTransformInfo doesn't use this for selects,
    // so doesn't seem like we need to bother with it.
    llvm::CmpInst::Predicate pred = T->isFPOrFPVectorTy()
                                      ? llvm::CmpInst::BAD_FCMP_PREDICATE
                                      : llvm::CmpInst::BAD_ICMP_PREDICATE;
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getCmpSelInstrCost(llvm::Instruction::Select, T, cmpT,
                                         pred, costKinds[n]);
    return ret;
  }
  template <bool TTI>
  [[nodiscard]] auto
  selectCost(target::Machine<TTI> target, unsigned int vectorWidth,
             CostKind costKind = CostKind::TCK_RecipThroughput) const
    -> llvm::InstructionCost {
    return selectCost<1>(target, getType(vectorWidth),
                         std::array<CostKind, 1>{costKind})[0];
  }
  template <bool TTI>
  [[nodiscard]] static auto
  selectCost(target::Machine<TTI> target, llvm::Type *T,
             CostKind costKind = CostKind::TCK_RecipThroughput)
    -> llvm::InstructionCost {
    return selectCost<1>(target, T, std::array<CostKind, 1>{costKind})[0];
  }
  [[nodiscard]] auto
  getCastContext() const -> llvm::TargetTransformInfo::CastContextHint {
    if (ins_->operandIsLoad() || ins_->userIsStore())
      return llvm::TargetTransformInfo::CastContextHint::Normal;
    if (auto *cast = llvm::dyn_cast_or_null<llvm::CastInst>(getInstruction()))
      return llvm::TargetTransformInfo::getCastContextHint(cast);
    // TODO: check for whether mask, interleave, or reversed is likely.
    return llvm::TargetTransformInfo::CastContextHint::None;
  }
  template <size_t N, bool TTI>
  [[nodiscard]] auto calcCastCost(target::Machine<TTI> target,
                                  unsigned int vectorWidth,
                                  std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *srcT = cost::getType(getOperand(0)->getType(), vectorWidth),
               *dstT = getType(vectorWidth);
    llvm::TargetTransformInfo::CastContextHint ctx = getCastContext();
    llvm::Intrinsic::ID idt = getOpCode();
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getCastInstrCost(idt, dstT, srcT, ctx, costKinds[n]);
    return ret;
  }
  template <bool TTI>
  [[nodiscard]] auto
  calcCastCost(target::Machine<TTI> target, unsigned int vectorWidth,
               CostKind costKind = CostKind::TCK_RecipThroughput) const
    -> llvm::InstructionCost {
    return calcCastCost<1>(target, vectorWidth, {costKind})[0];
  }
  // `getAltInstrCost`?
  // https://llvm.org/doxygen/classllvm_1_1TargetTransformInfo.html#ac442c18de69f9270e02ee8e35113502c
  // Useful for checking vfmaddsub or vfmsubadd?
  template <size_t N, bool TTI>
  [[nodiscard]] auto
  calculateCostFAddFSub(target::Machine<TTI> target, unsigned int vectorWidth,
                        std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    // TODO: allow not assuming hardware FMA support
    if ((isFMulOrFNegOfFMul(getOperand(0)) ||
         isFMulOrFNegOfFMul(getOperand(1))) &&
        ins_->allowsContract())
      return {};

    return calcBinaryArithmeticCost(target, vectorWidth, costKinds);
  }
  /// return `0` if all users are fusible with the `fmul`
  /// Fusion possibilities:
  /// fmadd a * b + c
  /// fmsub a * b - c
  /// fnmadd c - a * b // maybe -(a * b) + c ?
  /// fnmsub -(a * b) - c
  template <size_t N, bool TTI>
  [[nodiscard]] auto calculateCostFMul(target::Machine<TTI> target,
                                       unsigned int vectorWidth,
                                       std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    if (target.hasFMA() && ins_->canContract()) return {};
    return calcBinaryArithmeticCost(target, vectorWidth, costKinds);
  }
  template <size_t N, bool TTI>
  [[nodiscard]] auto calculateFNegCost(target::Machine<TTI> target,
                                       unsigned int vectorWidth,
                                       std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    // TODO: we aren't checking for fadd/fsub; should we ensure IR
    // canonicalization?
    if (target.hasFMA() &&
        std::ranges::all_of(ins_->getUsers(), [](Instruction *U) -> bool {
          auto *C = llvm::dyn_cast<Compute>(U);
          return C->isFMul() && C->canContract();
        }))
      return {};
    return calcUnaryArithmeticCost(target, vectorWidth, costKinds);
  }
  template <size_t N, bool TTI>
  [[nodiscard]] auto calcCost(target::Machine<TTI> target,
                              unsigned int vectorWidth,
                              std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    switch (getOpCode()) {
    case llvm::Instruction::FMul:
      return calculateCostFMul(target, vectorWidth, costKinds);
    case llvm::Instruction::FAdd:
    case llvm::Instruction::FSub:
    case llvm::Instruction::Add:
    case llvm::Instruction::Sub:
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
      return calcBinaryArithmeticCost(target, vectorWidth, costKinds);
    case llvm::Instruction::FNeg:
      // one arg arithmetic cost
      return calculateFNegCost(target, vectorWidth, costKinds);
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
      return calcCastCost(target, vectorWidth, costKinds);
    case llvm::Instruction::ICmp:
    case llvm::Instruction::FCmp:
    case llvm::Instruction::Select:
      return calcCmpSelectCost(target, vectorWidth, costKinds);
    default:
      std::array<llvm::InstructionCost, N> ret;
      ret.fill(llvm::InstructionCost::getInvalid());
      return ret;
    }
  }
  template <bool TTI>
  [[nodiscard]] auto
  calcCost(target::Machine<TTI> target, unsigned int vectorWidth,
           CostKind costKind = CostKind::TCK_RecipThroughput) const
    -> llvm::InstructionCost {
    return calcCost<1>(target, vectorWidth, {costKind})[0];
  }
};
// a call, e.g. fmuladd, sqrt, sin
class Call {
  Compute *ins;
  using CostKind = Instruction::CostKind;

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
  [[nodiscard]] constexpr auto
  isIntrinsic(llvm::Intrinsic::ID opCode) const -> bool {
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
  [[nodiscard]] auto getOperand(ptrdiff_t i) -> Value * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getOperand(ptrdiff_t i) const -> Value * {
    return ins->getOperand(i);
  }
  [[nodiscard]] auto getNumOperands() const -> size_t {
    return ins->getNumOperands();
  }
  template <size_t N, bool TTI>
  auto calcCallCost(target::Machine<TTI> target, unsigned int vectorWidth,
                    std::array<CostKind, N> costKinds)
    -> std::array<llvm::InstructionCost, N> {
    llvm::Type *T = ins->getType(vectorWidth);
    llvm::SmallVector<llvm::Type *, 4> arg_typs{ins->argTypes(vectorWidth)};
    llvm::Intrinsic::ID intrin = ins->getOpId();
    invariant(intrin != llvm::Intrinsic::not_intrinsic);
    llvm::IntrinsicCostAttributes attr(intrin, T, arg_typs);
    std::array<llvm::InstructionCost, N> ret;
    for (size_t n = 0; n < N; ++n)
      ret[n] = target.getIntrinsicInstrCost(attr, costKinds[n]);
    return ret;
  }
};
// inline auto // NOLINTNEXTLINE(misc-no-recursion)
// Value::getCost(const llvm::TargetTransformInfo &TTI,
//                cost::VectorWidth W) -> cost::RecipThroughputLatency {
//   if (auto *a = llvm::dyn_cast<Addr>(this)) return a->getCost(TTI, W);
//   invariant(getKind() >= VK_Func);
//   return static_cast<Compute *>(this)->getCost(TTI, W);
// }
// template <bool TTI>
// inline auto // NOLINTNEXTLINE(misc-no-recursion)
// Value::getCost(target::Machine<TTI> machine,
//                cost::VectorWidth W) -> cost::RecipThroughputLatency {
//   if constexpr (!TTI) {
//     if (auto *a = llvm::dyn_cast<Addr>(this)) return a->getCost(machine, W);
//     invariant(getKind() >= VK_Func);
//     return static_cast<Compute *>(this)->getCost(machine, W);
//   } else return getCost(machine.TTI, W);
// }

template <size_t N, bool TTI>
[[nodiscard]] inline auto Compute::calcCost(
  target::Machine<TTI> target, unsigned vectorWidth,
  std::array<CostKind, N> costKinds) -> std::array<llvm::InstructionCost, N> {
  if (auto op = Operation(this))
    return op.calcCost(target, vectorWidth, costKinds);
  if (auto call = Call(this))
    return call.calcCallCost(target, vectorWidth, costKinds);
  auto f = OpaqueFunc(this);
  invariant(f);
  return f.calcCallCost(target, vectorWidth, costKinds);
}
template <bool TTI>
[[nodiscard]] inline auto
Compute::calcCost(target::Machine<TTI> target, unsigned vectorWidth,
                  CostKind costKind) -> llvm::InstructionCost {
  return calcCost<1zU, TTI>(target, vectorWidth, std::array{costKind})[0];
}

// unsigned x = llvm::Instruction::FAdd;
// unsigned y = llvm::Instruction::LShr;
// unsigned z = llvm::Instruction::Call;
// unsigned w = llvm::Instruction::Load;
// unsigned v = llvm::Instruction::Store;
// // getIntrinsicID()
// llvm::Intrinsic::IndependentIntrinsics x = llvm::Intrinsic::sqrt;
// llvm::Intrinsic::IndependentIntrinsics y = llvm::Intrinsic::sin;

// [[nodiscard]] constexpr auto Addr::getReducingInstruction() const -> Compute
// * {
//   invariant(isStore());
//   return llvm::dyn_cast<Compute>(getStoredVal());
//   // auto *C = llvm::dyn_cast<Compute>(getStoredVal());
//   // return C ? C->reducer() : nullptr;
// }
// recursive thanks to Compute calling on args
// NOLINTNEXTLINE(misc-no-recursion)
constexpr auto Node::calcLoopMask() -> uint16_t {
  // if (Addr *a = llvm::dyn_cast<Addr>(this)) return a->calcLoopMask();
  if (auto *c = llvm::dyn_cast<Compute>(this)) return c->calcLoopMask();
  if (Loop *l = llvm::dyn_cast<Loop>(this)) return l->calcLoopMask();
  // if (Phi *p = llvm::dyn_cast<Phi>(this)) return p->calcLoopMask();
  return loopdeps;
}
[[nodiscard]] constexpr auto Node::checkDependsOnLoop(int depth0) -> bool {
  return calcLoopMask() >> depth0;
  // return (loopdeps >> depth) & 1;
}
inline auto operator<<(std::ostream &os, const Compute &C) -> std::ostream & {
  utils::printType(os, C.getType());
  C.printName(os << " ") << " = ";
  if (C.getKind() == Node::VK_Oprn) {
    if (C.getNumOperands() == 1) {
      invariant(C.getOpId() == llvm::Instruction::FNeg);
      C.getOperand(0)->printName(os << "-");
    } else if (C.getNumOperands() == 2) {
      C.getOperand(0)->printName(os) << " ";
      switch (C.getOpId()) {
      case llvm::Instruction::FAdd:
      case llvm::Instruction::Add: os << "+"; break;
      case llvm::Instruction::FSub:
      case llvm::Instruction::Sub: os << "-"; break;
      case llvm::Instruction::FMul:
      case llvm::Instruction::Mul: os << "*"; break;
      case llvm::Instruction::FDiv:
      case llvm::Instruction::SDiv:
      case llvm::Instruction::UDiv: os << "/"; break;
      case llvm::Instruction::FRem:
      case llvm::Instruction::SRem:
      case llvm::Instruction::URem: os << "%"; break;
      case llvm::Instruction::Shl: os << "<<"; break;
      case llvm::Instruction::LShr: os << ">>>"; break;
      case llvm::Instruction::AShr: os << ">>"; break;
      case llvm::Instruction::And: os << "&"; break;
      case llvm::Instruction::Or: os << "|"; break;
      case llvm::Instruction::Xor: os << "^"; break;
      default: os << "OpId<" << C.getOpId() << ">";
      }
      C.getOperand(1)->printName(os << " ");
    } else {
      invariant(C.getNumOperands() == 3);
    }
  } else {
    if (C.getKind() == Node::VK_Call) {
      switch (C.getOpId()) {
      case llvm::Intrinsic::abs: os << "abs"; break;
      case llvm::Intrinsic::smax: os << "smax"; break;
      case llvm::Intrinsic::smin: os << "smin"; break;
      case llvm::Intrinsic::umax: os << "umax"; break;
      case llvm::Intrinsic::umin: os << "umin"; break;
      case llvm::Intrinsic::sqrt: os << "sqrt"; break;
      case llvm::Intrinsic::powi: os << "powi"; break;
      case llvm::Intrinsic::sin: os << "sin"; break;
      case llvm::Intrinsic::cos: os << "cos"; break;
      case llvm::Intrinsic::exp: os << "exp"; break;
      case llvm::Intrinsic::exp2: os << "exp2"; break;
#if LLVM_VERSION_MAJOR >= 18
      case llvm::Intrinsic::exp10: os << "exp10"; break;
#endif
      case llvm::Intrinsic::ldexp: os << "ldexp"; break;
      case llvm::Intrinsic::frexp: os << "frexp"; break;
      case llvm::Intrinsic::log: os << "log"; break;
      case llvm::Intrinsic::log2: os << "log2"; break;
      case llvm::Intrinsic::log10: os << "log10"; break;
      case llvm::Intrinsic::fma: os << "fma"; break;
      case llvm::Intrinsic::fabs: os << "fabs"; break;
      case llvm::Intrinsic::minnum: os << "minnum"; break;
      case llvm::Intrinsic::maxnum: os << "maxnum"; break;
      case llvm::Intrinsic::minimum: os << "minimum"; break;
      case llvm::Intrinsic::maximum: os << "maximum"; break;
      case llvm::Intrinsic::copysign: os << "copysign"; break;
      case llvm::Intrinsic::floor: os << "floor"; break;
      case llvm::Intrinsic::ceil: os << "ceil"; break;
      case llvm::Intrinsic::trunc: os << "trunc"; break;
      case llvm::Intrinsic::rint: os << "rint"; break;
      case llvm::Intrinsic::nearbyint: os << "nearbyint"; break;
      case llvm::Intrinsic::round: os << "round"; break;
      case llvm::Intrinsic::roundeven: os << "roundeven"; break;
      case llvm::Intrinsic::lround: os << "lround"; break;
      case llvm::Intrinsic::llround: os << "llround"; break;
      case llvm::Intrinsic::lrint: os << "lrint"; break;
      case llvm::Intrinsic::llrint: os << "llrint"; break;
      case llvm::Intrinsic::bitreverse: os << "bitreverse"; break;
      case llvm::Intrinsic::bswap: os << "bswap"; break;
      case llvm::Intrinsic::ctpop: os << "ctpop"; break;
      case llvm::Intrinsic::ctlz: os << "ctlz"; break;
      case llvm::Intrinsic::cttz: os << "cttz"; break;
      case llvm::Intrinsic::fshl: os << "fshl"; break;
      case llvm::Intrinsic::fshr: os << "fshr"; break;
      case llvm::Intrinsic::fmuladd: os << "fmuladd"; break;
      default: os << "Intrin<" << C.getOpId() << ">";
      }
    } else {
      invariant(C.getKind() == Node::VK_Func);
      os << "opaque_fun";
    }
    os << "(";
    bool comma = false;
    for (Value *op : C.getOperands()) {
      if (comma) os << ", ";
      op->printName(os);
      comma = true;
    }
    os << ")";
  }
  return os;
}

} // namespace IR
