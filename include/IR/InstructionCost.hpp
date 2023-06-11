#pragma once
#include <Utilities/Invariant.hpp>
#include <bit>
#include <cstdint>
#include <llvm/Support/InstructionCost.h>

namespace poly::IR::cost {

constexpr size_t MaxVectorWidth = 128;
constexpr size_t log2MaxVectorWidth = std::countr_zero(MaxVectorWidth);
constexpr size_t NumberWidthsToCache = log2MaxVectorWidth + 1;

struct RecipThroughputLatency {
  enum State { NotComputed, Invalid, Valid };
  llvm::InstructionCost::CostType recipThroughput;
  llvm::InstructionCost::CostType latency;
  State state{NotComputed};
  [[nodiscard]] constexpr auto isValid() const -> bool {
    return state == Valid;
  }
  [[nodiscard]] constexpr auto notYetComputed() const -> bool {
    return state == NotComputed;
  }
  constexpr RecipThroughputLatency(llvm::InstructionCost::CostType rt,
                                   llvm::InstructionCost::CostType l, State s)
    : recipThroughput(rt), latency(l), state(s) {}
  static auto getInvalid() -> RecipThroughputLatency { return {0, 0, Invalid}; }
  RecipThroughputLatency(llvm::InstructionCost rt, llvm::InstructionCost l) {
    auto rtc = rt.getValue();
    auto lc = l.getValue();
    if (rtc && lc) {
      state = Valid;
      recipThroughput = *rtc;
      latency = *lc;
    } else state = Invalid;
  }
  constexpr RecipThroughputLatency() = default;
};

inline auto getType(llvm::Type *T, unsigned int vectorWidth) -> llvm::Type * {
  if (vectorWidth == 1) return T;
  return llvm::FixedVectorType::get(T, vectorWidth);
}

struct VectorWidth {
  unsigned width;
  unsigned log2Width;
  constexpr VectorWidth(unsigned w) : width(w), log2Width(std::countr_zero(w)) {
    utils::invariant(std::popcount(w) == 1);
    utils::invariant(w <= MaxVectorWidth);
  }
  constexpr VectorWidth(unsigned w, unsigned l2w) : width(w), log2Width(l2w) {
    utils::invariant(std::popcount(w) == 1);
    utils::invariant(int(l2w) == std::countr_zero(w));
    utils::invariant(w <= MaxVectorWidth);
  }
};
// supports vector widths up to 128
class VectorizationCosts {
  llvm::InstructionCost::CostType costs[8][2];
  RecipThroughputLatency::State valid[8];

public:
  [[nodiscard]] constexpr auto get(unsigned l2w) const
    -> RecipThroughputLatency {
    utils::invariant(l2w <= log2MaxVectorWidth);
    if (valid[l2w] == RecipThroughputLatency::Valid)
      return {costs[l2w][0], costs[l2w][1], valid[l2w]};
    return RecipThroughputLatency::getInvalid();
  }
  struct ProxyReference {
    VectorizationCosts &vc;
    unsigned l2w;
    constexpr operator RecipThroughputLatency() const { return vc.get(l2w); }
    constexpr auto operator=(RecipThroughputLatency rtl) -> ProxyReference & {
      vc.costs[l2w][0] = rtl.recipThroughput;
      vc.costs[l2w][1] = rtl.latency;
      vc.valid[l2w] = rtl.state;
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
    return {*this, vw.log2Width};
  }
  constexpr auto operator[](VectorWidth vw) const -> RecipThroughputLatency {
    return get(vw.log2Width);
  }
};

} // namespace poly::IR::cost
