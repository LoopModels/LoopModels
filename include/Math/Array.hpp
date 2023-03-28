#pragma once

#include "Math/AxisTypes.hpp"
#include "Math/Indexing.hpp"
#include "Math/Matrix.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Vector.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Iterators.hpp"
#include "Utilities/Optional.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <type_traits>
#include <utility>

namespace LinAlg {

template <class T>
concept Scalar =
  std::integral<T> || std::floating_point<T> || std::same_as<T, Rational>;

template <class T, class S, size_t N = PreAllocStorage<T>(),
          class A = std::allocator<T>,
          std::unsigned_integral U = default_capacity_type_t<S>>
struct ManagedArray;

/// Constant Array
template <class T, class S> struct Array {
  static_assert(!std::is_const_v<T>, "T shouldn't be const");
  static_assert(std::is_trivially_destructible_v<T>,
                "maybe should add support for destroying");
  using value_type = T;
  using reference = T &;
  using const_reference = const T &;
  using size_type = unsigned;
  using difference_type = int;
  using iterator = T *;
  using const_iterator = const T *;
  using pointer = T *;
  using const_pointer = const T *;
  using concrete = std::true_type;

  constexpr Array(const Array &) = default;
  constexpr Array(Array &&) noexcept = default;
  constexpr auto operator=(const Array &) -> Array & = default;
  constexpr auto operator=(Array &&) noexcept -> Array & = default;
  constexpr Array(T *p, S s) : ptr(p), sz(s) {}
  constexpr Array(NotNull<T> p, S s) : ptr(p), sz(s) {}
  constexpr Array(T *p, Row r, Col c) : ptr(p), sz(dimension<S>(r, c)) {}
  constexpr Array(NotNull<T> p, Row r, Col c)
    : ptr(p), sz(dimension<S>(r, c)) {}
  template <std::convertible_to<S> V>
  constexpr Array(Array<T, V> a) : ptr(a.wrappedPtr()), sz(a.dim()) {}
  template <size_t N>
  constexpr Array(const std::array<T, N> &a)
    : ptr(const_cast<T *>(a.data())), sz(N) {}
  [[nodiscard, gnu::returns_nonnull]] constexpr auto data() const noexcept
    -> const T * {
    invariant(ptr != nullptr);
    return ptr;
  }
  [[nodiscard]] constexpr auto wrappedPtr() noexcept -> NotNull<T> {
    return ptr;
  }

  [[nodiscard]] constexpr auto begin() const noexcept {
    if constexpr (std::is_same_v<S, StridedRange>)
      return StridedIterator{data(), sz.stride};
    else return data();
  }
  [[nodiscard]] constexpr auto end() const noexcept {
    return begin() + size_t(sz);
  }
  [[nodiscard]] constexpr auto rbegin() const noexcept {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const noexcept {
    return std::reverse_iterator(begin());
  }
  [[nodiscard]] constexpr auto front() const noexcept -> const T & {
    return *begin();
  }
  [[nodiscard]] constexpr auto back() const noexcept -> const T & {
    return *(end() - 1);
  }
  // indexing has two components:
  // 1. offsetting the pointer
  // 2. calculating new dim
  // static constexpr auto slice(NotNull<T>, Index<S> auto i){
  //   auto
  // }
  constexpr auto operator[](Index<S> auto i) const noexcept -> decltype(auto) {
    auto offset = calcOffset(sz, i);
    auto newDim = calcNewDim(sz, i);
    invariant(ptr != nullptr);
    if constexpr (std::is_same_v<decltype(newDim), Empty>) return ptr[offset];
    else return Array<T, decltype(newDim)>{ptr + offset, newDim};
  }
  // TODO: switch to operator[] when we enable c++23
  // for vectors, we just drop the column, essentially broadcasting
  template <class R, class C>
  constexpr auto operator()(R r, C c) const noexcept -> decltype(auto) {
    if constexpr (MatrixDimension<S>)
      return (*this)[CartesianIndex<R, C>{r, c}];
    else return (*this)[size_t(r)];
  }
  [[nodiscard]] constexpr auto minRowCol() const -> size_t {
    return std::min(size_t(numRow()), size_t(numCol()));
  }

  [[nodiscard]] constexpr auto diag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{sz}) + 1};
    invariant(ptr != nullptr);
    return Array<T, StridedRange>{ptr, r};
  }
  [[nodiscard]] constexpr auto antiDiag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{sz}) - 1};
    invariant(ptr != nullptr);
    return Array<T, StridedRange>{ptr + size_t(Col{sz}) - 1, r};
  }
  [[nodiscard]] constexpr auto isSquare() const noexcept -> bool {
    return Row{sz} == Col{sz};
  }
  [[nodiscard]] constexpr auto checkSquare() const -> Optional<size_t> {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return {};
    return N;
  }

  [[nodiscard]] constexpr auto numRow() const noexcept -> Row {
    return Row{sz};
  }
  [[nodiscard]] constexpr auto numCol() const noexcept -> Col {
    return Col{sz};
  }
  [[nodiscard]] constexpr auto rowStride() const noexcept -> RowStride {
    return RowStride{sz};
  }
  [[nodiscard]] constexpr auto empty() const -> bool { return sz == S{}; }
  [[nodiscard]] constexpr auto size() const noexcept {
    if constexpr (std::integral<S>) return sz;
    else if constexpr (std::is_same_v<S, StridedRange>) return size_t(sz);
    else return CartesianIndex{Row{sz}, Col{sz}};
  }
  [[nodiscard]] constexpr auto dim() const noexcept -> S { return sz; }
  constexpr void clear() { sz = S{}; }
  [[nodiscard]] constexpr auto transpose() const { return Transpose{*this}; }
  [[nodiscard]] constexpr auto isExchangeMatrix() const -> bool {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return false;
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j)
        if ((*this)(i, j) != (i + j == N - 1)) return false;
    }
  }
  [[nodiscard]] constexpr auto isDiagonal() const -> bool {
    for (Row r = 0; r < numRow(); ++r)
      for (Col c = 0; c < numCol(); ++c)
        if (r != c && (*this)(r, c) != 0) return false;
    return true;
  }
  [[nodiscard]] constexpr auto view() const noexcept -> Array<T, S> {
    invariant(ptr != nullptr);
    return Array<T, S>{ptr, this->sz};
  }
