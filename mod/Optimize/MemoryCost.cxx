#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#ifndef USE_MODULE
#include "Containers/BitSets.cxx"
#include "Containers/Pair.cxx"
#include "IR/Address.cxx"
#include "IR/OrthogonalAxes.cxx"
#include "Math/Array.cxx"
#include "Math/GreatestCommonDivisor.cxx"
#include "Math/Indexing.cxx"
#include "Optimize/Cost.cxx"
#include "Optimize/Unrolls.cxx"
#include "Polyhedra/DependencyPolyhedra.cxx"
#include "Utilities/Invariant.cxx"
#else
export module CostModeling:MemoryCost;
import Array;
import BitSet;
import GCD;
import Invariant;
import IR;
import OrthogonalAxes;
import Pair;
import Range;
import :Cost;
import :Unroll;
#endif

#ifdef USE_MODULE
export namespace CostModeling::Cost {
#else
namespace CostModeling::Cost {
#endif

using math::PtrVector, math::DensePtrMatrix, containers::Pair, math::_;

/// TODO: maybe two `uint8_t`s + `uint16_t`
/// We only get up to 16 dimensions, but that is already excessive
/// One `uint8_t` gives contig axis, the other the index into
/// the memory cost kind. Thus, the struct could differentiate
/// loads vs stores by itself, while also differentiating
/// between eltypes.
/// Another option is to store individual `MemoryCosts`,
/// so that we can aggregate/sum up.
struct MemCostSummary {
  std::array<IR::Addr::Costs, 2> loadstowcost_;
  IR::OrthogonalAxes orth_;
  // [[nodiscard]] constexpr auto contigAxis() const -> uint32_t {
  //   return data & 0xstoref;
  // }
  // mask containing `0` for dependent axes, 1s for independent
  // should contain `0` for all non-existent loops, e.g.
  // for (i = I, j = J, k = K, l = L) {
  //   A[j,l]
  //   for (a = A, b = B){ .... }
  // }
  // The mask should equal (1<<0) | (1<<2)  (for the i and k).
  // Only loops it is nested in that it doesn't depend on count.
  // private:
  //   friend constexpr auto operator&(MemCostSummary a,
  //                                   MemCostSummary b) -> uint32_t {
  //     return a.orth_.indep & b.orth_.indep;
  //   }
};

// costs is an array of length two.
// memory costs, unnormalized by `prod(unrolls)`
// `invunrolls` is a matrix, row-0 are the inverse unrolls, row-1 unrolls.
// TODO: add alignment to `MemCostSummary`
constexpr auto cost(Unrolls unrolls, MemCostSummary mcs) -> Cost {
  auto [mc, orth] = mcs;
  double c{unrolls.dependentUnrollProduct(orth.dep_)};
  double l{1.0}, s{1.0};
  VectorizationFactor vf = unrolls.vf_;
  if (orth.dep_ & vf.index_mask_) {
    // depends on vectorized index
    if (vf.index_mask_ & orth.contig_) {
      // TODO: misalignment penality?
      l = mc[0].contig_;
      s = mc[1].contig_;
    } else if (!orth.contig_) { // there is no contiguous axis
      l = mc[0].noncon_;
      s = mc[1].noncon_;
    } else {
      // Discontiguous vector load, but a contiguous axis exists.
      // We consider three alternatives:
      // 1. gather/scatter (discontiguous)
      // 2. contiguous load for each vectorization factor of length equal to
      // 3. hoist packing/unpacking, contiguous load/stores
      // unroll, followed by shuffles.
      // E.g., unroll contig by 4, another dim is vectorized by 8:
      // we'd have 8 vloads (max(4/8,1) * 8), followed by 4*log2(8) shuffles.
      // w_0 = [0,  8, 16, 24]
      // w_1 = [1,  9, 17, 25]
      // w_2 = [2, 10, 18, 26]
      // w_3 = [3, 11, 19, 27]
      // w_4 = [4, 12, 20, 28]
      // w_5 = [5, 13, 21, 29]
      // w_6 = [6, 14, 22, 30]
      // w_7 = [7, 15, 23, 31]
      //
      // x_0 = [0,  8, 16, 24, 4, 12, 20, 28]
      // x_1 = [1,  9, 17, 25, 5, 13, 21, 29]
      // x_2 = [2, 10, 18, 26, 6, 14, 22, 30]
      // x_3 = [3, 11, 19, 27, 7, 15, 23, 31]
      //
      // y_0 = [ 0,  1, 16, 17,  4,  5, 20, 21]
      // y_1 = [ 8,  9, 24, 25, 12, 13, 28, 29]
      // y_2 = [ 2,  3, 18, 19,  6,  7, 22, 23]
      // y_3 = [10, 11, 26, 27, 14, 15, 30, 31]
      //
      // z_0 = [ 0,  1,  2,  3,  4,  5,  6,  7]
      // z_1 = [ 8,  9, 10, 11, 12, 13, 14, 15]
      // z_2 = [16, 17, 18, 19, 20, 21, 22, 23]
      // z_3 = [24, 25, 26, 27, 28, 29, 30, 31]
      //
      // Or, if we unroll contig by 8, and another dim is vectorized by 2, we'd
      // have 8 = (max(8/2,1) * 2) vloads, 8*log2(2) shuffles.
      // E.g., imagine row-major memory
      //     <- unrolled ->
      // [ 0 2 4 6 8 10 12 14    ^ vectorized
      // [ 1 3 5 7 9 11 13 15 ]  v
      // Load:
      // w_0_0 = [0, 2]
      // w_0_1 = [4, 6]
      // w_0_2 = [8, 10]
      // w_0_3 = [12, 14]
      // w_1_0 = [1, 3]
      // w_1_1 = [5, 7]
      // w_1_2 = [9, 11]
      // w_1_3 = [13, 15]
      //
      // z_0 = [0, 1]   // shuffle w_0_0 and w_1_0
      // z_1 = [2, 3]   // shuffle w_0_0 and w_1_0
      // z_2 = [4, 5]   // shuffle w_0_1 and w_1_1
      // z_3 = [6, 7]   // shuffle w_0_1 and w_1_1
      // z_4 = [8, 9]   // shuffle w_0_2 and w_1_2
      // z_5 = [10, 11] // shuffle w_0_2 and w_1_2
      // z_6 = [12, 13] // shuffle w_0_3 and w_1_3
      // z_7 = [14, 15] // shuffle w_0_3 and w_1_3
      // Earlier, I had another term, `4*log2(max(8/4,1)) `8*log2(max(2/8,1))`
      // i.e. u*log2(max(v/u,1))
      // but I think we can avoid this by always working with `v`-length
      // vectors, inserting at the start or extracting at the end, whichever is
      // necessary. We divide by `u[contig]`, as it is now accounted for. So we
      // have v*max(u/v, 1) + u*log2(v) = max(u, v) + u*log2(v)
      //
      // We have
      // max(u, v) memory ops
      // u*log2(v) shuffle ops
      ptrdiff_t first_contig = std::countr_zero(orth.contig_);
      auto umi{unrolls.unrolls()[first_contig]};
      double u{umi};
      // Currently using contig for load/store costs...
      // Without the need for shuffles, this should be >= discontig
      // TODO: double check for for `u` not being a power of 2
      double ufactor = std::max(u, static_cast<double>(unrolls.vf_));
      double lc = mc[0].contig_, sc = mc[1].contig_, ld = mc[0].noncon_,
             sd = mc[1].noncon_, lcf = lc * ufactor, scf = sc * ufactor,
             shuf_count = u * vf.l2factor_, shuf_ratio = c / umi;
      bool prefer_shuf_over_gather = (lcf + shuf_count * lc) < ld * u,
           prefer_shuf_over_scatter = (scf + shuf_count * sc) < sd * u;
      double load_cost = prefer_shuf_over_gather ? lcf * shuf_ratio : ld * c,
             stow_cost = prefer_shuf_over_scatter ? scf * shuf_ratio : sd * c,
             comp_cost = 0.0;
      if (prefer_shuf_over_gather) comp_cost += shuf_count * lc;
      if (prefer_shuf_over_scatter) comp_cost += shuf_count * sc;

      Cost sgsc{.load_ = load_cost,
                .stow_ = stow_cost,
                .comp_ = comp_cost * shuf_ratio};
      // Whether we shuffle load/store or use gather/scatter are still relevant
      // for packing/unpacking
      if (std::popcount(orth.dep_) < unrolls.getDepth1()) {
        // for load cost, we need to do shuf or gather loads
        // and contiguous stores, ignoring any independent loops
        // We can ignore the independent loops being vectorized,
        // because we do know a dependent loop is vectorized (given that we are
        // here, which required `orth.dep_ & vf.index_mask_`).
        double indep_iters = unrolls.independentLoopIters(orth.dep_);
        // `sgsc` is loading from/storing to original array when transfering
        // between orig & pack. We add storing to/loading from packed array when
        // transfering between orig & pack.
        // TODO: should have separate costs vs frequencies so we can more
        // accurately swap load and store here? That is, we want to use load
        // frequency for stores, and store frequency for loads, but continue to
        // use load and store costs.
        // This *should* be fine, currently, because contiguous loads and stores
        // should be full rate, and then our `CoreWidth` indicates how many of
        // each can actually be performed per cycle. That is, `CoreWidth`
        // effectively gives load and store costs, while contiguous costs here
        // should basically just be the counts.
        l = mc[0].contig_ * c;
        s = mc[1].contig_ * c;
        Cost pack_overhead =
               (sgsc + Cost{.load_ = s, .stow_ = l}) / indep_iters,
             pack_cost = pack_overhead + Cost{.load_ = l, .stow_ = s};
        // This can be improved...
        if (pack_cost.load_ + pack_cost.stow_ + pack_cost.comp_ <
            sgsc.load_ + sgsc.stow_ + sgsc.comp_)
          return pack_cost;
      }
      return sgsc;
    }
  } else {
    l = mc[0].scalar_;
    s = mc[1].scalar_;
  }
  double lc{l * c}, sc{s * c};
  return {.load_ = lc, .stow_ = sc};
}

/// General fallback method for those without easy to represent structure
/// inds is an `IR::Address->indexMatrix()`, thus it is `arrayDim() x
/// getNumLoops()`
/// Non-standard structure here means that we have at least one loop with
/// more than one array dimension.
/// For these, we use the incorrect formula:
constexpr auto cost(Unrolls unrolls, MemCostSummary orth,
                    DensePtrMatrix<int64_t> inds) -> Cost {
  double c{1};
  auto [arrayDim, numLoops] = shape(inds);
  utils::invariant(numLoops > 0);
  utils::invariant(arrayDim > 0);
  utils::invariant(arrayDim <= 64);
  utils::invariant(unrolls.size(), ptrdiff_t(inds.numCol()));
  for (ptrdiff_t d = 0; d < arrayDim; ++d) {
    int64_t g = 0;
    containers::BitSet64 bs;
    double uprod;
    for (ptrdiff_t l = 0; l < numLoops; ++l) {
      if ((uint32_t(1) << l) == unrolls.vf_.index_mask_) continue;
      int64_t a = inds[d, l];
      if (!a) continue;
      bool docontinue{false};
      // We only
      for (ptrdiff_t k = 0; k < arrayDim; ++k) {
        if ((k == d) || (!inds[k, l])) continue;
        docontinue = (inds[d, _] != inds[k, _]) || (d > k);
        if (docontinue) break;
      }
      if (docontinue) continue;
      double u = static_cast<double>(unrolls.unrolls()[l]);
      if (bs.empty()) {
        g = a;
        uprod = u;
      } else {
        g = math::gcd(g, a);
        uprod *= u;
      }
      bs.insert(l);
    };
    if (bs.size() < 2) continue;
    double prod{1}, dg = static_cast<double>(g);
    // mask off the active vector lane to skip it
    bs &= containers::BitSet64::fromMask(
      ~static_cast<uint64_t>(unrolls.vf_.index_mask_));
    for (ptrdiff_t l : bs)
      if (int64_t a = inds[d, l])
        prod *= (1.0 - (static_cast<double>(a) / dg) *
                         (static_cast<double>(unrolls.unrolls()[l]) / uprod));
    c *= (1.0 - prod);
  }
  // c is a scaling factor; now we proceed to calculate cost similaly to the
  // orth-axis implementation above.
  return c * cost(unrolls, orth);
}

inline auto memcosts(Unrolls invunrolls, PtrVector<MemCostSummary> orth_axes)
  -> Cost {
  Cost costs{};
  for (auto mcs : orth_axes) costs += cost(invunrolls, mcs);
  return costs;
}
inline auto
memcosts(Unrolls unrolls,
         PtrVector<Pair<MemCostSummary, DensePtrMatrix<int64_t>>> orth_axes)
  -> Cost {
  Cost costs{};
  for (auto [mcs, inds] : orth_axes) costs += cost(unrolls, mcs, inds);
  return costs;
}

} // namespace CostModeling::Cost
