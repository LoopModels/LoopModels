#pragma once

#include "Math/AxisTypes.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

template <typename T>
concept SizeMultiple8 = (sizeof(T) % 8) == 0;

template <typename T> struct DefaultCapacityType {
  using type = unsigned int;
};
template <SizeMultiple8 T> struct DefaultCapacityType<T> {
  using type = std::size_t;
};
static_assert(!SizeMultiple8<uint32_t>);
static_assert(SizeMultiple8<uint64_t>);
static_assert(
  std::is_same_v<typename DefaultCapacityType<uint32_t>::type, uint32_t>);
static_assert(
  std::is_same_v<typename DefaultCapacityType<uint64_t>::type, uint64_t>);

template <typename T>
using DefaultCapacityType_t = typename DefaultCapacityType<T>::type;

/// Stores memory, then pointer.
/// Thus struct's alignment determines initial alignment
/// of the stack memory.
/// Information related to size is then grouped next to the pointer.
template <typename T, size_t N, typename S, typename A = std::allocator<T>,
          std::unsigned_integral U = DefaultCapacityType_t<S>>
struct Buffer {
  static_assert(std::is_trivially_destructible_v<T>);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  constexpr Buffer() noexcept : ptr{memory}, capacity{N} {}
  constexpr Buffer(S s) noexcept : ptr{memory}, capacity{N} {
    sz = s;
    U len = U(sz);
    if (len <= N) return;
    ptr = allocator.allocate(len);
    capacity = len;
  }
  constexpr Buffer(S s, T x) noexcept : ptr{memory}, capacity{N} {
    sz = s;
    U len = U(sz);
    if (len > N) {
      ptr = allocator.allocate(len);
      capacity = len;
    }
    std::uninitialized_fill_n((T *)(ptr), len, x);
  }
  template <typename D, std::unsigned_integral I>
  constexpr Buffer(const Buffer<T, N, D, A, I> &b) noexcept
    : ptr{memory}, capacity{U(N)}, sz{S(b.size())}, allocator{
                                                      b.get_allocator()} {
    U len = U(sz);
    growUndef(len);
    std::uninitialized_copy_n((const T *)(b.data()), len, (T *)(ptr));
  }
#pragma GCC diagnostic pop
  template <typename D, std::unsigned_integral I>
  constexpr Buffer(Buffer<T, N, D, A, I> &&b) noexcept
    : ptr{b.data()}, capacity{U(b.getCapacity())}, sz{b.size()},
      allocator{b.get_allocator()} {
    assert(uint64_t(b.getCapacity()) == uint64_t(getCapacity()) &&
           "capacity overflow");
    if (b.isSmall()) {
      ptr = memory;
      std::uninitialized_copy_n((T *)(b.data()), N, (T *)(ptr));
    }
    b.resetNoFree();
  }
  template <typename D, std::unsigned_integral I>
  constexpr Buffer(Buffer<T, N, D, A, I> &&b, S s) noexcept
    : ptr{b.data()}, capacity{U(b.getCapacity())}, sz{s}, allocator{
                                                            b.get_allocator()} {
    assert(U(s) == U(b.size()) && "size mismatch");
    assert(uint64_t(b.getCapacity()) == uint64_t(getCapacity()) &&
           "capacity overflow");
    if (b.isSmall()) {
      ptr = memory;
      std::uninitialized_copy_n((T *)(b.data()), N, (T *)(ptr));
    }
    b.resetNoFree();
  }
  template <typename D, std::unsigned_integral I>
  constexpr auto operator=(const Buffer<T, N, D, A, I> &b) noexcept
    -> Buffer & {
    if (this == &b) return *this;
    sz = b.size();
    U len = U(sz);
    growUndef(len);
    std::uninitialized_copy_n((T *)(b.data()), len, (T *)(ptr));
    return *this;
  }
  template <typename D, std::unsigned_integral I>
  constexpr auto operator=(Buffer<T, N, D, A, I> &&b) noexcept -> Buffer & {
    if (this == &b) return *this;
    // here, we commandeer `b`'s memory
    sz = b.size();
    allocator = std::move(b.allocator);
    if (b.isSmall()) {
      // if `b` is small, we need to copy memory
      // no need to shrink our capacity
      std::uninitialized_copy_n((T *)(b.data()), size_t(sz), (T *)(ptr));
    } else {
      // otherwise, we take its pointer
      maybeDeallocate(b.data(), b.getCapacity());
    }
    b.resetNoFree();
    return *this;
  }
  [[nodiscard]] constexpr auto data() noexcept -> NotNull<T> { return ptr; }
  [[nodiscard]] constexpr auto data() const noexcept -> NotNull<const T> {
    return ptr;
  }

