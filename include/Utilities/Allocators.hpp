#pragma once

/// The motivation of this file is to support realloc, allowing us to reasonably
/// use this allocator to back containers.
#include "Utilities/Invariant.hpp"
#include "Utilities/Iterators.hpp"
#include "Utilities/Valid.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/MathExtras.h>
#include <memory>
#include <type_traits>

template <size_t SlabSize = 4096, bool BumpUp = false, size_t MinAlignment = 8,
          typename AllocT = std::allocator<std::byte>>
struct BumpAlloc {
  static_assert(llvm::isPowerOf2_64(MinAlignment));

public:
  [[gnu::returns_nonnull]] auto allocate(size_t Size, size_t Align) -> void * {
    if (Size > SlabSize / 2) {
      void *p = A.allocate(Size, Align);
      customSlabs.emplace_back(p, Size);
      return p;
    }
    auto p = (Align > MinAlignment) ? bumpAlloc(Size, Align) : bumpAlloc(Size);
    __asan_unpoison_memory_region(p, Size);
    __msan_allocated_memory(p, Size);
    return p;
  }
  template <typename T>
  [[gnu::returns_nonnull]] auto allocate(size_t N) -> T * {
    static_assert(std::is_trivially_destructible_v<T>,
                  "BumpAlloc only supports trivially destructible types");
    return reinterpret_cast<T *>(allocate(N * sizeof(T), alignof(T)));
  }

  void deallocate(void *Ptr, size_t Size) {
    (void)Ptr;
    (void)Size;
    __asan_poison_memory_region(Ptr, Size);
  }
  template <bool ForOverwrite = false>
  [[gnu::returns_nonnull]] auto reallocate(void *Ptr, size_t OldSize,
                                           size_t NewSize, size_t Align)
    -> void * {
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
    if constexpr (!ForOverwrite) memcpy(NewPtr, Ptr, OldSize);
    deallocate(Ptr, OldSize);
    return NewPtr;
  }
  void reset() {
    resetSlabs();
    resetCustomSlabs();
  }
  template <bool ForOverwrite = false, typename T>
  [[gnu::returns_nonnull]] auto reallocate(T *Ptr, size_t OldSize,
                                           size_t NewSize) -> T * {
    return reinterpret_cast<T *>(reallocate<ForOverwrite>(
      Ptr, OldSize * sizeof(T), NewSize * sizeof(T), alignof(T)));
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
  static constexpr auto toPowerOf2(size_t n) -> size_t {
    size_t x = size_t(1) << ((8 * sizeof(n) - std::countl_zero(--n)));
    invariant(x >= n);
    return x;
  }
  static constexpr auto bump(std::byte *ptr, size_t N) -> std::byte * {
    if constexpr (BumpUp) return ptr + N;
    else return ptr - N;
  }
  static constexpr auto outOfSlab(std::byte *cur, std::byte *end) -> bool {
    if constexpr (BumpUp) return cur >= end;
    else return cur < end;
  }
  auto maybeNewSlab() -> bool {
    if (!outOfSlab(SlabCur, SlabEnd)) return false;
    newSlab();
    return true;
  }
  void newSlab() {
    std::byte *p = A.allocate(SlabSize);
    slabs.push_back(p);
    __asan_poison_memory_region(p, SlabSize);
    if constexpr (BumpUp) {
      SlabCur = align(p);
      SlabEnd = p + SlabSize;
    } else {
      SlabCur = align(p + SlabSize);
      SlabEnd = p;
    }
  }
  // updates SlabCur and returns the allocated pointer
  [[gnu::returns_nonnull]] auto allocCore(size_t Size, size_t Align)
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
  [[gnu::returns_nonnull]] auto allocCore(size_t Size) -> std::byte * {
    // we know we already have MinAlignment
    // and we need to preserve it.
    // Thus, we align `Size` and offset it.
    invariant(SlabCur.isAligned(MinAlignment));
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
  [[gnu::returns_nonnull]] auto bumpAlloc(size_t Size, size_t Align) -> void * {
    Align = toPowerOf2(Align);
    std::byte *ret = allocCore(Size, Align);
    if (maybeNewSlab()) ret = allocCore(Size, Align);
    return reinterpret_cast<void *>(ret);
  }
  [[gnu::returns_nonnull]] auto bumpAlloc(size_t Size) -> void * {
    std::byte *ret = allocCore(Size);
    if (maybeNewSlab()) ret = allocCore(Size);
    return reinterpret_cast<void *>(ret);
  }

  void resetSlabs() {
    if (size_t nSlabs = slabs.size()) {
      if (nSlabs > 1) {
        for (auto Slab : skipFirst(slabs)) A.deallocate(Slab, SlabSize);
        slabs.truncate(1);
      }
      __asan_poison_memory_region(slabs.front(), SlabSize);
      if constexpr (BumpUp) {
        SlabCur = slabs.front();
        SlabEnd = SlabCur + SlabSize;
      } else {
        SlabEnd = slabs.front();
        SlabCur = SlabCur + SlabSize;
      }
    }
  }
  void resetCustomSlabs() {
    for (auto [Ptr, Size] : customSlabs) A.deallocate(Ptr, Size);
    customSlabs.clear();
  }

  ~BumpAlloc() {
    for (auto Slab : slabs) A.deallocate(Slab, SlabSize);
    resetCustomSlabs();
  }
  NotNull<std::byte> SlabCur;                                  // 8 bytes
  NotNull<std::byte> SlabEnd;                                  // 8 bytes
  llvm::SmallVector<NotNull<std::byte>, 2> slabs;              // 16 + 16 bytes
  llvm::SmallVector<std::pair<void *, size_t>, 0> customSlabs; // 16 bytes
  [[no_unique_address]] AllocT A{};                            // 0 bytes
};
static_assert(sizeof(BumpAlloc<>) == 64);
static_assert(!std::is_trivially_copyable_v<BumpAlloc<>>);
static_assert(!std::is_trivially_destructible_v<BumpAlloc<>>);
