#pragma once

#include "Math/Array.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Utilities/Allocators.hpp"

namespace LinearAlgebra {

template <Scalar T>
constexpr auto vector(std::allocator<T>, unsigned int M) -> Vector<T> {
  return Vector<T>(M);
}
template <Scalar T>
constexpr auto vector(WBumpAlloc<T> alloc, unsigned int M) -> MutPtrVector<T> {
  return {alloc.allocate(M), M};
}
template <Scalar T>
constexpr auto vector(BumpAlloc<> &alloc, unsigned int M) -> MutPtrVector<T> {
  return {alloc.allocate<T>(M), M};
}

template <Scalar T>
constexpr auto vector(std::allocator<T>, unsigned int M, T x) -> Vector<T> {
  return {M, x};
}
template <Scalar T>
constexpr auto vector(WBumpAlloc<T> alloc, unsigned int M, T x)
  -> MutPtrVector<T> {
  MutPtrVector<T> a{alloc.allocate(M), M};
  a.fill(x);
  return a;
}
template <Scalar T>
constexpr auto vector(BumpAlloc<> &alloc, unsigned int M, T x)
  -> MutPtrVector<T> {
  MutPtrVector<T> a{alloc.allocate<T>(M), M};
  a.fill(x);
  return a;
}

template <Scalar T>
constexpr auto matrix(std::allocator<T>, unsigned int M) -> SquareMatrix<T> {
  return SquareDims{M};
}
template <Scalar T>
constexpr auto matrix(WBumpAlloc<T> alloc, unsigned int M)
  -> MutSquarePtrMatrix<T> {
  return {alloc.allocate(M * M), SquareDims{M}};
}
template <Scalar T>
constexpr auto matrix(BumpAlloc<> &alloc, unsigned int M)
  -> MutSquarePtrMatrix<T> {
  return {alloc.allocate<T>(M * M), SquareDims{M}};
}
template <Scalar T>
constexpr auto matrix(std::allocator<T>, unsigned int M, T x)
  -> SquareMatrix<T> {
  return {SquareDims{M}, x};
}
template <Scalar T>
constexpr auto matrix(WBumpAlloc<T> alloc, unsigned int M, T x)
  -> MutSquarePtrMatrix<T> {
  MutSquarePtrMatrix<T> A{alloc.allocate(M * M), SquareDims{M}};
  A.fill(x);
  return A;
}
template <Scalar T>
constexpr auto matrix(BumpAlloc<> &alloc, unsigned int M, T x)
  -> MutSquarePtrMatrix<T> {
  MutSquarePtrMatrix<T> A{alloc.allocate<T>(M * M), SquareDims{M}};
  A.fill(x);
  return A;
}

template <Scalar T>
constexpr auto matrix(std::allocator<T>, Row M, Col N) -> DenseMatrix<T> {
  return DenseDims{M, N};
}
template <Scalar T>
constexpr auto matrix(WBumpAlloc<T> alloc, Row M, Col N)
  -> MutDensePtrMatrix<T> {
  return {alloc.allocate(M * N), DenseDims{M, N}};
}
template <Scalar T>
constexpr auto matrix(BumpAlloc<> &alloc, Row M, Col N)
  -> MutDensePtrMatrix<T> {
  return {alloc.allocate<T>(M * N), M, N};
}
template <Scalar T>
constexpr auto matrix(std::allocator<T>, Row M, Col N, T x) -> DenseMatrix<T> {
  return {DenseDims{M, N}, x};
}
template <Scalar T>
constexpr auto matrix(WBumpAlloc<T> alloc, Row M, Col N, T x)
  -> MutDensePtrMatrix<T> {
  MutDensePtrMatrix<T> A{alloc.allocate(M * N), DenseDims{M, N}};
  A.fill(x);
  return A;
}
template <Scalar T>
constexpr auto matrix(BumpAlloc<> &alloc, Row M, Col N, T x)
  -> MutDensePtrMatrix<T> {
  MutDensePtrMatrix<T> A{alloc.allocate<T>(M * N), DenseDims{M, N}};
  A.fill(x);
  return A;
}

template <Scalar T>
constexpr auto identity(std::allocator<T>, unsigned int M) -> SquareMatrix<T> {
  SquareMatrix<T> A{M, T{}};
  A.diag() << T{1};
  return A;
}
template <Scalar T>
constexpr auto identity(WBumpAlloc<T> alloc, unsigned int M)
  -> MutSquarePtrMatrix<T> {
  MutSquarePtrMatrix<T> A{matrix(alloc, M, T{})};
  A.diag() << T{1};
  return A;
}
template <Scalar T>
constexpr auto identity(BumpAlloc<> &alloc, unsigned int M)
  -> MutSquarePtrMatrix<T> {
  MutSquarePtrMatrix<T> A{matrix(alloc, M, T{})};
  A.diag() << T{1};
  return A;
}

} // namespace LinearAlgebra

using LinearAlgebra::matrix, LinearAlgebra::vector, LinearAlgebra::identity;
