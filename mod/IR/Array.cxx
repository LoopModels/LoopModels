#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <type_traits>

#ifndef USE_MODULE
#include "IR/Node.cxx"
#include "Containers/Tuple.cxx"
#include "Math/SOA.cxx"
#include "Containers/Pair.cxx"
#include "Numbers/Int8.cxx"
#include "Math/Array.cxx"
#else
export module IR:Array;
import Array;
import Int8;
import Pair;
import SOA;
import Tuple;
import :Node;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using numbers::u8, math::PtrVector, math::MutPtrVector, containers::Pair;
struct Array {
  static constexpr ptrdiff_t BasePointerIdx = 0;
  static constexpr ptrdiff_t SizesIdx = 1;
  static constexpr ptrdiff_t DimIdx = 2;
  static constexpr ptrdiff_t AlignShiftIdx = 3;
  using Tuple = containers::Tuple<IR::Value *, IR::Value **, u8, u8>;

  [[nodiscard]] constexpr auto basePointer() const -> IR::Value * {
    return datadeps_.template get<BasePointerIdx>(id_);
  }
  [[nodiscard]] constexpr auto getSizes() const -> PtrVector<IR::Value *> {
    return {datadeps_.template get<SizesIdx>(id_),
            math::length(ptrdiff_t(getDim()))};
  }
  [[nodiscard]] constexpr auto getDim() const -> u8 {
    return datadeps_.template get<DimIdx>(id_);
  }
  [[nodiscard]] constexpr auto alignmentShift() const -> u8 {
    return datadeps_.template get<AlignShiftIdx>(id_);
  }
  constexpr void setAlignmentShift(unsigned shift) {
    u8& s = datadeps_.template get<AlignShiftIdx>(id_);
    s = u8(std::max(unsigned(s), shift));
  }
  [[nodiscard]] constexpr auto alignment() const -> uint64_t {
    return uint64_t(1) << uint64_t(alignmentShift());
  }
  constexpr Array(math::ManagedSOA<Tuple> &datadeps, ptrdiff_t id)
    : datadeps_(datadeps), id_(id) {}

  [[nodiscard]] constexpr auto name() const -> char { return 'A' + id_; }

private:
  math::ManagedSOA<Tuple> &datadeps_;
  ptrdiff_t id_;

  friend constexpr auto operator==(Array x, Array y) -> bool {
    return x.id_ == y.id_;
  }
  friend auto operator<<(std::ostream &os, Array array) -> std::ostream & {
    os << array.name() << " - ";
    if (array.getDim() == 0) return os << "0-dimensional array";
    os << "[unknown";
    auto sz{array.getSizes()[math::_(0, math::end - 1)]};
    for (auto *d : sz) os << ", " << d;
    return os << "]";
  }
};
static_assert(std::is_trivially_copyable_v<Array>);
static_assert(std::is_trivially_destructible_v<Array>);
// Class hodling the various arrays
// One of the purposes is for making cache-tiling decisions.
// To that end, it's useful to have an idea of the unique set of indexed arrays.
// E.g., we may wish to merge or to create separate tiles.
// It is also useful to have alignment information for cost-modeling.
class Arrays {
  using Tuple = Array::Tuple;
  math::ManagedSOA<Tuple> datadeps_;

public:
  constexpr auto get(ptrdiff_t i) -> Array { return {datadeps_, i}; }
  // returns a pair of `{array, array_was_already_present_p}`.
  // If `array_was_already_present_p`, then the pointer backing `sizes` may
  // immediately be freed. Otherwise, a reference is kept.
  constexpr auto emplace_back(Value *base_pointer, MutPtrVector<Value *> sizes,
                              u8 align_shift = u8{}) -> Pair<Array, bool> {
    ptrdiff_t id = datadeps_.size();
    for (ptrdiff_t i = 0; i < id; ++i)
      if (Array ai = get(i); ai.basePointer() == base_pointer &&
                             ai.getSizes() == sizes)
        return {ai, true};
    datadeps_.push_back(
      {base_pointer, sizes.data(), u8(sizes.size()), align_shift});
    return {{datadeps_, id}, false};
  }
};

} // namespace IR
