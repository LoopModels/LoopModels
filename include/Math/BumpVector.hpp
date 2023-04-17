#pragma once
#include "Math/Array.hpp"
#include "Math/Indexing.hpp"
#include "Utilities/Allocators.hpp"
#include <cstdint>
#include <llvm/Support/Alignment.h>

namespace LinAlg {
// BumpPtrVector
// Has reference semantics.
template <typename T, unsigned InitialCapacity = 8> struct BumpPtrVector {
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

  constexpr BumpPtrVector(WBumpAlloc<T> a)
    : mem(a.allocate(InitialCapacity)), Size(0), Capacity(InitialCapacity),
      Alloc(a.get_allocator()) {}
  constexpr BumpPtrVector(BumpAlloc<> &a) : BumpPtrVector(WBumpAlloc<T>(a)) {}
  constexpr BumpPtrVector(const BumpPtrVector<T> &x, WBumpAlloc<T> alloc)
    : mem(alloc.allocate(x.size())), Size(x.size()), Capacity(x.size()),
      Alloc(alloc.get_allocator()) {
    *this << x;
  }
  constexpr BumpPtrVector(const BumpPtrVector<T> &x)
    : BumpPtrVector(x, x.get_allocator()) {}
  constexpr BumpPtrVector(BumpPtrVector &&x, WBumpAlloc<T> alloc)
    : Alloc{alloc.get_allocator()} {
    mem = x.mem;
    Size = x.Size;
    Capacity = x.Capacity;
    x.mem = nullptr;
    x.Size = 0;
    x.Capacity = 0;
  }
  BumpPtrVector &operator=(const BumpPtrVector &x) {
    if (this != &x) {
      clear();
      resizeForOverwrite(x.Size);
      *this << x;
    }
    return *this;
  }
  BumpPtrVector &operator=(BumpPtrVector &&x) {
    if (this != &x) {
      mem = x.mem;
      Size = x.Size;
      Capacity = x.Capacity;
      Alloc = x.Alloc;
      x.mem = nullptr;
      x.Size = 0;
      x.Capacity = 0;
    }
    return *this;
  }

  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) -> T & {
    invariant(unsigned(i) < Size);
    return mem[canonicalize(i, Size)];
  }
  [[gnu::flatten]] constexpr auto operator[](const ScalarIndex auto i) const
    -> const T & {
    invariant(unsigned(i) < Size);
    return mem[canonicalize(i, Size)];
  }
  [[nodiscard]] constexpr auto front() -> T & {
    assert(Size > 0);
    return mem[0];
  }
  [[nodiscard]] constexpr auto back() -> T & {
    assert(Size > 0);
    return mem[Size - 1];
  }
  [[nodiscard]] constexpr auto front() const -> const T & {
    assert(Size > 0);
    return mem[0];
  }
  [[nodiscard]] constexpr auto back() const -> const T & {
    assert(Size > 0);
    return mem[Size - 1];
  }
  [[nodiscard]] constexpr auto isEmpty() const -> bool { return Size == 0; }
  constexpr void clear() {
    Size = 0;
    if constexpr (InitialCapacity == 0) {
      Capacity = 0;
      Alloc->deallocate(mem);
    }
  }
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
  constexpr auto operator==(PtrVector<T> x) const -> bool {
    return PtrVector<T>(*this) == x;
  }
  constexpr auto operator==(const llvm::ArrayRef<T> x) const -> bool {
    return std::equal(begin(), end(), x.begin(), x.end());
  }
  [[nodiscard]] constexpr auto view() const -> PtrVector<T> { return *this; };
  [[gnu::flatten]] constexpr auto operator<<(PtrVector<T> x)
    -> MutPtrVector<T> {
    return MutPtrVector<T>{*this} << x;
  }
  [[gnu::flatten]] constexpr auto operator<<(MutPtrVector<T> x)
    -> MutPtrVector<T> {
    return MutPtrVector<T>{*this} << x;
  }
  [[gnu::flatten]] constexpr auto operator<<(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    return MutPtrVector<T>{*this} << x;
  }
  [[gnu::flatten]] constexpr auto operator<<(std::integral auto x)
    -> MutPtrVector<T> {
    for (auto &&y : *this) y = x;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    invariant(Size, x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] += x[i];
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    invariant(Size, x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] -= x[i];
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator*=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    invariant(Size, x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] *= x[i];
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator/=(const AbstractVector auto &x)
    -> MutPtrVector<T> {
    invariant(Size, x.size());
    for (size_t i = 0; i < Size; ++i) mem[i] /= x[i];
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] += x;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] -= x;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator*=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] *= x;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator/=(const std::integral auto x)
    -> MutPtrVector<T> {
    for (size_t i = 0; i < Size; ++i) mem[i] /= x;
    return *this;
  }
  constexpr void reserveForOverwrite(size_t N) {
    if (N <= Capacity) return;
    mem = Alloc->reallocate<true>(mem, Capacity, N);
    Capacity = N;
  }
  constexpr void reserve(size_t N) {
    if (N <= Capacity) return;
    mem = Alloc->reallocate<false>(mem, Capacity, N);
    Capacity = N;
  }
  constexpr void truncate(size_t N) {
    assert(N <= Capacity);
    Size = N;
  }
  constexpr void resize(size_t N) {
    reserve(N);
    Size = N;
  }
  constexpr void resize(size_t N, T x) {
    reserve(N);
    unsigned oldSz = Size;
    Size = N;
    for (unsigned i = oldSz; i < N; ++i) mem[i] = x;
  }
  constexpr void resizeForOverwrite(size_t N) {
    reserveForOverwrite(N);
    Size = N;
  }
  constexpr void extendOrAssertSize(size_t N) {
    if (N != Size) resizeForOverwrite(N);
  }
  [[nodiscard]] constexpr auto get_allocator() const -> WBumpAlloc<T> {
    return Alloc;
  }
  constexpr auto push_back(T x) -> T & {
    size_t offset = Size++;
    if (Size > Capacity) [[unlikely]] {
      if constexpr (InitialCapacity == 0) reserve(Size + Size);
      else reserve(offset + offset);
    }
    return *std::construct_at(mem + offset, std::move(x));
  }
  template <typename... Args>
  constexpr auto emplace_back(Args &&...args) -> T & {
    size_t offset = Size++;
    if (Size > Capacity) [[unlikely]] {
      if constexpr (InitialCapacity == 0) reserve(Size + Size);
      else reserve(offset + offset);
    }
    return *std::construct_at(mem + offset, std::forward<Args>(args)...);
  }
  [[nodiscard]] constexpr auto empty() const -> bool { return Size == 0; }
  constexpr void pop_back() { --Size; }
  constexpr void erase(T *x) {
    assert(x >= mem && x < mem + Size);
    std::destroy_at(x);
    std::copy_n(x + 1, Size, x);
    --Size;
  }
};
static_assert(std::is_trivially_destructible_v<MutPtrVector<int64_t>>);
static_assert(std::is_trivially_destructible_v<BumpPtrVector<int64_t>>);
} // namespace LinAlg
