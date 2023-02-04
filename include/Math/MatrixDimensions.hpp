#pragma once

#include "Math/AxisTypes.hpp"
#include "Utilities/StackMeMaybe.hpp"
#include <cstdint>

namespace LinearAlgebra {

struct DenseDims;
struct StridedDims {
  unsigned int M;
  unsigned int N;
  unsigned int strideM;
  constexpr StridedDims(Row M, Col N) : M(M), N(N), strideM(N) {}
  constexpr StridedDims(Row M, Col N, RowStride X) : M(M), N(N), strideM(X) {}
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(strideM) == size_t(M * strideM) && "overflow");
    return M * strideM;
  }
  constexpr auto operator=(const DenseDims &D) -> StridedDims &;
  constexpr operator CarInd() const { return {M, N}; }
};
struct DenseDims {
  unsigned int M;
  unsigned int N;
  constexpr operator unsigned int() const {
    assert(size_t(M) * size_t(N) == size_t(M * N) && "overflow");
    return M * N;
  }
  constexpr operator StridedDims() const { return {M, N, N}; }
  constexpr operator CarInd() const { return {M, N}; }
};

constexpr auto StridedDims::operator=(const DenseDims &D) -> StridedDims & {
  M = D.M;
  N = D.N;
  strideM = N;
  return *this;
}

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

} // namespace LinearAlgebra
