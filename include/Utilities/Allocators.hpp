#pragma once

#define BUMP_ALLOC_LLVM_USE_ALLOCATOR
/// The advantages over llvm's bumpallocator are:
/// 1. Support realloc
/// 2. Support support checkpointing
#include "Math/Array.hpp"
#include "Math/Utilities.hpp"
#include "Utilities/Iterators.hpp"
#include "Utilities/Valid.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/MathExtras.h>
#include <llvm/Support/MemAlloc.h>
#include <memory>
#include <type_traits>
#ifndef BUMP_ALLOC_LLVM_USE_ALLOCATOR
/// We can't use `std::allocator` because it doesn't support passing alignment
#include <cstdlib>
#endif

template <size_t SlabSize = 16384, bool BumpUp = false,
          size_t MinAlignment = alignof(std::max_align_t)>
struct BumpAlloc {
  static_assert(llvm::isPowerOf2_64(MinAlignment));

public:
  static constexpr bool BumpDown = !BumpUp;
  using value_type = std::byte;
  [[gnu::returns_nonnull]] constexpr auto allocate(size_t Size, size_t Align)
    -> void * {
    if (Size > SlabSize / 2) {
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
      // void *p = llvm::allocate_buffer(Size, Align);
      void *p = llvm::allocate_buffer(Size, MinAlignment);
      customSlabs.emplace_back(p, Size);
#else
      void *p = std::aligned_alloc(Align, Size);
      customSlabs.emplace_back(p);
#endif
      return p;
    }
    auto p = (Align > MinAlignment) ? bumpAlloc(Size, Align) : bumpAlloc(Size);
    __asan_unpoison_memory_region(p, Size);
    __msan_allocated_memory(p, Size);
    return p;
  }
  template <typename T>
  [[gnu::returns_nonnull, gnu::flatten]] constexpr auto allocate(size_t N = 1)
    -> T * {
    static_assert(std::is_trivially_destructible_v<T>,
                  "BumpAlloc only supports trivially destructible types.");
    return reinterpret_cast<T *>(allocate(N * sizeof(T), alignof(T)));
  }
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
  static constexpr auto contains(std::pair<void *, size_t> P, void *p) -> bool {
    return P.first == p;
  }
  // static deallocateCustomSlab(std::pair<void *, size_t> slab) {
  //   llvm::deallocate_buffer(slab.first, slab.second, MinAlignment);
  // }
#else
  static constexpr auto contains(void *P, void *p) -> bool {
    return P.first == p;
  }
  // static deallocateCustomSlab(void * slab) {
  //   std::free(slab);
  // }
#endif
  constexpr void deallocate(std::byte *Ptr, size_t Size) {
    (void)Ptr;
    (void)Size;
    __asan_poison_memory_region(Ptr, Size);
    if constexpr (BumpUp) {
      if (Ptr + align(Size) == SlabCur) SlabCur = Ptr;
    } else if (Ptr == SlabCur) {
      SlabCur += align(Size);
      return;
    }
#ifdef BUMP_ALLOC_TRY_FREE
    if (size_t numCSlabs = customSlabs.size()) {
      for (size_t i = 0; i < std::min<size_t>(8, customSlabs.size()); ++i) {
        if (contains(customSlabs[customSlabs.size() - 1 - i], Ptr)) {
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
          llvm::deallocate_buffer(Ptr, Size, MinAlignment);
#else
          std::free(Ptr);
#endif
          if (i)
            std::swap(customSlabs[customSlabs.size() - 1],
                      customSlabs[customSlabs.size() - 1 - i]);
          customSlabs.pop_back();
          return;
        }
      }
    }
#endif
  }
  template <typename T> constexpr void deallocate(T *Ptr, size_t N = 1) {
    deallocate(reinterpret_cast<std::byte *>(Ptr), N * sizeof(T));
  }
  constexpr auto tryReallocate(std::byte *Ptr, size_t OldSize, size_t NewSize,
                               size_t Align) -> std::byte * {
    Align = Align > MinAlignment ? toPowerOf2(Align) : MinAlignment;
    if constexpr (BumpUp) {
      if (Ptr == SlabCur - align(OldSize)) {
        SlabCur = Ptr + align(NewSize);
        if (!outOfSlab(SlabCur, SlabEnd)) {
          __asan_unpoison_memory_region(Ptr + OldSize, NewSize - OldSize);
          __msan_allocated_memory(Ptr + OldSize, NewSize - OldSize);
          return Ptr;
        }
      }
    } else if (Ptr == SlabCur) {
      size_t extraSize = align(NewSize - OldSize, Align);
      SlabCur -= extraSize;
      if (!outOfSlab(SlabCur, SlabEnd)) {
        __asan_unpoison_memory_region(SlabCur, extraSize);
        __msan_allocated_memory(SlabCur, extraSize);
        return SlabCur;
      }
    }
    return nullptr;
  }
  template <typename T>
  constexpr auto tryReallocate(T *Ptr, size_t OldSize, size_t NewSize) -> T * {
    return reinterpret_cast<T *>(
      tryReallocate(reinterpret_cast<std::byte *>(Ptr), OldSize * sizeof(T),
                    NewSize * sizeof(T), alignof(T)));
  }
  /// reallocate<ForOverwrite>(void *Ptr, size_t OldSize, size_t NewSize,
  /// size_t Align) Should be safe with OldSize == 0, as it checks before
  /// copying
  template <bool ForOverwrite = false>
  [[gnu::returns_nonnull, nodiscard]] constexpr auto
  reallocate(std::byte *Ptr, size_t OldSize, size_t NewSize, size_t Align)
    -> void * {
    if (OldSize >= NewSize) return Ptr;
    if (auto *p = tryReallocate(Ptr, OldSize, NewSize, Align)) {
      if constexpr ((BumpDown) & (!ForOverwrite))
        std::copy(Ptr, Ptr + OldSize, p);
      return p;
    }
    if (OldSize >= NewSize) return Ptr;
    Align = Align > MinAlignment ? toPowerOf2(Align) : MinAlignment;
    if constexpr (BumpUp) {
      if (Ptr == SlabCur - align(OldSize)) {
        SlabCur = Ptr + align(NewSize);
        if (!outOfSlab(SlabCur, SlabEnd)) {
          __asan_unpoison_memory_region(Ptr + OldSize, NewSize - OldSize);
          __msan_allocated_memory(Ptr + OldSize, NewSize - OldSize);
          return Ptr;
        }
      }
    } else if (Ptr == SlabCur) {
      size_t extraSize = align(NewSize - OldSize, Align);
      SlabCur -= extraSize;
      if (!outOfSlab(SlabCur, SlabEnd)) {
        auto *old = reinterpret_cast<std::byte *>(Ptr);
        __asan_unpoison_memory_region(SlabCur, extraSize);
        __msan_allocated_memory(SlabCur, extraSize);
        if constexpr (!ForOverwrite) std::copy(old, old + OldSize, SlabCur);
        return SlabCur;
      }
    }
    // we need to allocate new memory
    auto NewPtr = allocate(NewSize, Align);
    if constexpr (!ForOverwrite)
      std::copy(Ptr, Ptr + OldSize, reinterpret_cast<std::byte *>(NewPtr));
    deallocate(Ptr, OldSize);
    return NewPtr;
  }
  constexpr void reset() {
    resetSlabs();
    resetCustomSlabs();
  }
  template <bool ForOverwrite = false, typename T>
  [[gnu::returns_nonnull, gnu::flatten, nodiscard]] constexpr auto
  reallocate(T *Ptr, size_t OldSize, size_t NewSize) -> T * {
    return reinterpret_cast<T *>(reallocate<ForOverwrite>(
      reinterpret_cast<std::byte *>(Ptr), OldSize * sizeof(T),
      NewSize * sizeof(T), alignof(T)));
  }
  constexpr BumpAlloc() : slabs({}), customSlabs({}) { newSlab(); }
  constexpr BumpAlloc(BumpAlloc &&alloc) noexcept {
    slabs = std::move(alloc.slabs);
    customSlabs = std::move(alloc.customSlabs);
    SlabCur = alloc.SlabCur;
    SlabEnd = alloc.SlabEnd;
  }
  constexpr ~BumpAlloc() {
    for (auto Slab : slabs)
      llvm::deallocate_buffer(Slab, SlabSize, MinAlignment);
    resetCustomSlabs();
  }
  template <typename T, typename... Args>
  constexpr auto construct(Args &&...args) -> T * {
    auto *p = allocate(sizeof(T), alignof(T));
    return new (p) T(std::forward<Args>(args)...);
  }
  constexpr auto isPointInSlab(std::byte *p) -> bool {
    if constexpr (BumpUp) return ((p + SlabSize) >= SlabEnd) && (p < SlabEnd);
    else return (p > SlabEnd) && (p <= (SlabEnd + SlabSize));
  }
  struct CheckPoint {
    constexpr CheckPoint(std::byte *b) : p(b) {}
    constexpr auto isInSlab(std::byte *send) -> bool {
      if constexpr (BumpUp) return ((p + SlabSize) >= send) && (p < send);
      else return (p > send) && (p <= (send + SlabSize));
    }
    std::byte *p;
  };
  [[nodiscard]] constexpr auto checkPoint() -> CheckPoint { return SlabCur; }
  constexpr void rollBack(CheckPoint p) {
    if (p.isInSlab(SlabEnd)) {
#if LLVM_ADDRESS_SANITIZER_BUILD
      if constexpr (BumpUp) __asan_poison_memory_region(p.p, SlabCur - p.p);
      else __asan_poison_memory_region(SlabCur, p.p - SlabCur);
#endif
      SlabCur = p.p;
    } else initSlab(slabs.back());
  }

private:
  static constexpr auto align(size_t x) -> size_t {
    return (x + MinAlignment - 1) & ~(MinAlignment - 1);
  }
  static constexpr auto align(size_t x, size_t alignment) -> size_t {
    return (x + alignment - 1) & ~(alignment - 1);
  }
  static constexpr auto align(std::byte *p) -> std::byte * {
    uint64_t i = reinterpret_cast<uintptr_t>(p), j = i;
    if constexpr (BumpUp) i += MinAlignment - 1;
    i &= ~(MinAlignment - 1);
    return p + (i - j);
  }
  static constexpr auto align(std::byte *p, size_t alignment) -> std::byte * {
    assert(llvm::isPowerOf2_64(alignment));
    uintptr_t i = reinterpret_cast<uintptr_t>(p), j = i;
    if constexpr (BumpUp) i += alignment - 1;
    i &= ~(alignment - 1);
    return p + (i - j);
  }
  static constexpr auto bump(std::byte *ptr, size_t N) -> std::byte * {
    if constexpr (BumpUp) return ptr + N;
    else return ptr - N;
  }
  static constexpr auto outOfSlab(std::byte *cur, std::byte *lst) -> bool {
    if constexpr (BumpUp) return cur >= lst;
    else return cur < lst;
  }
  constexpr auto maybeNewSlab() -> bool {
    if (!outOfSlab(SlabCur, SlabEnd)) return false;
    newSlab();
    return true;
  }
  constexpr void initSlab(std::byte *p) {
    __asan_poison_memory_region(p, SlabSize);
    if constexpr (BumpUp) {
      SlabCur = p;
      SlabEnd = p + SlabSize;
    } else {
      SlabCur = p + SlabSize;
      SlabEnd = p;
    }
  }
  constexpr void newSlab() {
    auto *p = reinterpret_cast<std::byte *>(
      llvm::allocate_buffer(SlabSize, MinAlignment));
    slabs.push_back(p);
    initSlab(p);
  }
  // updates SlabCur and returns the allocated pointer
  [[gnu::returns_nonnull]] constexpr auto allocCore(size_t Size, size_t Align)
    -> std::byte * {
#if LLVM_ADDRESS_SANITIZER_BUILD
    SlabCur = bump(SlabCur, Align); // poisoned zone
#endif
    if constexpr (BumpUp) {
      SlabCur = align(SlabCur, Align);
      std::byte *old = SlabCur;
      SlabCur += align(Size);
      return old;
    } else {
      SlabCur = align(SlabCur - Size, Align);
      return SlabCur;
    }
  }
  // updates SlabCur and returns the allocated pointer
  [[gnu::returns_nonnull]] constexpr auto allocCore(size_t Size)
    -> std::byte * {
    // we know we already have MinAlignment
    // and we need to preserve it.
    // Thus, we align `Size` and offset it.
    invariant((reinterpret_cast<size_t>(SlabCur) % MinAlignment) == 0);
#if LLVM_ADDRESS_SANITIZER_BUILD
    SlabCur = bump(SlabCur, MinAlignment); // poisoned zone
#endif
    if constexpr (BumpUp) {
      std::byte *old = SlabCur;
      SlabCur += align(Size);
      return old;
    } else {
      SlabCur -= align(Size);
      return SlabCur;
    }
  }
  //
  [[gnu::returns_nonnull]] constexpr auto bumpAlloc(size_t Size, size_t Align)
    -> void * {
    Align = toPowerOf2(Align);
    std::byte *ret = allocCore(Size, Align);
    if (maybeNewSlab()) ret = allocCore(Size, Align);
    return reinterpret_cast<void *>(ret);
  }
  [[gnu::returns_nonnull]] constexpr auto bumpAlloc(size_t Size) -> void * {
    std::byte *ret = allocCore(Size);
    if (maybeNewSlab()) ret = allocCore(Size);
    return reinterpret_cast<void *>(ret);
  }

