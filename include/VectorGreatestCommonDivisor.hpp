#pragma once
#include "./GreatestCommonDivisor.hpp"
#include "./Math.hpp"

using LinearAlgebra::PtrVector, LinearAlgebra::MutPtrVector;

[[maybe_unused]] static auto gcd(PtrVector<int64_t> x) -> int64_t {
  int64_t g = std::abs(x[0]);
  for (size_t i = 1; i < x.size(); ++i)
    g = gcd(g, x[i]);
  return g;
}
[[maybe_unused]] static void normalizeByGCD(MutPtrVector<int64_t> x) {
  if (size_t N = x.size()) {
    if (N == 1) {
      x[0] = 1;
      return;
    }
    int64_t g = gcd(x[0], x[1]);
    for (size_t n = 2; (n < N) & (g != 1); ++n)
      g = gcd(g, x[n]);
    if (g > 1)
      x /= g;
  }
}
