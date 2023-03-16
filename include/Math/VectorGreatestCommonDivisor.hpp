#pragma once
#include "Math/GreatestCommonDivisor.hpp"
#include "Math/Math.hpp"
#include <cstddef>
#include <cstdint>

using LinAlg::PtrVector, LinAlg::MutPtrVector;

constexpr auto gcd(PtrVector<int64_t> x) -> int64_t {
  const size_t N = x.size();
  if (!N) return 0;
  int64_t g = constexpr_abs(x[0]);
  for (size_t n = 1; (n < N) & (g != 1); ++n) g = gcd(g, x[n]);
  return g;
}
constexpr void normalizeByGCD(MutPtrVector<int64_t> x) {
  size_t N = x.size();
  switch (N) {
  case 0:
    return;
  case 1:
    x[0] = 1;
    return;
  default:
    int64_t g = gcd(x[0], x[1]);
    for (size_t n = 2; (n < N) & (g != 1); ++n) g = gcd(g, x[n]);
    if (g > 1) x /= g;
  }
}

constexpr auto lcm(AbstractVector auto x) -> int64_t {
  int64_t l = x[0];
  for (int64_t xi : x[_(1, end)]) l = lcm(l, xi);
  return l;
}
constexpr auto lcmNonUnity(AbstractVector auto x) -> std::pair<int64_t, bool> {
  int64_t l = x[0];
  bool nonUnity = (l != 1);
  for (int64_t xi : x[_(1, end)]) {
    nonUnity |= (xi != 1);
    l = lcm(l, xi);
  }
  return {l, nonUnity};
}
constexpr auto lcmSkipZero(AbstractVector auto x) -> int64_t {
  int64_t l = 1;
  for (int64_t xi : x)
    if (xi) l = lcm(l, xi);
  return l;
}
