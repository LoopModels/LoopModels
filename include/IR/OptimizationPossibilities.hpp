#pragma once
#include <cstdint>

class UnrollOptions {
  uint32_t options; // bitmask
public:
  constexpr UnrollOptions(uint32_t options) : options(options) {}
  static constexpr auto atMost(uint32_t x) -> UnrollOptions {
    return {(uint32_t{1} << x) - 1};
  }
  [[nodiscard]] constexpr auto allowed(uint32_t x) const -> bool {
    return (options & (uint32_t{1} << x));
  }
  [[nodiscard]] constexpr auto isDense() const -> bool {
    return options == std::numeric_limits<uint32_t>::max();
  }
  [[nodiscard]] constexpr auto getOptions() const -> uint32_t {
    return options;
  }
  [[nodiscard]] constexpr auto operator&(UnrollOptions other) const
    -> UnrollOptions {
    return {options & other.options};
  }
  constexpr auto operator&=(UnrollOptions other) -> UnrollOptions & {
    options &= other.options;
    return *this;
  }
};

class LoopDependencies {
  uint32_t nonContiguous; // bitmask
  uint32_t contiguous;    // bitmask, A[i+j,k+l]; multiple may be set
public:
  constexpr LoopDependencies(uint32_t nonContiguous, uint32_t contiguous)
    : nonContiguous(nonContiguous), contiguous(contiguous) {}
  [[nodiscard]] constexpr auto getNonContiguous() const -> uint32_t {
    return nonContiguous;
  }
  [[nodiscard]] constexpr auto getContiguous() const -> uint32_t {
    return contiguous;
  }
  [[nodiscard]] constexpr auto operator&(LoopDependencies other) const
    -> LoopDependencies {
    uint32_t nonContig = nonContiguous | other.nonContiguous;
    return {nonContig, (contiguous & other.contiguous) & ~nonContig};
  }
};

/// When we unroll, we have an ordering of the unrolled dimensions.
class RegisterTile {
  std::array<uint8_t, 3> unroll; //
  uint8_t vector;                //
  uint32_t unrollMask;           // bitmask
public:
  RegisterTile(std::array<uint8_t, 3> unroll, uint8_t vector)
    : unroll(unroll), vector(vector) {
    unrollMask = (1 << unroll[0]) | (1 << unroll[1]) | (1 << unroll[2]);
  }
};