#ifndef NDEBUG
  constexpr void extendOrAssertSize(Row MM, Col NN) const {
    assert(MM == numRow());
    assert(NN == numCol());
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif

  [[nodiscard]] constexpr auto deleteCol(size_t c) const -> ManagedArray<T, S> {
    static_assert(MatrixDimension<S>);
    auto newDim = dim().similar(numRow() - 1);
    ManagedArray<T, decltype(newDim)> A(newDim);
    for (size_t m = 0; m < numRow(); ++m) {
      A(m, _(0, c)) = (*this)(m, _(0, c));
      A(m, _(c, LinAlg::end)) = (*this)(m, _(c + 1, LinAlg::end));
    }
    return A;
  }
  [[nodiscard]] constexpr auto operator==(const Array &other) const noexcept
    -> bool {
    if (size() != other.size()) return false;
    if constexpr (std::is_same_v<S, StridedDims>) {
      // may not be dense, iterate over rows
      for (Row i = 0; i < numRow(); ++i)
        if ((*this)(i, _) != other(i, _)) return false;
      return true;
    }
    return std::equal(begin(), end(), other.begin());
  }
  [[nodiscard]] constexpr auto norm2() const noexcept -> value_type {
    return std::transform_reduce(begin(), end(), begin(), 0.0);
  }
  // [[nodiscard]] constexpr auto norm() const noexcept -> double {
  //   return std::sqrt(norm2());
  // }
  // constexpr auto operator!=(const Array &other) const noexcept -> bool {
  //   return !(*this == other);
  // }
  friend inline auto operator<<(std::ostream &os, const Array &x)
    -> std::ostream & {
    return adaptOStream(os, x);
  }
  friend inline void PrintTo(const Array &x, std::ostream *os) {
    adaptOStream(*os, x);
  }

  void dump() { llvm::errs() << *this << "\n"; }

protected:
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  [[no_unique_address]] T *ptr;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  [[no_unique_address]] S sz{};
};

