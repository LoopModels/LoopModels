#pragma once

#include "Utilities/Allocators.hpp"
#include <llvm/ADT/ArrayRef.h>

template <typename T>
[[nodiscard]] inline auto copyRef(BumpAlloc<> &alloc, llvm::ArrayRef<T> ref)
  -> llvm::ArrayRef<T> {
  size_t N = ref.size();
  T *p = alloc.allocate<T>(N);
  std::copy_n(ref.begin(), N, p);
  return llvm::ArrayRef<T>(p, N);
}
