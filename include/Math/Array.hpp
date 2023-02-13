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
#include <type_traits>
#include <utility>

namespace LinearAlgebra {

template <class T>
concept SizeMultiple8 = (sizeof(T) % 8) == 0;

template <class T> struct default_capacity_type {
  using type = unsigned int;
};
template <SizeMultiple8 T> struct default_capacity_type<T> {
  using type = std::size_t;
};
static_assert(!SizeMultiple8<uint32_t>);
static_assert(SizeMultiple8<uint64_t>);
static_assert(
  std::is_same_v<typename default_capacity_type<uint32_t>::type, uint32_t>);
static_assert(
  std::is_same_v<typename default_capacity_type<uint64_t>::type, uint64_t>);

template <class T>
using default_capacity_type_t = typename default_capacity_type<T>::type;

template <class T> consteval auto PreAllocStorage() -> size_t {
  constexpr size_t TotalBytes = 128;
  constexpr size_t RemainingBytes =
    TotalBytes - sizeof(llvm::SmallVector<T, 0>);
  constexpr size_t N = RemainingBytes / sizeof(T);
  return std::max<size_t>(1, N);
}
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

  constexpr Array(const Array &) = default;
  constexpr Array(Array &&) = default;
  constexpr Array &operator=(const Array &) = default;
  constexpr Array &operator=(Array &&) = default;
  constexpr Array(T *p, S s) : ptr(p), sz(s) {}
  constexpr Array(NotNull<T> p, S s) : ptr(p), sz(s) {}
  constexpr Array(T *p, Row r, Col c)
    : ptr(p), sz(MatrixDimension<S> ? DenseDims{r, c} : S(r)) {}
  constexpr Array(NotNull<T> p, Row r, Col c)
    : ptr(p), sz(MatrixDimension<S> ? DenseDims{r, c} : S(r)) {}

  [[nodiscard, gnu::returns_nonnull]] constexpr auto data() const noexcept
    -> const T * {
    return this->ptr;
  }
  [[nodiscard]] constexpr auto wrappedPtr() noexcept -> NotNull<T> {
    return this->ptr;
  }

  [[nodiscard]] constexpr auto begin() const noexcept {
    if constexpr (std::is_same_v<S, StridedRange>)
      StridedIterator{(const T *)ptr, sz.stride};
    else return (const T *)ptr;
  }
  [[nodiscard]] constexpr auto end() const noexcept {
    return begin() + size_t(sz);
  }
  [[nodiscard]] constexpr auto rbegin() const noexcept -> const T * {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const noexcept -> const T * {
    return std::reverse_iterator(begin());
  }
  // indexing has two components:
  // 1. offsetting the pointer
  // 2. calculating new dim
  // static constexpr auto slice(NotNull<T>, Index<S> auto i){
  //   auto
  // }
  constexpr auto operator[](Index<S> auto i) const noexcept {
    auto offset = calcOffset(sz, i);
    auto newDim = calcNewDim(sz, i);
    if constexpr (std::is_same_v<decltype(newDim), Empty>) return ptr[offset];
    else return Array<T, decltype(newDim)>{ptr + offset, newDim};
  }
  // TODO: switch to operator[] when we enable c++23
  // for vectors, we just drop the column, essentially broadcasting
  template <class R, class C>
  constexpr auto operator()(R r, C c) const noexcept {
    if constexpr (MatrixDimension<S>)
      return (*this)[CartesianIndex<R, C>{r, c}];
    else return (*this)[size_t(r)];
  }
  [[nodiscard]] constexpr auto minRowCol() const -> size_t {
    return std::min(size_t(numRow()), size_t(numCol()));
  }

  [[nodiscard]] constexpr auto diag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{sz}) + 1};
    return Array<T, StridedRange>{ptr, r};
  }
  [[nodiscard]] constexpr auto antiDiag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{sz}) - 1};
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
    else return std::make_pair(Row{sz}, Col{sz});
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
  constexpr auto view() const noexcept -> Array<T, S> {
    return Array<T, S>{this->ptr, this->sz};
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
      A(m, _(c, LinearAlgebra::end)) = (*this)(m, _(c + 1, LinearAlgebra::end));
    }
    return A;
  }

