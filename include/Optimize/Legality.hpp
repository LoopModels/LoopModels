#pragma once
#ifndef POLY_LEGALITY_HPP_INCLUDED
#define POLY_LEGALITY_HPP_INCLUDED

#include "Optimize/CostModeling.hpp"
#include "Polyhedra/Dependence.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace poly::CostModeling {

// If a loop doesn't carry a dependency, it is legal
// If a loop does carry a dependency, we can still consider
// unrolling and vectorization if at least one of:
// - that depenedncy is a reassociable reduction
// - the overlap is for a bounded number of iters, in which case we can peel
// Contains:
// - `getReduction()` enum indicating
// none vs unordered vs ordered
// - `minDistance()`, indicates the minimum distance
// between dependent loop iterations.
// for (ptrdiff_t i; i<I; ++i) x[i+8] = foo(x[i])
// would have a value of `8`, i.e. we can evaluate <=8
// contiguous iterations at a time in parallel safely.
// - `maxDistance()` is the opposite: the maximum
// distance of dependencies from the current iteration.
// In the above example, the value is also `8`.
// This is useful for considering, e.g., trapezoidal tiling.
// - `maxIters()` - maximum number of iterations in which a dependence is held
struct Legality {
  enum class Reduction { None = 0, Unordered = 1, Ordered = 2 };
  uint8_t reduction : 2 {0};
  uint8_t mindistance : 6 {(1 << 6) - 1};
  uint8_t maxdistance{0};
  uint16_t maxiters{0};
  [[nodiscard]] constexpr auto getReduction() const -> Reduction {
    return Reduction(reduction);
  }
  [[nodiscard]] constexpr auto minDistance() const -> uint16_t {
    return mindistance;
  }
  [[nodiscard]] constexpr auto maxDistance() const -> uint16_t {
    return mindistance;
  }
  [[nodiscard]] constexpr auto maxIters() const -> uint16_t { return maxiters; }
  constexpr auto operator&=(Legality other) -> Legality & {
    reduction = std::max(reduction, other.reduction);
    mindistance = std::min(mindistance, other.mindistance);
    maxdistance = std::max(maxdistance, other.maxdistance);
    maxiters = std::max(maxiters, other.maxiters);
    return *this;
  }
  [[nodiscard]] constexpr auto operator&(Legality other) const -> Legality {
    Legality l{*this};
    return l &= other;
  }
  constexpr Legality() = default;
  constexpr Legality(const Legality &) = default;
  Legality(Dependence d){
    // TODO: check if addr match
    // "Reduction" dependences should correspond to time dims.
    // In the memory-optimized IR, we have read/write to the
    // same address hoisted outside of the loop carrying `d`
  };
  Legality(LoopDepSatisfaction deps, IR::Loop *L)   {
    for (poly::Dependence d : deps.depencencies(L)) (*this) &= Legality(d);
  }
};

} // namespace poly::CostModeling
#endif // POLY_LEGALITY_HPP_INCLUDED
