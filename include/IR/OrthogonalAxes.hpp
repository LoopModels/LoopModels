#pragma once
#ifndef OrthogonalAxes_hpp_INCLUDED
#define OrthogonalAxes_hpp_INCLUDED

#include <bit>
#include <cstdint>

/// `indep` must be `0` for any `invunrolls` it doesn't depend on
struct OrthogonalAxes {
  uint32_t indep_axes : 1;
  uint32_t contig : 31; // max number of dims of 31
  uint32_t indep;       // max loop depth of 32
};
constexpr auto operator==(OrthogonalAxes a, OrthogonalAxes b) -> bool {
  return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}
static_assert(sizeof(OrthogonalAxes) == 8);

#endif // OrthogonalAxes_hpp_INCLUDED
