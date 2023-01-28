#pragma once

#include "Utilities/Valid.hpp"
#include <cstddef>
#include <type_traits>

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <typename T, size_t N, typename P, typename A> struct Memory {
  static_assert(std::is_trivially_destructible_v<T>);
  std::array<T, N> memory;
  [[no_unique_address]] NotNull<T> pointer;
  [[no_unique_address]] unsigned capacity{N};
  [[no_unique_address]] A allocator;
  constexpr bool isSmall(){
    return pointer == memory.data();
  }
  constexpr void maybeDeallocate(){
    if (!isSmall()){
      allocator.deallocate(pointer);
    }
  }
  constexpr void grow(size_t M){
    if (M <= capacity) return;
    maybeDeallocate();
    M += M; // double
    pointer = allocator.allocate(M);
    capacity = M;
  }
  ~Memory(){
    maybeDeallocate();
  }
};
