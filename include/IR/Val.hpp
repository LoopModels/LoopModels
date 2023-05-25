#pragma once

#include <Math/Array.hpp>
#include <llvm/Support/Casting.h>

// We take an approach similar to LLVM's RTTI
// however, we want to take advantage of FAMs while having a "hieararchy"
// we accomplish this via a base class, and then wrapper classes that simply
// hold the pointers.

class Val {
public:
  enum ValKind { VK_Load, VK_Store, VK_Instr, VK_Block, VK_Loop };

private:
  unsigned numParents;
  unsigned numChildren;
  ValKind kind;

#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  alignas(int64_t) char mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
public:
  [[nodiscard]] auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] auto parents() -> MutPtrVector<Val *> {
    void *p = mem;
    return {(Val **)p, numParents};
  }
  [[nodiscard]] auto parents() const -> PtrVector<Val *> {
    const void *p = mem;
    return {(Val **)p, numParents};
  }
  [[nodiscard]] auto children() -> MutPtrVector<Val *> {
    void *p = mem;
    return {((Val **)p) + numParents, numChildren};
  }
  [[nodiscard]] auto children() const -> PtrVector<Val *> {
    const void *p = mem;
    return {((Val **)p) + numParents, numChildren};
  }
};
