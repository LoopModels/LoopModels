#ifndef USE_MODULE
#pragma once
#include <bit>
#include <cstdint>

namespace IR {
#else
module;
export module OrthogonalAxes;
import STL;

export namespace IR {
#endif
/// `indep` must be `0` for any `invunrolls` it doesn't depend on
struct OrthogonalAxes {
  /// Bit mask: are the axes contiguous?
  uint32_t contig_ : 16; // max number of dims of 15
  /// Flag indicating whether the axis is independent of loops
  /// `1` per independent loops
  uint32_t conv_axes_ : 1; // max loop depth of 32
  uint32_t dep_ : 15;      // max loop depth of 15
private:
  friend constexpr auto operator==(OrthogonalAxes a, OrthogonalAxes b) -> bool {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
  }
};
static_assert(sizeof(OrthogonalAxes) == 4);

} // namespace IR
