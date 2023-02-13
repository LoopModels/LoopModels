#pragma once

#include "Math/Array.hpp"
#include "Utilities/Allocators.hpp"

namespace LinearAlgebra {

template <typename T>
inline auto vector(std::allocator<T>, size_t M) -> Vector<T> {
  return Vector<T>(M);
}
template <typename T>
constexpr auto vector(WBumpAlloc<T> alloc, size_t M) -> MutPtrVector<T> {
  return {alloc.allocate(M), M};
}
template <typename T>
constexpr auto matrix(BumpAlloc<> &alloc, size_t M) -> MutPtrVector<T> {
  return {alloc.allocate<T>(M), M};
}

template <typename T>
inline auto matrix(std::allocator<T>, size_t M, size_t N)
  -> Matrix<T, DenseDims> {
  return Matrix<T, DenseDims, 64>::undef(M, N);
}
template <typename T>
constexpr auto matrix(WBumpAlloc<T> alloc, size_t M, size_t N)
  -> MutPtrMatrix<T, DenseDims> {
  return {alloc.allocate(M * N), M, N};
}
template <typename T>
constexpr auto matrix(BumpAlloc<> &alloc, size_t M, size_t N)
  -> MutPtrMatrix<T, DenseDims> {
  return {alloc.allocate<T>(M * N), M, N};
}
template <typename T>
inline auto identity(std::allocator<T>, size_t M) -> Matrix<T, SquareDims> {
  return Matrix<T, SquareDims>::identity(M);
}
template <typename T>
constexpr auto identity(WBumpAlloc<T> alloc, size_t M)
  -> MutPtrMatrix<T, SquareDims> {
  MutPtrMatrix<T, SquareDims> A = {alloc.allocate(M * M), M};
  A << 0;
  A.diag() << 1;
  return A;
}
template <typename T>
constexpr auto identity(BumpAlloc<> &alloc, size_t M)
  -> MutPtrMatrix<T, SquareDims> {
  return identity(WBumpAlloc<T>(alloc), M);
}

} // namespace LinearAlgebra
