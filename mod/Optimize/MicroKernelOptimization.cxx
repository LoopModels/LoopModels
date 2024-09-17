#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Math/Constructors.cxx"
#include "Math/ManagedArray.cxx"
#include "Numbers/Int8.cxx"
#include "Optimize/BBCosts.cxx"
#include "Optimize/CacheOptimization.cxx"
#include "Optimize/Cost.cxx"
#include "Optimize/LoopTransform.cxx"
#include "Optimize/RegisterUse.cxx"
#include "Optimize/Unrolls.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>
#else
export module CostModeling:MicroKernel;
import Arena;
import ArrayConstructors;
import CacheModel;
import Int8;
import Invariant;
import IR;
import LeakyReluCost;
import LoopTransform;
import ManagedArray;
import Optional;
import STL;
import TargetMachine;
import :BasicBlock;
import :Cost;
import :Unroll;
#endif

using math::Vector, math::MutPtrVector, math::DensePtrMatrix;
using numbers::u8;
using utils::invariant;
#ifdef USE_MODULE
export namespace CostModeling::Hard {
#else
namespace CostModeling::Hard {
#endif

// For cache tiling, we want to ignore all outer non-reorderable loops (as we're
// not tiling them!), and all loops that don't have any arrays not-dependent
// upon them (no reuse).
struct SubCostFn {

  alloc::Arena<> *alloc_;
  // BBCosts state_;
  // for leaves, we need latency information
  target::CoreWidth corewidth_;
  Unrolls unroll_;
  Cache::CacheOptimizer::DepSummary *leafdepsummary_;
  containers::TinyVector<target::MachineCore::Cache, 4> caches_;
  int cachelinebits_;
  int register_count_;
  int l2maxvf_;
  int max_depth_{};
  int len_{};

