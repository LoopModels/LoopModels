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
constexpr auto requiredScheduleStorage(unsigned nL) -> unsigned {
  // layout:
  // [0: 1): nL (numLoops)
  // [1: nL * nL + 1): Phi
  // [nL * nL + 1: nL * nL + nL + 2): fusion omega
  // [nL * nL + nL + 2: nL * nL + 2 * nL + 2): offset omega
  return nL * (nL + 2) + 2; // * sizeof(int64_t);
}
struct AffineSchedule {

  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return unsigned(mem[0]);
  }
  [[nodiscard]] constexpr auto getNumLoopsSquared() const -> size_t {
    size_t numLoops = getNumLoops();
    return numLoops * numLoops;
  }

  constexpr AffineSchedule() : mem(nullptr) {}
  constexpr AffineSchedule(int64_t *m) : mem(m) {}
  constexpr AffineSchedule(BumpAlloc<> &alloc, unsigned nL)
    : mem(alloc.allocate<int64_t>(requiredScheduleStorage(nL))) {
    mem[0] = nL;
  }
  constexpr auto copy(BumpAlloc<> &alloc) const -> AffineSchedule {
    size_t reqMem = requiredScheduleStorage(getNumLoops());
    AffineSchedule res{alloc.allocate<int64_t>(reqMem)};
    std::copy_n(mem, reqMem, res.mem);
    return res;
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
  /// getSchedule, loops are always indexed from outer to inner
  [[nodiscard]] constexpr auto getSchedule(size_t d) const
    -> PtrVector<int64_t> {
    return getPhi()(d, _);
  }
  [[nodiscard]] constexpr auto getSchedule(size_t d) -> MutPtrVector<int64_t> {
    return getPhi()(d, _);
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
  int64_t *mem;
};
