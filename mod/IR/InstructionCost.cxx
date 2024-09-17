#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <bit>
#include <cstddef>
#include <cstdint>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/InstructionCost.h>

#ifndef USE_MODULE
#include "Utilities/Invariant.cxx"
#else
export module InstructionCost;
import Invariant;
#endif

#ifdef USE_MODULE
export namespace IR::cost {
#else
namespace IR::cost {
#endif

constexpr size_t MaxVectorWidth = 128;
constexpr size_t log2MaxVectorWidth = std::countr_zero(MaxVectorWidth);
constexpr size_t NumberWidthsToCache = log2MaxVectorWidth + 1;

struct RecipThroughputLatency {
  enum State : uint8_t { NotComputed, Invalid, Valid };
  llvm::InstructionCost::CostType recip_throughput_;
  llvm::InstructionCost::CostType latency_;
  State state_{NotComputed};
  [[nodiscard]] constexpr auto isValid() const -> bool {
    return state_ == Valid;
  }
  [[nodiscard]] constexpr auto notYetComputed() const -> bool {
    return state_ == NotComputed;
  }
  constexpr RecipThroughputLatency(llvm::InstructionCost::CostType rt,
                                   llvm::InstructionCost::CostType l, State s)
    : recip_throughput_(rt), latency_(l), state_(s) {}
  static auto getInvalid() -> RecipThroughputLatency { return {0, 0, Invalid}; }
  RecipThroughputLatency(llvm::InstructionCost rt, llvm::InstructionCost l) {
    auto rtc = rt.getValue();
    auto lc = l.getValue();
    if (rtc && lc) {
      state_ = Valid;
      recip_throughput_ = *rtc;
      latency_ = *lc;
    } else state_ = Invalid;
  }
  constexpr RecipThroughputLatency() = default;
};

inline auto getType(llvm::Type *T, unsigned int vectorWidth) -> llvm::Type * {
  if (vectorWidth == 1) return T;
  return llvm::FixedVectorType::get(T, vectorWidth);
}

class VectorWidth {
  unsigned width_;
  unsigned log2_width_;

public:
  constexpr explicit VectorWidth(unsigned w)
    : width_(w), log2_width_(std::countr_zero(w)) {
    utils::invariant(std::popcount(w) == 1);
    utils::invariant(w <= MaxVectorWidth);
  }
  constexpr explicit VectorWidth(unsigned w, unsigned l2w)
    : width_(w), log2_width_(l2w) {
    utils::invariant(std::popcount(w) == 1);
    utils::invariant(int(l2w) == std::countr_zero(w));
    utils::invariant(w <= MaxVectorWidth);
  }

  [[nodiscard]] constexpr auto getWidth() const -> unsigned { return width_; }
  [[nodiscard]] constexpr auto getLog2Width() const -> unsigned {
    return log2_width_;
  }
};
// supports vector widths up to 128
class VectorizationCosts {
  llvm::InstructionCost::CostType costs_[8][2];
  RecipThroughputLatency::State valid_[8];

public:
  [[nodiscard]] constexpr auto
  get(unsigned l2w) const -> RecipThroughputLatency {
    utils::invariant(l2w <= log2MaxVectorWidth);
    if (valid_[l2w] == RecipThroughputLatency::Valid)
      return {costs_[l2w][0], costs_[l2w][1], valid_[l2w]};
    return RecipThroughputLatency::getInvalid();
  }
  struct ProxyReference {
    VectorizationCosts &vc_;
    unsigned l2w_;
    constexpr operator RecipThroughputLatency() const { return vc_.get(l2w_); }
    constexpr auto operator=(RecipThroughputLatency rtl) -> ProxyReference & {
      vc_.costs_[l2w_][0] = rtl.recip_throughput_;
      vc_.costs_[l2w_][1] = rtl.latency_;
      vc_.valid_[l2w_] = rtl.state_;
      return *this;
    }
  };
  constexpr auto operator[](unsigned l2w) -> ProxyReference {
    utils::invariant(l2w <= log2MaxVectorWidth);
    return {*this, l2w};
  }
  constexpr auto operator[](unsigned l2w) const -> RecipThroughputLatency {
    return get(l2w);
  }
  constexpr auto operator[](VectorWidth vw) -> ProxyReference {
    return {*this, vw.getLog2Width()};
  }
  constexpr auto operator[](VectorWidth vw) const -> RecipThroughputLatency {
    return get(vw.getLog2Width());
  }
};

} // namespace IR::cost