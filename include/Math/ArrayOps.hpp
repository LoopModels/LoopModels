#pragma once

#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Vector.hpp"
#include <type_traits>

namespace LinAlg {

template <typename T> class SmallSparseMatrix;
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
    -> P &;
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
    if constexpr (DenseLayout<S> &&
                  DataMatrix<std::remove_cvref_t<decltype(B)>> &&
                  DenseLayout<std::remove_cvref_t<decltype(B.dim())>>) {
      std::copy_n(B.data(), M * N, data_());
    } else {
      for (size_t i = 0; i < M; ++i)
        for (size_t j = 0; j < N; ++j) index(i, j) = B(i, j);
    }
    return *static_cast<P *>(this);
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const Y b) -> P & {
    if constexpr (DenseLayout<S>) {
      std::fill_n(data_(), size_t(dim_()), T(b));
    } else if constexpr (std::is_same_v<S, StridedRange>) {
      for (size_t c = 0, L = size_(); c < L; ++c) index(c) = b;
    } else {
      size_t M = nr(), N = nc(), X = rs();
      T *p = data_();
      for (size_t r = 0; r < M; ++r, p += X) std::fill_n(p, N, T(b));
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
