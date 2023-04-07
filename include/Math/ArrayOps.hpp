#pragma once

#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Vector.hpp"

namespace LinAlg {

template <class T, class S, class P> class ArrayOps {
  [[gnu::returns_nonnull]] constexpr auto data_() -> T * {
    return static_cast<P *>(this)->data();
  }
  [[gnu::returns_nonnull]] constexpr auto data_() const -> const T * {
    return static_cast<const P *>(this)->data();
  }
  constexpr auto size_() const { return static_cast<const P *>(this)->size(); }
  constexpr auto dim_() const -> S {
    return static_cast<const P *>(this)->dim();
  }
  constexpr auto index(size_t i) -> T & { return (*static_cast<P *>(this))[i]; }
  constexpr auto index(size_t i, size_t j) -> T & {
    return (*static_cast<P *>(this))(i, j);
  }
  [[nodiscard]] constexpr auto nr() const -> size_t {
    return size_t(static_cast<const P *>(this)->numRow());
  }
  [[nodiscard]] constexpr auto nc() const -> size_t {
    return size_t(static_cast<const P *>(this)->numCol());
  }
  [[nodiscard]] constexpr auto rs() const -> size_t {
    return size_t(static_cast<const P *>(this)->rowStride());
  }

public:
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const UniformScaling<Y> &B)
    -> P & {
    static_assert(MatrixDimension<S>);
    std::fill_n(data_(), size_t(this->dim()), T{});
    this->diag() << B.value;
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator<<(const SmallSparseMatrix<T> &B)
    -> P & {
    static_assert(MatrixDimension<S>);
    size_t M = nr(), N = nc(), k = 0;
    invariant(M, size_t(B.numRow()));
    invariant(N, size_t(B.numCol()));
    T *mem = data_();
    for (size_t i = 0; i < M; ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0, l = rs() * i;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        for (; tz; --tz) mem[l + j++] = T{};
        mem[l + j++] = B.nonZeros[k++];
      }
      for (; j < N; ++j) mem[l + j] = T{};
    }
    assert(k == B.nonZeros.size());
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator<<(const AbstractVector auto &B)
    -> P & {
    if constexpr (MatrixDimension<S>) {
      size_t M = nr(), N = nc();
      invariant(M, B.size());
      for (size_t i = 0; i < M; ++i) {
        T Bi = B[i];
        for (size_t j = 0; j < N; ++j) index(i, j) = Bi;
      }
    } else {
      size_t L = size_();
      invariant(L, size_t(B.size()));
      for (size_t i = 0; i < L; ++i) index(i) = B[i];
    }
    return *static_cast<P *>(this);
  }

  [[gnu::flatten]] constexpr auto operator<<(const AbstractMatrix auto &B)
    -> P & {
    static_assert(MatrixDimension<S>);
    size_t M = nr(), N = nc();
    invariant(M, size_t(B.numRow()));
    invariant(N, size_t(B.numCol()));
    for (size_t i = 0; i < M; ++i)
      for (size_t j = 0; j < N; ++j) index(i, j) = B(i, j);
    return *static_cast<P *>(this);
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const Y b) -> P & {
    if constexpr (std::integral<S> || std::is_same_v<S, StridedRange>) {
      for (size_t c = 0, L = size_(); c < L; ++c) index(c) = b;
    } else {
      size_t M = nr(), N = nc();
      for (size_t r = 0; r < M; ++r)
        for (size_t c = 0; c < N; ++c) index(r, c) = b;
    }
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractMatrix auto &B)
    -> P & {
    static_assert(MatrixDimension<S>);
    size_t M = nr(), N = nc();
    invariant(M, size_t(B.numRow()));
    invariant(N, size_t(B.numCol()));
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) index(r, c) += B(r, c);
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractMatrix auto &B)
    -> P & {
    static_assert(MatrixDimension<S>);
    size_t M = nr(), N = nc();
    invariant(M, size_t(B.numRow()));
    invariant(N, size_t(B.numCol()));
    for (size_t r = 0; r < M; ++r)
      for (size_t c = 0; c < N; ++c) index(r, c) -= B(r, c);
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractVector auto &B)
    -> P & {
    if constexpr (MatrixDimension<S>) {
      size_t M = nr(), N = nc();
      invariant(M, B.size());
      for (size_t r = 0; r < M; ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < N; ++c) index(r, c) += Br;
      }
    } else {
      size_t L = size_();
      invariant(L, size_t(B.size()));
      for (size_t i = 0; i < L; ++i) index(i) += B[i];
    }
    return *static_cast<P *>(this);
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator+=(Y b) -> P & {
    if constexpr (MatrixDimension<S> && !DenseLayout<S>) {
      size_t M = nr(), N = nc();
      for (size_t r = 0; r < M; ++r)
        for (size_t c = 0; c < N; ++c) index(r, c) += b;
    } else {
      for (size_t i = 0, L = size_(); i < L; ++i) index(i) += b;
    }
    return *static_cast<P *>(this);
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractVector auto &B)
    -> P & {
    if constexpr (MatrixDimension<S>) {
      size_t M = nr(), N = nc();
      invariant(M == B.size());
      for (size_t r = 0; r < M; ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < N; ++c) index(r, c) -= Br;
      }
    } else {
      size_t L = size_();
      invariant(L == B.size());
      for (size_t i = 0; i < L; ++i) index(i) -= B[i];
    }
    return *static_cast<P *>(this);
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator*=(Y b) -> P & {
    if constexpr (MatrixDimension<S> && !DenseLayout<S>) {
      size_t M = nr(), N = nc();
      for (size_t r = 0; r < M; ++r)
        for (size_t c = 0; c < N; ++c) index(r, c) *= b;
    } else {
      for (size_t c = 0, L = size_t(dim_()); c < L; ++c) index(c) *= b;
    }
    return *static_cast<P *>(this);
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator/=(Y b) -> P & {
    if constexpr (MatrixDimension<S> && !DenseLayout<S>) {
      size_t M = nr(), N = nc();
      for (size_t r = 0; r < M; ++r)
        for (size_t c = 0; c < N; ++c) index(r, c) /= b;
    } else {
      for (size_t c = 0, L = size_t(dim_()); c < L; ++c) index(c) /= b;
    }
    return *static_cast<P *>(this);
  }
};
} // namespace LinAlg
