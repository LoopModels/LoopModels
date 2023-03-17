#pragma once
#include <cassert>

[[gnu::artificial]] constexpr inline void invariant(bool condition) {
  assert(condition && "invariant violation");
  if (!condition) {
#if __cplusplus >= 202202L
    std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
    __builtin_unreachable();
#endif
#endif
#endif
  }
}
template <typename T>
[[gnu::artificial]] constexpr inline void invariant(const T &x, const T &y) {
  if (x != y) {
#ifdef NDEBUG
#if __cplusplus >= 202202L
    std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
    __builtin_unreachable();
#endif
#endif
#endif
#else
    llvm::errs() << "invariant violation: " << x << " != " << y << "\n";
    assert(false);
#endif
  }
}
