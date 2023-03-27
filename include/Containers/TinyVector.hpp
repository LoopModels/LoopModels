#pragma once

#include "Utilities/Invariant.hpp"
#include <cstddef>
#include <memory>
template <class T, size_t N> struct TinyVector {
  static_assert(N > 0);
  T data[N]; // NOLINT (modernize-avoid-c-style-arrays)
  // std::array<T, N> data;
  size_t len{};
  constexpr TinyVector() = default;
  constexpr TinyVector(const std::initializer_list<T> &list) {
    invariant(list.size() <= N);
    len = list.size();
    std::copy(list.begin(), list.end(), data);
  }
  constexpr TinyVector(T t) : len{1} { data[0] = std::move(t); }

  constexpr auto operator[](size_t i) -> T & {
    invariant(i < len);
    return data[i];
  }
  constexpr auto operator[](size_t i) const -> const T & {
    invariant(i < len);
    return data[i];
  }
  constexpr auto back() -> T & {
    invariant(len > 0);
    return data[len - 1];
  }
  constexpr auto back() const -> const T & {
    invariant(len > 0);
    return data[len - 1];
  }
  constexpr auto front() -> T & {
    invariant(len > 0);
    return data[0];
  }
  constexpr auto front() const -> const T & {
    invariant(len > 0);
    return data[0];
  }
  constexpr void push_back(const T &t) {
    invariant(len < N);
    data[len++] = t;
  }
  constexpr void push_back(T &&t) {
    invariant(len < N);
    data[len++] = std::move(t);
  }
  template <class... Args> constexpr void emplace_back(Args &&...args) {
    invariant(len < N);
    data[len++] = T(std::forward<Args>(args)...);
  }
  constexpr void pop_back() {
    invariant(len > 0);
    --len;
  }
  [[nodiscard]] constexpr auto size() const -> size_t {
    invariant(len <= N);
    return len;
  }
  [[nodiscard]] constexpr auto empty() const -> bool { return len == 0; }
  constexpr void clear() { len = 0; }

  constexpr auto begin() -> T * { return data; }
  constexpr auto begin() const -> const T * { return data; }
  constexpr auto end() -> T * { return data + size(); }
  constexpr auto end() const -> const T * { return data + size(); }
};
