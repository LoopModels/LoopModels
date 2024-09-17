#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Support/Casting.h>
#include <type_traits>

#ifndef USE_MODULE
#include "Optimize/Unrolls.cxx"
#include "Containers/TinyVector.cxx"
#include "Support/Permutation.cxx"
#include "Dicts/Linear.cxx"
#include "IR/IR.cxx"
#include "Utilities/Invariant.cxx"
#include "Numbers/Int8.cxx"
#include "Graphs/IndexGraphs.cxx"
#include "Math/ElementarySIMD.cxx"
#include "Math/Constructors.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#else
export module CostModeling:RegisterUse;
import Arena;
import Array;
import ArrayConstructors;
import Elementary;
import IndexGraph;
import Int8;
import Invariant;
import IR;
import LinearDict;
import Permutation;
import TinyVector;
import :Unroll;
#endif

#ifdef USE_MODULE
export namespace CostModeling {
#else
namespace CostModeling {
#endif
using numbers::i8;
using utils::invariant, math::AbstractMatrix, math::PtrVector,
  math::MutPtrVector, math::end, math::_;

struct MaskCoefs {
  uint16_t mask_, coef_;
};

// We need to define an unroll ordering.
class IntraBlockRegisterUse {
  using Order = containers::TinyVector<i8, 15, int8_t>;
  // `perms` is the set of all unroll orders worth considering.
  // One of these is guaranteed to minimize register use as a function
  // of the unrolling factors.
  utils::LoopPermutations perms_{};
  PtrVector<MaskCoefs> mask_coefs_; // mask, coef pairs
  ptrdiff_t num_temp_;
  // unsigned register_count_; // includes constant offset
  [[nodiscard]] constexpr auto
  ephemeralMaskCoefs() const -> PtrVector<MaskCoefs> {
    return mask_coefs_[_(0, num_temp_)];
  }
  [[nodiscard]] constexpr auto
  perennialMaskCoefs() const -> PtrVector<MaskCoefs> {
    return mask_coefs_[_(num_temp_, end)];
  }

  [[nodiscard]] static constexpr auto
  registerConsumption(Order order, uint32_t dep_mask,
                      const Unrolls &unrolls) -> double {
    // depMask bits go from [0,...,inner,...,outer]
    // i.e. `0` is the outermost loop.
    // The `order` itself goes from outer<->inner unroll order
    // e.g., order = [2,0,1] means the innermost loop (loop 2) is the outermost
    // unroll, while the middle loop (loop 1) is the innermost.
    //
    // The idea of how this works is that the register use is the
    // product of all unroll factors the instruction depends on that are
    // interior to an unroll factor it does *not* depend on.
    //
    // As we can ignore all unrolls exterior the outermost independent uf,
    // we shift by `trailing_ones`, and take the product of dependent
    // ufs from there.
    // We shift/increment by an extra `1`, as the first non-zero bit
    // is (obviously) non-zero, and thus would be skipped in the loop anyway.
    //
    // Example, 3 loops (outer->inner): m, n, k
    // order = [k,m,n] = [2,0,1]
    // depmask 5 = 000000101, e.g. A[m,k]
    // after skip, d == 3 > pop == 2 -> return 1.0
    // depmask 6 = 000000110, e.g. B[k,n]
    // after skip, d == 2
    // rpop = 0, so we return r = unrolls()[order[2]], i.e. `n`'s unroll
    // depmask 3 = 000000011, e.g. C[m,n]
    // after skip, d == 1
    // rpop = 1, so we return
    // r = unrolls()[order[1]] * unrolls()[order[2]]
    invariant(dep_mask != 0);
    ptrdiff_t pop = std::popcount(dep_mask), D = order.size();
    invariant(D >= pop);
    if (D == pop) return 1.0;
    ptrdiff_t d = 0;
    // skip all the outermost unrolls
    for (;;)
      if (!((1 << int(order[d++])) & dep_mask)) break;
    if (d > pop) return 1.0;
    double r{1.0};
    for (ptrdiff_t rpop = pop - d;; ++d) {
      int i = int(order[d]);
      if (!((1 << i) & dep_mask)) continue;
      r *= static_cast<double>(unrolls.unrolls()[i]);
      if (rpop-- == 0) return r;
    }
  }

public:
  // TODO (maybe): return all, rather than just peak?
  [[nodiscard]] constexpr auto
  ephemeralUse(const Unrolls &unrolls) const -> double {
    if (perms_.empty()) return 0.0;
    // we want the minimum register use across orders
    // so we use maximum across remaining registers
    double acc{std::numeric_limits<double>::max()};
    invariant(num_temp_ > 0);
    for (Order order : perms_) {
      double ao{0.0};
      for (auto [m, c] : ephemeralMaskCoefs())
        ao += static_cast<double>(c) * registerConsumption(order, m, unrolls);
      acc = std::min(acc, ao);
    }
    return acc;
  }
  [[nodiscard]] constexpr auto
  perennialUse(const Unrolls &unrolls) const -> double {
    double acc{0.0};
    for (auto [m, c] : perennialMaskCoefs())
      acc += c * unrolls.dependentUnrollProduct(m);
    return acc;
  }

  IntraBlockRegisterUse(
    alloc::Arena<> *alloc,
    const dict::Linear<uint16_t, uint16_t> &ephemeral_mask_coefs,
    const dict::Linear<uint16_t, uint16_t> &perennial_mask_coefs,
    int16_t depth1) {
    utils::IndexRelationGraph ind_dep_graph{depth1};
    ptrdiff_t n_intra = ephemeral_mask_coefs.size(),
              n_inter = perennial_mask_coefs.size();
    MutPtrVector<MaskCoefs> mask_coefs =
      math::vector<MaskCoefs>(alloc, n_intra + n_inter);
    PtrVector<uint16_t> keys = ephemeral_mask_coefs.keys(),
                        vals = ephemeral_mask_coefs.values();
    for (ptrdiff_t i = 0; i < n_intra; ++i) {
      auto m = keys[i];
      auto c = vals[i];
      invariant(m < (1 << depth1));
      mask_coefs[i] = {m, c};
      for (uint16_t a :
           utils::LoopSet::fromMask(utils::flipMask(m, uint16_t(depth1))))
        ind_dep_graph.add_edges(a, utils::LoopSet::fromMask(m));
    }
    keys = perennial_mask_coefs.keys();
    vals = perennial_mask_coefs.values();
    for (ptrdiff_t i = 0; i < n_inter; ++i)
      mask_coefs[n_intra + i] = {keys[i], vals[i]};
    num_temp_ = n_intra;
    mask_coefs_ = mask_coefs;
    // TODO: can we prove that this produces results where the earliest SCCs
    // are always worse, and should therefore be placed outside of inner SCCs?
    if (n_intra)
      graph::stronglyConnectedComponents(perms_.subperms_, ind_dep_graph);
  }
  constexpr IntraBlockRegisterUse() = default;
  constexpr IntraBlockRegisterUse(const IntraBlockRegisterUse &) = default;
  constexpr auto
  operator=(const IntraBlockRegisterUse &) -> IntraBlockRegisterUse & = default;
};
static_assert(std::is_trivially_copyable_v<IntraBlockRegisterUse>);
static_assert(std::is_trivially_destructible_v<IntraBlockRegisterUse>);

} // namespace CostModeling