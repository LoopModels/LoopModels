#pragma once
#ifndef POLY_LEGALITY_HPP_INCLUDED
#define POLY_LEGALITY_HPP_INCLUDED

#include <cstdint>

namespace poly {
namespace IR {
class Loop;
class Addr;
}; // namespace IR
namespace poly {
struct Dependence;
class Dependencies;
}; // namespace poly
namespace CostModeling {

struct LoopDepSatisfaction;

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
//
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
//
//
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
// For examples 2-3 above, we should have a concept of must-scalarize this
// loop's execution, but that we can vectorize/reorder it within subloops.
class Legality {
  // enum class Illegal : uint8_t {
  //   None = 0,
  //   Unroll = 1,
  //   ReorderThis = 2,
  //   ReorderSubLoops = 4
  // };
  uint16_t peelFlag{0};
  // TODO: use min and max distance!
  // uint16_t mindistance{std::numeric_limits<uint16_t>::max()};
  // uint8_t maxdistance{0};
  uint16_t ordered_reduction_count{0};
  uint16_t unordered_reduction_count{0};
  bool reorderable{true};
  // uint8_t illegalFlag{0};

public:
  // [[nodiscard]] constexpr auto minDistance() const -> uint16_t {
  //   return mindistance;
  // }
  // [[nodiscard]] constexpr auto maxDistance() const -> uint16_t {
  //   return maxdistance;
  // }
  // [[nodiscard]] constexpr auto noUnroll() const -> bool {
  //   return illegalFlag & uint8_t(Illegal::Unroll);
  // }
  // [[nodiscard]] constexpr auto canUnroll() const -> bool { return
  // !noUnroll(); }
  constexpr auto operator&=(Legality other) -> Legality & {
    ordered_reduction_count += other.ordered_reduction_count;
    unordered_reduction_count += other.unordered_reduction_count;
    // mindistance = std::min(mindistance, other.mindistance);
    // maxdistance = std::max(maxdistance, other.maxdistance);
    peelFlag |= other.peelFlag;
    // illegalFlag |= other.illegalFlag;
    return *this;
  }
  constexpr auto operator=(const Legality &) -> Legality & = default;
  [[nodiscard]] constexpr auto operator&(Legality other) const -> Legality {
    Legality l{*this};
    return l &= other;
  }
  constexpr Legality() = default;
  constexpr Legality(const Legality &) = default;
  Legality(LoopDepSatisfaction &deps, IR::Loop *L);
  // deeperAccess(const poly::Dependencies &deps, IR::Loop *L, IR::Addr *in)
  // are any of the outputs of `in` in a subloop of `L`
  // static auto deeperAccess(const poly::Dependencies &deps, IR::Loop *L,
  //                          IR::Addr *in) -> bool {
  //   return std::ranges::any_of(in->outputEdgeIDs(deps),
  //                              [&](int32_t id) -> bool {
  //                                IR::Addr *a =
  //                                deps.output(Dependence::ID{id}); return
  //                                (a->getLoop() != L) && L->contains(a);
  //                              });
  // }
  // inline auto anyInteriorDependents(IR::Loop *L, IR::Addr *out) -> bool {
  //   return std::ranges::any_of(out->outputEdgeIDs(*this),
  //                              [&](int32_t i) -> bool {
  //                                IR::Addr *a = output(Dependence::ID{i});
  //                                return (a->getLoop() != L) &&
  //                                L->contains(a);
  //                              });
  // }

  // inline auto anyInteriorDependencies(IR::Loop *L, IR::Addr *in) -> bool {

  //   return std::ranges::any_of(in->inputEdgeIDs(*this), [&](int32_t i) ->
  //   bool {
  //     IR::Addr *a = input(Dependence::ID{i});
  //     return (a->getLoop() != L) && L->contains(a);
  //   });
  // }
  auto update(poly::Dependencies &deps, IR::Loop *L, int32_t did) -> bool;
};
static_assert(sizeof(Legality) == 8);
} // namespace CostModeling
} // namespace poly
#endif // POLY_LEGALITY_HPP_INCLUDED
