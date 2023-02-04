#pragma once

#include "Utilities/Valid.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T>
concept SizeMultiple8 = sizeof(T)
% 8 == 0;

template <typename T> struct DefaultCapacityType {
  using type = unsigned int;
};
template <SizeMultiple8 T> struct DefaultCapacityType<T> {
  using type = std::size_t;
};
static_assert(!SizeMultiple8<uint32_t>);
static_assert(SizeMultiple8<uint64_t>);
static_assert(
  std::is_same_v<typename DefaultCapacityType<uint32_t>::type, uint32_t>);
static_assert(
  std::is_same_v<typename DefaultCapacityType<uint64_t>::type, uint64_t>);

template <typename T>
using DefaultCapacityType_t = typename DefaultCapacityType<T>::type;

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <typename T, size_t N, typename S, typename A = std::allocator<T>,
          std::unsigned_integral U = DefaultCapacityType_t<S>>
struct Buffer {
  static_assert(std::is_trivially_destructible_v<T>);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  constexpr Buffer() noexcept : pointer{memory.data()}, capacity{N} {}
  constexpr Buffer(S s) noexcept : pointer{memory.data()}, capacity{N} {
    sz = s;
    U len = sz;
    if (len <= N) return;
    pointer = allocator.allocate(len);
    capacity = len;
  }
#pragma GCC diagnostic pop
  constexpr Buffer(Buffer &&b) noexcept
    : pointer{b.pointer}, sz{b.sz}, capacity{b.capacity} {
    if (b.isSmall()) {
      memory = std::move(b.memory);
      pointer = memory.data();
    }
    b.pointer = b.memory.data();
    b.sz = 0;
    b.capacity = N;
  }
  constexpr auto data() noexcept -> NotNull<T> { return pointer; }
  constexpr auto data() const noexcept -> NotNull<const T> { return pointer; }

  template <typename... Args> constexpr auto emplace_back(Args &&...args) {
    if (sz == capacity) grow(capacity * 2);
    new (pointer + sz++) T(std::forward<Args>(args)...);
  }
  constexpr void push_back(T value) {
    if (sz == capacity) grow(capacity * 2);
    new (pointer + sz++) T(std::move(value));
  }
  constexpr void pop_back() {
    if (sz) pointer[--sz].~T();
  }
  constexpr void resize(S M) {
    U L = M;
    if (L > sz) {
      grow(L);
      for (size_t i = sz; i < L; ++i) new (pointer + i) T();
    } else if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_t i = L; i < sz; ++i) pointer[i].~T();
    }
    sz = M;
  }
  constexpr void resizeForOverwrite(S M) {
    U L = M;
    if (L > sz) grow(L);
    else if constexpr (!std::is_trivially_destructible_v<T>)
      for (size_t i = L; i < sz; ++i) pointer[i].~T();
    sz = M;
  }
  constexpr ~Buffer() { maybeDeallocate(); }
  constexpr auto size() const noexcept -> S { return sz; }
  // does not free memory, leaving capacity unchanged
  constexpr void clear() { sz = S{}; }

private:
  [[no_unique_address]] NotNull<T> pointer;
  [[no_unique_address]] U capacity{N};
  [[no_unique_address]] S sz{};
  [[no_unique_address]] A allocator{};
  std::array<T, N> memory;

  // Buffer() : capacity{N} { pointer = memory.data(); }

  constexpr auto isSmall() -> bool { return pointer == memory.data(); }
  constexpr void maybeDeallocate() {
    if (!isSmall()) allocator.deallocate(pointer, capacity);
  }
  constexpr void grow(U M) {
    if (M <= capacity) return;
    maybeDeallocate();
    pointer = allocator.allocate(M);
    capacity = M;
  }
};