  constexpr void resetSlabs() {
    size_t nSlabs = slabs.size();
    if (nSlabs == 0) return;
    if (nSlabs > 1) {
      for (size_t i = 1; i < nSlabs; ++i)
        llvm::deallocate_buffer(slabs[i], SlabSize, MinAlignment);
      // for (auto Slab : skipFirst(slabs))
      // llvm::deallocate_buffer(Slab, SlabSize, MinAlignment);
      slabs.truncate(1);
    }
    initSlab(slabs.front());
  }
  constexpr void resetCustomSlabs() {
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
    for (auto [Ptr, Size] : customSlabs)
      llvm::deallocate_buffer(Ptr, Size, MinAlignment);
#else
    for (auto Ptr : customSlabs) std::free(Ptr);
#endif
    customSlabs.clear();
  }

  std::byte *SlabCur{nullptr};    // 8 bytes
  std::byte *SlabEnd{nullptr};    // 8 bytes
  Vector<std::byte *, 2> slabs{}; // 16 + 16 bytes
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
  Vector<std::pair<void *, size_t>, 0> customSlabs{}; // 16 bytes
#else
  Vector<void *, 0> customSlabs{}; // 16 bytes
#endif
};
static_assert(sizeof(BumpAlloc<>) == 64);
static_assert(!std::is_trivially_copyable_v<BumpAlloc<>>);
static_assert(!std::is_trivially_destructible_v<BumpAlloc<>>);
static_assert(
  std::same_as<std::allocator_traits<BumpAlloc<>>::size_type, size_t>);