template <class T, class S> struct MutArray : Array<T, S> {
  using BaseT = Array<T, S>;
  // using BaseT::BaseT;
  using BaseT::operator[], BaseT::operator(), BaseT::data, BaseT::begin,
    BaseT::end, BaseT::rbegin, BaseT::rend, BaseT::front, BaseT::back;

  constexpr MutArray(const MutArray &) = default;
  constexpr MutArray(MutArray &&) noexcept = default;
  constexpr auto operator=(const MutArray &) -> MutArray & = default;
  constexpr auto operator=(MutArray &&) noexcept -> MutArray & = default;

  constexpr void truncate(S nz) {
    S oz = this->sz;
    this->sz = nz;
    if constexpr (std::integral<S>) {
      invariant(size_t(nz) <= size_t(oz));
    } else if constexpr (std::is_same_v<S, StridedDims>) {
      invariant(nz.row() <= oz.row());
      invariant(nz.col() <= oz.col());
    } else {
      static_assert(MatrixDimension<S>, "Can only resize 1 or 2d containers.");
      auto newX = unsigned{RowStride{nz}}, oldX = unsigned{RowStride{oz}},
           newN = unsigned{Col{nz}}, oldN = unsigned{Col{oz}},
           newM = unsigned{Row{nz}}, oldM = unsigned{Row{oz}};
      invariant(newM <= oldM);
      invariant(newN <= oldN);
      invariant(newX <= oldX);
      unsigned colsToCopy = newN;
      // we only need to copy if memory shifts position
      bool copyCols = ((colsToCopy > 0) && (newX != oldX));
      // if we're in place, we have 1 less row to copy
      unsigned rowsToCopy = newM;
      if (rowsToCopy && (--rowsToCopy) && (copyCols)) {
        // truncation, we need to copy rows to increase stride
        T *src = data(), *dst = src;
        do {
          src += oldX;
          dst += newX;
          std::copy_n(src, colsToCopy, dst);
        } while (--rowsToCopy);
      }
    }
  }

  constexpr void truncate(Row r) {
    if constexpr (std::integral<S>) {
      return truncate(S(r));
    } else if constexpr (std::is_same_v<S, StridedDims>) {
      invariant(r <= Row{this->sz});
      this->sz.set(r);
    } else { // if constexpr (std::is_same_v<S, DenseDims>) {
      static_assert(std::is_same_v<S, DenseDims>,
                    "if truncating a row, matrix must be strided or dense.");
      invariant(r <= Row{this->sz});
      DenseDims newSz = this->sz;
      truncate(newSz.set(r));
    }
  }
  constexpr void truncate(Col c) {
    if constexpr (std::integral<S>) {
      return truncate(S(c));
    } else if constexpr (std::is_same_v<S, StridedDims>) {
      invariant(c <= Col{this->sz});
      this->sz.set(c);
    } else { // if constexpr (std::is_same_v<S, DenseDims>) {
      static_assert(std::is_same_v<S, DenseDims>,
                    "if truncating a col, matrix must be strided or dense.");
      invariant(c <= Col{this->sz});
      DenseDims newSz = this->sz;
      truncate(newSz.set(c));
    }
  }

  template <class... Args>
  constexpr MutArray(Args &&...args)
    : Array<T, S>(std::forward<Args>(args)...) {}

  template <std::convertible_to<T> U, std::convertible_to<S> V>
  constexpr MutArray(Array<U, V> a) : Array<T, S>(a) {}
  template <size_t N>
  constexpr MutArray(std::array<T, N> &a) : Array<T, S>(a.data(), N) {}
  [[nodiscard, gnu::returns_nonnull]] constexpr auto data() noexcept -> T * {
    invariant(this->ptr != nullptr);
    return this->ptr;
  }
  [[nodiscard]] constexpr auto wrappedPtr() noexcept -> NotNull<T> {
    return this->ptr;
  }

  [[nodiscard, gnu::returns_nonnull]] constexpr auto begin() noexcept -> T * {
    return this->ptr;
  }
  [[nodiscard, gnu::returns_nonnull]] constexpr auto end() noexcept -> T * {
    return this->ptr + size_t(this->sz);
  }
  [[nodiscard]] constexpr auto rbegin() noexcept {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() noexcept {
    return std::reverse_iterator(begin());
  }
  constexpr auto front() noexcept -> T & { return *begin(); }
  constexpr auto back() noexcept -> T & { return *(end() - 1); }
  constexpr auto operator[](Index<S> auto i) noexcept -> decltype(auto) {
    auto offset = calcOffset(this->sz, i);
    auto newDim = calcNewDim(this->sz, i);
    if constexpr (std::is_same_v<decltype(newDim), Empty>)
      return this->ptr[offset];
    else return MutArray<T, decltype(newDim)>{this->ptr + offset, newDim};
  }
  // TODO: switch to operator[] when we enable c++23
  template <class R, class C>
  constexpr auto operator()(R r, C c) noexcept -> decltype(auto) {
    if constexpr (MatrixDimension<S>)
      return (*this)[CartesianIndex<R, C>{r, c}];
    else return (*this)[size_t(r)];
  }
  constexpr void fill(T value) {
    std::fill_n((T *)(this->ptr), size_t(this->dim()), value);
  }
  [[nodiscard]] constexpr auto diag() noexcept {
    StridedRange r{unsigned(min(Row{this->sz}, Col{this->sz})),
                   unsigned(RowStride{this->sz}) + 1};
    return MutArray<T, StridedRange>{this->ptr, r};
  }
  [[nodiscard]] constexpr auto antiDiag() noexcept {
    Col c = Col{this->sz};
    StridedRange r{unsigned(min(Row{this->sz}, c)),
                   unsigned(RowStride{this->sz}) - 1};
    return MutArray<T, StridedRange>{this->ptr + size_t(c) - 1, r};
  }
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
    invariant(this->numRow(), B.numRow());
    invariant(this->numCol(), B.numCol());
    size_t M = size_t(this->numRow()), N = size_t(this->numCol()), k = 0;
    T *mem = data();
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
      invariant(this->numRow(), B.size());
      for (size_t i = 0; i < this->numRow(); ++i) {
        T Bi = B[i];
        for (size_t j = 0; j < this->numCol(); ++j) (*this)(i, j) = Bi;
      }
    } else {
      invariant(size_t(this->size()), size_t(B.size()));
      for (size_t i = 0; i < this->size(); ++i) (*this)[i] = B[i];
    }
    return *this;
  }

  [[gnu::flatten]] constexpr auto operator<<(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(this->numRow(), B.numRow());
    invariant(this->numCol(), B.numCol());
    for (size_t i = 0; i < this->numRow(); ++i)
      for (size_t j = 0; j < this->numCol(); ++j) (*this)(i, j) = B(i, j);
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator<<(const Y b) -> decltype(auto) {
    if constexpr (std::integral<S> || std::is_same_v<S, StridedRange>) {
      for (size_t c = 0, L = size_t(this->sz); c < L; ++c) (*this)[c] = b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) = b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(this->numRow(), B.numRow());
    invariant(this->numCol(), B.numCol());
    for (size_t r = 0; r < this->numRow(); ++r)
      for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(this->numRow(), B.numRow());
    invariant(this->numCol(), B.numCol());
    for (size_t r = 0; r < this->numRow(); ++r)
      for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractVector auto &B)
    -> decltype(auto) {
    if constexpr (MatrixDimension<S>) {
      invariant(this->numRow(), B.size());
      for (size_t r = 0; r < this->numRow(); ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) += Br;
      }
    } else {
      invariant(size_t(this->size()), size_t(B.size()));
      for (size_t i = 0; i < this->size(); ++i) (*this)[i] += B[i];
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator+=(Y b) -> decltype(auto) {
    if constexpr (MatrixDimension<S> && !DenseLayout<S>) {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) += b;
    } else {
      for (size_t i = 0; i < this->size(); ++i) (*this)[i] += b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractVector auto &B)
    -> decltype(auto) {
    if constexpr (MatrixDimension<S>) {
      invariant(this->numRow() == B.size());
      for (size_t r = 0; r < this->numRow(); ++r) {
        auto Br = B[r];
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) -= Br;
      }
    } else {
      invariant(this->size() == B.size());
      for (size_t i = 0; i < this->size(); ++i) (*this)[i] -= B[i];
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator*=(Y b) -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = size_t(this->sz); c < L; ++c) (*this)[c] *= b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) *= b;
    }
    return *this;
  }
  template <std::convertible_to<T> Y>
  [[gnu::flatten]] constexpr auto operator/=(Y b) -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = size_t(this->sz); c < L; ++c) (*this)[c] /= b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) /= b;
    }
    return *this;
  }
  constexpr void erase(S i) {
    static_assert(std::integral<S>, "erase requires integral size");
    S oldLen = this->sz--;
    if (i < this->sz)
      std::copy((T *)(this->ptr) + i + 1, this->ptr + oldLen,
                (T *)(this->ptr) + i);
  }
  constexpr void erase(Row r) {
    if constexpr (std::integral<S>) {
      return erase(S(r));
    } else if constexpr (std::is_same_v<S, StridedDims>) {

      auto stride = unsigned{RowStride{this->sz}},
           col = unsigned{Col{this->sz}}, newRow = unsigned{Row{this->sz}} - 1;
      this->sz.set(Row{newRow});
      if ((col == 0) || (r == newRow)) return;
      invariant(col <= stride);
      if ((col + (512 / (sizeof(T)))) <= stride) {
        T *dst = this->ptr + r * stride;
        for (size_t m = *r; m < newRow; ++m) {
          T *src = dst + stride;
          std::copy_n(src, col, dst);
          dst = src;
        }
      } else {
        T *dst = this->ptr + r * stride;
        std::copy_n(dst + stride, (newRow - unsigned(r)) * stride, dst);
      }
    } else { // if constexpr (std::is_same_v<S, DenseDims>) {
      static_assert(std::is_same_v<S, DenseDims>,
                    "if erasing a row, matrix must be strided or dense.");
      auto col = unsigned{Col{this->sz}}, newRow = unsigned{Row{this->sz}} - 1;
      this->sz.set(Row{newRow});
      if ((col == 0) || (r == newRow)) return;
      T *dst = this->ptr + r * col;
      std::copy_n(dst + col, (newRow - unsigned(r)) * col, dst);
    }
  }
  constexpr void erase(Col c) {
    if constexpr (std::integral<S>) {
      return erase(S(c));
    } else if constexpr (std::is_same_v<S, StridedDims>) {
      auto stride = unsigned{RowStride{this->sz}},
           newCol = unsigned{Col{this->sz}} - 1, row = unsigned{Row{this->sz}};
      this->sz.set(Col{newCol});
      unsigned colsToCopy = newCol - unsigned(c);
      if ((colsToCopy == 0) || (row == 0)) return;
      // we only need to copy if memory shifts position
      for (size_t m = 0; m < row; ++m) {
        T *dst = this->ptr + m * stride + unsigned(c);
        std::copy_n(dst + 1, colsToCopy, dst);
      }
    } else { // if constexpr (std::is_same_v<S, DenseDims>) {
      static_assert(std::is_same_v<S, DenseDims>,
                    "if erasing a col, matrix must be strided or dense.");
      auto newCol = unsigned{Col{this->sz}}, oldCol = newCol--,
           row = unsigned{Row{this->sz}};
      this->sz.set(Col{newCol});
      unsigned colsToCopy = newCol - unsigned(c);
      if ((colsToCopy == 0) || (row == 0)) return;
      // we only need to copy if memory shifts position
      for (size_t m = 0; m < row; ++m) {
        T *dst = this->ptr + m * newCol + unsigned(c);
        T *src = this->ptr + m * oldCol + unsigned(c) + 1;
        std::copy_n(src, colsToCopy, dst);
      }
    }
  }
  constexpr void moveLast(Col j) {
    static_assert(MatrixDimension<S>);
    if (j == this->numCol()) return;
    Col Nm1 = this->numCol() - 1;
    for (size_t m = 0; m < this->numRow(); ++m) {
      auto x = (*this)(m, j);
      for (Col n = j; n < Nm1;) {
        Col o = n++;
        (*this)(m, o) = (*this)(m, n);
      }
      (*this)(m, Nm1) = x;
    }
  }
};

template <typename T, typename S> MutArray(T *, S) -> MutArray<T, S>;

template <typename T, typename S> MutArray(MutArray<T, S>) -> MutArray<T, S>;

static_assert(std::convertible_to<MutArray<int64_t, SquareDims>,
                                  MutArray<int64_t, DenseDims>>);
static_assert(std::convertible_to<MutArray<int64_t, SquareDims>,
                                  MutArray<int64_t, StridedDims>>);
static_assert(std::convertible_to<MutArray<int64_t, DenseDims>,
                                  MutArray<int64_t, StridedDims>>);

/// Non-owning view of a managed array, capable of resizing,
/// but not of re-allocating in case the capacity is exceeded.
template <class T, class S,
          std::unsigned_integral U = default_capacity_type_t<S>>
struct ResizeableView : MutArray<T, S> {
  using BaseT = MutArray<T, S>;

