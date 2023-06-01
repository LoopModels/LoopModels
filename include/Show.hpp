#pragma once

#include <concepts>
template <typename T>
concept OStream = requires(T &os, size_t Size, const char *Ptr) {
  { os.flush() };
  { os << Ptr } -> std::convertible_to<T &>;
  { os.write(Ptr, Size) } -> std::convertible_to<T &>;
};
