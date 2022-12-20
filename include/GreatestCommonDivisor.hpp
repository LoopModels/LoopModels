#pragma once
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <tuple>

[[maybe_unused]] static auto gcd(int64_t x, int64_t y) -> int64_t {
  if (x == 0) {
    return std::abs(y);
  } else if (y == 0) {
    return std::abs(x);
  }
  assert(x != std::numeric_limits<int64_t>::min());
  assert(y != std::numeric_limits<int64_t>::min());
  int64_t a = std::abs(x);
  int64_t b = std::abs(y);
  if ((a == 1) | (b == 1))
    return 1;
  int64_t az = std::countr_zero(uint64_t(x));
  int64_t bz = std::countr_zero(uint64_t(y));
  b >>= bz;
  int64_t k = std::min(az, bz);
  while (a) {
    a >>= az;
    int64_t d = a - b;
    az = std::countr_zero(uint64_t(d));
    b = std::min(a, b);
    a = std::abs(d);
  }
  return b << k;
}
[[maybe_unused]] static auto lcm(int64_t x, int64_t y) -> int64_t {
  if (std::abs(x) == 1)
    return y;
  if (std::abs(y) == 1)
    return x;
  return x * (y / gcd(x, y));
}
// https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
template <std::integral T> auto gcdx(T a, T b) -> std::tuple<T, T, T> {
  T old_r = a;
  T r = b;
  T old_s = 1;
  T s = 0;
  T old_t = 0;
  T t = 1;
  while (r) {
    T quotient = old_r / r;
    old_r -= quotient * r;
    old_s -= quotient * s;
    old_t -= quotient * t;
    std::swap(r, old_r);
    std::swap(s, old_s);
    std::swap(t, old_t);
  }
  // Solving for `t` at the end has 1 extra division, but lets us remove
  // the `t` updates in the loop:
  // T t = (b == 0) ? 0 : ((old_r - old_s * a) / b);
  // For now, I'll favor forgoing the division.
  return std::make_tuple(old_r, old_s, old_t);
}

/// divgcd(x, y) = (x / gcd(x, y), y / gcd(x, y))
constexpr auto divgcd(int64_t x, int64_t y) -> std::pair<int64_t, int64_t> {
  if (x) {
    if (y) {
      int64_t g = gcd(x, y);
      assert(g == gcd(x, y));
      return std::make_pair(x / g, y / g);
    } else {
      return std::make_pair(1, 0);
    }
  } else if (y) {
    return std::make_pair(0, 1);
  } else {
    return std::make_pair(0, 0);
  }
}

template <typename T, typename S> void divExact(T &x, S const &y) {
  auto d = x / y;
  assert(d * y == x);
  x = d;
}