// Alloc wrapper people can pass and store by value
// with a specific value type, so that it can act more like a
// `std::allocator`.
template <typename T, size_t SlabSize = 16384, bool BumpUp = false,
          size_t MinAlignment = alignof(std::max_align_t)>
class WBumpAlloc {
  using Alloc = BumpAlloc<SlabSize, BumpUp, MinAlignment>;
  [[no_unique_address]] NotNull<Alloc> A;

public:
  using value_type = T;
  template <typename U> struct rebind {
    using other = WBumpAlloc<U, SlabSize, BumpUp, MinAlignment>;
  };
  constexpr WBumpAlloc(Alloc &alloc) : A(&alloc) {}
  constexpr WBumpAlloc(NotNull<Alloc> alloc) : A(alloc) {}
  constexpr WBumpAlloc(const WBumpAlloc &other) = default;
  template <typename U>
  constexpr WBumpAlloc(WBumpAlloc<U> other) : A(other.get_allocator()) {}
  [[nodiscard]] constexpr auto get_allocator() -> NotNull<Alloc> { return A; }
  constexpr void deallocate(T *p, size_t n) { A->deallocate(p, n); }
  [[gnu::returns_nonnull]] constexpr auto allocate(size_t n) -> T * {
    return A->template allocate<T>(n);
  }
  constexpr auto checkPoint() -> typename Alloc::CheckPoint {
    return A->rollBack();
  }
  constexpr void rollBack(typename Alloc::CheckPoint p) { A->rollBack(p); }
};
static_assert(std::same_as<
              std::allocator_traits<WBumpAlloc<int64_t *>>::size_type, size_t>);