  constexpr ResizeableView(NotNull<T> p, S s, U c) noexcept
    : BaseT(p, s), capacity(c) {}

  template <class... Args>
  constexpr auto emplace_back(Args &&...args) -> decltype(auto) {
    static_assert(std::is_integral_v<S>, "emplace_back requires integral size");
    invariant(U(this->sz) < capacity);
    return *(new (this->ptr + this->sz++) T(std::forward<Args>(args)...));
  }
  constexpr void push_back(T value) {
    static_assert(std::is_integral_v<S>, "push_back requires integral size");
    invariant(U(this->sz) < capacity);
    new (this->ptr + this->sz++) T(std::move(value));
  }
  constexpr void pop_back() {
    static_assert(std::is_integral_v<S>, "pop_back requires integral size");
    assert(this->sz > 0 && "pop_back on empty buffer");
    if constexpr (std::is_trivially_destructible_v<T>) --this->sz;
    else this->ptr[--this->sz].~T();
  }
  constexpr auto pop_back_val() -> T {
    static_assert(std::is_integral_v<S>, "pop_back requires integral size");
    assert(this->sz > 0 && "pop_back on empty buffer");
    return std::move(this->ptr[--this->sz]);
  }
  // behavior
  // if S is StridedDims, then we copy data.
  // If the new dims are larger in rows or cols, we fill with 0.
  // If the new dims are smaller in rows or cols, we truncate.
  // New memory outside of dims (i.e., stride larger), we leave uninitialized.
  //
  constexpr void resize(S nz) {
    S oz = this->sz;
    this->sz = nz;
    if constexpr (std::integral<S>) {
      if (nz <= this->capacity) return;
      U newCapacity = U(nz);
#if __cplusplus >= 202202L
      std::allocation_result res = allocator.allocate_at_least(newCapacity);
      T *newPtr = res.ptr;
      newCapacity = U(res.count);
#else
      T *newPtr = this->allocator.allocate(newCapacity);
#endif
      if (oz) std::uninitialized_copy_n((T *)(this->ptr), oz, newPtr);
      maybeDeallocate(newPtr, newCapacity);
      invariant(newCapacity > oz);
      std::uninitialized_fill_n((T *)(newPtr + oz), newCapacity - oz, T{});
    } else {
      static_assert(MatrixDimension<S>, "Can only resize 1 or 2d containers.");
      auto newX = unsigned{RowStride{nz}}, oldX = unsigned{RowStride{oz}},
           newN = unsigned{Col{nz}}, oldN = unsigned{Col{oz}},
           newM = unsigned{Row{nz}}, oldM = unsigned{Row{oz}};
      invariant(U(nz) <= capacity);
      U len = U(nz);
      T *npt = (T *)(this->ptr);
      // we can copy forward so long as the new stride is smaller
      // so that the start of the dst range is outside of the src range
      // we can also safely forward copy if we allocated a new ptr
      bool forwardCopy = (newX <= oldX);
      unsigned colsToCopy = std::min(oldN, newN);
      // we only need to copy if memory shifts position
      bool copyCols = ((colsToCopy > 0) && (newX != oldX));
      // if we're in place, we have 1 less row to copy
      unsigned rowsToCopy = std::min(oldM, newM) - 1;
      unsigned fillCount = newN - colsToCopy;
      if ((rowsToCopy) && (copyCols || fillCount)) {
        if (forwardCopy) {
          // truncation, we need to copy rows to increase stride
          T *src = this->ptr + oldX;
          T *dst = npt + newX;
          do {
            if (copyCols) std::copy_n(src, colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
            src += oldX;
            dst += newX;
          } while (--rowsToCopy);
        } else /* [[unlikely]] */ {
          // backwards copy, only needed when we increasing stride but not
          // reallocating, which should be comparatively uncommon.
          // Should probably benchmark or determine actual frequency
          // before adding `[[unlikely]]`.
          T *src = this->ptr + (rowsToCopy + 1) * oldX;
          T *dst = npt + (rowsToCopy + 1) * newX;
          do {
            src -= oldX;
            dst -= newX;
            if (colsToCopy) std::copy_backward(src, src + colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
          } while (--rowsToCopy);
        }
      }
      // zero init remaining rows
      for (size_t m = oldM; m < newM; ++m)
        std::fill_n(npt + m * newX, newN, T{});
    }
  }
  constexpr void resize(Row r) {
    if constexpr (std::integral<S>) {
      return resize(S(r));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resize(nz.set(r));
    }
  }
  constexpr void resize(Col c) {
    if constexpr (std::integral<S>) {
      return resize(S(c));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resize(nz.set(c));
    }
  }
  constexpr void resizeForOverwrite(S M) {
    U L = U(M);
    invariant(L <= U(this->sz));
    this->sz = M;
  }
  constexpr void resizeForOverwrite(Row r) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(r));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(Col c) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(c));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(c));
    }
  }
  [[nodiscard]] constexpr auto getCapacity() const -> U { return capacity; }

  // set size and 0.
  constexpr void setSize(Row r, Col c) {
    resizeForOverwrite({r, c});
    this->fill(0);
  }
  constexpr void resize(Row MM, Col NN) { resize(DenseDims{MM, NN}); }
  constexpr void resizeForOverwrite(Row M, Col N, RowStride X) {
    invariant(X >= N);
    if constexpr (std::is_same_v<S, StridedDims>) resizeForOverwrite({M, N, X});
    else if constexpr (std::is_same_v<S, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }
  constexpr void resizeForOverwrite(Row M, Col N) {
    if constexpr (std::is_same_v<S, StridedDims>)
      resizeForOverwrite({M, N, *N});
    else if constexpr (std::is_same_v<S, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }

  constexpr void extendOrAssertSize(Row R, Col C) {
    resizeForOverwrite(DenseDims{R, C});
  }

protected:
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  [[no_unique_address]] U capacity{0};
};

/// Non-owning view of a managed array, capable of reallocating, etc.
/// It does not own memory. Mostly, it serves to drop the inlined
/// stack capacity of the `ManagedArray` from the type.
template <class T, class S, class A = std::allocator<T>,
          std::unsigned_integral U = default_capacity_type_t<S>,
          bool Init = false>
struct ReallocView : ResizeableView<T, S, U> {
  using BaseT = ResizeableView<T, S, U>;

  constexpr ReallocView(NotNull<T> p, S s, U c) noexcept : BaseT(p, s, c) {}
  constexpr ReallocView(NotNull<T> p, S s, U c, A alloc) noexcept
    : BaseT(p, s, c), allocator(alloc) {}

  template <class... Args>
  constexpr auto emplace_back(Args &&...args) -> decltype(auto) {
    static_assert(std::is_integral_v<S>, "emplace_back requires integral size");
    if (this->sz == this->capacity) [[unlikely]]
      reserve((Init && (this->capacity == 0)) ? U{1} : 2 * this->capacity);
    return *(new (this->ptr + this->sz++) T(std::forward<Args>(args)...));
  }
  constexpr void push_back(T value) {
    static_assert(std::is_integral_v<S>, "push_back requires integral size");
    if (this->sz == this->capacity) [[unlikely]]
      reserve((Init && (this->capacity == 0)) ? U{1} : 2 * this->capacity);
    new (this->ptr + this->sz++) T(std::move(value));
  }
  // behavior
  // if S is StridedDims, then we copy data.
  // If the new dims are larger in rows or cols, we fill with 0.
  // If the new dims are smaller in rows or cols, we truncate.
  // New memory outside of dims (i.e., stride larger), we leave uninitialized.
  //
  constexpr void resize(S nz) {
    S oz = this->sz;
    this->sz = nz;
    if constexpr (std::integral<S>) {
      if (nz <= this->capacity) return;
      U newCapacity = U(nz);
#if __cplusplus >= 202202L
      std::allocation_result res = allocator.allocate_at_least(newCapacity);
      T *newPtr = res.ptr;
      newCapacity = U(res.count);
#else
      T *newPtr = this->allocator.allocate(newCapacity);
#endif
      if (oz) std::uninitialized_copy_n((T *)(this->ptr), oz, newPtr);
      maybeDeallocate(newPtr, newCapacity);
      invariant(newCapacity > oz);
      std::uninitialized_fill_n((T *)(newPtr + oz), newCapacity - oz, T{});
    } else {
      static_assert(MatrixDimension<S>, "Can only resize 1 or 2d containers.");
      auto newX = unsigned{RowStride{nz}}, oldX = unsigned{RowStride{oz}},
           newN = unsigned{Col{nz}}, oldN = unsigned{Col{oz}},
           newM = unsigned{Row{nz}}, oldM = unsigned{Row{oz}};
      U len = U(nz);
      bool newAlloc = U(len) > this->capacity;
      bool inPlace = !newAlloc;
#if __cplusplus >= 202202L
      T *npt = (T *)(this->ptr);
      if (newAlloc) {
        std::allocation_result res = allocator.allocate_at_least(len);
        npt = res.ptr;
        len = U(res.count);
      }
#else
      T *npt = newAlloc ? this->allocator.allocate(len) : (T *)(this->ptr);
#endif
      // we can copy forward so long as the new stride is smaller
      // so that the start of the dst range is outside of the src range
      // we can also safely forward copy if we allocated a new ptr
      bool forwardCopy = (newX <= oldX) || newAlloc;
      unsigned colsToCopy = std::min(oldN, newN);
      // we only need to copy if memory shifts position
      bool copyCols = newAlloc || ((colsToCopy > 0) && (newX != oldX));
      // if we're in place, we have 1 less row to copy
      unsigned rowsToCopy = std::min(oldM, newM) - inPlace;
      unsigned fillCount = newN - colsToCopy;
      if ((rowsToCopy) && (copyCols || fillCount)) {
        if (forwardCopy) {
          // truncation, we need to copy rows to increase stride
          T *src = this->ptr + inPlace * oldX;
          T *dst = npt + inPlace * newX;
          do {
            if (copyCols) std::copy_n(src, colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
            src += oldX;
            dst += newX;
          } while (--rowsToCopy);
        } else /* [[unlikely]] */ {
          // backwards copy, only needed when we increasing stride but not
          // reallocating, which should be comparatively uncommon.
          // Should probably benchmark or determine actual frequency
          // before adding `[[unlikely]]`.
          T *src = this->ptr + (rowsToCopy + inPlace) * oldX;
          T *dst = npt + (rowsToCopy + inPlace) * newX;
          do {
            src -= oldX;
            dst -= newX;
            if (colsToCopy) std::copy_backward(src, src + colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
          } while (--rowsToCopy);
        }
      }
      // zero init remaining rows
      for (size_t m = oldM; m < newM; ++m)
        std::fill_n(npt + m * newX, newN, T{});
      if (newAlloc) maybeDeallocate(npt, len);
    }
  }
  constexpr void resize(Row r) {
    if constexpr (std::integral<S>) {
      return resize(S(r));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resize(nz.set(r));
    }
  }
  constexpr void resize(Col c) {
    if constexpr (std::integral<S>) {
      return resize(S(c));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resize(nz.set(c));
    }
  }
  constexpr void resizeForOverwrite(S M) {
    U L = U(M);
    if (L > U(this->sz)) growUndef(L);
    this->sz = M;
  }
  constexpr void resizeForOverwrite(Row r) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(r));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(Col c) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(c));
    } else if constexpr (MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(c));
    }
  }
  constexpr void reserve(S nz) {
    U newCapacity = U(nz);
    if (newCapacity <= this->capacity) return;
      // allocate new, copy, deallocate old
#if __cplusplus >= 202202L
    std::allocation_result res = allocator.allocate_at_least(newCapacity);
    T *newPtr = res.ptr;
    newCapacity = U(res.size);
#else
    T *newPtr = allocator.allocate(newCapacity);
#endif
    if (U oldLen = U(this->sz))
      std::uninitialized_copy_n((T *)(this->ptr), oldLen, newPtr);
    maybeDeallocate(newPtr, newCapacity);
  }
  [[nodiscard]] constexpr auto get_allocator() const noexcept -> A {
    return allocator;
  }
  // set size and 0.
  constexpr void setSize(Row r, Col c) {
    resizeForOverwrite({r, c});
    this->fill(0);
  }
  constexpr void resize(Row MM, Col NN) { resize(DenseDims{MM, NN}); }
  constexpr void reserve(Row M, Col N) {
    if constexpr (std::is_same_v<S, StridedDims>)
      reserve(StridedDims{M, N, max(N, RowStride{this->dim()})});
    else if constexpr (std::is_same_v<S, SquareDims>)
      reserve(SquareDims{unsigned(std::max(*M, *N))});
    else reserve(DenseDims{M, N});
  }
  constexpr void reserve(Row M, RowStride X) {
    if constexpr (std::is_same_v<S, StridedDims>)
      reserve(StridedDims{*M, *X, *X});
    else if constexpr (std::is_same_v<S, SquareDims>)
      reserve(SquareDims{unsigned(std::max(*M, *X))});
    else reserve(DenseDims{*M, *X});
  }
  constexpr void clearReserve(Row M, Col N) {
    this->clear();
    reserve(M, N);
  }
  constexpr void clearReserve(Row M, RowStride X) {
    this->clear();
    reserve(M, X);
  }
  constexpr void resizeForOverwrite(Row M, Col N, RowStride X) {
    invariant(X >= N);
    if constexpr (std::is_same_v<S, StridedDims>) resizeForOverwrite({M, N, X});
    else if constexpr (std::is_same_v<S, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }
  constexpr void resizeForOverwrite(Row M, Col N) {
    if constexpr (std::is_same_v<S, StridedDims>)
      resizeForOverwrite({M, N, *N});
    else if constexpr (std::is_same_v<S, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }

  constexpr void extendOrAssertSize(Row R, Col C) {
    resizeForOverwrite(DenseDims{R, C});
  }
  constexpr void moveLast(Col j) {
    static_assert(MatrixDimension<S>);
    if (j == this->numCol()) return;
    Col Nm1 = this->numCol() - 1;
    for (size_t m = 0; m < this->numRow(); ++m) {
      auto x = (*this)(m, j);
      for (Col n = j; n < Nm1;) {
        Col o = n++;
        (*this)(m, o) = (*this)(m, n);
      }
      (*this)(m, Nm1) = x;
    }
  }

protected:
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  [[no_unique_address]] A allocator{};

  constexpr void allocateAtLeast(U len) {
#if __cplusplus >= 202202L
    std::allocation_result res = allocator.allocate_at_least(len);
    this->ptr = res.ptr;
    this->capacity = res.count;
#else
    this->ptr = allocator.allocate(len);
    this->capacity = len;
#endif
  }
  [[nodiscard]] constexpr auto isSmall() const -> bool {
    return (reinterpret_cast<const std::byte *>(this->data())) ==
           (reinterpret_cast<const std::byte *>(this) + sizeof(*this));
  }
  // this method should only be called from the destructor
  // (and the implementation taking the new ptr and capacity)
  constexpr void maybeDeallocate() noexcept {
    if (!isSmall()) this->allocator.deallocate(this->ptr, this->capacity);
  }
  // this method should be called whenever the buffer lives
  constexpr void maybeDeallocate(NotNull<T> newPtr, U newCapacity) noexcept {
    maybeDeallocate();
    this->ptr = newPtr;
    this->capacity = newCapacity;
  }
  // grow, discarding old data
  constexpr void growUndef(U M) {
    if (M <= this->capacity) return;
    maybeDeallocate();
    // because this doesn't care about the old data,
    // we can allocate after freeing, which may be faster
    this->ptr = this->allocator.allocate(M);
    this->capacity = M;
#ifndef NDEBUG
    if constexpr (std::numeric_limits<T>::has_signaling_NaN)
      std::fill_n(this->ptr, M, std::numeric_limits<T>::signaling_NaN());
    else std::fill_n(this->ptr, M, std::numeric_limits<T>::min());
#endif
  }
};

template <class T, class S>
concept AbstractSimilar =
  (MatrixDimension<S> && AbstractMatrix<T>) ||
  ((std::integral<S> || std::is_same_v<S, StridedRange>) && AbstractVector<T>);

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <class T, class S, size_t N, class A, std::unsigned_integral U>
struct ManagedArray : ReallocView<T, S, A, U> {
  static_assert(std::is_trivially_destructible_v<T>);
  using BaseT = ReallocView<T, S, A, U>;

  constexpr ManagedArray() noexcept : ReallocView<T, S, A, U>{memory, S{}, N} {
#ifndef NDEBUG
    if constexpr (std::numeric_limits<T>::has_signaling_NaN)
      std::fill_n(this->ptr, N, std::numeric_limits<T>::signaling_NaN());
    else std::fill_n(this->ptr, N, std::numeric_limits<T>::min());
#endif
  }
  constexpr ManagedArray(S s) noexcept : BaseT{memory, s, N} {
    U len = U(this->sz);
    if (len > N) this->allocateAtLeast(len);
#ifndef NDEBUG
    if constexpr (std::numeric_limits<T>::has_signaling_NaN)
      std::fill_n(this->ptr, len, std::numeric_limits<T>::signaling_NaN());
    else std::fill_n(this->ptr, len, std::numeric_limits<T>::min());
#endif
  }
  constexpr ManagedArray(S s, T x) noexcept : BaseT{memory, s, N} {
    U len = U(this->sz);
    if (len > N) this->allocateAtLeast(len);
    std::uninitialized_fill_n((T *)(this->ptr), len, x);
  }
  template <class D, std::unsigned_integral I>
  constexpr ManagedArray(const ManagedArray<T, D, N, A, I> &b) noexcept
    : BaseT{memory, S(b.dim()), U(N), b.get_allocator()} {
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
  template <std::convertible_to<T> Y, class D, class AY,
            std::unsigned_integral I>
  constexpr ManagedArray(const ManagedArray<Y, D, N, AY, I> &b) noexcept
    : BaseT{memory, S(b.dim()), U(N), b.get_allocator()} {
    U len = U(this->sz);
    this->growUndef(len);
    if constexpr (DenseLayout<D> && DenseLayout<S>) {
      std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    } else if constexpr (MatrixDimension<D> && MatrixDimension<S>) {
      invariant(b.numRow() == this->numRow());
      invariant(b.numCol() == this->numCol());
      for (size_t m = 0; m < this->numRow(); ++m)
        for (size_t n = 0; n < this->numCol(); ++n) (*this)(m, n) = b(m, n);
    } else if constexpr (MatrixDimension<D>) {
      size_t j = 0;
      for (size_t m = 0; m < b.numRow(); ++m)
        for (size_t n = 0; n < b.numCol(); ++n) (*this)(j++) = b(m, n);
    } else if constexpr (MatrixDimension<S>) {
      size_t j = 0;
      for (size_t m = 0; m < this->numRow(); ++m)
        for (size_t n = 0; n < this->numCol(); ++n) (*this)(m, n) = b(j++);
    } else {
      for (size_t i = 0; i < len; ++i) new ((T *)(this->ptr) + i) T(b[i]);
    }
  }
  template <std::convertible_to<T> Y>
  constexpr ManagedArray(std::initializer_list<Y> il) noexcept
    : BaseT{memory, S(il.size()), U(N)} {
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(il.begin(), len, (T *)(this->ptr));
  }
  template <std::convertible_to<T> Y, class D, class AY,
            std::unsigned_integral I>
  constexpr ManagedArray(const ManagedArray<Y, D, N, AY, I> &b, S s) noexcept
    : BaseT{memory, S(s), U(N), b.get_allocator()} {
    U len = U(this->sz);
    invariant(len == U(b.size()));
    this->growUndef(len);
    for (size_t i = 0; i < len; ++i) new ((T *)(this->ptr) + i) T(b[i]);
  }
  constexpr ManagedArray(const ManagedArray &b) noexcept
    : BaseT{memory, S(b.dim()), U(N), b.get_allocator()} {
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
  constexpr ManagedArray(const Array<T, S> &b) noexcept
    : BaseT{memory, S(b.dim()), U(N)} {
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
  template <AbstractSimilar<S> V>
  constexpr ManagedArray(const V &b) noexcept
    : BaseT{memory, S(b.size()), U(N)} {
    U len = U(this->sz);
    this->growUndef(len);
    (*this) << b;
  }
  template <class D, std::unsigned_integral I>
  constexpr ManagedArray(ManagedArray<T, D, N, A, I> &&b) noexcept
    : BaseT{b.data(), b.dim(), U(b.getCapacity()), b.get_allocator()} {
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    if (b.isSmall()) {
      this->ptr = memory;
      std::uninitialized_copy_n(b.data(), N, (T *)(this->ptr));
    }
    b.resetNoFree();
  }
  constexpr ManagedArray(ManagedArray &&b) noexcept
    : BaseT{b.data(), b.dim(), U(b.getCapacity()), b.get_allocator()} {
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    if (b.isSmall()) {
      this->ptr = memory;
      std::uninitialized_copy_n(b.data(), N, (T *)(this->ptr));
    }
    b.resetNoFree();
  }
  template <class D, std::unsigned_integral I>
  constexpr ManagedArray(ManagedArray<T, D, N, A, I> &&b, S s) noexcept
    : BaseT{b.data(), s, U(b.getCapacity()), b.get_allocator()} {
    assert(U(s) == U(b.dim()) && "size mismatch");
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    if (b.isSmall()) {
      this->ptr = memory;
      std::uninitialized_copy_n(b.data(), N, (T *)(this->ptr));
    }
    b.resetNoFree();
  }
  template <std::convertible_to<T> Y>
  constexpr ManagedArray(const SmallSparseMatrix<Y> &B)
    : ReallocView<T, S, A, U>{memory, B.dim(), N} {
    U len = U(this->sz);
    this->growUndef(len);
    this->fill(0);
    size_t k = 0;
    for (size_t i = 0; i < this->numRow(); ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        (*this)(i, j++) = T(B.nonZeros[k++]);
      }
    }
    assert(k == B.nonZeros.size());
  }

  template <class D, std::unsigned_integral I>
  constexpr auto operator=(const ManagedArray<T, D, N, A, I> &b) noexcept
    -> ManagedArray & {
    if (this == &b) return *this;
    this->sz = b.dim();
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  template <class D, std::unsigned_integral I>
  constexpr auto operator=(ManagedArray<T, D, N, A, I> &&b) noexcept
    -> ManagedArray & {
    if (this->begin() == b.begin()) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.get_allocator());
    // if `b` is small, we need to copy memory
    // no need to shrink our capacity
    if (b.isSmall())
      std::uninitialized_copy_n(b.data(), size_t(this->sz), (T *)(this->ptr));
    else this->maybeDeallocate(b.wrappedPtr(), b.getCapacity());
    b.resetNoFree();
    return *this;
  }
  constexpr auto operator=(const ManagedArray &b) noexcept -> ManagedArray & {
    if (this == &b) return *this;
    this->sz = b.dim();
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  constexpr auto operator=(ManagedArray &&b) noexcept -> ManagedArray & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.get_allocator());
    if (b.isSmall()) {
      // if `b` is small, we need to copy memory
      // no need to shrink our capacity
      std::uninitialized_copy_n(b.data(), size_t(this->sz), (T *)(this->ptr));
    } else {
      // otherwise, we take its pointer
      this->maybeDeallocate(b.wrappedPtr(), b.getCapacity());
    }
    b.resetNoFree();
    return *this;
  }
  [[nodiscard]] constexpr auto isSmall() const -> bool {
    return this->ptr == memory;
  }
  constexpr void resetNoFree() {
    this->ptr = memory;
    this->sz = S{};
    this->capacity = N;
  }
  constexpr ~ManagedArray() noexcept { this->maybeDeallocate(); }

  [[nodiscard]] static constexpr auto identity(unsigned M) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    ManagedArray B(SquareDims{M}, T{0});
    B.diag() << 1;
    return B;
  }
  [[nodiscard]] static constexpr auto identity(Row R) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    return identity(unsigned(R));
  }
  [[nodiscard]] static constexpr auto identity(Col C) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    return identity(unsigned(C));
  }
  friend inline void PrintTo(const ManagedArray &x, std::ostream *os) {
    adaptOStream(*os, x);
  }

