#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <bit>
#include <cstdint>

#ifndef USE_MODULE
#include "Math/Array.cxx"
#include "Math/MultiplicativeInverse.cxx"
#include "Target/Machine.cxx"
#else
export module CostModeling:Cost;
import Array;
import MultiplicativeInverse;
import TargetMachine;
#endif

#ifdef USE_MODULE
export namespace CostModeling::Cost {
#else
namespace CostModeling::Cost {
#endif

using math::PtrVector;

/// Cost in recip throughput, divided between load, store, and total.
struct Cost {
  double load_{0.0}, stow_{0.0}, comp_{0.0}, latency_{0.0};
  constexpr auto operator+=(Cost other) -> Cost & {
    load_ += other.load_;
    stow_ += other.stow_;
    comp_ += other.comp_;
    // latency = std::max(latency, other.latency);
    return *this;
  }
  [[nodiscard]] constexpr auto reduce(target::CoreWidth c) const -> double {
    double totalops = load_ + stow_ + comp_;
    double l = load_ / c.load_, s = stow_ / c.stow_, a = comp_ / c.comp_,
           t = totalops / c.total_, mx = std::max({l, s, a, latency_, t}),
           acc = l + s + a + latency_ + t;
    static constexpr double leakage = 1.0 / 8.0;
    // FIXME: no longer represents cycles, due to double-counting of load, stow,
    // and comp w/in totalops
    return (1.0 - leakage) * mx + leakage * acc;
  }
  constexpr void addLoad(double cost) { load_ += cost; }
  constexpr void addStow(double cost) { stow_ += cost; }
  constexpr void addCompute(double cost) { comp_ += cost; }
  constexpr void addLoadStow(double cost) {
    load_ += cost;
    stow_ += cost;
  }
  constexpr void setLatency(double l) { latency_ = l; }
  constexpr auto operator*=(double f) -> Cost & {
    *this = *this * f;
    return *this;
  }

private:
  friend constexpr auto operator+(Cost a, Cost b) -> Cost {
    return {.load_ = a.load_ + b.load_,
            .stow_ = a.stow_ + b.stow_,
            .comp_ = a.comp_ + b.comp_,
            .latency_ = std::max(a.latency_, b.latency_)};
  }
  friend constexpr auto operator*(Cost c, double f) -> Cost {
    return {.load_ = f * c.load_,
            .stow_ = f * c.stow_,
            .comp_ = f * c.comp_,
            .latency_ = f * c.latency_};
  }
  friend constexpr auto operator*(double f, Cost c) -> Cost { return c * f; }
  friend constexpr auto operator/(Cost c, double d) -> Cost {
    return {.load_ = c.load_ / d,
            .stow_ = c.stow_ / d,
            .comp_ = c.comp_ / d,
            .latency_ = c.latency_ / d};
  }
};
/// Basic idea is that costs are divided by loops they do not depend on
/// So `indep_axes` is `1` for each axis it does not depend on.
///
constexpr auto cost(PtrVector<math::MultiplicativeInverse<double>> unrolls,
                    uint32_t indep_axes) -> double {
  // perhaps one way to calculate it would be to pre-take the product of all dep
  // trip counts, and then multiply by cld(trip_count, uf) for all indeps.
  // Currently, it is multiplying by all and then dividing by indep ufs.
  if (!indep_axes) return 1.0;
  uint32_t tz = std::countr_zero(indep_axes);
  double c{unrolls[tz++]};
  for (uint32_t d = indep_axes >> tz, i = tz; d; d >>= tz, i += tz) {
    tz = std::countr_zero(d);
    c *= static_cast<double>(unrolls[i + tz++]);
  }
  return c;
}
constexpr auto cost(PtrVector<int> unrolls, uint32_t deps) -> int {
  if (!deps) return 1;
  uint32_t tz = std::countr_zero(deps);
  int c{unrolls[tz++]};
  for (uint32_t d = deps >> tz, i = tz; d; d >>= tz, i += tz) {
    tz = std::countr_zero(d);
    c *= unrolls[i + tz++];
  }
  return c;
}

} // namespace CostModeling::Cost
