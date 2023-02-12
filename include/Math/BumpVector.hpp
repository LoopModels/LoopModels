#pragma once
#include "Math/Indexing.hpp"
#include "Utilities/Allocators.hpp"
#include <cstdint>
#include <llvm/Support/Alignment.h>

namespace LinearAlgebra {
// BumpPtrVector
// Has reference semantics.
template <typename T> struct BumpPtrVector {
  static_assert(!std::is_const_v<T>, "T shouldn't be const");
  static_assert(std::is_trivially_destructible_v<T>);
  using eltype = T;
  using value_type = T;
  using reference = T &;
  using const_reference = const T &;
  using size_type = unsigned;
  using difference_type = int;
  using iterator = T *;
  using const_iterator = const T *;
  using pointer = T *;
  using const_pointer = const T *;
  using allocator_type = WBumpAlloc<T>;

  // using eltype = std::remove_const_t<T>;
  [[no_unique_address]] T *mem;
  [[no_unique_address]] unsigned Size;
  [[no_unique_address]] unsigned Capacity;
  [[no_unique_address]] NotNull<BumpAlloc<>> Alloc;

  BumpPtrVector(BumpAlloc<> &a)
    : mem(nullptr), Size(0), Capacity(0), Alloc(a) {}
  BumpPtrVector(WBumpAlloc<T> a)
    : mem(nullptr), Size(0), Capacity(0), Alloc(a.get_allocator()) {}

  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) -> T & {
    invariant(unsigned(i) < Size);
    return mem[canonicalize(i, Size)];
  }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
    invariant(unsigned(i) < Size);
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
  void reserveForOverwrite(size_t N) {
    if (N <= Capacity) return;
    mem = Alloc->reallocate<true>(mem, Capacity, N);
    Capacity = N;
  }
  void reserve(size_t N) {
    if (N <= Capacity) return;
    mem = Alloc->reallocate<false>(mem, Capacity, N);
    Capacity = N;
  }
  void truncate(size_t N) {
    assert(N <= Capacity);
    Size = N;
  }
  void resize(size_t N) {
    reserve(N);
    Size = N;
  }
  void resizeForOverwrite(size_t N) {
    reserveForOverwrite(N);
    Size = N;
  }
  void extendOrAssertSize(size_t N) {
    if (N != Size) resizeForOverwrite(N);
  }
  [[nodiscard]] constexpr auto get_allocator() -> WBumpAlloc<T> {
    return Alloc;
  }
  template <typename... Args>
  constexpr auto emplace_back(Args &&...args) -> T & {
    size_t offset = Size++;
    if (Size > Capacity) [[unlikely]]
      reserve(Size + Size);
    T *p = mem + offset;
    ::new ((void *)p) T(std::forward<Args>(args)...);
    return *p;
  }
  [[nodiscard]] constexpr auto empty() const -> bool { return Size == 0; }
  constexpr void pop_back() { --Size; }
};
static_assert(std::is_trivially_destructible_v<MutPtrVector<int64_t>>);
static_assert(std::is_trivially_destructible_v<BumpPtrVector<int64_t>>);
} // namespace LinearAlgebra