  // auto operator()(PtrVector<LoopTransform> trfs) -> double { return 0.0; }
  // // implementing recursively, we want to maintain a stack
  // // can we create a non-recursive implementation?
  // // `trfs` is mutated to return optimal loop transforms
  struct OptResult {
    LoopSummaries loop_summaries_; ///< summary per loop
    BBCosts bb_costs_;             ///< cost per BB
    double best_cost_;
    double *phi_costs_;
  };
  // `best_cost` is the best total cost achieved; any search path that exceeds
  // it can stop early
  //
  // We have loop-specific infomation and state in `LoopTransform`s and
  // `LoopSummary`. We have BB-specific information and states in `BBCost`.
  // TODO: how to handle best_trfs?
  // NOLINTNEXTLINE(misc-no-recursion)
  auto optimize(OptResult entry_state) -> OptResult {
    auto [loopinfo, loop_summaries] = entry_state.loop_summaries_.popFront();
    double best_c_external = entry_state.best_cost_;
    int umax = loopinfo.reorderable() ? 16 : 1,
        l2vmax =
          (loopinfo.reorderable() && (!unroll_.vf_.index_mask_)) ? l2maxvf_ : 0;
    // LoopTransform *trf_ = loopinfo.trf_; // maybe null
    double best_c_internal{std::numeric_limits<double>::infinity()};
    int best_u = -1, best_l2v = -1, best_cuf = -1;
    OptResult ret;
    bool ret_set{false}, allocated_trfs{false};
    auto s = alloc_->scope();
    MutPtrVector<LoopTransform> best_trfs = loop_summaries.trfs_,
                                trfs = best_trfs;
    ptrdiff_t sts = loopinfo.reorderableSubTreeSize();
    double *phic = entry_state.phi_costs_, *best_phic = phic;
    //  we use `liveregcnt`, as it has the history
    u8 *liveregcnt = entry_state.bb_costs_.live_counts_,
       *best_liveregcnt = liveregcnt;
    // We need copies of all mutable state, this includes:
    // 1. LoopTransform
    // 2. live registers
    // TODO: 3. array-packing info?
    // TODO: We should return/have sub-tree sizes of each, to avoid need for
    // over-allocating or over-copying.
    for (int u = 0; u++ < umax;) {
      unroll_.pushUnroll(u, loopinfo.estimatedTripCount(),
                         loopinfo.knownTrip());
      for (int l2v = l2vmax;; l2v = 0) {
        // for (int l2v = l2vmax; l2v >= 0; --l2v) {
        unroll_.setVF(l2v);
        // We always pass `best_c_internal`, so the `optimize` calls on
        // sub-loops can quit if they exceed the best cost across `u` and `l2v`
        // values at this loop level.
        OptResult state = {
          .loop_summaries_ = {.loop_summaries_ = loop_summaries.loop_summaries_,
                              .trfs_ = trfs},
          .bb_costs_ = entry_state.bb_costs_,
          .best_cost_ = best_c_internal,
          .phi_costs_ = phic + 1};
        // TODO:
        // Add a lower bound cost estimate and check vs best. Lazily use LB as
        // 0? There may be cases where we can tell a priori that we have to
        // spill at least `X>0` registers, but that should be uncommon enough
        // (unless we get really fancy, maybe?) that we can implement that
        // later.
        //
        // we set at the top of iteration, so that they're incremented by end.
        // Similarly, `depth1_` field of `Unroll_` object gets incremented and
        // decremented in `u` loop
        // `optimize` has the original array-starts as local variables
        // Lets evaluate by BB, even the costs, instead of aggregating, as
        // otherwise the register spill costs can't be combined with the
        // `reduce` as well.
        double cur_c{0.0};
        {
          BBCost::ReductionExpansionBounds reduction_expansion{
            .upper_bound_ = double(unroll_.getUnroll())};
          for (ptrdiff_t i = 0, num_sub_loops = loopinfo.numSubLoops();; ++i) {
            // bb cost
            auto [cur_state, next_state] = state.bb_costs_.popFront();
            state.bb_costs_ = next_state;
            Cost::Cost c = cur_state.cost(unroll_, register_count_, i == 0,
                                          &reduction_expansion,
                                          double(corewidth_.comp_), phic);
            // clang-format off
            // to break here, use a command like:
            // br MicroKernelOptimization.cxx:150 if (((int)unroll_.unrolls_.len_)==3) && (unroll_.unrolls_[0].unroll_.divisor_ == 9) && (unroll_.unrolls_[1].unroll_.divisor_ == 3) && (unroll_.unrolls_[2].unroll_.divisor_ == 1) && (unroll_.vf_.index_mask_ == 2)
            // clang-format on
            if (i == num_sub_loops) {
              if (ptrdiff_t nreduct = loopinfo.numReductions()) {
                // this modifies `bb_costs_`, popping off `nreduct`
                auto reducts = state.bb_costs_.reductions(nreduct);
                auto [rex, uf] =
                  reduction_expansion.choose(double(unroll_.getUnroll()));
                // `unroll_.getUnroll() / rex` and `rex` are integers
                c.latency_ *= uf;
                if (rex > 1.0) {
                  auto L = unroll_.popUnrollVal();
                  // we have to decide whether we want to replicate this
                  // variable across unrolls, in which case we are forced to
                  // reduce in the end.
                  c.addCompute(compcosts(unroll_, reducts) * (rex - 1.0));
                  unroll_.push_back(L);
                }
              }
              cur_c += c.reduce(corewidth_);
              if (!ret_set) ret = state;
              ret_set = true;
              break;
            }
            cur_c += c.reduce(corewidth_);
            // eval subloop
            state = optimize(state);
            cur_c += std::exchange(state.best_cost_, best_c_internal);
            // TODO: reorganize code so we don't need `ret_set &&`?
            if (ret_set && cur_c > best_c_external) break;
          }
        }
        // we need `ret` to contain the tail of best_trfs
        utils::invariant(ret_set);
        if (cur_c >= best_c_external) {
          if (l2v) continue;
          else break;
        }
        if (cur_c < best_c_internal) {
          if (unroll_.size() == 1) {
            // we're the outer-most loop
            // TODO: redefine `last_iter_best` if cache opt makes no-longer best
            // What we need:
            // 1. phi-spill-costs - needs to be calculated up-front, conditional
            //    on u-params
            // 2. fill `DepSummary *leafdepsummary_`- calculated up front,
            //    independently of model parameters. There is one dep-summary
            //    per leaf.
            //
            CostModeling::Cache::CacheOptimizer co{.unrolls_ = {},
                                                   .caches_ = caches_,
                                                   .cachelinebits_ =
                                                     cachelinebits_,
                                                   .alloc_ = *alloc_};
            // FIXME: incongruity between entry_state.loop_summaries_, as
            // `cacheOptEntry` wants this outer-most loop, and the fact that we
            // should be passing in `trfs`, in `state` construction.
            LoopTransform trf{.l2vector_width_ = static_cast<uint32_t>(l2v),
                              .register_unroll_factor_ =
                                static_cast<uint32_t>(u - 1),
                              .cache_unroll_factor_ = 0,
                              .cache_permutation_ = 0};
            auto [best, dsnext] =
              co.cacheOpt(loopinfo, trf,
                          {.loop_summaries_ = loop_summaries.loop_summaries_,
                           .trfs_ = trfs},
                          phic, leafdepsummary_);
            cur_c = static_cast<double>(best.cost_ + cur_c);
            if (cur_c >= best_c_internal) {
              if (l2v) continue;
              else break;
            }
            best_cuf = best.cache_factor_;
          }
          best_c_internal = cur_c;
          best_u = u;
          best_l2v = l2v;
          invariant(trfs.size() - state.loop_summaries_.trfs_.size() == sts);
          ptrdiff_t nliveregcnt = entry_state.bb_costs_.interblock_reg_.size() -
                                  state.bb_costs_.interblock_reg_.size();
          if (allocated_trfs) {
            if (sts) {
              std::memcpy(best_trfs.data(), trfs.data(),
                          sts * sizeof(LoopTransform));
              std::memcpy(best_phic, phic, (sts + 1) * sizeof(double));
            }
            if (nliveregcnt)
              std::memcpy(best_liveregcnt, liveregcnt, nliveregcnt);
          } else if (l2v || u < umax) {
            // only skip if !l2v && u == umax
            allocated_trfs = true;
            if (sts) {
              trfs = math::vector<LoopTransform>(alloc_, sts);
              phic = alloc_->template allocate<double>(sts + 1);
            }
            if (nliveregcnt) liveregcnt = alloc_->allocate<u8>(nliveregcnt);
          }
          // best_trfs << trfs;
        }
        if (!l2v) break;
      }
      unroll_.popUnroll();
    }
    if (loopinfo.reorderable())
      entry_state.loop_summaries_.trfs_[0] = {
        .l2vector_width_ = static_cast<uint32_t>(best_l2v),
        .register_unroll_factor_ = uint32_t(best_u - 1),
        .cache_unroll_factor_ = static_cast<uint32_t>(best_cuf - 1)};
    invariant(ret_set);
    invariant(ret.bb_costs_.cost_counts_.size() <
              entry_state.bb_costs_.cost_counts_.size());
    ret.best_cost_ = best_c_internal;
    return ret;
  }
};
} // namespace CostModeling::Hard
