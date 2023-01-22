#pragma once

#include "Utilities/Allocators.hpp"
#include <llvm/ADT/ArrayRef.h>

template <typename T>
[[nodiscard]] inline auto copyRef(BumpAlloc<> &alloc, llvm::ArrayRef<T> ref)
  -> llvm::ArrayRef<T> {
  T *p = alloc.allocate<T>(ref.size());
  std::copy(ref.begin(), ref.end(), p);
  return llvm::ArrayRef<T>(p, ref.size());
}
