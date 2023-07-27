#pragma once
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include <ankerl/unordered_dense.h>
#include <llvm/ADT/Hashing.h>

namespace poly::Hash {

// update x with the hash `y`
constexpr auto combineHash(uint64_t x, uint64_t y) -> uint64_t {
  // trunc(UInt64,Int128(2)^64/big(Base.MathConstants.golden))
  constexpr uint64_t magic = 0x9e3779b97f4a7c15ULL;
  return x ^ (y + magic + (x << 6) + (x >> 2));
};
template <typename T> inline auto getHash(T const &x) noexcept -> uint64_t {
  return ankerl::unordered_dense::hash<T>{}(x);
}
} // namespace poly::Hash
template <> struct ankerl::unordered_dense::hash<poly::IR::Cnst::Identifier> {
  using is_avalanching = void;
  [[nodiscard]] auto
  operator()(poly::IR::Cnst::Identifier const &x) const noexcept -> uint64_t {
    using poly::Hash::combineHash, poly::Hash::getHash;
    uint64_t seed = getHash(x.kind);
    seed = combineHash(seed, getHash(x.typ));
    switch (x.kind) {
    case poly::IR::Node::VK_Cint:
      return combineHash(seed, getHash(x.payload.i));
    case poly::IR::Node::VK_Cflt:
      return combineHash(seed, getHash(x.payload.f));
    case poly::IR::Node::VK_Bint:
      return combineHash(seed, llvm::hash_value(*x.payload.ci));
    default:
      poly::invariant(x.kind == poly::IR::Node::VK_Bint);
      return combineHash(seed, llvm::hash_value(*x.payload.cf));
    }
  }
};

template <>
struct ankerl::unordered_dense::hash<poly::IR::Instruction::Identifier> {
  using is_avalanching = void;
  [[nodiscard]] auto
  operator()(poly::IR::Instruction::Identifier const &x) const noexcept
    -> uint64_t {
    using poly::Hash::combineHash, poly::Hash::getHash;
    uint64_t seed = getHash(x.kind);
    seed = combineHash(seed, getHash(x.type));
    return combineHash(seed, getHash(x.ID));
  }
};

/// here, we define an avalanching hash function for `InstByValue`
///
template <> struct ankerl::unordered_dense::hash<poly::IR::InstByValue> {
  using is_avalanching = void;
  [[nodiscard]] auto operator()(poly::IR::InstByValue const &x) const noexcept
    -> uint64_t {
    using poly::Hash::combineHash, poly::Hash::getHash, poly::containers::UList,
      poly::IR::Value;
    uint64_t seed = getHash(x.inst->getKind());
    seed = combineHash(seed, getHash(x.inst->getType()));
    seed = combineHash(seed, getHash(x.inst->getOpId()));
    if (x.inst->isIncomplete())
      return combineHash(seed, getHash(x.inst->getLLVMInstruction()));
    uint8_t assocFlag = x.inst->associativeOperandsFlag();
    // combine all operands
    size_t offset = 0;
    poly::PtrVector<Value *> operands = x.inst->getOperands();
    if (assocFlag) {
      poly::invariant(assocFlag, uint8_t(3));
      // we combine hashes in a commutative way
      seed = combineHash(seed, getHash(operands[0]) + getHash(operands[1]));
      offset = 2;
    }
    for (auto B = operands.begin() + offset, E = operands.end(); B != E; ++B)
      seed = combineHash(seed, getHash(*B));
    return seed;
  }
};
