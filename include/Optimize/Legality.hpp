#pragma once
#ifndef POLY_LEGALITY_HPP_INCLUDED
#define POLY_LEGALITY_HPP_INCLUDED

#include "Optimize/CostModeling.hpp"
#include "Polyhedra/Dependence.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace poly::CostModeling {


auto searchReduction(IR::Instruction* in, IR::Instruction*out)->bool{

}

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
  // enum class Reduction { None = 0, Unordered = 1, Ordered = 2 };
  uint16_t unordered_reduction_count{0};
  uint16_t mindistance{std::numeric_limits<uint16_t>::max()};
  uint16_t maxdistance{0};
  uint16_t maxiters{0};
  [[nodiscard]] constexpr auto minDistance() const -> uint16_t {
    return mindistance;
  }
  [[nodiscard]] constexpr auto maxDistance() const -> uint16_t {
    return mindistance;
  }
  [[nodiscard]] constexpr auto maxIters() const -> uint16_t { return maxiters; }
  constexpr auto operator&=(Legality other) -> Legality & {
    unordered_reduction_count += other.unordered_reduction_count;
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
  Legality(IR::Dependencies deps, Dependence d) {
    // TODO: check if addr match
    // "Reduction" dependences should correspond to time dims.
    // In the memory-optimized IR, we have read/write to the
    // same address hoisted outside of the loop carrying `d`
    if (d.revTimeEdge() && d.out->isLoad()) {
      // Dependence rd = deps.get(rid);
      // We don't actually need to use rid
      // TODO: search between out and in
      // If we have a reduction, then `d.out` is a load from an address
      // that is then updated by some sequence of operations, before
      // being stord in `d.in`
      // (Because this is revTime, the load is the output as it must
      //  happen after the previous iteration's store.)
      // We thus search the operation graph to find all paths
      // from `d.out` to `d.in`. If there are none, then it is
      // not a reduction, but updated in some other manner.
      // If there is exactly one reassociable path, the reduction is unordered.
      // Else, it is ordered? Do we need to consider ordered differently from no
      // reduction?
      IR::Addr *in = d.out, *out = d.in;
      // If we have an operation chain leading from in->out
    }
  };
  Legality(LoopDepSatisfaction deps, IR::Loop *L) {
    for (poly::Dependence d : deps.depencencies(L))
      (*this) &= Legality(deps.deps, d);
  }
};

} // namespace poly::CostModeling
#endif // POLY_LEGALITY_HPP_INCLUDED
