#pragma once

#include "Utilities/Valid.hpp"
#include <cstddef>
#include <type_traits>

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <typename T, size_t N, typename S, typename A> struct Buffer {
  static_assert(std::is_trivially_destructible_v<T>);
  [[no_unique_address]] NotNull<T> pointer;
  [[no_unique_address]] unsigned capacity{N};
  [[no_unique_address]] S size{};
  [[no_unique_address]] A allocator;
  std::array<T, N> memory;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  Buffer() : pointer{memory.data()}, capacity{N} {}
#pragma GCC diagnostic pop
  // Buffer() : capacity{N} { pointer = memory.data(); }

  constexpr auto isSmall() -> bool { return pointer == memory.data(); }
  constexpr void maybeDeallocate() {
    if (!isSmall()) allocator.deallocate(pointer, capacity);
  }
  constexpr void grow(size_t M) {
    if (M <= capacity) return;
    maybeDeallocate();
    pointer = allocator.allocate(M);
    capacity = M;
  }
  template <typename... Args> constexpr auto emplace_back(Args &&...args) {
    if (size == capacity) grow(capacity * 2);
    new (pointer + size++) T(std::forward<Args>(args)...);
  }
  constexpr void push_back(T value) {
    if (size == capacity) grow(capacity * 2);
    new (pointer + size++) T(std::move(value));
  }
  constexpr void pop_back() {
    if (size) pointer[--size].~T();
  }
  constexpr void resize(size_t M) {
    if (M > size) {
      grow(M);
      for (size_t i = size; i < M; ++i) new (pointer + i) T();
    } else if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_t i = M; i < size; ++i) pointer[i].~T();
    }
    size = M;
  }
  constexpr void resizeForOverwrite(size_t M) {
    if (M > size) grow(M);
    else if constexpr (!std::is_trivially_destructible_v<T>)
      for (size_t i = M; i < size; ++i) pointer[i].~T();
    size = M;
  }

  // constexpr unsigned growSize(unsigned N){
  //   return N > capacity ? N : capacity;
  // }
  ~Buffer() { maybeDeallocate(); }
};