protected:
  [[no_unique_address]] NotNull<T> ptr;
  [[no_unique_address]] S sz{};
};

template <class T, class S> struct MutArray : Array<T, S> {
  using BaseT = Array<T, S>;
  // using BaseT::BaseT;
  using BaseT::operator[], BaseT::operator();

  constexpr MutArray(const MutArray &) = default;
  constexpr MutArray(MutArray &&) = default;
  constexpr MutArray &operator=(const MutArray &) = default;
  constexpr MutArray &operator=(MutArray &&) = default;

  template <class... Args>
  constexpr MutArray(Args &&...args)
    : Array<T, S>(std::forward<Args>(args)...) {}

  [[nodiscard, gnu::returns_nonnull]] constexpr auto data() noexcept -> T * {
    return this->ptr;
  }
  [[nodiscard]] constexpr auto wrappedPtr() noexcept -> NotNull<T> {
    return this->ptr;
  }

  [[nodiscard]] constexpr auto begin() noexcept -> T * { return this->ptr; }
  [[nodiscard]] constexpr auto end() noexcept -> T * {
    return this->ptr + size_t(this->sz);
  }
  [[nodiscard]] constexpr auto rbegin() noexcept -> T * {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() noexcept -> T * {
    return std::reverse_iterator(begin());
  }
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
    StridedRange r{min(Row{this->sz}, Col{this->sz}),
                   unsigned(RowStride{this->sz}) + 1};
    return MutArray<T, StridedRange>{this->ptr, r};
  }
  [[nodiscard]] constexpr auto antiDiag() noexcept {
    Col c = Col{this->sz};
    StridedRange r{min(Row{this->sz}, c), unsigned(RowStride{this->sz}) - 1};
    return MutArray<T, StridedRange>{this->ptr + size_t(c) - 1, r};
  }

  [[gnu::flatten]] constexpr auto operator<<(const SmallSparseMatrix<T> &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    assert(this->numRow() == B.numRow());
    assert(this->numCol() == B.numCol());
    T *mem = data();
    size_t k = 0;
    for (size_t i = 0; i < this->numRow(); ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        mem[this->rowStride() * i + (j++)] = B.nonZeros[k++];
      }
    }
    assert(k == B.nonZeros.size());
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator<<(const AbstractVector auto &B)
    -> decltype(auto) {
    return copyto(*this, B);
  }

  [[gnu::flatten]] constexpr auto operator<<(const AbstractMatrix auto &B)
    -> decltype(auto) {
    return copyto(*this, B);
  }
  [[gnu::flatten]] constexpr auto operator<<(const std::integral auto b)
    -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = this->sz; c < L; ++c) (*this)(c) = b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) = b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(this->numRow() == B.numRow());
    invariant(this->numCol() == B.numCol());
    for (size_t r = 0; r < this->numRow(); ++r)
      for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractMatrix auto &B)
    -> decltype(auto) {
    static_assert(MatrixDimension<S>);
    invariant(this->numRow() == B.numRow());
    invariant(this->numCol() == B.numCol());
    for (size_t r = 0; r < this->numRow(); ++r)
      for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator*=(const std::integral auto b)
    -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = this->sz; c < L; ++c) (*this)(c) *= b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) *= b;
    }
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator/=(const std::integral auto b)
    -> decltype(auto) {
    if constexpr (std::integral<S>) {
      for (size_t c = 0, L = this->sz; c < L; ++c) (*this)(c) /= b;
    } else {
      for (size_t r = 0; r < this->numRow(); ++r)
        for (size_t c = 0; c < this->numCol(); ++c) (*this)(r, c) /= b;
    }
    return *this;
  }
};
/// Non-owning view of a managed array, capable of reallocating, etc.
/// It does not own memory. Mostly, it serves to drop the inlined
/// stack capacity of the `ManagedArray` from the type.
template <class T, class S, class A = std::allocator<T>,
          std::unsigned_integral U = default_capacity_type_t<S>>
