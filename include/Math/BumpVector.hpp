#pragma once
#include "Math/Vector.hpp"
#include <cstdint>
#include <llvm/Support/Allocator.h>

namespace LinearAlgebra {
template <typename T> struct BumpPtrVector {
  static_assert(!std::is_const_v<T>, "T shouldn't be const");
  static_assert(std::is_trivially_destructible_v<T>);
  using eltype = T;
  // using eltype = std::remove_const_t<T>;
  [[no_unique_address]] NotNull<T> mem;
  [[no_unique_address]] unsigned Size;
  [[no_unique_address]] unsigned Capacity;
  [[no_unique_address]] llvm::BumpPtrAllocator &Allocator;
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) -> T & {
#ifndef NDEBUG
    checkIndex(size_t(Size), i);
#endif
    return mem[canonicalize(i, Size)];
  }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
#ifndef NDEBUG
    checkIndex(size_t(Size), i);
#endif
    return mem[canonicalize(i, Size)];
  }
  [[nodiscard]] auto front() -> T & {
    assert(Size > 0);
    return mem[0];
  }
  [[nodiscard]] auto back() -> T & {
    assert(Size > 0);
    return mem[Size - 1];
  }
  [[nodiscard]] auto front() const -> const T & {
    assert(Size > 0);
    return mem[0];
  }
  [[nodiscard]] auto back() const -> const T & {
    assert(Size > 0);
    return mem[Size - 1];
  }
  [[nodiscard]] constexpr auto isEmpty() const -> bool { return Size == 0; }
  // copy constructor
  // constexpr MutPtrVector() = default;
  // constexpr MutPtrVector(const MutPtrVector<T> &x) = default;
  // constexpr MutPtrVector(llvm::MutableArrayRef<T> x)
  //   : mem(x.data()), N(x.size()) {}
  // constexpr MutPtrVector(T *pt, size_t NN) : mem(pt), N(NN) {}
  constexpr auto operator[](Range<size_t, size_t> i) -> MutPtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= Size);
    return MutPtrVector<T>{mem + i.b, i.e - i.b};
  }
  constexpr auto operator[](Range<size_t, size_t> i) const -> PtrVector<T> {
    assert(i.b <= i.e);
    assert(i.e <= Size);
    return PtrVector<T>{mem + i.b, i.e - i.b};
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) -> MutPtrVector<T> {
    return (*this)[canonicalizeRange(i, Size)];
  }
  template <typename F, typename L>
  constexpr auto operator[](Range<F, L> i) const -> PtrVector<T> {
    return (*this)[canonicalizeRange(i, Size)];
  }
  [[gnu::returns_nonnull]] constexpr auto begin() -> T * { return mem; }
  [[gnu::returns_nonnull]] constexpr auto end() -> T * { return mem + Size; }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto begin() const
    -> const T * {
    return mem;
  }
  [[gnu::returns_nonnull]] [[nodiscard]] constexpr auto end() const
    -> const T * {
    return mem + Size;
  }
  [[nodiscard]] constexpr auto size() const -> size_t { return Size; }
  constexpr operator PtrVector<T>() const { return PtrVector<T>{mem, Size}; }
  constexpr operator llvm::ArrayRef<T>() const {
    return llvm::ArrayRef<T>{mem, Size};
  }
  constexpr operator llvm::MutableArrayRef<T>() {
    return llvm::MutableArrayRef<T>{mem, Size};
  }
  // llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
  auto operator==(const MutPtrVector<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  auto operator==(const PtrVector<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
  }
  auto operator==(const llvm::ArrayRef<T> x) const -> bool {
    return llvm::ArrayRef<T>(*this) == x;
  }
  [[nodiscard]] constexpr auto view() const -> PtrVector<T> { return *this; };
  [[gnu::flatten]] auto operator=(PtrVector<T> x) -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(MutPtrVector<T> x) -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    return copyto(*this, x);
  }
  [[gnu::flatten]] auto operator=(std::integral auto x) -> MutPtrVector<T> {
    for (auto &&y : *this) y = x;
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(Size == x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] += x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(Size == x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] -= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(Size == x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] *= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    assert(Size == x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] /= x[i];
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] += x;
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] -= x;
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] *= x;
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] /= x;
    return *this;
  }
#ifndef NDEBUG
  void extendOrAssertSize(size_t M) const { assert(M == Size); }
#else
  static constexpr void extendOrAssertSize(size_t) {}
#endif
};
static_assert(std::is_trivially_destructible_v<MutPtrVector<int64_t>>);
static_assert(std::is_trivially_destructible_v<BumpPtrVector<int64_t>>);
} // namespace LinearAlgebra
