#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifndef USE_MODULE
#include "Bit/Float.cxx"
#include "Containers/TinyVector.cxx"
#include "IR/InstructionCost.cxx"
#include "Math/Array.cxx"
#include "Math/AxisTypes.cxx"
#include "Math/MultiplicativeInverse.cxx"
#include "Utilities/Invariant.cxx"
#else
export module CostModeling:Unroll;
import Array;
import BitHack;
import Invariant;
import InstructionCost;
import MultiplicativeInverse;
import TinyVector;
#endif

using math::PtrVector;

#ifdef USE_MODULE
export namespace CostModeling {
#else
namespace CostModeling {
#endif
using utils::invariant;

/// Order is outermost -> innermost
struct VectorizationFactor {
  uint32_t l2factor_{0};
  // trailing bit is outermost loop, so if iterating by shifting,
  // we go outer->inner
  uint32_t index_mask_{0};
  constexpr operator IR::cost::VectorWidth() const {
    return IR::cost::VectorWidth{unsigned(1) << l2factor_, l2factor_};
  }
  /// move the log2 into the exponent, and cast
  /// `double` is sign * exp2(exponent - 1023) * mantissa
  /// zero bits correspond to sign and mantissa = 1
  /// so we just set the exponent to log2 + 1023
  explicit constexpr operator double() const {
    return bit::exp2unchecked(l2factor_);
  }
  [[nodiscard]] constexpr auto mask() const -> uint32_t {
    utils::invariant(std::popcount(index_mask_) <= 1);
    return index_mask_;
  }
  constexpr auto dyndiv(double x) const -> double {
    return std::bit_cast<double>(std::bit_cast<int64_t>(x) -
                                 (static_cast<int64_t>(l2factor_) << 52));
  }

private:
  friend constexpr auto operator*(VectorizationFactor x, double y) -> double {
    return static_cast<double>(x) * y;
  }
  friend constexpr auto operator*(double x, VectorizationFactor y) -> double {
    return x * static_cast<double>(y);
  }
  friend constexpr auto operator/(double x, VectorizationFactor y) -> double {
    return std::ceil(std::bit_cast<double>(
      std::bit_cast<int64_t>(x) - (static_cast<int64_t>(y.l2factor_) << 52)));
  }
  friend constexpr auto cld(double x, VectorizationFactor y) -> double {
    return std::ceil(std::bit_cast<double>(
      std::bit_cast<int64_t>(x) - (static_cast<int64_t>(y.l2factor_) << 52)));
  }
};
/// Handles the stack of unrolls and vectorization factors for the current loop
struct Unrolls {
  // We use `double` as the scalar type.
  // The primary reason is to gracefully handle large products, e.g.
  // 1024**8 = (2**10)**8 = 2**80, which overflows with 64-bit integes
  // but happens to be exactly representable by a `double` (being a power of 2).
  // A secondary benefit is that `imul` is often slower than `vmulsd`, e.g. both
  // may have about 4 cycles of latency, but on e.g. Skylake through Golden Cove
  // CPUs, they have throughputs of 1/cycle vs 2/cycle.
  using S = double;
  using T = math::MultiplicativeInverse<S>;
  struct Loop {
    T unroll_;
    S trip_count_;
    [[nodiscard]] constexpr auto getTripCount() const -> S {
      return std::abs(trip_count_);
    }
    [[nodiscard]] constexpr auto knownTripCount() const -> bool {
      // returns `true` if negative, false otherwise
      return std::signbit(trip_count_ > 0.0);
    }
    /// Gives trip count divided by unroll factor (ignores vectorization)
    [[nodiscard]] constexpr auto unrolledIterCount() const -> S {
      S tc = getTripCount();
      return knownTripCount() ? cld(tc, unroll_) : tc * unroll_.inv();
    }
  };
  // order is outer<->inner, i.e. `unrolls_[0]` is outermost
  containers::TinyVector<Loop, 15> unrolls_;
  // only a single loop can be vectorized
  VectorizationFactor vf_;
  static_assert(std::is_trivially_default_constructible_v<Loop> &&
                std::is_trivially_destructible_v<Loop>);

  struct UnrollFactors {
    PtrVector<Loop> data_;
    constexpr auto operator[](ptrdiff_t i) const -> T {
      return data_[i].unroll_;
    }
  };

  struct TripCounts {
    PtrVector<Loop> data_;
    constexpr auto operator[](ptrdiff_t i) const -> S {
      return std::abs(data_[i].trip_count_);
    }
  };