static_assert(
  std::same_as<std::allocator_traits<WBumpAlloc<int64_t>>::pointer, int64_t *>);

static_assert(std::is_trivially_copyable_v<NotNull<BumpAlloc<>>>);
static_assert(std::is_trivially_copyable_v<WBumpAlloc<int64_t>>);

template <size_t SlabSize, bool BumpUp, size_t MinAlignment>
auto operator new(size_t Size, BumpAlloc<SlabSize, BumpUp, MinAlignment> &Alloc)
  -> void * {
  return Alloc.allocate(Size, std::min<size_t>(llvm::NextPowerOf2(Size),
                                               alignof(std::max_align_t)));
}

template <size_t SlabSize, bool BumpUp, size_t MinAlignment>
void operator delete(void *, BumpAlloc<SlabSize, BumpUp, MinAlignment> &) {}

template <typename A>
concept Allocator =
  requires(A a) {
    typename A::value_type;
    {
      a.allocate(1)
      } -> std::same_as<typename std::allocator_traits<A>::pointer>;
    {
      a.deallocate(std::declval<typename std::allocator_traits<A>::pointer>(),
                   1)
    };
  };
static_assert(Allocator<WBumpAlloc<int64_t>>);
static_assert(Allocator<std::allocator<int64_t>>);

struct NoCheckpoint {};

constexpr auto checkpoint(const auto &) { return NoCheckpoint{}; }
template <class T> constexpr auto checkpoint(WBumpAlloc<T> alloc) {
  return alloc.rollBack();
}
constexpr auto checkpoint(BumpAlloc<> &alloc) { return alloc.checkPoint(); }

constexpr void rollback(const auto &, NoCheckpoint) {}
template <class T> constexpr void rollback(WBumpAlloc<T> alloc, auto p) {
  alloc.rollBack(p);
}
constexpr void rollback(BumpAlloc<> &alloc, auto p) { alloc.rollBack(p); }
