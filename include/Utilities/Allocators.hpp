#pragma once

#include <limits>
#define BUMP_ALLOC_LLVM_USE_ALLOCATOR
/// The advantages over llvm's bumpallocator are:
/// 1. Support realloc
/// 2. Support support checkpointing
#include "Math/Array.hpp"
#include "Math/Utilities.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Iterators.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
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
  using value_type = void;
  [[gnu::returns_nonnull, gnu::alloc_size(2), gnu::alloc_align(3),
    gnu::malloc]] constexpr auto
  allocate(size_t Size, size_t Align) -> void * {
    if (Size > SlabSize / 2) {
#ifdef BUMP_ALLOC_LLVM_USE_ALLOCATOR
      // void *p = llvm::allocate_buffer(Size, Align);
      void *p = llvm::allocate_buffer(Size, MinAlignment);
      customSlabs.emplace_back(p, Size);
#else
      void *p = std::aligned_alloc(Align, Size);
      customSlabs.emplace_back(p);
#endif
#ifndef NDEBUG
      std::fill_n((char *)(p), Size, -1);
#endif
      return p;
    }
    auto p = (Align > MinAlignment) ? bumpAlloc(Size, Align) : bumpAlloc(Size);
    __asan_unpoison_memory_region(p, Size);
    __msan_allocated_memory(p, Size);
#ifndef NDEBUG
    if ((MinAlignment >= alignof(int64_t)) && ((Size & 7) == 0)) {
      std::fill_n(reinterpret_cast<std::int64_t *>(p), Size >> 3,
                  std::numeric_limits<std::int64_t>::min());
    } else std::fill_n((char *)(p), Size, -1);
#endif
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
  constexpr void deallocate(void *Ptr, size_t Size) {
    (void)Ptr;
    (void)Size;
    __asan_poison_memory_region(Ptr, Size);
    if constexpr (BumpUp) {
      if (Ptr + align(Size) == slab) slab = Ptr;
    } else if (Ptr == slab) {
      slab = (char *)slab + align(Size);
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
    deallocate((void *)Ptr, N * sizeof(T));
  }
  constexpr auto tryReallocate(void *Ptr, size_t szOld, size_t szNew,
                               size_t Align) -> void * {
    Align = Align > MinAlignment ? toPowerOf2(Align) : MinAlignment;
    if constexpr (BumpUp) {
      if (Ptr == slab - align(szOld)) {
        slab = Ptr + align(szNew);
        if (!outOfSlab(slab, sEnd)) {
          __asan_unpoison_memory_region((char *)Ptr + szOld, szNew - szOld);
          __msan_allocated_memory((char *)Ptr + OldSize, NewSize - OldSize);
          return Ptr;
        }
      }
    } else if (Ptr == slab) {
      size_t extraSize = align(szNew - szOld, Align);
      slab = (char *)slab - extraSize;
      if (!outOfSlab(slab, sEnd)) {
        __asan_unpoison_memory_region(slab, extraSize);
        __msan_allocated_memory(SlabCur, extraSize);
        return slab;
      }
    }
    return nullptr;
  }
  template <typename T>
  constexpr auto tryReallocate(T *Ptr, size_t OldSize, size_t NewSize) -> T * {
    return reinterpret_cast<T *>(
      tryReallocate(Ptr, OldSize * sizeof(T), NewSize * sizeof(T), alignof(T)));
  }
  /// reallocate<ForOverwrite>(void *Ptr, size_t OldSize, size_t NewSize,
  /// size_t Align) Should be safe with OldSize == 0, as it checks before
  /// copying
  template <bool ForOverwrite = false>
  [[gnu::returns_nonnull, nodiscard]] constexpr auto
  reallocate(void *Ptr, size_t szOld, size_t szNew, size_t Align) -> void * {
    if (szOld >= szNew) return Ptr;
    if (void *p = tryReallocate(Ptr, szOld, szNew, Align)) {
      if constexpr ((BumpDown) & (!ForOverwrite))
        std::copy_n((char *)Ptr, szOld, (char *)p);
      return p;
    }
    if (szOld >= szNew) return Ptr;
    Align = Align > MinAlignment ? toPowerOf2(Align) : MinAlignment;
    if constexpr (BumpUp) {
      if (Ptr == slab - align(szOld)) {
        slab = Ptr + align(szNew);
        if (!outOfSlab(slab, sEnd)) {
          __asan_unpoison_memory_region((char *)Ptr + szOld, szNew - szOld);
          __msan_allocated_memory((char *)Ptr + OldSize, NewSize - OldSize);
          return Ptr;
        }
      }
    } else if (Ptr == slab) {
      size_t extraSize = align(szNew - szOld, Align);
      slab = (char *)slab - extraSize;
      if (!outOfSlab(slab, sEnd)) {
        __asan_unpoison_memory_region(slab, extraSize);
        __msan_allocated_memory(SlabCur, extraSize);
        if constexpr (!ForOverwrite)
          std::copy_n((char *)Ptr, szOld, (char *)slab);
        return slab;
      }
    }
    // we need to allocate new memory
    auto NewPtr = allocate(szNew, Align);
    if constexpr (!ForOverwrite)
      std::copy_n((char *)Ptr, szOld, (char *)NewPtr);
    deallocate(Ptr, szOld);
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
      Ptr, OldSize * sizeof(T), NewSize * sizeof(T), alignof(T)));
  }
  constexpr BumpAlloc() : slabs({}), customSlabs({}) { newSlab(); }
  constexpr BumpAlloc(BumpAlloc &&alloc) noexcept {
    slabs = std::move(alloc.slabs);
    customSlabs = std::move(alloc.customSlabs);
    slab = alloc.slab;
    sEnd = alloc.sEnd;
  }
  constexpr ~BumpAlloc() {
    for (auto *Slab : slabs)
      llvm::deallocate_buffer(Slab, SlabSize, MinAlignment);
    resetCustomSlabs();
  }
  template <typename T, typename... Args>
  constexpr auto construct(Args &&...args) -> T * {
    auto *p = allocate(sizeof(T), alignof(T));
    return new (p) T(std::forward<Args>(args)...);
  }
  constexpr auto isPointInSlab(void *p) -> bool {
    if constexpr (BumpUp) return (((char *)p + SlabSize) >= sEnd) && (p < sEnd);
    else return (p > sEnd) && ((char *)p <= ((char *)sEnd + SlabSize));
  }
  struct CheckPoint {
    constexpr CheckPoint(void *b) : p(b) {}
    constexpr auto isInSlab(void *send) -> bool {
      if constexpr (BumpUp)
        return (((char *)p + SlabSize) >= send) && (p < send);
      else return (p > send) && (p <= ((char *)send + SlabSize));
    }
    void *const p; // NOLINT(misc-non-private-member-variables-in-classes)
  };
  [[nodiscard]] constexpr auto checkpoint() -> CheckPoint { return slab; }
  constexpr void rollback(CheckPoint p) {
    if (p.isInSlab(sEnd)) {
#if LLVM_ADDRESS_SANITIZER_BUILD
      if constexpr (BumpUp)
        __asan_poison_memory_region(p.p, (char *)slab - (char *)p.p);
      else __asan_poison_memory_region(slab, (char *)p.p - (char *)slab);
#endif
      slab = p.p;
    } else initSlab(slabs.back());
  }