  [[nodiscard]] constexpr auto unrolls() const -> UnrollFactors {
    return {{unrolls_.data(), math::length(unrolls_.size())}};
  }
  [[nodiscard]] constexpr auto tripCounts() const -> TripCounts {
    return {{unrolls_.data(), math::length(unrolls_.size())}};
  }
  constexpr void setVF(int l2v) {
    auto d0 = getDepth0();
    uint32_t mask = uint32_t(1) << d0;
    utils::invariant((l2v == 0) || ((vf_.index_mask_ & ~mask) == 0));
    if (l2v)
      vf_ = {.l2factor_ = static_cast<uint32_t>(l2v), .index_mask_ = mask};
    else if (vf_.index_mask_ == mask) vf_ = {};
  }
  [[nodiscard]] constexpr auto getUnroll() const -> T {
    utils::invariant(unrolls_.size() > 0);
    return unrolls_.back().unroll_;
  }
  [[nodiscard]] constexpr auto getTripCount() const -> S {
    utils::invariant(unrolls_.size() > 0);
    return unrolls_.back().getTripCount();
  }
  [[nodiscard]] constexpr auto knownTripCount() const -> bool {
    utils::invariant(unrolls_.size() > 0);
    return unrolls_.back().knownTripCount();
  }
  constexpr void pushUnroll(int unroll, ptrdiff_t trip_count, bool known_trip) {
    utils::invariant(unrolls_.size() < 15z);
    unrolls_.emplace_back(unroll, known_trip ? -trip_count : trip_count);
  }
  constexpr void popUnroll() { unrolls_.pop_back(); }
  constexpr void popUnroll(ptrdiff_t N) {
    invariant(N >= 0);
    invariant(N <= unrolls_.size());
    unrolls_.resize(unrolls_.size() - N);
  }
  constexpr auto popUnrollVal() -> Loop { return unrolls_.pop_back_val(); }
  [[nodiscard]] constexpr auto getDepth0() const -> ptrdiff_t {
    return getDepth1() - 1;
  }
  [[nodiscard]] constexpr auto getDepth1() const -> ptrdiff_t {
    return unrolls_.size();
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
    return unrolls_.size();
  }
  constexpr void push_back(Loop L) { unrolls_.push_back(L); }
  // `1` bits mean that we do not depend on that loop, and thus we divide trip
  // count by the corresponding unroll factor.
  // This gives the number of executions.
  // Note that vectorized always reduces call count, independent or not.
  // Vectorized calls themselves may be more expensive. The cost of the call
  // itself, by which this count-of-calls must be multiplied, must take
  // vectorization into account.
  [[nodiscard]] constexpr auto
  countIterationsIndependent(uint32_t indep_axes) const -> S {
    S c{1.0};
    uint16_t index_mask = vf_.index_mask_;
    // We use that cld(x, y*z) == cld(cld(x, y), z)
    for (Loop l : unrolls_) {
      S tc = l.getTripCount();
      if (l.knownTripCount()) {
        if (indep_axes & 1) tc = cld(tc, l.unroll_);
        if (index_mask & 1) tc = cld(tc, vf_);
      } else {
        if (indep_axes & 1) tc = tc * l.unroll_.inv();
        if (index_mask & 1) tc = vf_.dyndiv(tc);
      }
      c *= tc;
      indep_axes >>= 1;
      index_mask >>= 1;
    }
    return c;
  }
  // TODO: this is inefficient to constantly re-call
  [[nodiscard]] constexpr auto countIterations() const -> S {
    S c{1.0};
    uint16_t index_mask = vf_.index_mask_;
    // We use that cld(x, y*z) == cld(cld(x, y), z)
    for (Loop l : unrolls_) {
      S tc = l.getTripCount();
      tc = l.knownTripCount() ? cld(tc, l.unroll_) : tc * l.unroll_.inv();
      if (index_mask & 1)
        tc = l.knownTripCount() ? cld(tc, vf_) : vf_.dyndiv(tc);
      c *= tc;
      index_mask >>= 1;
    }
    return c;
  }
  [[nodiscard]] constexpr auto countHoistedIter() const -> S {
    S c{1.0};
    uint16_t index_mask = vf_.index_mask_;
    // We use that cld(x, y*z) == cld(cld(x, y), z)
    for (ptrdiff_t i = 0, L = unrolls_.size() - 1; i < L; ++i) {
      Loop l = unrolls_[i];
      double tc = l.getTripCount();
      tc = l.knownTripCount() ? cld(tc, l.unroll_) : tc * l.unroll_.inv();
      if (index_mask & 1)
        tc = l.knownTripCount() ? cld(tc, vf_) : vf_.dyndiv(tc);
      c *= tc;
      index_mask >>= 1;
    }
    return c;
  }
  [[nodiscard]] constexpr auto
  dependentUnrollProduct(uint32_t dep_axes) const -> S {
    S p{1.0};
    for (Loop l : unrolls_) {
      if (dep_axes & 1) p *= static_cast<double>(l.unroll_);
      dep_axes >>= 1;
    }
    return p;
  }
  [[nodiscard]] constexpr auto dependentUnrollProduct() const -> S {
    S p{1.0};
    for (Loop l : unrolls_) p *= static_cast<double>(l.unroll_);
    return p;
  }
  // Counts the total trip count of independent loops,
  // and asserts that a dependent loop is vectorized.
  // The reason we assert this is because it is currently only used for
  // packing-cost calculation to compensate for discontiguous loads/stores.
  [[nodiscard]] constexpr auto
  independentLoopIters(uint32_t dep_axes) const -> S {
    S c{1.0};
    uint16_t index_mask = vf_.index_mask_;
    for (Loop l : unrolls_) {
      uint32_t da = dep_axes, im = index_mask;
      dep_axes >>= 1;
      index_mask >>= 1;
      if (da & 1) continue;
      utils::invariant(!(im & 1));
      c *= l.unrolledIterCount();
    }
    return c;
  }
};

} // namespace CostModeling

template class containers::TinyVector<CostModeling::Unrolls::Loop, 15>;

