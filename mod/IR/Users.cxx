#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <cassert>
#include <ranges>

#ifndef USE_MODULE
#include "Utilities/Invariant.cxx"
#include "Alloc/Arena.cxx"
#else
export module IR:Users;
import Arena;
import Invariant;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using alloc::Arena, utils::invariant;
class Value;
class Instruction;
class Addr;
class Users {
  union {
    Instruction *v_{nullptr};
    Instruction **p_;
    Value *val_;
  } ptr_;
  int size_{0};
  int capacity_{0};
  friend class Addr;
  [[nodiscard]] constexpr auto getVal() const -> Value * { return ptr_.val_; }
  [[nodiscard]] constexpr auto getValPtr() -> Value ** { return &ptr_.val_; }
  constexpr void setVal(Value *val) { ptr_.val_ = val; }

public:
  constexpr Users() = default;
  // we need to make sure we don't make a copy, where pushing doesn't
  // update the size and capacity of the original
  Users(const Users &) = delete;
  Users(Users &&) = delete;
  constexpr auto operator=(const Users &) -> Users & = default;
  [[nodiscard]] constexpr auto begin() noexcept -> Instruction ** {
    return (capacity_) ? ptr_.p_ : &ptr_.v_;
  }
  [[nodiscard]] constexpr auto end() noexcept -> Instruction ** {
    return ((capacity_) ? ptr_.p_ : (&ptr_.v_)) + size();
  }
  [[nodiscard]] constexpr auto begin() const noexcept -> Instruction *const * {
    return (capacity_) ? ptr_.p_ : &ptr_.v_;
  }
  [[nodiscard]] constexpr auto end() const noexcept -> Instruction *const * {
    return ((capacity_) ? ptr_.p_ : (&ptr_.v_)) + size();
  }
  [[nodiscard]] constexpr auto size() const noexcept -> int {
    invariant(size_ >= 0);
    return size_;
  }
  constexpr auto contains(Instruction *v) const noexcept -> bool {
    return std::ranges::find(*this, v) != end();
  }
  constexpr void pushKnownUnique(Arena<> *alloc, Instruction *v) {
    invariant(size_ >= 0);
    if (size_ >= capacity_) { // we could have capacity=0,size==1
      if (size_) {
        Instruction **p = begin();
        capacity_ = size_ > 2 ? size_ + size_ : 4;
        auto **new_ptr = alloc->allocate<Instruction *>(capacity_);
        for (int i = 0; i < size_; ++i) new_ptr[i] = p[i];
        new_ptr[size_] = v;
        ptr_.p_ = new_ptr;
      } else ptr_.v_ = v;
    } else ptr_.p_[size_] = v;
    ++size_;
  }
  constexpr void push_back(Arena<> *alloc, Instruction *v) {
    invariant(size_ >= 0);
    if (!contains(v)) pushKnownUnique(alloc, v);
  }
  constexpr void push_back_within_capacity(Instruction *v) {
    assert(!contains(v));
    invariant(size_ >= 0);
    invariant(size_ < capacity_);
    ptr_.p_[size_++] = v;
  }
  constexpr void remove(Instruction *v) noexcept {
    invariant(size_ >= 0);
    if (capacity_) {
      auto *it = std::ranges::find(*this, v);
      invariant(it != end());
      *it = ptr_.p_[--size_];
    } else {
      invariant(size_ == 1);
      invariant(ptr_.v_ == v);
      ptr_.v_ = nullptr;
      size_ = 0;
    }
  }
  constexpr void clear() { size_ = 0; }
};

static_assert(std::ranges::range<Users>);

} // namespace IR