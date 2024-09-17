#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <algorithm>
#include <array>
#include <cstddef>
#include <ostream>

#ifndef USE_MODULE
#include "IR/Address.cxx"
#include "IR/Node.cxx"
#include "Math/Array.cxx"
#include "Math/AxisTypes.cxx"
#else
export module IR:Phi;
import Array;
import :Address;
import :Node;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif

/// Our Phi are simple.
/// for (ptrdiff_t m = 0; m < M; ++m){
///   xm = 0.0; // or xm = x[m];
///   for (ptrdiff_t n = 0; n < N; ++n)
///     xm += A[m, n] * y[n];
///   x[m] = xm;
/// }
/// We would have
/// %0 = 0.0
/// %1 = loopstart
///   // or %xinit = x[%1]
///   %2 = loopstart
///     %3 = Phi(%0, %7)
///     %4 = A[%1, %2]
///     %5 = y[%2]
///     %6 = %4 * %5
///     %7 = %3 + %6
///   x[m] = %3
///
/// `getOperand(0)` if no trips completed,
/// `getOperand(1)` otherwise.
/// Or, a double-reduction example:
/// for (ptrdiff_t m = 0; m < M; ++m){
///   xm = 0.0; // or xm = x[m];
///   for (ptrdiff_t n = 0; n < N; ++n)
///     for (ptrdiff_t k = 0; k < K; ++k)
///       xm += A[m, n, k] * y[n, k];
///   x[m] = xm;
/// }
/// We would have
/// %0 = 0.0
/// %1 = loopstart
///   // or %xinit = x[%1]
///   %2 = loopstart
///     %3 = Phi(%0, %10) // accu - loopmask = 0x01
///     %4 = loopstart
///       %5 = Phi(%3, %9) // accu - loopmask = 0x01
///       %6 = A[%1, %2, %4]
///       %7 = y[%2, %4]
///       %8 = %6 * %7
///       %9 = %5 + %8
///     %10 = Phi(%3, %9) // join
///   %11 = Phi(%0, %10) // join
///   x[m] = %11
class Phi : public Instruction {
  std::array<Value *, 2> operands_;

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_PhiN;
  }
  [[nodiscard]] constexpr auto isAccumPhi() const -> bool {
    return getCurrentDepth() == operands_[1]->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto isJoinPhi() const -> bool {
    return !isAccumPhi();
  }
  /// places `Phi(a, b)` in `L`
  /// `a` is assumed to be a hoisted initializer, and `b`
  /// The loop mask excludes the current and deeper loops, as it is not
  /// unrolled with respect to any of these!
  /// This sets `getOperands()` to `a` and `b->getStoredVal()`,
  /// but does not update the users of the oeprands;
  /// that is the responsibility of the `IR::Cache` object.
  constexpr Phi(Addr *a, Addr *b, Loop *L)
    : Instruction(VK_PhiN, L->getCurrentDepth(),
                  (a->loopMask() | b->loopMask()), a->getType()),
      operands_{a, b->getStoredVal()} {
    invariant((this->loopdeps & (~((1 << (L->getCurrentDepth() - 1)) - 1))) ==
              0);
  };
  constexpr auto getOperands() -> math::MutPtrVector<Value *> {
    return {operands_.data(), math::length(2z)};
  }
  [[nodiscard]] constexpr auto getOperands() const -> math::PtrVector<Value *> {
    return {operands_.data(), math::length(2z)};
  }
  [[nodiscard]] constexpr auto getOpArray() const -> std::array<Value *, 2> {
    return operands_;
  }
  [[nodiscard]] constexpr auto getOperand(ptrdiff_t i) const -> Value * {
    return operands_[i];
  }
  constexpr void setOperands(math::PtrVector<Value *> ops) {
    invariant(ops.size(), 2z);
    std::copy_n(ops.begin(), 2, operands_.begin());
  }
  [[nodiscard]] constexpr auto isReassociable() const -> bool {
    return getReductionDst() != nullptr;
  }
  auto dump(std::ostream &os) const -> std::ostream & {
    printName(os) << " = \u03d5(";
    operands_[0]->printName(os) << ", ";
    operands_[1]->printName(os) << ")";
    return os;
  }
};

} // namespace IR
