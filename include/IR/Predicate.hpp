#pragma once

#include "Dicts/BumpVector.hpp"
#include <Containers/TinyVector.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Invariant.hpp>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/Pass.h>
#include <llvm/Support/Allocator.h>
#include <utility>
#include <variant>

namespace poly::IR {

using utils::invariant;
class Intr;

namespace Predicate {
enum struct Relation : uint8_t {
  Any = 0,
  True = 1,
  False = 2,
  Empty = 3,
};

constexpr auto operator&(Relation a, Relation b) -> Relation {
  return Relation(uint8_t(a) | uint8_t(b));
}
constexpr auto operator|(Relation a, Relation b) -> Relation {
  return Relation(uint8_t(a) & uint8_t(b));
}

/// Predicate::Intersection
/// Represents the intersection of up to 32 predicates.
/// These are represented by a 64-bit unsigned integer, which is interpreted as
/// a vector of 32 `Predicate::Relation`s. The specific instructions these
/// correspond to are stored in an ordered container.
struct Intersection {
  [[no_unique_address]] uint64_t predicates;
  constexpr Intersection() = default;
  constexpr Intersection(uint64_t pred) : predicates(pred) {}
  constexpr Intersection(size_t index, Relation value)
    : predicates(static_cast<uint64_t>(value) << (2 * index)) {}
  constexpr auto operator[](size_t index) const -> Relation {
    assert(index < 32);
    return static_cast<Relation>((predicates >> (2 * (index))) & 3);
  }
  void set(size_t index, Relation value) {
    assert(index < 32);
    index += index;
    uint64_t maskedOff = predicates & ~(3ULL << (index));
    predicates = maskedOff | static_cast<uint64_t>(value) << (index);
  }
  [[nodiscard]] auto intersect(size_t index, Relation value) const
    -> Intersection {
    assert(index < 32);
    index += index;
    return {predicates | static_cast<uint64_t>(value) << (index)};
  }
  struct Reference {
    [[no_unique_address]] uint64_t &rp;
    [[no_unique_address]] size_t index;
    operator Relation() const { return static_cast<Relation>(rp >> index); }
    auto operator=(Relation relation) -> Reference & {
      this->rp =
        (this->rp & ~(3 << index)) | (static_cast<uint64_t>(relation) << index);
      return *this;
    }
  };
  [[nodiscard]] auto operator[](size_t index) -> Reference {
    return {predicates, 2 * index};
  }
  [[nodiscard]] constexpr auto operator&(Intersection other) const
    -> Intersection {
    return {predicates | other.predicates};
  }
  auto operator&=(Intersection other) -> Intersection & {
    predicates |= other.predicates;
    return *this;
  }
  [[nodiscard]] constexpr auto popCount() const -> int {
    return std::popcount(predicates);
  }
  [[nodiscard]] constexpr auto getFirstIndex() const -> int {
    return std::countr_zero(predicates) / 2;
  }
  [[nodiscard]] constexpr auto getNextIndex(ptrdiff_t i) const -> ptrdiff_t {
    ++i;
    return std::countr_zero(predicates >> (2 * size_t(i))) / 2 + i;
  }
  /// returns 00 if non-empty, 01 if empty
  [[nodiscard]] static constexpr auto emptyMask(uint64_t x) -> uint64_t {
    return ((x & (x >> 1)) & 0x5555555555555555);
  }
  /// returns 11 if non-empty, 00 if empty
  [[nodiscard]] static constexpr auto keepEmptyMask(uint64_t x) -> uint64_t {
    uint64_t y = emptyMask(x);
    return (y | (y << 1));
  }
  /// returns 11 if non-empty, 00 if empty
  [[nodiscard]] static constexpr auto removeEmptyMask(uint64_t x) -> uint64_t {
    return ~keepEmptyMask(x);
  }
  [[nodiscard]] static constexpr auto isEmpty(uint64_t x) -> bool {
    return emptyMask(x) != 0;
  }
  /// returns `true` if the PredicateIntersection is empty, `false` otherwise
  [[nodiscard]] constexpr auto empty() const -> bool {
    return isEmpty(predicates);
  }
  [[nodiscard]] constexpr auto getConflict(Intersection other) const
    -> Intersection {
    uint64_t m = keepEmptyMask(predicates & other.predicates);
    return Intersection{predicates & m};
  }
  [[nodiscard]] constexpr auto countTrue() const {
    return std::popcount(predicates & 0x5555555555555555);
  }
  [[nodiscard]] constexpr auto countFalse() const {
    return std::popcount(predicates & 0xAAAAAAAAAAAAAAAA);
  }

