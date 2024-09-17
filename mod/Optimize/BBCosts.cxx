#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>

#ifndef USE_MODULE
#include "Containers/Pair.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#include "Math/Array.cxx"
#include "Math/Factor.cxx"
#include "Numbers/Int8.cxx"
#include "Optimize/Cost.cxx"
#include "Optimize/LoopTransform.cxx"
#include "Optimize/MemoryCost.cxx"
#include "Optimize/RegisterLife.cxx"
#include "Optimize/RegisterUse.cxx"
#include "Optimize/Unrolls.cxx"
#include "Target/Machine.cxx"
#else
export module CostModeling:BasicBlock;
import Array;
import Factor;
import Int8;
import IR;
import Pair;
import TargetMachine;
import :Cost;
import :MemoryCost;
import :RegisterLife;
import :RegisterUse;
import :Unroll;
#endif

#ifdef USE_MODULE
export namespace CostModeling {
#else
namespace CostModeling {
#endif
using containers::Pair;
using math::PtrVector, math::DensePtrMatrix;
using numbers::u8;

/// POD. Gives counts for the different kinds of costs.
/// Fields:
/// `bool known_trip`
/// `uint15_t trip_count`- we're unlikely to change decisions for >32k
///         negative indicates compile-time known size.
/// `uint16_t compute` number of compute.
/// `uint16_t omemory` number of orthogonal sets.
/// `uint16_t cmemory` number of mem sets.
/// `uint5_t exit` loop exit/entry.
/// `uint3_t l2vectorWidth` number of compute sets.
/// These give us info for iterating over the costs associated with a loop.
/// for (i : I){
///   for (j : J){
///     for (k : K){ // leaf
///       ...
///     }
///     for (k : K){ // leaf
///       ...
///     }
///   }
///   for (j : J){ // leaf
///     ...
///   }
/// }
/// For leaves, we compute latency as well as register cost.
/// Note that we compute all costs at the header for a given depth,
/// thus we only need headers and num-pops.
struct BasicBlockCostCounts {
  u8 latency_, n_orth_axes_, n_conv_axes_, n_comp_, n_intrablock_reg_,
    n_live_histories_;
  [[nodiscard]] constexpr auto nOrthAxes() const -> int {
    return int(n_orth_axes_);
  }
  [[nodiscard]] constexpr auto nConvAxes() const -> int {
    return int(n_conv_axes_);
  }
  [[nodiscard]] constexpr auto nCompAxes() const -> int { return int(n_comp_); }
  [[nodiscard]] constexpr auto numIntrablockCheckPoints() const -> int {
    return int(n_intrablock_reg_);
  }
  [[nodiscard]] constexpr auto numLiveHistories() const -> int {
    return int(n_live_histories_);
  }
  [[nodiscard]] constexpr auto latency() const -> double {
    return static_cast<double>(latency_);
  }
  void setLatency(llvm::InstructionCost cost) {
    auto val = cost.getValue();
    u8 latency = val && (*val <= 255) ? u8(*val) : u8(255);
    if (latency > latency_) latency_ = latency;
  }
};
static_assert(sizeof(BasicBlockCostCounts) == 6);

struct CompCost {
  uint16_t cost_;
  uint16_t mask_;
};
inline auto compcosts(Unrolls unrolls,
                      PtrVector<CompCost> compindep) -> double {
  double cc{0.0};
  // FIXME: scale by dependent axes instead
  // TODO: SIMD ;)
  for (auto [sf, ia] : compindep)
    cc += static_cast<double>(sf) * unrolls.dependentUnrollProduct(ia);
  return cc;
}

// Evaluate the cost for a BB
struct BBCost {
  BasicBlockCostCounts cost_counts_;
  // orthogonal axes and costs
  PtrVector<Cost::MemCostSummary> orth_axes_;
  // non-orthogonal axes and costs
  PtrVector<Pair<Cost::MemCostSummary, DensePtrMatrix<int64_t>>> conv_axes_;
  // compute cost summary
  PtrVector<CompCost> compute_independence_;
  PtrVector<IntraBlockRegisterUse> intrablock_reg_;
  PtrVector<Register::UsesAcrossBBs::LiveInfo> interblock_reg_;
  u8 *live_counts_;
  /// How often do we duplicate a reduction in registers?
  /// Duplicating a reduction in registers increases register use, and it
  /// also forces us to reduce use `r-1` instructions.
  /// When we call `cost` for a BB with latency,
  /// we narrow the upper bound to avoid register spills (to a minimum of 1),
  /// and increase the lower bound to avoid latency costs. In terms of cost
  /// handling, we:
  /// - Avoid scaling latency by the unroll. When we select the final expansion
  ///   factor, we scale latency up by unroll/factor. Note, we require this to
  ///   be an integer, i.e., if unrolling by `4`, we can expand by `1`, `2`, or
  ///   `4`, but not `3`.
  /// - Compute register costs using the upper bound. We do not retroactively
  ///   update old costs. Those old costs should have lowered the upper bound
  ///   appropriately to avoid penalties. The main potential issue here is that
  /// TODO: we currently don't count cost of spilling registers not used in this
  /// loop. It'd be good to handle this.
  struct ReductionExpansionBounds {
    // Selected to avoid spilling registers.
    double upper_bound_;
    // Selected to avoid lost throughput because of latency
    double lower_bound_{1};
    // We prefer the smallest value at least equal to the lower bound.
    // The upper bound is stronger than the lower bound, and imposes
    // a hard limit.
    [[nodiscard]] constexpr auto
    choose(double ub) const -> std::array<double, 2> {
      double rx = std::min(lower_bound_, upper_bound_);
      return math::lower_bound_factor(ub, rx);
    }
    constexpr void updateLowerBound(double throughput, double latency,
                                    double comp) {
      double tl = throughput * latency;
      if (tl > lower_bound_ * comp) lower_bound_ = std::ceil(tl / comp);
    }
    constexpr auto updateUpperBound(double ephemeral, double perennial,
                                    double register_count) -> double {
      // reg_expansion * pu + eu < register_count
      // reg_expansion < (register_count - eu) / pu
      double d = register_count - ephemeral;
      if (d < perennial * upper_bound_)
        upper_bound_ = d > perennial ? std::floor(d / perennial) : 1.0;
      return ephemeral + (perennial * upper_bound_);
    }
  };
  // cumulative_trip_count should be of previous/outer loops
  // Make sure our story on `cost` scaling by loop unroll
  // factors is straight! A consideration is that we want `cld`
  // on unroll factors to be correct. I.e., a trip count count of 17 with UF=4
  // means we have cld(17,4) = 5 trips.
  //
  // General approach:
  // costs shuold return total cost of a micro-kernel invocation,
  // then we scale by total number of microkenerl calls.
  [[nodiscard]] auto cost(const Unrolls &unroll, int register_count,
                          bool can_hoist, ReductionExpansionBounds *reb,
                          double comp_throughput,
                          double *phi_cost) const -> Cost::Cost {
    Cost::Cost c = memcosts(unroll, orth_axes_);
    c += memcosts(unroll, conv_axes_);
    c.addCompute(compcosts(unroll, compute_independence_));
    c.setLatency(cost_counts_.latency());
    reb->updateLowerBound(comp_throughput, c.latency_, c.comp_);
    double num_iters = unroll.countIterations();
    // reductions can't be added to comp costs above
    // because we need to add the `log2(invunrolls[1,depth0])` factor
    // to reducts.
    // TODO: `intraBlockRegUse` can have multiple check points,
    // and these each have unroll-ordered and unordered sets of registers.
    // We must sum penalities across check points, and take the maximum for
    // the spilling cost calculations that follow.
    double reg_use = 0.0, max_peren = 0.0;
    // TODO: store `ephem`
    for (auto rubu : intrablock_reg_) {
      double peren = rubu.perennialUse(unroll),
             ephem = rubu.ephemeralUse(unroll),
             ru = reb->updateUpperBound(ephem, peren, register_count);
      max_peren = std::max(max_peren, peren);
      reg_use = std::max(reg_use, ru);
    }
    *phi_cost = max_peren;
    double register_deficit = reg_use - register_count;
    if (register_deficit > 0.0)
      c.addLoadStow(unroll.dependentUnrollProduct() * register_deficit);
    register_deficit = std::min(register_deficit, 0.0);
    c *= num_iters;
    if (ptrdiff_t L = cost_counts_.numLiveHistories()) {
      double hoisted_trip_count =
        can_hoist ? unroll.countHoistedIter() : num_iters;
      for (ptrdiff_t i = 0; i < L; ++i) {
        Register::UsesAcrossBBs::LiveInfo li = interblock_reg_[i];
        int lc = 0;
        for (int j = 0; (j < 2) && ptrdiff_t(li.prev_idxs_[j]); ++j)
          lc += int(live_counts_[-ptrdiff_t(li.prev_idxs_[j])]);
        if (li.used_here_) {
          // must load all spilled
          double reg_per = unroll.dependentUnrollProduct(li.dep_mask_);
          double to_load =
            (int(li.total_count_ - li.additional_) * reg_per) - lc;
          invariant(to_load >= 0);
          c.addLoad(hoisted_trip_count * to_load);
          lc = int(li.total_count_) * int(reg_per);
        } else {
          // spill if excess
          register_deficit += lc;
          if (register_deficit > 0.0) {
            c.addStow(hoisted_trip_count * register_deficit);
            lc -= static_cast<int>(register_deficit);
            register_deficit = 0.0;
          }
          lc += li.additional_;
        }
        live_counts_[i] = u8(lc);
      }
    }
    return c;
  }
};
// Contains loop info and sub-info
// struct LoopCosts {};
struct BBCosts {
  // counts per loop, indicating how many of each of the following three
  // fields
  PtrVector<BasicBlockCostCounts> cost_counts_;
  // orthogonal axes and costs
  PtrVector<Cost::MemCostSummary> orth_axes_;
  // non-orthogonal axes and costs
  PtrVector<Pair<Cost::MemCostSummary, DensePtrMatrix<int64_t>>> conv_axes_;
  // compute cost summary
  PtrVector<CompCost> compute_independence_;
  PtrVector<IntraBlockRegisterUse> intrablock_reg_;
  PtrVector<Register::UsesAcrossBBs::LiveInfo> interblock_reg_;
  u8 *live_counts_;

