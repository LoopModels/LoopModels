#pragma once

#include "Math/Math.hpp"
#include "Utilities/Allocators.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/User.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>

/// We represent a schedule as
/// Phi_s'*i + omega_s <_{lex} Phi_t'*s + Omega_t
/// means that schedule `s` executes before schedule `t`.
///
/// S_0 = {Phi_0, omega_0}
/// S_1 = {Phi_1, omega_1}
/// given i_0 and i_1, if
/// Phi_0 * i_0 + omega_0 << Phi_1 * i_1 + omega_1
/// then "i_0" for schedule "S_0" happens before
/// "i_1" for schedule "S_1"
///
constexpr auto requiredScheduleStorage(unsigned n) -> unsigned {
  return (n * (n + 2) + 1) * sizeof(int64_t);
}
struct AffineSchedule {

  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return unsigned(mem[0]);
  }
  [[nodiscard]] constexpr auto getNumLoopsSquared() const -> size_t {
    size_t numLoops = getNumLoops();
    return numLoops * numLoops;
  }

  static auto construct(BumpAlloc<> &alloc, unsigned nL)
    -> NotNull<AffineSchedule> {
    auto *mem =
      alloc.allocate(requiredScheduleStorage(nL), alignof(AffineSchedule));
    return new (mem) AffineSchedule(nL);
  }
  constexpr void truncate(size_t newNumLoops) {
    size_t numLoops = getNumLoops();
    if (newNumLoops < numLoops) {
      int64_t *data = mem + 1;
      size_t oOffset = getNumLoopsSquared() + size_t(numLoops) - newNumLoops;
      size_t nOffset = newNumLoops * newNumLoops;
      for (size_t i = 0; i < newNumLoops; ++i)
        data[i + nOffset] = data[i + oOffset];
      numLoops = newNumLoops;
    }
    getPhi().diag() << 1;
  }
  [[nodiscard]] constexpr auto data() const -> int64_t * {
    return const_cast<int64_t *>(mem + 1);
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getPhi() -> MutSquarePtrMatrix<int64_t> {
    return {data(), SquareDims{unsigned(getNumLoops())}};
  }
  [[nodiscard]] constexpr auto getPhi() const -> SquarePtrMatrix<int64_t> {
    return {data(), SquareDims{getNumLoops()}}; //
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) const
    -> PtrVector<int64_t> {
    return getPhi()(last - d, _);
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) -> MutPtrVector<int64_t> {
    return getPhi()(last - d, _);
  }
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) const -> int64_t {
    return data()[getNumLoopsSquared() + i];
  }
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) const -> int64_t {
    return data()[getNumLoopsSquared() + getNumLoops() + 1 + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega(size_t i) -> int64_t & {
    return data()[getNumLoopsSquared() + i];
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega(size_t i) -> int64_t & {
    return data()[getNumLoopsSquared() + getNumLoops() + 1 + i];
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return {data() + getNumLoopsSquared(), getNumLoops() + 1};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {data() + getNumLoopsSquared() + getNumLoops() + 1, getNumLoops()};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return {data() + getNumLoopsSquared(), getNumLoops() + 1};
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {data() + getNumLoopsSquared() + getNumLoops() + 1, getNumLoops()};
  }
  [[nodiscard]] constexpr auto fusedThrough(const NotNull<AffineSchedule> y,
                                            const size_t numLoopsCommon) const
    -> bool {
    const auto *o = getFusionOmega().begin();
    return std::equal(o, o + numLoopsCommon, y->getFusionOmega().begin());
  }
  [[nodiscard]] constexpr auto
  fusedThrough(const NotNull<AffineSchedule> y) const -> bool {
    return fusedThrough(y, std::min(getNumLoops(), y->getNumLoops()));
  }

private:
  constexpr AffineSchedule(unsigned numLoops) { mem[0] = numLoops; }
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#pragma clang diagnostic ignored "-Wgnu-empty-struct"
#endif
  int64_t mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
};
static_assert(sizeof(AffineSchedule) == 0);
