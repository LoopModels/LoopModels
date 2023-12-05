#pragma once
#ifndef OrthogonalAxes_hpp_INCLUDED
#define OrthogonalAxes_hpp_INCLUDED

#include <bit>
#include <cstdint>

/// `indep` must be `0` for any `invunrolls` it doesn't depend on
struct OrthogonalAxes {
  /// Boolean: Are the axes independent?
  uint32_t indep_axes : 1;
  /// Bit mask: are the axes contiguous?
  uint32_t contig : 31; // max number of dims of 31
  /// Flag indicating whether the axis is independent of loops
  /// `1` per independent loops
  uint32_t indep; // max loop depth of 32
};
static_assert(sizeof(OrthogonalAxes) == 8);
constexpr auto operator==(OrthogonalAxes a, OrthogonalAxes b) -> bool {
  return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}

#endif // OrthogonalAxes_hpp_INCLUDED
