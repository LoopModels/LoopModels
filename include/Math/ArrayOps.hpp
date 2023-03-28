#pragma once

#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/Vector.hpp"

namespace LinAlg {

template <class T, class S, class P> class ArrayOps {
  constexpr auto data_() -> T * { return static_cast<P *>(this)->data(); }
  constexpr auto data_() const -> const T * {
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
  constexpr auto nr() { return static_cast<const P *>(this)->numRow(); }
  constexpr auto nc() { return static_cast<const P *>(this)->numCol(); }

public:
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const UniformScaling<Y> &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    std::fill_n((T *)(this->ptr), size_t(this->dim()), T{});
    this->diag() << B.value;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator<<(const SmallSparseMatrix<T> &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(nr(), B.numRow());
    invariant(nc(), B.numCol());
    size_t M = size_t(nr()), N = size_t(nc()), k = 0;
    T *mem = data_();
    for (size_t i = 0; i < M; ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0, l = size_t(this->rowStride() * i);
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        for (; tz; --tz) mem[l + j++] = T{};
        mem[l + j++] = B.nonZeros[k++];
      }
      for (; j < N; ++j) mem[l + j] = T{};
    }
    assert(k == B.nonZeros.size());
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator<<(const AbstractVector auto &B)
    -> decltype(auto) {
    if constexpr (MatrixDimension<S>) {
      invariant(nr(), B.size());
      for (size_t i = 0; i < nr(); ++i) {
        T Bi = B[i];
        for (size_t j = 0; j < nc(); ++j) index(i, j) = Bi;
      }
    } else {
      invariant(size_t(size_()), size_t(B.size()));
      for (size_t i = 0; i < size_(); ++i) index(i) = B[i];
    }
    return *this;
  }

  [[gnu::flatten]] constexpr auto operator<<(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(nr(), B.numRow());
    invariant(nc(), B.numCol());
    for (size_t i = 0; i < nr(); ++i)
      for (size_t j = 0; j < nc(); ++j) index(i, j) = B(i, j);
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const Y b) -> decltype(auto) {
    if constexpr (std::integral<S> || std::is_same_v<S, StridedRange>) {
      for (size_t c = 0, L = size_t(dim_()); c < L; ++c) index(c) = b;
    } else {
      for (size_t r = 0; r < nr(); ++r)
        for (size_t c = 0; c < nc(); ++c) index(r, c) = b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(nr(), B.numRow());
    invariant(nc(), B.numCol());
    for (size_t r = 0; r < nr(); ++r)
      for (size_t c = 0; c < nc(); ++c) index(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(nr(), B.numRow());
    invariant(nc(), B.numCol());
    for (size_t r = 0; r < nr(); ++r)
      for (size_t c = 0; c < nc(); ++c) index(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractVector auto &B)
    -> decltype(auto) {
    if constexpr (MatrixDimension<S>) {
      invariant(nr(), B.size());
      for (size_t r = 0; r < nr(); ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < nc(); ++c) index(r, c) += Br;
      }
    } else {
      invariant(size_t(size_()), size_t(B.size()));
      for (size_t i = 0; i < size_(); ++i) index(i) += B[i];
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator+=(Y b) -> decltype(auto) {
    if constexpr (MatrixDimension<S> && !DenseLayout<S>) {
      for (size_t r = 0; r < nr(); ++r)
        for (size_t c = 0; c < nc(); ++c) index(r, c) += b;
    } else {
      for (size_t i = 0; i < size_(); ++i) index(i) += b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractVector auto &B)
    -> decltype(auto) {
    if constexpr (MatrixDimension<S>) {
      invariant(nr() == B.size());
      for (size_t r = 0; r < nr(); ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < nc(); ++c) index(r, c) -= Br;
      }
    } else {
      invariant(size_() == B.size());
      for (size_t i = 0; i < size_(); ++i) index(i) -= B[i];
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator*=(Y b) -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = size_t(dim_()); c < L; ++c) index(c) *= b;
    } else {
      for (size_t r = 0; r < nr(); ++r)
        for (size_t c = 0; c < nc(); ++c) index(r, c) *= b;
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator/=(Y b) -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = size_t(dim_()); c < L; ++c) index(c) /= b;
    } else {
      for (size_t r = 0; r < nr(); ++r)
        for (size_t c = 0; c < nc(); ++c) index(r, c) /= b;
    }
    return *this;
  }
};
} // namespace LinAlg
