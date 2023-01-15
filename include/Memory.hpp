#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/Allocator.h>

template <typename T>
[[nodiscard]] inline auto copyRef(llvm::BumpPtrAllocator &alloc,
                                  llvm::ArrayRef<T> ref) -> llvm::ArrayRef<T> {
  T *p = alloc.Allocate<T>(ref.size());
  std::copy(ref.begin(), ref.end(), p);
  return llvm::ArrayRef<T>(p, ref.size());
}
