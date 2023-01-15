#pragma once

constexpr void invariant(bool condition) {
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