private:
  static constexpr auto align(size_t x) -> size_t {
    return (x + MinAlignment - 1) & ~(MinAlignment - 1);
  }
  static constexpr auto align(size_t x, size_t alignment) -> size_t {
    return (x + alignment - 1) & ~(alignment - 1);
  }
  static constexpr auto align(void *p) -> void * {
    uint64_t i = reinterpret_cast<uintptr_t>(p), j = i;
    if constexpr (BumpUp) i += MinAlignment - 1;
    i &= ~(MinAlignment - 1);
    return (char *)p + (i - j);
  }
  static constexpr auto align(void *p, size_t alignment) -> void * {
    invariant(llvm::isPowerOf2_64(alignment));
    uintptr_t i = reinterpret_cast<uintptr_t>(p), j = i;
    if constexpr (BumpUp) i += alignment - 1;
    i &= ~(alignment - 1);
    return (char *)p + (i - j);
  }
  static constexpr auto bump(void *ptr, size_t N) -> void * {
    if constexpr (BumpUp) return (char *)ptr + N;
    else return (char *)ptr - N;
  }
  static constexpr auto outOfSlab(void *cur, void *lst) -> bool {
    if constexpr (BumpUp) return cur >= lst;
    else return cur < lst;
  }
  constexpr void initSlab(void *p) {
    __asan_poison_memory_region(p, SlabSize);
    if constexpr (BumpUp) {
      slab = p;
      sEnd = (char *)p + SlabSize;
    } else {
      slab = (char *)p + SlabSize;
      sEnd = p;
    }
  }
  constexpr void newSlab() {
    auto *p = llvm::allocate_buffer(SlabSize, MinAlignment);
    slabs.push_back(p);
    initSlab(p);
  }
  // updates SlabCur and returns the allocated pointer
  [[gnu::returns_nonnull]] constexpr auto allocCore(size_t Size, size_t Align)
    -> void * {
#if LLVM_ADDRESS_SANITIZER_BUILD
    slab = bump(slab, Align); // poisoned zone
#endif
    if constexpr (BumpUp) {
      slab = align(slab, Align);
      void *old = slab;
      slab = (char *)slab + align(Size);
      return old;
    } else {
      slab = align((char *)slab - Size, Align);
      return slab;
    }
  }
  // updates SlabCur and returns the allocated pointer
  [[gnu::returns_nonnull]] constexpr auto allocCore(size_t Size) -> void * {
    // we know we already have MinAlignment
    // and we need to preserve it.
    // Thus, we align `Size` and offset it.
    invariant((reinterpret_cast<size_t>(slab) % MinAlignment) == 0);
#if LLVM_ADDRESS_SANITIZER_BUILD
    slab = bump(slab, MinAlignment); // poisoned zone
#endif
    if constexpr (BumpUp) {
      void *old = slab;
      slab = (char *)slab + align(Size);
      return old;
    } else {
      slab = (char *)slab - align(Size);
      return slab;
    }
  }
  //
  [[gnu::returns_nonnull]] constexpr auto bumpAlloc(size_t Size, size_t Align)
    -> void * {
    Align = toPowerOf2(Align);
    void *ret = allocCore(Size, Align);
    if (outOfSlab(slab, sEnd)) [[unlikely]] {
      newSlab();
      ret = allocCore(Size, Align);
    }
    return ret;
  }
  [[gnu::returns_nonnull]] constexpr auto bumpAlloc(size_t Size) -> void * {
    void *ret = allocCore(Size);
    if (outOfSlab(slab, sEnd)) [[unlikely]] {
      newSlab();
      ret = allocCore(Size);
    }
    return ret;
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
    if (!customSlabs.empty())
      for (auto [Ptr, Size] : customSlabs)
        llvm::deallocate_buffer(Ptr, Size, MinAlignment);
#else
    if (!customSlabs.empty())
      for (auto Ptr : customSlabs) std::free(Ptr);
#endif
    customSlabs.clear();
  }

  void *slab{nullptr};                                // 8 bytes
  void *sEnd{nullptr};                                // 8 bytes
  Vector<void *, 2> slabs{};                          // 16 + 16 bytes
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
  template <typename U> struct rebind { // NOLINT(readability-identifier-naming)
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
  constexpr auto checkpoint() -> typename Alloc::CheckPoint {
    return A->checkpoint();
  }
  constexpr void rollback(typename Alloc::CheckPoint p) { A->rollback(p); }
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
concept Allocator = requires(A a) {
  typename A::value_type;
  { a.allocate(1) } -> std::same_as<typename std::allocator_traits<A>::pointer>;
  {
    a.deallocate(std::declval<typename std::allocator_traits<A>::pointer>(), 1)
  };
};
static_assert(Allocator<WBumpAlloc<int64_t>>);
static_assert(Allocator<std::allocator<int64_t>>);

struct NoCheckpoint {};

constexpr auto checkpoint(const auto &) { return NoCheckpoint{}; }
template <class T> constexpr auto checkpoint(WBumpAlloc<T> alloc) {
  return alloc.checkpoint();
}
constexpr auto checkpoint(BumpAlloc<> &alloc) { return alloc.checkpoint(); }

constexpr void rollback(const auto &, NoCheckpoint) {}
template <class T> constexpr void rollback(WBumpAlloc<T> alloc, auto p) {
  alloc.rollback(p);
}
constexpr void rollback(BumpAlloc<> &alloc, auto p) { alloc.rollback(p); }