  /// if the union between `this` and `other` can be expressed as an
  /// intersection of their constituents, return that intersection. Return an
  /// empty vector otherwise. The cases we handle are:
  /// (a & b) | a = a
  /// (a & b) | (a & !b) = a
  [[nodiscard]] constexpr auto compactUnion(Intersection other) const
    -> containers::TinyVector<Intersection, 2> {
    if (empty()) return {other};
    if (other.empty()) return {*this};
    uint64_t x = predicates, y = other.predicates;
    // 010000 = 010100 & 010000
    uint64_t intersect = x & y;
    if (x == intersect || y == intersect) return {Intersection{intersect}};
    // 011100 = 010100 | 011000
    // 010000 = 010100 & 011000
    // we can't handle (a & b) | (a & !b & c) because
    // (a & b) | (a & !b & c) = a & (b | c)) = (a & b) | (a & c)
    // bit representation:
    // 010000 = 010100 & 011001
    // we thus check all bits equal after masking off `b`.
    // We could consider returning a pair of options, so we can return the
    // simplified expression.
    uint64_t bitUnion = x | y;
    uint64_t mask = emptyMask(bitUnion);
    if (std::popcount(mask) == 1) { // a single b & !b case
      uint64_t remUnionMask =
        ~(mask | (mask << 1));      // 0s `b`, meaning b can be either.
      uint64_t w = remUnionMask & x;
      uint64_t z = remUnionMask & y;
      if (w == z) return {Intersection{w}};
      // if we now have
      //  a     |  a & c
      // 010000 | 010001
      uint64_t wz = w & z;
      if (wz == w) return {*this, Intersection{z}};
      if (wz == z) return {Intersection{w}, other};
    }
    return {};
  }
}; // struct Predicate::Intersection

/// Predicate::Set
/// This type is not owning!
/// A type for performing set algebra on predicates, representing sets
/// Note:
/// Commutative:
///     a | b == b | a
///     a & b == b & a
/// Distributive:
///     a | (b & c) == (a | b) & (a | c)
///     a & (b | c) == (a & b) | (a & c)
/// Associative:
///    a | (b | c) == (a | b) | c
///    a & (b & c) == (a & b) & c
/// Idempotent:
///    a | a == a
///    a & a == a
/// The internal representation can be interpreted as the intersection
/// of a vector of predicates.
/// This makes intersection operations efficient, but means we
/// may need to allocate new instructions to represent unions.
/// Unions are needed for merging divergent control flow branches.
/// For union calculation, we'd simplify:
/// (a & b) | (a & c) == a & (b | c)
/// If c == !b, then
/// (a & b) | (a & !b) == a & (b | !b) == a & True == a
/// Generically:
/// (a & b) | (c & d) == ((a & b) | c) & ((a & b) | d)
/// == (a | c) & (b | c) & (a | d) & (b | d)
struct Set {
  union {
    Intersection intersect;
    containers::UList<Intersection> *intersects;
  } intersectUnion;
  bool allocated{false};
  // ptrdiff_t count;
  // 0: empty
  // -1 if we have a single intersection
  // >=1 may still be empty, but it means we've allocated
  // and need to check `intersects`.
  //
  // containers::UList<Intersection> *intersectUnion{nullptr};
  // [[no_unique_address]] math::BumpPtrVector<Intersection> intersectUnion;
  Set() = default;
  explicit Set(Intersection pred) : intersectUnion(pred){};
  Set(const Set &) = default;
  Set(Set &&) = default;
  auto operator=(Set &&other) noexcept -> Set & {
    intersectUnion = other.intersectUnion;
    allocated = std::exchange(other.allocated, false);
    return *this;
  };
  auto operator=(const Set &other) -> Set & = default;
  [[nodiscard]] auto operator[](ptrdiff_t index) -> Intersection & {
    if (allocated) return (*intersectUnion.intersects)[index];
    invariant(index == 0);
    return intersectUnion.intersect;
  }
  [[nodiscard]] auto operator[](ptrdiff_t index) const -> Intersection {
    if (allocated) return (*intersectUnion.intersects)[index];
    invariant(index == 0);
    return intersectUnion.intersect;
  }
  [[nodiscard]] auto operator()(ptrdiff_t i, ptrdiff_t j) const -> Relation {
    return (*this)[i][j];
  }
  // [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
  //   return ptrdiff_t(std::max(ptrdiff_t(0), count));
  // }
  [[nodiscard]] constexpr auto empty() const -> bool {
    return allocated ? intersectUnion.intersects->empty()
                     : intersectUnion.intersect.empty();
  }
  [[nodiscard]] constexpr auto transform_reduce(auto init, const auto &f) {
    if (allocated) return intersectUnion.intersects->transform_reduce(init, f);
    return f(init, intersectUnion.intersect);
  }
  // auto getIntersects() -> containers::UList<Intersection> {
  //   if (count > 0) return *intersectUnion.intersects;
  //   return {intersectUnion.intersect};
  // }
  /// Cases we simplify:
  /// a | {} = a
  /// Impl: if either empty, set to other
  /// a | (a & b) == a & (a | b) == a
  /// Impl: if one is super set of other, set to subset
  /// (a & b) | (a & !b) == a
  /// Impl: if exactly one full intersection, zero that cond, check if
  /// remaining match, if so, set to remaining.
  /// (a & b) | !b == a | !b
  /// Impl: if one contains only one cond, drop that cond if it's reversed in
  /// other.
  /// Currently, it should be able to simplify:
  /// *this U other, where
  /// *this = (a & !b & c) | (a & !c)
  /// other = (a & b)
  /// to:
  /// (a & b) | (a & c) | (a & !c) = (a & b) | a = a
  /// TODO: handle more cases? Smarter algorithm that applies rewrite rules?
  auto Union(Arena<> *alloc, Intersection other) -> Set & {
    if (other.empty()) return *this;
    if (empty()) {
      if (allocated) intersectUnion.intersects->pushHasCapacity(other);
      else intersectUnion.intersect = other;
      return *this;
    }
    if (!allocated) { // fast path
      Intersection intersect = intersectUnion.intersect;
      auto u = intersect.compactUnion(other);
      if (u.size() == 1) {
        intersectUnion.intersect = u[0];
      } else {
        allocated = true;
        intersectUnion.intersects =
          alloc.create<containers::UList<Intersection>>();
        if (u.size() == 2) {
          intersectUnion.intersects->pushHasCapacity(u[0]);
          intersectUnion.intersects->pushHasCapacity(u[1]);
        } else {
          intersectUnion.intersects->pushHasCapacity(intersect);
          intersectUnion.intersects->pushHasCapacity(other);
        }
      }
      return *this;
    }
    // allocated == true
    bool simplifyPreds = false;
    containers::UList<Intersection> *intersects = intersectUnion.intersects;
    for (auto *l = intersects; l; l = l->getNext()) {
      for (auto it = l->dbegin(), e = l->dend(); it != e; ++it) {
        auto u = it->compactUnion(other);
        if (u.empty()) continue;
        *it = u[0];
        if (u.size() == 1) return *this;
        invariant(u.size() == 2);
        simplifyPreds = true;
        other = u[1];
      }
    }
    intersectUnion.intersects = intersects->push(alloc, other);
    if (simplifyPreds) simplify();
    return *this;
  }
  [[nodiscard]] constexpr auto begin() const {
    invariant(allocated);
    // poly::containers::UList<poly::IR::Predicate::Intersection>::Iterator it(
    //   intersectUnion.intersects->begin());
    // return it;
    return intersectUnion.intersects->begin();
  }
  [[nodiscard]] static constexpr auto end() {
    return containers::UList<IR::Predicate::Intersection>::end();
  }
  void simplify() const {
    bool simplifyPreds = allocated;
    while (simplifyPreds) {
      simplifyPreds = false;
      for (auto *l = intersectUnion.intersects; l; l = l->getNext()) {
        for (auto it = l->dbegin(), e = l->dend(); it != e; ++it) {
          for (auto *j = l; j; j = j->getNext()) {
            for (auto jt = j == l ? it + 1 : j->dbegin(), je = j->dend();
                 jt != je; ++jt) {

              auto u = it->compactUnion(*jt);
              if (u.empty()) continue;
              *it = u[0];
              simplifyPreds = true;
              if (u.size() == 2) {
                ASSERT((std::popcount(u[0].predicates) +
                        std::popcount(u[1].predicates)) <=
                       (std::popcount(it->predicates) +
                        std::popcount(jt->predicates)));
                *jt = u[1];
              } else j->eraseUnordered(jt--);
            }
          }
        }
      }
    }
  }
  /// if *this = [(a & b) | (c & d)]
  /// and other = [(e & f) | (g & h)]
  /// then
  /// [(a & b) | (c & d)] | [(e & f) | (g & h)] =
  ///   [(a & b) | (c & d) | (e & f) | (g & h)]
  auto Union(Arena<> *alloc, const Set &other) -> Set & {
    if (!other.allocated) return Union(alloc, other.intersectUnion.intersect);
    other.intersectUnion.intersects->forEach(
      [&](Intersection pred) { Union(alloc, pred); });
    return *this;
  }
  auto operator&=(Intersection pred) -> Set & {
    if (!allocated) {
      intersectUnion.intersect &= pred;
      return *this;
    }
    // for (auto *l = intersectUnion.intersects; l; l = l->getNext())
    //   for (auto it = l->begin(), e = l->localEnd(); it != e; ++it)
    //     if ((*it &= pred).isEmpty()) l->eraseUnordered(it--);
    for (auto *l = intersectUnion.intersects; l; l = l->getNext())
      for (auto it = l->dbegin(), e = l->dend(); it != e; ++it) {
        *it &= pred;
        if (it->empty()) l->eraseUnordered(it--);
      }
    simplify();
    return *this;
  }
  [[nodiscard]] auto operator&=(Set &pred) -> Set & {
    if (!pred.allocated) return *this &= pred.intersectUnion.intersect;
    pred.intersectUnion.intersects->forEach(
      [&](Intersection pred) { *this &= pred; });
    return *this;
  }
  auto copy(Arena<> *alloc) const -> Set {
    if (!allocated) return Set{intersectUnion.intersect};
    Set ret{};
    ret.intersectUnion.intersects = intersectUnion.intersects->copy(alloc);
    ret.allocated = true;
    return ret;
  }
  // [[nodiscard]] auto intersect(Arena<> *alloc, const Set &other) const {
  //   // old implementation had |= (a & b); // why?
  //   if (!allocated) return copy(alloc) &= other;
  //   return other->copy(alloc) &= *this;
  // }
  [[nodiscard]] auto getConflict(Intersection other) const -> Intersection {
    assert(intersectionIsEmpty(other));
    if (!allocated) return intersectUnion.intersect.getConflict(other);
    return intersectUnion.intersects->reduce(
      Intersection{}, [&](Intersection a, Intersection b) {
        return a &= b.getConflict(other);
      });
  }
  [[nodiscard]] auto getConflict(const Set &other) const -> Intersection {
    assert(intersectionIsEmpty(other));
    if (!allocated) return other.getConflict(intersectUnion.intersect);
    if (!other.allocated) return getConflict(other.intersectUnion.intersect);
    return intersectUnion.intersects->reduce(
      Intersection{}, [&](Intersection a, Intersection b) {
        return a &= other.getConflict(b);
      });
  }
  /// intersectionIsEmpty(const Set &other) -> bool
  /// returns `true` if the intersection of `*this` and `other` is empty
  /// if *this = [(a & b) | (c & d)]
  ///    other = [(e & f) | (g & h)]
  /// then
  /// [(a & b) | (c & d)] & [(e & f) | (g & h)] =
  ///   [(a & b) & (e & f)] |
  ///   [(a & b) & (g & h)] |
  ///   [(c & d) & (e & f)] |
  ///   [(c & d) & (g & h)]
  /// So iterating over the union elements, if any of them are not empty, then
  /// the intersection is not empty.
  [[nodiscard]] auto intersectionIsEmpty(const Set &other) const -> bool {
    for (auto pred : *this)
      for (auto otherPred : other)
        if (!((pred & otherPred).empty())) return false;
    return true;
  }
  [[nodiscard]] auto intersectionIsEmpty(Intersection otherPred) const -> bool {
    for (auto pred : *this) // NOLINT(readability-use-anyofallof)
      if (!((pred & otherPred).empty())) return false;
    return true;
  }

  // static auto getIndex(llvm::SmallVectorImpl<Instruction *> &instructions,
  //                      Instruction *instruction) -> size_t {
  //     size_t I = instructions.size();
  //     for (size_t i = 0; i < I; i++)
  //         if (instructions[i] == instruction)
  //             return i;
  //     instructions.push_back(instruction);
  //     return I;
  // }
  // PredicateSet() = default;
  // PredicateSet(Arena<> *alloc, Instruction::Cache
  // &ic,
  //                    llvm::SmallVector<Instruction *> &predicates,
  //                    Predicates &pred) {
  //     for (Predicate &p : pred) {
  //         Instruction *i = ic.get(alloc, p.condition);
  //         size_t index = getIndex(predicates, i);
  //         PredicateRelation val =
  //             p.flip ? PredicateRelation::False :
  //             PredicateRelation::True;
  //         set(index, val);
  //     }
  // }
}; // struct Predicate::Set

class Map;
}; // namespace Predicate
}; // namespace poly::IR
