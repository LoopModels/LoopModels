#pragma once
#include <cstddef>

template <typename T, size_t N> struct Storage {
  alignas(T) char mem[N * sizeof(T)]; // NOLINT (modernize-avoid-c-style-arrays)
  constexpr T *data() {
    void *p = mem;
    return (T *)p;
  }
  constexpr const T *data() const {
    const void *p = mem;
    return (T *)p;
  }
};
template <typename T> struct alignas(T) Storage<T, 0> {
  static constexpr T *data() { return nullptr; }
};
