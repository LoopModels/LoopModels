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
  constexpr auto isSmall() -> bool { return pointer == memory.data(); }
  constexpr void maybeDeallocate() {
    if (!isSmall()) allocator.deallocate(pointer);
  }
  constexpr void grow(size_t M) {
    if (M <= capacity) return;
    maybeDeallocate();
    pointer = allocator.allocate(M);
    capacity = M;
  }

  // constexpr unsigned growSize(unsigned N){
  //   return N > capacity ? N : capacity;
  // }
  ~Memory() { maybeDeallocate(); }
};
