#pragma once

#include "Utilities/StackMeMaybe.hpp"
#include <cstdint>

struct StridedDims {
  unsigned int M;
  unsigned int N;
  unsigned int strideM;
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(strideM) == size_t(M * strideM) && "overflow");
    return M * strideM;
  }
};
struct DenseDims {
  unsigned int M;
  unsigned int N;
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(N) == size_t(M * N) && "overflow");
    return M * N;
  }
  constexpr operator StridedDims() const { return {M, N, N}; }
};

// Check that `[[no_unique_address]]` is working.
// sizes should be:
// [ptr, capacity, dims, allocator, array]
// 8 + 4 + 3*4 + 0 + 64*8 = 536
static_assert(
  sizeof(Buffer<int64_t, 64, StridedDims, std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, capacity, dims, allocator, array]
// 8 + 8 + 2*4 + 0 + 64*8 = 536
static_assert(sizeof(Buffer<int64_t, 64, DenseDims, std::allocator<int64_t>>) ==
              536);