private:
  T memory[N]; // NOLINT (modernize-avoid-c-style-arrays)
};
template <class T, class S, class A, std::unsigned_integral U>
struct ManagedArray<T, S, 0, A, U> : ReallocView<T, S, A, U, true> {
  static_assert(std::is_trivially_destructible_v<T>);
  using BaseT = ReallocView<T, S, A, U, true>;
  constexpr ManagedArray() noexcept : BaseT{nullptr, S{}, U{}} {};
  constexpr ManagedArray(S s) noexcept : BaseT{A{}.allocate(U(s)), s, U(s)} {
#ifndef NDEBUG
    if constexpr (std::numeric_limits<T>::has_signaling_NaN)
      std::fill_n(this->ptr, U(s), std::numeric_limits<T>::signaling_NaN());
    else std::fill_n(this->ptr, U(s), std::numeric_limits<T>::min());
#endif
  }
  constexpr ManagedArray(S s, T x) noexcept
    : BaseT{A{}.allocate(U(s)), s, U(s)} {
    std::uninitialized_fill_n((T *)(this->ptr), U(s), x);
  }
  template <class D, size_t N, std::unsigned_integral I>
  constexpr ManagedArray(const ManagedArray<T, D, N, A, I> &b) noexcept
    : BaseT{b.get_allocator().allocate(U(b.dim())), S(b.dim()), U(b.dim()),
            b.get_allocator()} {
    std::uninitialized_copy_n(b.data(), U(b.dim()), (T *)(this->ptr));
  }
  // template <class Y, class D, class AY, std::unsigned_integral I,
  // std::enable_if_t<std::is_convertible_v<Y, T>>>
  template <std::convertible_to<T> Y, class D, class AY,
            std::unsigned_integral I, size_t N>
  constexpr ManagedArray(const ManagedArray<Y, D, N, AY, I> &b) noexcept
    : BaseT{b.get_allocator().allocate(U(b.dim())), S(b.dim()), U(b.dim()),
            b.get_allocator()} {
    U len = U(this->sz);
    for (size_t i = 0; i < len; ++i) new ((T *)(this->ptr) + i) T(b[i]);
  }
  constexpr ManagedArray(const ManagedArray &b) noexcept
    : BaseT{b.get_allocator().allocate(U(b.dim())), S(b.dim()), U(b.dim()),
            b.get_allocator()} {
    U len = U(this->sz);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
  template <class D, std::unsigned_integral I>
  constexpr ManagedArray(ManagedArray<T, D, 0, A, I> &&b) noexcept
    : BaseT{b.data(), b.dim(), U(b.getCapacity()), b.get_allocator()} {
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    b.resetNoFree();
  }
  constexpr ManagedArray(ManagedArray &&b) noexcept
    : BaseT{b.data(), b.dim(), U(b.getCapacity()), b.get_allocator()} {
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    b.resetNoFree();
  }
  template <class D, std::unsigned_integral I>
  constexpr ManagedArray(ManagedArray<T, D, 0, A, I> &&b, S s) noexcept
    : BaseT{b.data(), s, U(b.getCapacity()), b.get_allocator()} {
    assert(U(s) == U(b.dim()) && "size mismatch");
    assert(uint64_t(b.getCapacity()) == uint64_t(this->getCapacity()) &&
           "capacity overflow");
    b.resetNoFree();
  }
  template <std::convertible_to<T> Y>
  constexpr ManagedArray(const SmallSparseMatrix<Y> &B)
    : ReallocView<T, S, A, U>{A{}.allocate(U(B.dim())), B.dim(), U(B.dim())} {
    U len = U(this->sz);
    this->growUndef(len);
    this->fill(0);
    size_t k = 0;
    for (size_t i = 0; i < this->numRow(); ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        (*this)(i, j++) = T(B.nonZeros[k++]);
      }
    }
    assert(k == B.nonZeros.size());
  }