  template <typename... Args> constexpr auto emplace_back(Args &&...args) {
    static_assert(std::is_integral_v<S>, "emplace_back requires integral size");
    if (sz == capacity) [[unlikely]]
      reserve(capacity + capacity);
    // ptr[sz++] = T(std::forward<Args>(args)...);
    new (ptr + sz++) T(std::forward<Args>(args)...);
  }
  constexpr void push_back(T value) {
    static_assert(std::is_integral_v<S>, "push_back requires integral size");
    if (sz == capacity) [[unlikely]]
      reserve(capacity + capacity);
    // ptr[sz++] = value;
    new (ptr + sz++) T(std::move(value));
  }
  constexpr void pop_back() {
    static_assert(std::is_integral_v<S>, "pop_back requires integral size");
    assert(sz > 0 && "pop_back on empty buffer");
    if constexpr (std::is_trivially_destructible_v<T>) --sz;
    else ptr[--sz].~T();
  }
  // behavior
  // if S is StridedDims, then we copy data.
  // If the new dims are larger in rows or cols, we fill with 0.
  // If the new dims are smaller in rows or cols, we truncate.
  // New memory outside of dims (i.e., stride larger), we leave uninitialized.
  //
  constexpr void resize(S nz) {
    S oz = sz;
    sz = nz;
    if constexpr (std::integral<S>) {
      if (nz <= capacity) return;
      U newCapacity = U(nz);
      T *newPtr = allocator.allocate(newCapacity);
      if (oz) std::uninitialized_copy_n((T *)(ptr), oz, newPtr);
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
      bool newAlloc = U(len) > capacity;
      T *npt = newAlloc ? allocator.allocate(len) : (T *)(ptr);
      // we can copy forward so long as the new stride is smaller
      // so that the start of the dst range is outside of the src range
      // we can also safely forward copy if we allocated a new ptr
      bool forwardCopy = (newX <= oldX) || newAlloc;
      unsigned colsToCopy = std::min(oldN, newN);
      // we only need to copy if memory shifts position
      bool copyCols = newAlloc || ((colsToCopy > 0) && (newX != oldX));
      unsigned rowsToCopy = std::min(oldM, newM);
      unsigned fillCount = newN - colsToCopy;
      if (copyCols || fillCount) {
        if (forwardCopy) {
          // truncation, we need to copy rows to increase stride
          for (size_t m = 1; m < rowsToCopy; ++m) {
            T *src = ptr + m * oldX;
            T *dst = npt + m * newX;
            if (copyCols) std::copy(src, src + colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
          }
        } else /* [[unlikely]] */ {
          // backwards copy, only needed when we increasing stride but not
          // reallocating, which should be comparatively uncommon.
          // Should probably benchmark or determine actual frequency
          // before adding `[[unlikely]]`.
          for (size_t m = rowsToCopy; --m != 0;) {
            T *src = ptr + m * oldX;
            T *dst = npt + m * newX;
            if (colsToCopy) std::copy_backward(src, src + colsToCopy, dst);
            if (fillCount) std::fill_n(dst + colsToCopy, fillCount, T{});
          }
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
      S nz = sz;
      return resize(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(S M) {
    U L = U(M);
    if (L > U(sz)) growUndef(L);
    sz = M;
  }
  constexpr void resizeForOverwrite(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(r));
    } else if constexpr (LinearAlgebra::MatrixDimension<S>) {
      S nz = sz;
      return resizeForOverwrite(nz.set(r));
    }
  }
  constexpr void resizeForOverwrite(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return resizeForOverwrite(S(c));
    } else if constexpr (LinearAlgebra::MatrixDimension<S>) {
      S nz = sz;
      return resizeForOverwrite(nz.set(c));
    }
  }
  constexpr void erase(S i) {
    static_assert(std::integral<S>, "erase requires integral size");
    S oldLen = sz--;
    if (i < sz) std::copy((T *)(ptr) + i + 1, ptr + oldLen, (T *)(ptr) + i);
  }
  constexpr void erase(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return erase(S(r));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {

      auto stride = unsigned{LinearAlgebra::RowStride{sz}},
           col = unsigned{LinearAlgebra::Col{sz}},
           newRow = unsigned{LinearAlgebra::Row{sz}} - 1;
      sz.set(LinearAlgebra::Row{newRow});
      if ((col == 0) || (r == newRow)) return;
      invariant(col <= stride);
      if ((col + (512 / (sizeof(T)))) <= stride) {
        T *dst = ptr + r * stride;
        for (size_t m = *r; m < newRow; ++m) {
          T *src = dst + stride;
          std::copy_n(src, col, dst);
          dst = src;
        }
      } else {
        T *dst = ptr + r * stride;
        std::copy_n(dst + stride, (newRow - unsigned(r)) * stride, dst);
      }
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if erasing a row, matrix must be strided or dense.");
      auto col = unsigned{LinearAlgebra::Col{sz}},
           newRow = unsigned{LinearAlgebra::Row{sz}} - 1;
      sz.set(LinearAlgebra::Row{newRow});
      if ((col == 0) || (r == newRow)) return;
      T *dst = ptr + r * col;
      std::copy_n(dst + col, (newRow - unsigned(r)) * col, dst);
    }
  }
  constexpr void erase(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return erase(S(c));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      auto stride = unsigned{LinearAlgebra::RowStride{sz}},
           newCol = unsigned{LinearAlgebra::Col{sz}} - 1,
           row = unsigned{LinearAlgebra::Row{sz}};
      sz.set(LinearAlgebra::Col{newCol});
      unsigned colsToCopy = newCol - unsigned(c);
      if ((colsToCopy == 0) || (row == 0)) return;
      // we only need to copy if memory shifts position
      for (size_t m = 0; m < row; ++m) {
        T *dst = ptr + m * stride + unsigned(c);
        std::copy_n(dst + 1, colsToCopy, dst);
      }
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if erasing a col, matrix must be strided or dense.");
      auto newCol = unsigned{LinearAlgebra::Col{sz}}, oldCol = newCol--,
           row = unsigned{LinearAlgebra::Row{sz}};
      sz.set(LinearAlgebra::Col{newCol});
      unsigned colsToCopy = newCol - unsigned(c);
      if ((colsToCopy == 0) || (row == 0)) return;
      // we only need to copy if memory shifts position
      for (size_t m = 0; m < row; ++m) {
        T *dst = ptr + m * newCol + unsigned(c);
        T *src = ptr + m * oldCol + unsigned(c) + 1;
        std::copy_n(src, colsToCopy, dst);
      }
    }
  }
  constexpr void truncate(S newLen) {
    invariant(U(newLen) <= capacity);
    sz = newLen;
  }
  constexpr void truncate(LinearAlgebra::Row r) {
    if constexpr (std::integral<S>) {
      return truncate(S(r));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      invariant(r < LinearAlgebra::Row{sz});
      sz.set(r);
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if truncating a row, matrix must be strided or dense.");
      invariant(r < LinearAlgebra::Row{sz});
      LinearAlgebra::DenseDims newSz = sz;
      resize(newSz.set(r));
    }
  }
  constexpr void truncate(LinearAlgebra::Col c) {
    if constexpr (std::integral<S>) {
      return truncate(S(c));
    } else if constexpr (std::is_same_v<S, LinearAlgebra::StridedDims>) {
      invariant(c < LinearAlgebra::Col{sz});
      sz.set(c);
    } else { // if constexpr (std::is_same_v<S, LinearAlgebra::DenseDims>) {
      static_assert(std::is_same_v<S, LinearAlgebra::DenseDims>,
                    "if truncating a col, matrix must be strided or dense.");
      invariant(c < LinearAlgebra::Col{sz});
      LinearAlgebra::DenseDims newSz = sz;
      resize(newSz.set(c));
    }
  }

  constexpr ~Buffer() { maybeDeallocate(); }
  [[nodiscard]] constexpr auto size() const noexcept -> S { return sz; }
  // does not free memory, leaving capacity unchanged
  constexpr void clear() { sz = S{}; }
  constexpr auto operator[](size_t i) noexcept -> T & {
    invariant(i < size_t(size()));
    return ptr[i];
  }
  constexpr auto operator[](size_t i) const noexcept -> const T & {
    invariant(i < size_t(size()));
    return ptr[i];
  }
  constexpr void fill(T value) {
    std::fill_n((T *)(ptr), size_t(size()), value);
  }
  constexpr void reserve(S nz) {
    U newCapacity = U(nz);
    if (newCapacity <= capacity) return;
    // allocate new, copy, deallocate old
    T *newPtr = allocator.allocate(newCapacity);
    if (U oldLen = U(sz)) std::uninitialized_copy_n((T *)(ptr), oldLen, newPtr);
    maybeDeallocate();
    ptr = newPtr;
    capacity = newCapacity;
  }
  [[nodiscard]] constexpr auto get_allocator() const noexcept -> A {
    return allocator;
  }
  [[nodiscard]] constexpr auto getCapacity() const -> U { return capacity; }
  [[nodiscard]] constexpr auto isSmall() const -> bool { return ptr == memory; }
  constexpr void resetNoFree() {
    ptr = memory;
    sz = S{};
    capacity = N;
  }

private:
  [[no_unique_address]] NotNull<T> ptr;
  [[no_unique_address]] U capacity{N};
  [[no_unique_address]] S sz{};
  [[no_unique_address]] A allocator{};
  T memory[N]; // NOLINT (modernize-avoid-c-style-arrays)

  // this method should only be called from the destructor
  // (and the implementation taking the new ptr and capacity)
  constexpr void maybeDeallocate() {
    if (!isSmall()) allocator.deallocate(ptr, capacity);
  }
  // this method should be called whenever the buffer lives
  constexpr void maybeDeallocate(NotNull<T> newPtr, U newCapacity) {
    maybeDeallocate();
    ptr = newPtr;
    capacity = newCapacity;
  }
  // grow, discarding old data
  constexpr void growUndef(U M) {
    if (M <= capacity) return;
    maybeDeallocate();
    // because this doesn't care about the old data,
    // we can allocate after freeing, which may be faster
    ptr = allocator.allocate(M);
    capacity = M;
  }
};

static_assert(std::move_constructible<Buffer<intptr_t, 14, unsigned>>);
static_assert(std::copyable<Buffer<intptr_t, 14, unsigned>>);
// Check that `[[no_unique_address]]` is working.
// sizes should be:
// [ptr, capacity, dims, allocator, array]
// 8 + 4 + 3*4 + 0 + 64*8 = 24 + 512 = 536
static_assert(sizeof(Buffer<int64_t, 64, LinearAlgebra::StridedDims,
                            std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, capacity, dims, allocator, array]
// 8 + 8 + 2*4 + 0 + 64*8 = 24 + 512 = 536
static_assert(sizeof(Buffer<int64_t, 64, LinearAlgebra::DenseDims,
                            std::allocator<int64_t>>) == 536);
// sizes should be:
// [ptr, capacity, dims, allocator, array]
// 8 + 4 + 1*4 + 0 + 64*8 = 16 + 512 = 528
static_assert(sizeof(Buffer<int64_t, 64, LinearAlgebra::SquareDims,
                            std::allocator<int64_t>>) == 528);
