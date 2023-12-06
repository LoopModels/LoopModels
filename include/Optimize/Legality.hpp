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
//
// Note that it is always legal to unroll an innermost loop (scalarizing).
// But we need reorderability for unroll and jam.
// For example, this loop carries a dependency
// example 0:
// for (ptrdiff_t i = 1; i < x.size(); ++i)
//   x[i] += x[i-1];
// but we may wish to unroll it to reduce the amount of `mov` instructions
// needed, as well as `i` increments.
// However, if we had some other loop dependent on this
//
// example 1:
// for (ptrdiff_t i = 1; i < x.size(); ++i){
//   decltype(y[0,0]/x[0]) s = 0;
//   for (ptrdiff_t j = 0; j < y.size(); ++j)
//     s += y[i,j] / x[i-1];
//   x[i] += s * x[i-1];
// }
// an unroll and jam would be illegal.
// TODO: what if the innermost loop isn't dependent?
// example 2:
// for (ptrdiff_t i = 1; i < x.size(); ++i){
//   decltype(y[0,0]+y[0,0]) s = 0;
//   for (ptrdiff_t j = 0; j < y.size(); ++j)
//     s += y[i,j];
//   x[i] += s * x[i-1];
// }
// Here, we can unroll and jam.
// example 3:
// for (ptrdiff_t i = 1; i < x.size()-3; i+=4){
//   decltype(y[0,0]+y[0,0]) s0 = 0;
//   decltype(y[0,0]+y[0,0]) s1 = 0;
//   decltype(y[0,0]+y[0,0]) s2 = 0;
//   decltype(y[0,0]+y[0,0]) s3 = 0;
//   for (ptrdiff_t j = 0; j < y.size(); ++j){
//     s0 += y[i,j];
//     s1 += y[i+1,j];
//     s2 += y[i+2,j];
//     s3 += y[i+3,j];
//   }
//   x[i] += s0 * x[i-1];
//   x[i+1] += s1 * x[i];
//   x[i+2] += s2 * x[i+1];
//   x[i+3] += s3 * x[i+2];
// }
// So we can generalize to say, we can always unroll the innermost where the
// addr are read.
//
// example 4:
// for (i : I)
//   for (j : J)
//     for (k : K)
//       for (l : L)
//         B[i,j] += A[i+k,j+l] * K[k,l];
//
//
// TODO items:
// [x] Store time deps in cycle w/in `Dependencies` object so we can iterate
//     over all of them.
// [ ] Check `Addr` hoisting code for how it handles reductions, ensuring we can
//     hoist them out.
// [ ] Fuse legality checking, at least in part, with it, as that may indicate
//     unrolling in example 3 above.
// [ ] See discussionin CostModeling.hpp above `optimize` about unrolling.
// Okay, we'll take a somewhat different approach:
// it shouldn't be too difficult to check for extra outputs, etc.
// so we do that all here, after the `Addr` placements and simplifications
// 
struct Legality {
  // enum class Reduction { None = 0, Unordered = 1, Ordered = 2 };
  uint16_t unordered_reduction_count{0};
  uint16_t mindistance{std::numeric_limits<uint16_t>::max()};
  uint16_t maxdistance{0};
  bool canPeel{true};
  bool canUnroll{true};
  // uint16_t maxiters{0};
  [[nodiscard]] constexpr auto minDistance() const -> uint16_t {
    return mindistance;
  }
  [[nodiscard]] constexpr auto maxDistance() const -> uint16_t {
    return mindistance;
  }
  // [[nodiscard]] constexpr auto maxIters() const -> uint16_t { return
  // maxiters; }
  constexpr auto operator&=(Legality other) -> Legality & {
    unordered_reduction_count += other.unordered_reduction_count;
    mindistance = std::min(mindistance, other.mindistance);
    maxdistance = std::max(maxdistance, other.maxdistance);
    canPeel = canPeel & other.canPeel;
    canUnroll = canUnroll & other.canUnroll;
    // maxiters = std::max(maxiters, other.maxiters);
    return *this;
  }
  [[nodiscard]] constexpr auto operator&(Legality other) const -> Legality {
    Legality l{*this};
    return l &= other;
  }
  constexpr Legality() = default;
  constexpr Legality(const Legality &) = default;
  static auto deeperAccess(poly::Dependencies deps, IR::Loop *L, IR::Addr *in)
    -> bool {
    return std::ranges::any_of(in->outputEdgeIDs(deps), [=](int32_t id) {
      IR::Addr *a = deps.output(Dependence::ID{id});
      return (a->getLoop() != L) && L->contains(a);
    });
  }
  void update(poly::Dependencies deps, IR::Loop *L, Dependence d) {
    IR::Addr *in = d.out, *out = d.in;
    if (d.revTimeEdge() && (in->reassociableReductionPair() != out)) {
      ++unordered_reduction_count;
      return;
    }
    /// Now we check if we're allowed any reorderings
    /// First of all, consider that unrolling is allowed if no memory accesses
    /// dependent on the `in` or `out` are in a deeper loop (e.g. example 3),
    /// even if vectorization is not.
    if (canUnroll && d.revTimeEdge() && deeperAccess(deps, L, in)) {
      mindistance = 0;
      maxdistance = std::numeric_limits<uint16_t>::max();
      canPeel = false;
      canUnroll = false;
      return;
    }
    if (mindistance || maxdistance != std::numeric_limits<uint16_t>::max()) {
      // for now, just check peelability
    }
    if (!canPeel){
      // then we have a dependence
    }
  };
  Legality(LoopDepSatisfaction deps, IR::Loop *L) {
    for (poly::Dependence d : deps.depencencies(L)) update(deps.deps, L, d);
  }
};

} // namespace poly::CostModeling
#endif // POLY_LEGALITY_HPP_INCLUDED