  template <class D, std::unsigned_integral I, size_t N>
  constexpr auto operator=(const ManagedArray<T, D, N, A, I> &b) noexcept
    -> ManagedArray & {
    if (this == &b) return *this;
    this->sz = b.dim();
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  template <class D, std::unsigned_integral I, size_t N>
  constexpr auto operator=(ManagedArray<T, D, N, A, I> &&b) noexcept
    -> ManagedArray & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.get_allocator());
    if (b.isSmall()) {
      // if `b` is small, we need to copy memory
      // no need to shrink our capacity
      std::uninitialized_copy_n(b.data(), size_t(this->sz), (T *)(this->ptr));
    } else {
      // otherwise, we take its pointer
      maybeDeallocate(b.wrappedPtr(), b.getCapacity());
    }
    b.resetNoFree();
    return *this;
  }
  constexpr auto operator=(const ManagedArray &b) noexcept -> ManagedArray & {
    if (this == &b) return *this;
    this->sz = b.dim();
    U len = U(this->sz);
    this->growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  constexpr auto operator=(ManagedArray &&b) noexcept -> ManagedArray & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.get_allocator());
    maybeDeallocate(b.wrappedPtr(), b.getCapacity());
    b.resetNoFree();
    return *this;
  }
  constexpr void resetNoFree() {
    this->sz = S{};
    this->capacity = 0;
  }
  constexpr ~ManagedArray() { this->maybeDeallocate(); }
  [[nodiscard]] constexpr auto isSmall() const -> bool { return false; }
  [[nodiscard]] static constexpr auto identity(unsigned M) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    ManagedArray B(SquareDims{M}, T{0});
    B.diag() << 1;
    return B;
  }
  [[nodiscard]] static constexpr auto identity(Row R) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    return identity(unsigned(R));
  }
  [[nodiscard]] static constexpr auto identity(Col C) -> ManagedArray {
    static_assert(MatrixDimension<S>);
    return identity(unsigned(C));
  }
};