struct ManagedArrayView : MutArray<T, S> {
  using BaseT = MutArray<T, S>;

  template <class... Args> constexpr auto emplace_back(Args &&...args) {
    static_assert(std::is_integral_v<S>, "emplace_back requires integral size");
    if (this->sz == this->capacity) [[unlikely]]
      reserve(this->capacity + this->capacity);
    new (this->ptr + this->sz++) T(std::forward<Args>(args)...);
  }
  constexpr void push_back(T value) {
    static_assert(std::is_integral_v<S>, "push_back requires integral size");
    if (this->sz == this->capacity) [[unlikely]]
      reserve(this->capacity + this->capacity);
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
      static_assert(LinearAlgebra::MatrixDimension<S>,
                    "Can only resize 1 or 2d containers.");
      auto newX = unsigned{LinearAlgebra::RowStride{nz}},
           oldX = unsigned{LinearAlgebra::RowStride{oz}},
           newN = unsigned{LinearAlgebra::Col{nz}},
           oldN = unsigned{LinearAlgebra::Col{oz}},
           newM = unsigned{LinearAlgebra::Row{nz}},
           oldM = unsigned{LinearAlgebra::Row{oz}};
      U len = U(nz);
      bool newAlloc = U(len) > this->capacity;
      bool inPlace = !newAlloc;
#if __cplusplus >= 202202L
      T *npt = (T *)(ptr);
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
      unsigned rowsToCopy = std::min(oldM, newM);
      unsigned fillCount = newN - colsToCopy;
      if ((rowsToCopy) && (copyCols || fillCount)) {
        if (forwardCopy) {
          // truncation, we need to copy rows to increase stride
          T *src = this->ptr + inPlace * oldX;
          T *dst = npt + inPlace * newX;
          rowsToCopy -= inPlace;
          do {
            if (copyCols) std::copy(src, src + colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
            src += oldX;
            dst += newX;
          } while (--rowsToCopy);
        } else /* [[unlikely]] */ {
          // backwards copy, only needed when we increasing stride but not
          // reallocating, which should be comparatively uncommon.
          // Should probably benchmark or determine actual frequency
          // before adding `[[unlikely]]`.
          T *src = this->ptr + rowsToCopy * oldX;
          T *dst = npt + rowsToCopy * newX;
          rowsToCopy -= inPlace;
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
  constexpr void resize(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return resize(S(r));
    } else if constexpr (LinearAlgebra::MatrixDimension<S>) {
      S nz = this->sz;
      return resize(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(S M) {
    U L = U(M);
    if (L > U(this->sz)) growUndef(L);
    this->sz = M;
  }
  constexpr void resizeForOverwrite(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(r));
    } else if constexpr (LinearAlgebra::MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(c));
    } else if constexpr (LinearAlgebra::MatrixDimension<S>) {
      S nz = this->sz;
      return resizeForOverwrite(nz.set(c));
    }
  }
  constexpr void erase(S i) {
    static_assert(std::integral<S>, "erase requires integral size");
    S oldLen = this->sz--;
    if (i < this->sz)
      std::copy((T *)(this->ptr) + i + 1, this->ptr + oldLen,
                (T *)(this->ptr) + i);
  }
  constexpr void erase(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return erase(S(r));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {

      auto stride = unsigned{LinearAlgebra::RowStride{this->sz}},
           col = unsigned{LinearAlgebra::Col{this->sz}},
           newRow = unsigned{LinearAlgebra::Row{this->sz}} - 1;
      this->sz.set(LinearAlgebra::Row{newRow});
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
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if erasing a row, matrix must be strided or dense.");
      auto col = unsigned{LinearAlgebra::Col{this->sz}},
           newRow = unsigned{LinearAlgebra::Row{this->sz}} - 1;
      this->sz.set(LinearAlgebra::Row{newRow});
      if ((col == 0) || (r == newRow)) return;
      T *dst = this->ptr + r * col;
      std::copy_n(dst + col, (newRow - unsigned(r)) * col, dst);
    }
  }
  constexpr void erase(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return erase(S(c));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      auto stride = unsigned{LinearAlgebra::RowStride{this->sz}},
           newCol = unsigned{LinearAlgebra::Col{this->sz}} - 1,
           row = unsigned{LinearAlgebra::Row{this->sz}};
      this->sz.set(LinearAlgebra::Col{newCol});
      unsigned colsToCopy = newCol - unsigned(c);
      if ((colsToCopy == 0) || (row == 0)) return;
      // we only need to copy if memory shifts position
      for (size_t m = 0; m < row; ++m) {
        T *dst = this->ptr + m * stride + unsigned(c);
        std::copy_n(dst + 1, colsToCopy, dst);
      }
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if erasing a col, matrix must be strided or dense.");
      auto newCol = unsigned{LinearAlgebra::Col{this->sz}}, oldCol = newCol--,
           row = unsigned{LinearAlgebra::Row{this->sz}};
      this->sz.set(LinearAlgebra::Col{newCol});
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
  constexpr void truncate(S newLen) {
    invariant(U(newLen) <= this->capacity);
    this->sz = newLen;
  }
  constexpr void truncate(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return truncate(S(r));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      invariant(r <= LinearAlgebra::Row{this->sz});
      this->sz.set(r);
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if truncating a row, matrix must be strided or dense.");
      invariant(r <= LinearAlgebra::Row{this->sz});
      LinearAlgebra::DenseDims newSz = this->sz;
      resize(newSz.set(r));
    }
  }
  constexpr void truncate(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return truncate(S(c));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      invariant(c <= LinearAlgebra::Col{this->sz});
      this->sz.set(c);
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if truncating a col, matrix must be strided or dense.");
      invariant(c <= LinearAlgebra::Col{this->sz});
      LinearAlgebra::DenseDims newSz = this->sz;
      resize(newSz.set(c));
    }
  }
  constexpr void reserve(S nz) {
    U newCapacity = U(nz);
    if (newCapacity <= capacity) return;
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
  [[nodiscard]] constexpr auto getCapacity() const -> U { return capacity; }

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
  [[no_unique_address]] U capacity{0};
  [[no_unique_address]] A allocator{};

  constexpr void allocateAtLeast(U len) {
#if __cplusplus >= 202202L
    std::allocation_result res = allocator.allocate_at_least(len);
    this->ptr = res.ptr;
    capacity = res.count;
#else
    this->ptr = allocator.allocate(len);
    capacity = len;
#endif
  }
};

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <class T, class S, size_t N, class A, std::unsigned_integral U>
struct ManagedArray : ManagedArrayView<T, S, A, U> {
  static_assert(std::is_trivially_destructible_v<T>);
  using BaseT = ManagedArrayView<T, S, A, U>;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  constexpr ManagedArray() noexcept
    : ManagedArrayView<T, S, A, U>{memory, S{}, N} {}
  constexpr ManagedArray(S s) noexcept : BaseT{memory, s, N} {
    U len = U(this->sz);
    if (len <= N) return;
    this->allocateAtLeast(len);
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
    growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
  // template <class Y, class D, class AY, std::unsigned_integral I,
  // std::enable_if_t<std::is_convertible_v<Y, T>>>
  template <std::convertible_to<T> Y, class D, class AY,
            std::unsigned_integral I>
  constexpr ManagedArray(const ManagedArray<Y, D, N, AY, I> &b) noexcept
    : BaseT{memory, S(b.dim()), U(N), b.get_allocator()} {
    U len = U(this->sz);
    growUndef(len);
    for (size_t i = 0; i < len; ++i) new ((T *)(this->ptr) + i) T(b[i]);
  }
  constexpr ManagedArray(const ManagedArray &b) noexcept
    : BaseT{memory, S(b.dim()), U(N), b.get_allocator()} {
    U len = U(this->sz);
    growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
  }
#pragma GCC diagnostic pop
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
    : ManagedArrayView<T, S, A, U>{memory, B.dim(), N} {
    U len = U(this->sz);
    growUndef(len);
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
    growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  template <class D, std::unsigned_integral I>
  constexpr auto operator=(ManagedArray<T, D, N, A, I> &&b) noexcept
    -> ManagedArray & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.allocator);
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
    growUndef(len);
    std::uninitialized_copy_n(b.data(), len, (T *)(this->ptr));
    return *this;
  }
  constexpr auto operator=(ManagedArray &&b) noexcept -> ManagedArray & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    this->sz = b.dim();
    this->allocator = std::move(b.allocator);
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
  [[nodiscard]] constexpr auto isSmall() const -> bool {
    return this->ptr == memory;
  }
  constexpr void resetNoFree() {
    this->ptr = memory;
    this->sz = S{};
    this->capacity = N;
  }
  constexpr ~ManagedArray() { maybeDeallocate(); }

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

private:
  T memory[N]; // NOLINT (modernize-avoid-c-style-arrays)

  // this method should only be called from the destructor
  // (and the implementation taking the new ptr and capacity)
  constexpr void maybeDeallocate() {
    if (!isSmall()) this->allocator.deallocate(this->ptr, this->capacity);
  }
  // this method should be called whenever the buffer lives
  constexpr void maybeDeallocate(NotNull<T> newPtr, U newCapacity) {
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
  }
};

static_assert(std::move_constructible<ManagedArray<intptr_t, unsigned>>);
static_assert(std::copyable<ManagedArray<intptr_t, unsigned>>);
// Check that `[[no_unique_address]]` is working.
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 3*4 + 4 + 0 + 64*8 = 24 + 512 = 536
static_assert(sizeof(ManagedArray<int64_t, LinearAlgebra::StridedDims, 64,
                                  std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 2*4 + 8 + 0 + 64*8 = 24 + 512 = 536
static_assert(sizeof(ManagedArray<int64_t, LinearAlgebra::DenseDims, 64,
                                  std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, dims, capacity, allocator, array]
// 8 + 1*4 + 4 + 0 + 64*8 = 16 + 512 = 528
static_assert(sizeof(ManagedArray<int64_t, LinearAlgebra::SquareDims, 64,
                                  std::allocator<int64_t>>) == 528);

template <class T> using Vector = ManagedArray<T, unsigned>;
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

template <class T> using PtrMatrix = Array<T, LinearAlgebra::DenseDims>;
template <class T> using MutPtrMatrix = MutArray<T, LinearAlgebra::DenseDims>;
template <class T> using Matrix = ManagedArray<T, LinearAlgebra::DenseDims>;

static_assert(sizeof(PtrMatrix<int64_t>) ==
              2 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(MutPtrMatrix<int64_t>) ==
              2 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> is not trivially copyable!");
static_assert(std::is_trivially_copyable_v<PtrVector<int64_t>>,
              "PtrVector<int64_t,0> is not trivially copyable!");
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>,
//               "MutPtrMatrix<int64_t> is not trivially copyable!");

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

using IntMatrix = Matrix<int64_t>;
static_assert(std::same_as<IntMatrix::value_type, int64_t>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(std::copyable<IntMatrix>);
static_assert(std::same_as<eltype_t<Matrix<int64_t>>, int64_t>);

} // namespace LinearAlgebra
