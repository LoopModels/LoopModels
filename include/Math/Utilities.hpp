#pragma once
#include "Utilities/Invariant.hpp"

constexpr auto toPowerOf2(size_t n) -> size_t {
  size_t x = size_t(1) << ((8 * sizeof(n) - std::countl_zero(--n)));
  invariant(x >= n);
  return x;
}
