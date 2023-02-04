#pragma once

#include "Utilities/Valid.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T>
concept SizeMultiple8 = sizeof(T)
% 8 == 0;

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
    U len = sz;
    if (len <= N) return;
    ptr = allocator.allocate(len);
    capacity = len;
  }
  constexpr Buffer(S s, T x) noexcept : ptr{memory}, capacity{N} {
    sz = s;
    U len = sz;
    if (len > N) {
      ptr = allocator.allocate(len);
      capacity = len;
    }
    std::fill_n(ptr, len, x);
  }
#pragma GCC diagnostic pop
  template <typename D, std::unsigned_integral I>
  constexpr Buffer(Buffer<T, N, D, A, I> &&b) noexcept
    : ptr{b.ptr}, sz{b.sz}, capacity{b.capacity}, allocator{b.allocator} {
    if (b.isSmall()) {
      ptr = memory;
      std::copy_n(b.data(), N, ptr);
    }
    b.resetNoFree();
  }
  template <typename D, std::unsigned_integral I>
  constexpr Buffer(const Buffer<T, N, D, A, I> &b) noexcept
    : ptr{memory}, sz{b.sz}, capacity{N}, allocator{b.allocator} {
    U len = sz;
    grow(len);
    std::copy_n(b.data(), len, ptr);
  }
  template <typename D, std::unsigned_integral I>
  constexpr auto operator=(const Buffer<T, N, D, A, I> &b) noexcept
    -> Buffer & {
    if (this == &b) return *this;
    sz = b.size();
    U len = sz;
    grow(len);
    std::copy_n(b.data(), len, ptr);
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
      std::copy_n(b.data(), size_t(sz), ptr);
    } else {
      // otherwise, we take its pointer
      maybeDeallocate();
      ptr = b.ptr;
      capacity = b.capacity;
    }
    b.resetNoFree();
    return *this;
  }
  [[nodiscard]] constexpr auto data() noexcept -> NotNull<T> { return ptr; }
  [[nodiscard]] constexpr auto data() const noexcept -> NotNull<const T> {
    return ptr;
  }

  template <typename... Args> constexpr auto emplace_back(Args &&...args) {
    if (sz == capacity) grow(capacity * 2);
    new (ptr + sz++) T(std::forward<Args>(args)...);
  }
  constexpr void push_back(T value) {
    if (sz == capacity) grow(capacity * 2);
    new (ptr + sz++) T(std::move(value));
  }
  constexpr void pop_back() {
    if (sz) ptr[--sz].~T();
  }
  constexpr void resize(S M) {
    U L = M;
    if (L > sz) {
      grow(L);
      for (size_t i = sz; i < L; ++i) new (ptr + i) T();
    } else if constexpr (!std::is_trivially_destructible_v<T>) {
      for (size_t i = L; i < sz; ++i) ptr[i].~T();
    }
    sz = M;
  }
  constexpr void resizeForOverwrite(S M) {
    U L = M;
    if (L > sz) grow(L);
    else if constexpr (!std::is_trivially_destructible_v<T>)
      for (size_t i = L; i < sz; ++i) ptr[i].~T();
    sz = M;
  }
  constexpr ~Buffer() { maybeDeallocate(); }
  [[nodiscard]] constexpr auto size() const noexcept -> S { return sz; }
  // does not free memory, leaving capacity unchanged
  constexpr void clear() { sz = S{}; }

private:
  [[no_unique_address]] NotNull<T> ptr;
  [[no_unique_address]] U capacity{N};
  [[no_unique_address]] S sz{};
  [[no_unique_address]] A allocator{};
  T memory[N]; // NOLINT (modernize-avoid-c-style-arrays)

  constexpr auto isSmall() -> bool { return ptr == memory; }
  constexpr void maybeDeallocate() {
    if (!isSmall()) allocator.deallocate(ptr, capacity);
  }
  constexpr void resetNoFree() {
    ptr = memory;
    sz = S{};
    capacity = N;
  }
  constexpr void grow(U M) {
    if (M <= capacity) return;
    maybeDeallocate();
    ptr = allocator.allocate(M);
    capacity = M;
  }
};

static_assert(std::move_constructible<Buffer<intptr_t, 14, unsigned>>);
static_assert(std::copyable<Buffer<intptr_t, 14, unsigned>>);