  [[nodiscard]] auto popFront() const -> Pair<BBCost, BBCosts> {
    auto [bbcc, cost_counts_remainder] = cost_counts_.popFront();
    auto [orth_axes, orth_remainder] = orth_axes_.split(bbcc.nOrthAxes());
    auto [conv_axes, conv_remainder] = conv_axes_.split(bbcc.nConvAxes());
    auto [comp_indp, comp_remainder] =
      compute_independence_.split(bbcc.nCompAxes());

    auto [intrablock, intrablock_remainder] =
      intrablock_reg_.split(bbcc.numIntrablockCheckPoints());
    uint32_t bb_live_counts = cost_counts_.front().numLiveHistories();
    auto [livereg, livereg_remainder] = interblock_reg_.split(bb_live_counts);

    return {{bbcc, orth_axes, conv_axes, comp_indp, intrablock, livereg,
             live_counts_},
            {cost_counts_remainder, orth_remainder, conv_remainder,
             comp_remainder, intrablock_remainder, livereg_remainder,
             live_counts_ + bb_live_counts}};
  }
  [[nodiscard]] auto reductions(ptrdiff_t nreduct) -> PtrVector<CompCost> {
    auto [comp_indp, comp_remainder] = compute_independence_.split(nreduct);
    compute_independence_ = comp_remainder;
    return comp_indp;
  }
};

// Note that cost-counts start at blk_idx `0`, because it excludes
// the first top-level block.
template <bool TTI>
inline void
reductionLatency(IR::Value *v,
                 MutPtrVector<CostModeling::BasicBlockCostCounts> cost_counts,
                 target::Machine<TTI> target, unsigned vector_width) {
  using CostKind = llvm::TargetTransformInfo::TargetCostKind;
  llvm::InstructionCost latency{0};
  int blk = 0; // we ignore latency of blk `0`
  for (IR::Instruction *d = v->getReductionDst();; d = d->getReductionDst()) {
    if (int cidx = d ? d->getBlkIdx() : -1; cidx != blk) {
      if (blk) cost_counts[blk - 1].setLatency(latency);
      if (!d) return;
      invariant(cidx >= 0);
      blk = cidx;
      latency = 0;
    }
    if (auto *c = llvm::dyn_cast<IR::Compute>(d))
      latency += c->calcCost(target, vector_width, CostKind::TCK_Latency);
  }
}

} // namespace CostModeling