static_assert(std::move_constructible<ManagedArray<intptr_t, unsigned>>);
static_assert(std::copyable<ManagedArray<intptr_t, unsigned>>);
// Check that `[[no_unique_address]]` is working.
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 3*4 + 4 + 0 + 64*8 = 24 + 512 = 536
static_assert(
  sizeof(ManagedArray<int64_t, StridedDims, 64, std::allocator<int64_t>>) ==
  536);
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 2*4 + 8 + 0 + 64*8 = 24 + 512 = 536
static_assert(
  sizeof(ManagedArray<int64_t, DenseDims, 64, std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 1*4 + 4 + 0 + 64*8 = 16 + 512 = 528
static_assert(
  sizeof(ManagedArray<int64_t, SquareDims, 64, std::allocator<int64_t>>) ==
  528);

template <class T, size_t N = PreAllocStorage<T>()>
using Vector = ManagedArray<T, unsigned, N>;
template <class T> using PtrVector = Array<T, unsigned>;
template <class T> using MutPtrVector = MutArray<T, unsigned>;

static_assert(std::move_constructible<Vector<intptr_t>>);
static_assert(std::copy_constructible<Vector<intptr_t>>);
static_assert(std::copyable<Vector<intptr_t>>);
static_assert(AbstractVector<Array<int64_t, unsigned>>);
static_assert(AbstractVector<MutArray<int64_t, unsigned>>);
static_assert(AbstractVector<Vector<int64_t>>);
static_assert(!AbstractVector<int64_t>);

template <typename T> using StridedVector = Array<T, StridedRange>;
template <typename T> using MutStridedVector = MutArray<T, StridedRange>;

static_assert(AbstractVector<StridedVector<int64_t>>);
static_assert(AbstractVector<MutStridedVector<int64_t>>);
static_assert(std::is_trivially_copyable_v<StridedVector<int64_t>>);

template <class T> using PtrMatrix = Array<T, StridedDims>;
template <class T> using MutPtrMatrix = MutArray<T, StridedDims>;
template <class T, size_t L = 64>
using Matrix = ManagedArray<T, StridedDims, L>;
template <class T> using DensePtrMatrix = Array<T, DenseDims>;
template <class T> using MutDensePtrMatrix = MutArray<T, DenseDims>;
template <class T, size_t L = 64>
using DenseMatrix = ManagedArray<T, DenseDims, L>;
template <class T> using SquarePtrMatrix = Array<T, SquareDims>;
template <class T> using MutSquarePtrMatrix = MutArray<T, SquareDims>;
template <class T, size_t L = 16>
using SquareMatrix = ManagedArray<T, SquareDims, L>;

static_assert(sizeof(PtrMatrix<int64_t>) ==
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(MutPtrMatrix<int64_t>) ==
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(DensePtrMatrix<int64_t>) ==
              2 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(MutDensePtrMatrix<int64_t>) ==
              2 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(SquarePtrMatrix<int64_t>) ==
              sizeof(size_t) + sizeof(int64_t *));
static_assert(sizeof(MutSquarePtrMatrix<int64_t>) ==
              sizeof(size_t) + sizeof(int64_t *));
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> is not trivially copyable!");
static_assert(std::is_trivially_copyable_v<PtrVector<int64_t>>,
              "PtrVector<int64_t,0> is not trivially copyable!");
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>,
//               "MutPtrMatrix<int64_t> is not trivially copyable!");
static_assert(sizeof(ManagedArray<int32_t, DenseDims, 15>) ==
              sizeof(int32_t *) + 4 * sizeof(unsigned int) +
                16 * sizeof(int32_t));
static_assert(sizeof(ReallocView<int32_t, DenseDims>) ==
              sizeof(int32_t *) + 4 * sizeof(unsigned int));

static_assert(!AbstractVector<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");

static_assert(AbstractMatrix<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(std::same_as<std::remove_reference_t<decltype(PtrMatrix<int64_t>(
                             nullptr, Row{0}, Col{0})(size_t(0), size_t(0)))>,
                           int64_t>);
static_assert(
  std::same_as<std::remove_reference_t<decltype(MutPtrMatrix<int64_t>(
                 nullptr, Row{0}, Col{0})(size_t(0), size_t(0)))>,
               int64_t>);

static_assert(AbstractMatrix<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");

static_assert(AbstractVector<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");
static_assert(AbstractVector<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");

static_assert(AbstractVector<Vector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");

static_assert(!AbstractMatrix<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(std::is_convertible_v<DenseMatrix<int64_t>, Matrix<int64_t>>);
static_assert(
  std::is_convertible_v<DenseMatrix<int64_t>, DensePtrMatrix<int64_t>>);
static_assert(std::is_convertible_v<DenseMatrix<int64_t>, PtrMatrix<int64_t>>);
static_assert(std::is_convertible_v<SquareMatrix<int64_t>, Matrix<int64_t>>);
static_assert(
  std::is_convertible_v<SquareMatrix<int64_t>, MutPtrMatrix<int64_t>>);

using IntMatrix = Matrix<int64_t>;
static_assert(std::same_as<IntMatrix::value_type, int64_t>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(std::copyable<IntMatrix>);
static_assert(std::same_as<eltype_t<Matrix<int64_t>>, int64_t>);

} // namespace LinAlg
using LinAlg::AbstractVector, LinAlg::AbstractMatrix, LinAlg::PtrVector,
  LinAlg::MutPtrVector, LinAlg::Vector, LinAlg::Matrix, LinAlg::SquareMatrix,
  LinAlg::IntMatrix, LinAlg::PtrMatrix, LinAlg::MutPtrMatrix;
