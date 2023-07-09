#pragma once

#include "Utilities/Invariant.hpp"
#include <Utilities/Allocators.hpp>
#include <bits/ranges_base.h>
#include <limits>

namespace poly::IR {
using utils::Arena, utils::invariant;
class Value;
class Instruction;
class Addr;
class Users {
  union {
    Instruction *v{nullptr};
    Instruction **p;
    Value *val;
  } ptr;
  unsigned size_{0};
  unsigned capacity_{0};
  friend class Addr;
  [[nodiscard]] constexpr auto getVal() const -> Value * { return ptr.val; }
  [[nodiscard]] constexpr auto getValPtr() -> Value ** { return &ptr.val; }
  constexpr void setVal(Value *val) { ptr.val = val; }

public:
  constexpr Users() = default;
  // we need to make sure we don't make a copy, where pushing doesn't
  // update the size and capacity of the original
  Users(const Users &) = delete;
  Users(Users &&) = delete;
  constexpr auto operator=(const Users &) -> Users & = default;
  [[nodiscard]] constexpr auto begin() noexcept -> Instruction ** {
    return (capacity_) ? ptr.p : &ptr.v;
  }
  [[nodiscard]] constexpr auto end() noexcept -> Instruction ** {
    return (capacity_) ? ptr.p + size_ : (&ptr.v) + 1;
  }
  [[nodiscard]] constexpr auto begin() const noexcept -> Instruction *const * {
    return (capacity_) ? ptr.p : &ptr.v;
  }
  [[nodiscard]] constexpr auto end() const noexcept -> Instruction *const * {
    return ((capacity_) ? ptr.p : (&ptr.v)) + size_;
  }
  [[nodiscard]] constexpr auto size() const noexcept -> unsigned {
    return size_;
  }
  constexpr auto contains(Instruction *v) const noexcept -> bool {
    return std::ranges::find(*this, v) != end();
  }
  constexpr void pushKnownUnique(Arena<> *alloc, Instruction *v) {
    invariant(size_ != std::numeric_limits<unsigned>::max());
    if (size_ >= capacity_) { // we could have capacity=0,size==1
      if (size_) {
        capacity_ = std::max<unsigned>(4, size_ + size_);
        auto *newPtr = alloc->allocate<Instruction *>(capacity_);
        for (unsigned i = 0; i < size_; ++i) newPtr[i] = ptr.p[i];
        newPtr[size_] = v;
        ptr.p = newPtr;
      } else ptr.v = v;
    } else ptr.p[size_] = v;
    ++size_;
  }
  constexpr void push_back(Arena<> *alloc, Instruction *v) {
    invariant(size_ != std::numeric_limits<unsigned>::max());
    if (!contains(v)) pushKnownUnique(alloc, v);
  }
  constexpr void remove(Instruction *v) noexcept {
    invariant(size_ != std::numeric_limits<unsigned>::max());
    if (capacity_) {
      auto *it = std::ranges::find(*this, v);
      invariant(it != end());
      *it = ptr.p[--size_];
    } else {
      invariant(size_ == 1);
      invariant(ptr.v == v);
      ptr.v = nullptr;
      size_ = 0;
    }
  }
  constexpr void clear() { size_ = 0; }
};

static_assert(std::ranges::range<Users>);

} // namespace poly::IR
