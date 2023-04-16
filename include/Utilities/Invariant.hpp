#pragma once

#ifndef NDEBUG
#include <cstdlib>
#include <source_location>
[[gnu::artificial]] constexpr inline void
invariant(bool condition,
          std::source_location location = std::source_location::current()) {
  if (!condition) {
    llvm::errs() << "invariant violation\nfile: " << location.file_name() << ":"
                 << location.line() << ":" << location.column() << " `"
                 << location.function_name() << "`\n";
    abort();
  }
}
template <typename T>
[[gnu::artificial]] constexpr inline void
invariant(const T &x, const T &y,
          std::source_location location = std::source_location::current()) {
  if (x != y) {
    llvm::errs() << "invariant violation: " << x << " != " << y
                 << "\nfile: " << location.file_name() << ":" << location.line()
                 << ":" << location.column() << " `" << location.function_name()
                 << "`\n";
    abort();
  }
}
#else // ifdef NDEBUG
#if __cplusplus >= 202202L
#include <utility>
#endif
[[gnu::artificial]] constexpr inline void invariant(bool condition) {
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

#endif
