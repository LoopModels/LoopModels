#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include "Containers/Pair.cxx"
#include "Math/Array.cxx"
#include "Math/Indexing.cxx"
#include "Utilities/Invariant.cxx"
#include <cstddef>
#include <cstdint>
#else
export module LoopTransform;
import Array;
import Invariant;
import Pair;
import STL;
#endif

#ifndef USE_MODULE
namespace CostModeling {
#else
export namespace CostModeling {
#endif
using math::PtrVector, math::MutPtrVector, math::end, math::_, containers::Pair;

struct LoopTransform {
  uint32_t l2vector_width_ : 4; // 1<<15 =
  // reg unroll factor is this value + 1, valid values 1...16
  uint32_t register_unroll_factor_ : 4;
  // cache unroll factor is this (value + 1) * reg unroll factor *
  // (1<<l2vectorWidth)
  uint32_t cache_unroll_factor_ : 20;
  uint32_t cache_permutation_ : 4 {0xf};
  [[nodiscard]] constexpr auto vector_width() const -> int32_t {
    // Initialized to 15, so this causes failures
    utils::invariant(l2vector_width_ != 15);
    return 1 << l2vector_width_;
  }
  [[nodiscard]] constexpr auto reg_unroll() const -> int32_t {
    return register_unroll_factor_ + 1;
  }
  [[nodiscard]] constexpr auto reg_factor() const -> int32_t {
    return vector_width() * reg_unroll();
  }
  [[nodiscard]] constexpr auto cache_unroll() const -> int32_t {
    return cache_unroll_factor_ + 1;
  }
  [[nodiscard]] constexpr auto cache_perm() const -> int32_t {
    return cache_permutation_;
  }
};
static_assert(sizeof(LoopTransform) == 4);
struct LoopSummary {
  uint32_t reorderable_ : 1;
  uint32_t known_trip_ : 1;
  uint32_t reorderable_sub_tree_size_ : 14;
  uint32_t num_reduct_ : 8;
  uint32_t num_sub_loops_ : 8;
  uint32_t trip_count_ : 32;
  [[nodiscard]] constexpr auto reorderable() const -> bool {
    return reorderable_;
  }
  [[nodiscard]] constexpr auto estimatedTripCount() const -> ptrdiff_t {
    return ptrdiff_t(trip_count_);
  }
  [[nodiscard]] constexpr auto numSubLoops() const -> ptrdiff_t {
    return ptrdiff_t(num_sub_loops_);
  }
  [[nodiscard]] constexpr auto numReductions() const -> ptrdiff_t {
    return ptrdiff_t(num_reduct_);
  }
  [[nodiscard]] constexpr auto reorderableSubTreeSize() const -> ptrdiff_t {
    return ptrdiff_t(reorderable_sub_tree_size_);
  }
  [[nodiscard]] constexpr auto reorderableTreeSize() const -> ptrdiff_t {
    return reorderableSubTreeSize() + reorderable();
  }
  [[nodiscard]] constexpr auto knownTrip() const -> bool { return known_trip_; }
};
static_assert(sizeof(LoopSummary) == 8);

struct LoopSummaries {
  PtrVector<LoopSummary> loop_summaries_;
  MutPtrVector<LoopTransform> trfs_;
  constexpr auto popFront() -> Pair<LoopSummary, LoopSummaries> {
    auto [ls, ls_remainder] = loop_summaries_.popFront();
    return {.first = ls,
            .second = {.loop_summaries_ = loop_summaries_[_(1, end)],
                       .trfs_ = trfs_[_(ls.reorderable_, end)]}};
  }
};

} // namespace CostModeling
