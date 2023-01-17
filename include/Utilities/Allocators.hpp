#pragma once

#define BUMP_MAP_LLVM_USE_ALLOCATOR
/// The motivation of this file is to support realloc, allowing us to reasonably
/// use this allocator to back containers.
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
#include <type_traits>
#ifndef BUMP_MAP_LLVM_USE_ALLOCATOR
#include <cstdlib>
#endif

template <size_t SlabSize = 4096, bool BumpUp = false,
          size_t MinAlignment = 8> // alignof(std::max_align_t)>
struct BumpAlloc {
  static_assert(llvm::isPowerOf2_64(MinAlignment));

public:
  using value_type = std::byte;
  [[gnu::returns_nonnull]] auto allocate(size_t Size, size_t Align) -> void * {
    if (Size > SlabSize / 2) {
#ifdef BUMP_MAP_LLVM_USE_ALLOCATOR
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
  template <typename T> void deallocate(T *Ptr, size_t N) {
    deallocate((void *)(Ptr), N * sizeof(T));
  }
  /// reallocate<ForOverwrite>(void *Ptr, size_t OldSize, size_t NewSize, size_t
  /// Align)
  /// Should be safe with OldSize == 0, as it checks before copying
  template <bool ForOverwrite = false>
  [[gnu::returns_nonnull]] auto reallocate(std::byte *Ptr, size_t OldSize,
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
        if constexpr (!ForOverwrite)
          std::copy(old, old + OldSize, reinterpret_cast<std::byte *>(SlabCur));
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
  void reset() {
    resetSlabs();
    resetCustomSlabs();
  }
  template <bool ForOverwrite = false, typename T>
  [[gnu::returns_nonnull]] auto reallocate(T *Ptr, size_t OldSize,
                                           size_t NewSize) -> T * {
    return reinterpret_cast<T *>(reallocate<ForOverwrite>(
      reinterpret_cast<std::byte *>(Ptr), OldSize * sizeof(T),
      NewSize * sizeof(T), alignof(T)));
  }
  BumpAlloc() : slabs({}), customSlabs({}) { newSlab(); }
  BumpAlloc(BumpAlloc &&alloc) noexcept {
    slabs = std::move(alloc.slabs);
    customSlabs = std::move(alloc.customSlabs);
    SlabCur = alloc.SlabCur;
    SlabEnd = alloc.SlabEnd;
  }
  ~BumpAlloc() {
    for (auto Slab : slabs)
      llvm::deallocate_buffer(Slab, SlabSize, MinAlignment);
    resetCustomSlabs();
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
  auto maybeNewSlab() -> bool {
    if (!outOfSlab(SlabCur, SlabEnd)) return false;
    newSlab();
    return true;
  }
  void initSlab(std::byte *p) {
    __asan_poison_memory_region(p, SlabSize);
    if constexpr (BumpUp) {
      SlabCur = p;
      SlabEnd = p + SlabSize;
    } else {
      SlabCur = p + SlabSize;
      SlabEnd = p;
    }
  }
  void newSlab() {
    std::byte *p = reinterpret_cast<std::byte *>(
      llvm::allocate_buffer(SlabSize, MinAlignment));
    slabs.push_back(p);
    initSlab(p);
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
  void resetCustomSlabs() {
#ifdef BUMP_MAP_LLVM_USE_ALLOCATOR
    for (auto [Ptr, Size] : customSlabs)
      llvm::deallocate_buffer(Ptr, Size, MinAlignment);
#else
    for (auto Ptr : customSlabs) std::free(Ptr);
#endif
    customSlabs.clear();
  }

  std::byte *SlabCur{nullptr};                    // 8 bytes
  std::byte *SlabEnd{nullptr};                    // 8 bytes
  llvm::SmallVector<NotNull<std::byte>, 2> slabs; // 16 + 16 bytes
#ifdef BUMP_MAP_LLVM_USE_ALLOCATOR
  llvm::SmallVector<std::pair<void *, size_t>, 0> customSlabs; // 16 bytes
#else
  llvm::SmallVector<void *, 0> customSlabs; // 16 bytes
#endif
};
static_assert(sizeof(BumpAlloc<>) == 64);
static_assert(!std::is_trivially_copyable_v<BumpAlloc<>>);
static_assert(!std::is_trivially_destructible_v<BumpAlloc<>>);
static_assert(
  std::same_as<std::allocator_traits<BumpAlloc<>>::size_type, size_t>);

// // Alloc wrapper people can pass and store by value
// // with a specific value type, so that it can act more like a
// // `std::allocator`.
// template <typename T, size_t SlabSize = 4096, bool BumpUp = false,
//           size_t MinAlignment = 8>
// struct WBumpAlloc {
//   using value_type = T;

// private:
//   using Alloc = BumpAlloc<SlabSize, BumpUp, MinAlignment>;
//   NotNull<Alloc> A;
// };
