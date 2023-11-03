#pragma once

#include "IR/Address.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "IR/OrthogonalAxes.hpp"
#include <Containers/BitSets.hpp>
#include <Containers/Pair.hpp>
#include <Math/Array.hpp>
#include <Math/Constructors.hpp>
#include <Math/Exp.hpp>
#include <Math/GreatestCommonDivisor.hpp>
#include <Math/Math.hpp>
#include <Math/Matrix.hpp>
#include <Math/Vector.hpp>
namespace poly::CostModeling {
using containers::Pair;
using math::AbstractVector, math::AbstractMatrix, math::DensePtrMatrix, math::_;
using utils::Optional;

/// POD. Gives counts for the different kinds of costs.
/// Fields:
/// `int16_t trip_count`- we're unlikely to change decisions for >32k
///         negative indicates compile-time known size.
/// `uint16_t memory` number of mem sets.
/// `bool exit` loop exit/entry.
/// `uint31_t compute` number of compute sets.
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
struct LoopCostCounts {
  uint16_t known_trip : 1;
  uint16_t trip_count : 15;
  uint16_t compute;
  uint16_t omemory;
  uint8_t cmemory;
  uint8_t exit : 5;              /// how many blocks we exit after this
  uint8_t l2vectorWidth : 3 {0}; // 1<<7 == 128
};
static_assert(sizeof(LoopCostCounts) == 8);

/// Order is outermost -> innermost
struct VectorizationFactor {
  uint32_t l2factor;
  // trailing bit is outermost loop, so if iterating by shifting,
  // we go outer->inner
  uint32_t indexMask;
  constexpr operator IR::VectorWidth() const {
    return IR::VectorWidth{unsigned(1) << l2factor, l2factor};
  }
};

/// TODO: maybe two `uint8_t`s + `uint16_t`
/// We only get up to 16 dimensions, but that is already excessive
/// One `uint8_t` gives contig axis, the other the index into
/// the memory cost kind. Thus, the struct could differentiate
/// loads vs stores by itself, while also differentiating
/// between eltypes.
/// Another option is to store individual `MemoryCosts`,
/// so that we can aggregate/sum up.
struct MemCostSummary {
  IR::Addr::Costs memcost;
  OrthogonalAxes orth;
  // [[nodiscard]] constexpr auto contigAxis() const -> uint32_t {
  //   return data & 0xff;
  // }
  // mask containing `0` for dependent axes, 1s for independent
  // should contain `0` for all non-existent loops, e.g.
  // for (i = I, j = J, k = K, l = L) {
  //   A[j,l]
  //   for (a = A, b = B){ .... }
  // }
  // The mask should equal (1<<0) | (1<<2)  (for the i and k).
  // Only loops it is nested in that it doesn't depend on count.
  // [[nodiscard]] constexpr auto indepAxes() const -> uint32_t {
  //   return data >> 8;
  // };
};
constexpr auto operator&(MemCostSummary a, MemCostSummary b) -> uint32_t {
  return a.orth.indep & b.orth.indep;
}
constexpr auto cost(const AbstractMatrix auto &invunrolls, uint32_t indepAxes)
  -> utils::eltype_t<decltype(invunrolls)> {
  utils::eltype_t<decltype(invunrolls)> c{};
  if (indepAxes) {
    uint32_t tz = std::countr_zero(indepAxes);
    c = invunrolls[0, tz++];
    for (uint32_t d = indepAxes >> tz, i = tz; d; d >>= tz, i += tz) {
      tz = std::countr_zero(d);
      c *= invunrolls[0, i + tz++];
    }
  } else c = 1;
  return c;
}
// costs is an array of length two.
// memory costs, unnormalized by `prod(unrolls)`
// `invunrolls` is a matrix, row-0 are the inverse unrolls, row-1 unrolls.
constexpr auto cost(const AbstractMatrix auto &invunrolls, MemCostSummary mcs,
                    VectorizationFactor vfi)
  -> utils::eltype_t<decltype(invunrolls)> {
  auto [mc, orth] = mcs;
  utils::eltype_t<decltype(invunrolls)> c{cost(invunrolls, orth.indep)};
  if (!(orth.indep & vfi.indexMask)) {
    // depends on vectorized index
    if (vfi.indexMask & orth.contig) {
      c *= mc.contiguous;
    } else if (!orth.contig) {
      c *= mc.discontiguous;
    } else {
      // Discontiguous vector load.
      // We consider two alternatives:
      // 1. gather/scatter (discontiguous)
      // 2. contiguous load for each vectorization factor of length equal to
      // unroll, followed by shuffles.
      // E.g., unroll contig by 4, another dim is vectorized by 8:
      // we'd have 8 vloads (max(4/8,1) * 8), followed by 4*log2(8) +
      // log2(max(8/4,1))*4 shuffles.
      // Or, if we unroll contig by 8, and another dim is vectorzeed by 2, we'd
      // have 8 = (max(8/2,1) * 2) vloads, 8*log2(2) + log2(max(2/8,1))*8
      // shuffles.
      // We divide by `u[contig]`, as it is now accounted for
      // So we have
      // max(v/u, 1) + u*log2(v) + log2(max(v/u ,1))*u
      utils::eltype_t<decltype(invunrolls)> iu{invunrolls[0, orth.contig]},
        u{invunrolls[1, orth.contig]},
        mr{math::smax((1 << vfi.l2factor) * iu, 1)};
      utils::invariant(iu == 1 / u);
      c *= math::smin(mc.contiguous * mr + u * (vfi.l2factor + log2(mr)),
                      mc.discontiguous);
    }
  } else c *= mc.scalar;
  return c;
}
/// General fallback method for those without easy to represent structure
/// inds is an `IR::Address->indexMatrix()`, thus it is `arrayDim() x
/// getNumLoops()`
/// Non-standard structure here means that we have at least one loop with
/// more than one array dimension.
/// For these, we use the incorrect formula:
///
constexpr auto cost(const AbstractMatrix auto &invunrolls, MemCostSummary orth,
                    VectorizationFactor vfi, DensePtrMatrix<int64_t> inds)
  -> utils::eltype_t<decltype(invunrolls)> {
  utils::eltype_t<decltype(invunrolls)> c{1};
  auto [arrayDim, numLoops] = inds.size();
  utils::invariant(numLoops > 0);
  utils::invariant(arrayDim > 0);
  utils::invariant(arrayDim <= 64);
  utils::invariant(invunrolls.numCol(), inds.numCol());
  for (ptrdiff_t d = 0; d < arrayDim; ++d) {
    int64_t g = 0;
    containers::BitSet64 bs;
    utils::eltype_t<decltype(invunrolls)> uprod;
    for (ptrdiff_t l = 0; l < numLoops; ++l) {
      if ((uint32_t(1) << l) == vfi.indexMask) continue;
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
      if (bs.empty()) {
        g = a;
        uprod = invunrolls[0, l];
      } else {
        g = math::gcd(g, a);
        uprod *= invunrolls[0, l];
      }
      bs.insert(l);
    };
    if (bs.size() < 2) continue;
    utils::eltype_t<decltype(invunrolls)> prod{1};
    for (ptrdiff_t l : bs) {
      if ((uint32_t(1) << l) == vfi.indexMask) continue;
      int64_t a = inds[d, l];
      if (!a) continue;
      prod *= (1 - (a / g) * (uprod / invunrolls[0, l]));
    }
    c *= (1 - prod);
  }
  // c is a scaling factor; now we proceed to calculate cost similaly to the
  // orth-axis implementation above.
  return c * cost(invunrolls, orth, vfi);
}

// We need to define an unroll ordering.
struct RegisterUseByUnroll {
  math::PtrVector<std::array<uint32_t, 2>> masks; // coef, mask pairs
  unsigned register_count;                        // includes constant offset
  [[nodiscard]] constexpr auto begin() const
    -> const std::array<uint32_t, 2> * {
    return masks.begin();
  }
  [[nodiscard]] constexpr auto end() const -> const std::array<uint32_t, 2> * {
    return masks.end();
  }
};
// TODO: define function to implement register_count
constexpr auto registerPressure(const AbstractMatrix auto &invunrolls,
                                const RegisterUseByUnroll &r)
  -> utils::eltype_t<decltype(invunrolls)> {
  utils::eltype_t<decltype(invunrolls)> acc{0};
  for (auto [c, m] : r) {
    utils::eltype_t<decltype(invunrolls)> t{1};
    containers::BitSet64 bs{std::array<uint64_t, 1>{m}};
    for (ptrdiff_t i : bs) t *= invunrolls[1, i];
    acc += c * t;
  }
  // note the softplus(8x)/4, so 2x scaling on penalty representing
  // the stack load+store combination.
  return 0.25 * math::softplus(8.0 * (acc - r.register_count));
}

auto memcosts(
  const AbstractMatrix auto &invunrolls, VectorizationFactor vf,
  math::PtrVector<Pair<MemCostSummary, IR::Addr::Costs>> orth_axes) {
  utils::eltype_t<decltype(invunrolls)> ic{};
  for (auto [oa, mc] : orth_axes) ic += cost(invunrolls, oa, mc, vf);
  return ic;
}
auto memcosts(
  const AbstractMatrix auto &invunrolls, VectorizationFactor vf,
  math::PtrVector<std::tuple<MemCostSummary, DensePtrMatrix<int64_t>>>
    orth_axes) {
  utils::eltype_t<decltype(invunrolls)> ic{};
  for (auto [oa, inds] : orth_axes) ic += cost(invunrolls, oa, vf, inds);
  return ic;
}
auto compcosts(const AbstractMatrix auto &invunrolls,
               math::PtrVector<std::array<uint32_t, 2>> compindep) {
  utils::eltype_t<decltype(invunrolls)> cc{};
  for (auto [oa, sf] : compindep) cc += cost(invunrolls, oa) * sf;
  return cc;
}
// We then additionally need a throughput vs latency estimator, and code for
// handling the tail.
// Standard throughput is fairly trivial/should be a vector sum,
// although we may have some operations not dependent on all loops,
// in which case unrolling the loops they don't depend on will help.
// Thus, it would probably be best to handle these with code
// similar to the memory cost-fun above, ideally we can abstract away the core.
/// memcost = I*J*(Ui*Uj*C_{Al} + Uj*C_{yl}) / (Ui*Uj) +
///    I*(C_{xl}*Ui + C_{xs}*Ui) / Ui
/// cthroughput = I*J*(Ui*Uj*C_{t,fma}) / (Ui*Uj) + I*(Ui*C_{t,add}*(Uj-1)) /
/// Ui clatency = I*J*C_{l,fma}/smin(Ui*Uj, C_{l,fma}/C_{t,fma}) +
///    I*C_{l,add}*log2(Uj)
///
/// Here, we define a cost fn that can be optimized to produce
/// vectorization and unrolling factors.
/// We assemble all addrs into a vector, sorted by depth first traversal order
/// of the loop tree, e.g.
/// A(0) --> B(1) --> C(2) --> D(3)
///      \-> E(5) --> F(6) \-> G(4)
///      \-> H(7) --> I(8) --> J(9)
/// Focusing only on memory addresses initially...
/// The cost of a particular read/write can be looked up from LLVM
/// as a function of scalar/gather/scatter/broadcast/contiguous.
/// Then this can be adjusted by the product of all unroll factors of loops
/// it depends on, divided by the product of all unroll factors of all
/// containing loops.
/// To optimize, we can branch and bound. Unrolling factors lead to a natural
/// relaxation that plays well, but less so for binary variables like which
/// loop is vectorized. Additionally, patterns such as replacing
/// gather/scatters with shuffle sequences need special handling, that
/// restricts the branch and bound to powers of 2. To be able to build such a
/// cost model, we need to estimate the number of live variables as a result
/// of unroll factors, in order to impose constraints.
///
/// We use soft constraints for register pressuring, representing the
/// store/reload pair of a spill.
///
/// Furthermore, we also need to consider the possibility of dependency
/// chains. Consider, for example
/// ```
/// for (ptrdiff_t i = 0; i < I; ++i){
///   eltype_t<A> xi = x[i];
///   for (ptrdiff_t j = 0; j < J; ++j)
///     xi += A[i][j] * y[j];
///   x[i] = xi;
/// }
/// ```
/// The `j` loop itself has a dependency chain.
/// Two options for addressing this:
/// 1. unrolling `j`, cloning the accumulation registers, and reducing at the
/// end.
/// 2. unrolling the `i` loop.
/// The second option is better, but may not be possible, e.g. if there is no
/// `i` loop or it carries some dependency. Thus, we want our model to unroll
/// `i` when legal, and unroll `j` otherwise.
/// Assuming a throughput of 2 fma/cycle and a latency of 4 cycles, an
/// estimate of the cost as a function of I, J, Ui, and Uj is (ignoring
/// vectorization): 4*I*J/min(Ui*Uj, 2*4) + 4*I*log2(Uj) The first term is
/// latency per fma (because of the dependency chain) * the number of
/// iterations, divided by however many unrolling allows us to have inflight.
/// The second term is for the reduction of the cloned `Uj` accumulators. Each
/// step in the reduction has a latency of 4 cycles, and we need to do
/// `log2(Uj)` steps.
///
/// Note, `y-softplus(l*(y-x))/l` is a good smooth minimum function,
/// monotonic in `x` and differentiable everywhere. `l` controls
/// sharpness. Likewise, `y+softplus(l*(x-y))/l` for `max`.
///
/// Thus, a cost function for the above gemv could be something like
/// memcost = I*J*(Ui*Uj*C_{Al} + Uj*C_{yl}) / (Ui*Uj) +
///    I*(C_{xl}*Ui + C_{xs}*Ui) / Ui
/// cthroughput = I*J*(Ui*Uj*C_{t,fma}) / (Ui*Uj) + I*(Ui*C_{t,add}*(Uj-1)) /
/// Ui clatency = I*J*C_{l,fma}/smin(Ui*Uj, C_{l,fma}/C_{t,fma}) +
///    I*C_{l,add}*log2(Uj)
/// cost = memcost + smax(cthroughput, clatency)
/// or, if the it is easier to solve:
/// cost = memcost + cthroughput + clatency
///
/// We may initially want to add a small cost for loop increment and
/// cmp/branch, to encourage unrolling more generally, plus a cost for
/// unrolling to discourse any excess unrolling when it doesn't provide
/// meaningful benefits (representing the general cost of code size/ filling
/// uop cache -- we definitely want loops to fit in the uop cache of any CPU
/// sporting one!!! ).
///
///
///
/// Note that if we had
/// ```
/// for (ptrdiff_t i = 0; i < I; ++i){
///   eltype_t<A> yi = y[i];
///   for (ptrdiff_t j = 0; j < J; ++j)
///     x[j] += A[i][j] * yi;
/// }
/// ```
/// then unrolling the `i` loop doesn't increase OOO (Out Of Order execution),
/// but we can assume that as successive `j` iterations are independent/do not
/// have a dependency chain, this isn't an issue. That is, we only consider
/// reductions across the inner-most loop as requiring cloning of accumulators.
///
/// On throughput modeling, LLVM seems to generally give a recip throughput of
/// 1 for pipelined instructions, regardless of number of ports. This is
/// actually what we want, as this allows RTs to be additive (e.g., we may
/// have a fma that is able to run on 2 ports (e.g. p0 or p5) and a permute
/// that can only execute on one (e.g. p5); when mixing these instructions,
/// they have the same effective cost -- they use a port -- and the more
/// limited port choices of one isn't a problem so long as others can use what
/// remains. For our purposes, it isn't worth getting too fancy here. It is
/// worth noting that the baseline model presented here
/// https://arxiv.org/pdf/2107.14210.pdf
/// performed respectively well when compared to vastly more sophisticated
/// tools; for example, it performed similarly well as llvm-mca on most tested
/// architectures!
/// The baseline model used above for loops was
/// max(1, (n-1)/i, m_r/m, m_w/w)
/// where
/// n - the number of instructions in the benchmark (-1 because of assumption
/// that the cmp and branch are macro-fused, meaning the last two instructions
/// count as 1)
/// m_r - number of memory reads
/// m_w - number of memory writes
/// i - the issue width, e.g. 4 for Intel Skylake CPUs.
/// m - number of reads the CPU can do per cycle (2 for all in the article)
/// w - number of writes the CPU can do per cycle (e.g. 2 for Ice Lake and
/// newer, 1 for older) Unfortunately, we cannot get the CPU-specific
/// information (`i`,`m`,or`w`) from LLVM. However, these are largely a matter
/// of scale, and are generally correlated. E.g., Intel's Alderlake's values
/// would be 6, 3, and 2, vs the older Skylake's 4, 2, and 1. While not all
/// the ratios are equal (`w`'s is 2 instead of 1.5), it is unlikely that many
/// optimization decisions are going to be made differently between them.
/// A possible exception is that we may wish to unroll more for CPUs with more
/// out of order execution abilities. `getMaxInterleaveFactor` is an indicator
/// of whether the pipeline might be very narrow.
///
///
/// Given `x[a*i + b*j]`, where neither `i` or `j` are vectorized (and `a` and
/// `b` are compile time constants), we use:
/// (a_g*U_i + b_g*U_j - a_g*b_g) / (U_i*U_j)
/// = a_g/U_j + b_g/U_i - a_g*b_g / (U_i*U_j)
/// = 1 - (1 - a_g/U_j ) * (1 - b_g/U_i)
/// as the cost, where `a_g = abs(a/gcd(a,b))` and `b_g = abs(b/gcd(a,b))`.
///
/// For more, we generalize this pattern
/// = 1 - \prod_{d}^{D}\left(1 - \frac{coef_{g,d}*U_d}{\prod_{i}^{D}U_i}\right)
///
/// In the `D=3` case, this expands to
/// 1 - (1 - a_g/(U_j*U_k))(1 - b_g/(U_i*U_k))(1 - c_g/(U_i*U_j))
/// = 1 - (1 - c_g/(U_i*U_j))*
///    (1 - a_g/(U_j*U_k) - b_g/(U_i*U_k)) + a_g*b_g/(U_i*U_j*U_k^2))
/// = a_g/(U_j*U_k) + b_g/(U_i*U_k)) + c_g/(U_i*U_j) - a_g*b_g/(U_i*U_j*U_k^2))
///     - a_g*c_g/(U_i*U_j^2*U_k) - b_g*c_g/(U_i^2*U_j*U_k))
///     + a_g*b_g*c_g/(U_i^2*U_j^2*U_k^2))
///
/// TODO: check the degree of correctness...
/// I kind of just made something up that looks sort of right.
///
/// For register consumption, we
/// 1. Determine an ordering of unroll factors for each inner most loop.
/// 2. Define a registers used as a function of these unroll factors.
///
/// Loads from inner unrolls that don't depend on any outer-unrolls must have
/// lifetimes spanning all outer-unrolls, if they're re-used by an op
/// depending on that outer. Our heuristic for ordering unrolls is based on
/// the twin observations:
/// 1. Inner unrolls are likely to consume more registers for longer.
/// 2. More ops with overlapping lifetimes dependent on one particular loop
/// require more registers.
///
/// As the ordering of unrolls influences register pressure, we sort them
/// first by register cost per unroll (placing those with the highest register
/// cost outside), and then by memory op cost within these categories, placing
/// the highest costs innermost  (higher memory cost means lower unroll
/// relative to the lower cost, so that we get more reuse on the higher cost
/// operations; lower unroll means we place inside, reducing the cost of these
/// unrolls).
///
/// So, how do we define register cost per unroll in an unroll-order
/// independent manner, so that we can use this for determining the order?
/// ```
/// for (int m=0; m<M; ++m){
///   for (int n=0; n<N; ++n){
///     auto Cmn = C[m,n];
///     for (int k=0; k<K; ++k)
///       Cmn += A[m,k]*B[k,n];
///     C[m,n] = Cmn;
///   }
/// }
/// ```
/// In this example, we have 4 ops in the inner loop
/// A[m,k] --->*--> (Cmn +=)
/// B[k,n] -/
///
/// Register Costs:
/// Amk_rc = U_m * U_k // live until use
/// Bkn_rc = U_k * U_n // live until use
/// Cmn_rc = U_m * U_n // live until end of loop
/// Memory Op Costs, m-vectorized (assuming column-major):
/// Amk_rc = L_c * U_m * U_k
/// Bkn_rc = L_b * U_k * U_n
/// Cmn_rc = 0 * U_m * U_n
/// L_c > L_b, so A-contiguous load should be interior to B-broadcast load.
///
/// As the cost function is evaluated many times, we try and move as much work
/// to the setup as possible. Loop cost is thus divided into some structured
/// components, and much of the interpreting work hoisted to a step defining a
/// parameterization.
/// Ideally, we would avoid repeating this work for different vectorization
/// decisions. However, vectorization decisions may impact unroll ordering
/// decisions.
///
///
///
/// ///
class LoopTreeCostFn {
  math::Vector<LoopCostCounts> cost_counts{};
  math::Vector<MemCostSummary> orth_axes{};
  math::Vector<Pair<MemCostSummary, DensePtrMatrix<int64_t>>> conv_axes{};
  math::Vector<Pair<float, uint32_t>> compute_independence{};
  math::Vector<Pair<RegisterUseByUnroll, Pair<uint16_t, uint16_t>>> leafs{};
  unsigned maxVectorWidth;
  ptrdiff_t max_depth{};

  constexpr void clear() {
    cost_counts.clear();
    orth_axes.clear();
    conv_axes.clear();
    compute_independence.clear();
    leafs.clear();
    max_depth = 0;
  }

  // should only have to `init` once per `root`, with `VectorizationFactor`
  // being adjustable.
  // Note: we are dependent upon scanning in top order, so that operands'
  // `calcLoopDepFlag()` are calculated before we get.
  // TODO: vec factor should be a tree-flag
  // Iteration order:
  // We fully iterate over a loop before descending
  // for (i : I){
  //   // block 0
  //   for (j : J){
  //     // block 1
  //   }
  //   // block 2
  //   for (j : J){
  //     // block 3
  //   }
  //   // block 4
  // }
  // we'd iterate 0, 2, 4, 1, 3.
  // This way we can store once we hit the end.
  // If there are no subloops to iterate to after, then we store the exit count.
  // If there are, then the exit-count is 0, forward '1+exit' count to the last
  // sub-loop, and `1` to all previous sub-loops.
  // It's thus natural to implement recursively.
  // NOLINTNEXTLINE(misc-no-recursion)
  void initLoop(IR::Loop *L, unsigned maxl2VF,
                const llvm::TargetTransformInfo &TTI, ptrdiff_t depth,
                unsigned exitCount) {
    invariant(depth > 0);
    ptrdiff_t compute = compute_independence.size(), omemory = orth_axes.size(),
              cmemory = conv_axes.size();
    unsigned maxVF = 1 << maxl2VF;
    // Loop and push throughput costs
    for (IR::Node *N = L->getChild(); N; N = N->getNext()) {
      if (auto *A = llvm::dyn_cast<IR::Addr>(N)) {
        OrthogonalAxes oa = A->calcOrthAxes(depth);
        IR::Addr::Costs rtl = A->calcCostContigDiscontig(TTI, maxVF);
        if (oa.indep_axes) {
          // check for duplicate
          bool found = false;
          for (ptrdiff_t i = omemory; i < orth_axes.size(); ++i) {
            if (orth_axes[i].orth != oa) continue;
            found = true;
            orth_axes[i].memcost += rtl;
            break;
          }
          if (!found) orth_axes.emplace_back(rtl, oa);
        } else {
          bool found = false;
          for (ptrdiff_t i = cmemory; i < conv_axes.size(); ++i) {
            if (conv_axes[i].first.orth != oa) continue;
            if (conv_axes[i].second != A->indexMatrix()) continue;
            found = true;
            conv_axes[i].first.memcost += rtl;
            break;
          }
          if (!found)
            conv_axes.emplace_back(MemCostSummary{rtl, oa}, A->indexMatrix());
        }
      } else if (auto *C = llvm::dyn_cast<IR::Compute>(N)) {
        bool found = false;
        uint32_t indep = C->calcLoopDepFlag(depth);
        float cc{float(
          C->getCost(TTI, IR::VectorWidth{maxVF, maxl2VF}).recipThroughput)};
        for (ptrdiff_t i = compute; i < compute_independence.size(); ++i) {
          if (compute_independence[i].second != indep) continue;
          found = true;
          compute_independence[i].first += cc;
          break;
        }
        if (!found) compute_independence.emplace_back(cc, indep);
      } // else if (auto *S = llvm::dyn_cast<IR::Loop>(N)) {
    }
    auto [known_trip, trip_count] = L->getAffineLoop()->tripCount(depth);
    uint16_t compcnt = compute_independence.size() - compute,
             omemcnt = orth_axes.size() - omemory,
             cmemcnt = conv_axes.size() - cmemory;
    IR::Loop *SL = L->getSubLoop();
    cost_counts.emplace_back(known_trip, trip_count, compcnt, omemcnt, cmemcnt,
                             SL ? 0 : exitCount);
    if (SL) iterLoopLevel(SL, maxl2VF, TTI, ++depth, exitCount);
    // TODO: if (!SL) we're in a leaf, and need compute latency
    // The process here is to check if any computations are stored outside of
    // this loop. If so, walk predecessors (counting accumulated latency as we
    // go) to find a chain. We also track if we're able to reassociate the
    // chain, i.e. if we're able to split accumulators (e.g. are they all
    // additions with reassoc flag?).
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void iterLoopLevel(IR::Loop *L, unsigned maxl2VF,
                     const llvm::TargetTransformInfo &TTI, ptrdiff_t depth,
                     unsigned exitCount) {
    do {
      IR::Loop *N = L->getNextLoop();
      unsigned ec = N ? ++exitCount : 1;
      initLoop(L, maxl2VF, TTI, depth, ec);
      L = N;
    } while (L);
  }

public:
  // this is a vector fun, where indexing may do non-trivial computation
  // also, mapping from this vector to loop position isn't trivial either
  // hence, we use a 2 x max_depth matrix that we copy into as we descend
  // (and pop from as we ascend). Row `0` is for inverse values,
  // and row `1` for direct values.
  // Inverses are favored as our costs fns use them more often.
  constexpr auto operator()(alloc::Arena<> alloc,
                            const AbstractVector auto &x) const {
    using T = utils::eltype_t<decltype(x)>;
    utils::invariant(max_depth < 16);
    // row 0: inverse unrolls
    // row 1: unrolls
    // row 2: cumprod invunroll
    math::MutArray<T, math::DenseDims<3>> invunrolls{
      math::matrix<T>(alloc, math::Row<3>{}, math::Col<>{max_depth})};
    ptrdiff_t i = 0, depth = 0, mi = 0, mc = 0, ci = 0, li = 0;
    double tripcounts[16];
    VectorizationFactor vf{};
    // we evaluate every iteration
    T c{};
    for (auto [comptimetrip, trip_count, compute, omem, cmem, exit, l2vw] :
         cost_counts) {
      if (l2vw) {
        invariant(vf.l2factor == 0);
        invariant(vf.indexMask == 0);
        vf.l2factor = l2vw;
        vf.indexMask = uint32_t(1) << depth;
      }
      invunrolls[1, depth] = x[i++];
      invunrolls[2, depth] = invunrolls[0, depth] = 1 / invunrolls[1, depth];
      if (depth) invunrolls[2, depth] *= invunrolls[2, depth - 1];
      tripcounts[depth] =
        (depth ? tripcounts[depth - 1] * trip_count : trip_count);
      T cc{compcosts(invunrolls, compute_independence[_(0, compute) + ci])};
      ci += compute;
      if (exit) {
        auto [reguse, lt] = leafs[li++];
        auto [l, numreduct] = lt;
        // we're now in a leaf, meaning we must consider register costs,
        // as well as reduction costs and latency of reduction chains.
        cc = smax(cc, l * invunrolls[2, depth]);
        cc += registerPressure(invunrolls, reguse);
        if (numreduct) {
          cc +=
            compcost(invunrolls, compute_independence[_(0, numreduct) + ci]) *
            log2(invunrolls[1, depth]) / trip_count;
          ci += numreduct;
        }
      }
      cc += memcosts(invunrolls, vf, orth_axes[_(0, omem) + mi]);
      mi += omem;
      cc += memcosts(invunrolls, vf, conv_axes[_(0, cmem) + mc]);
      mc += cmem;
      c += tripcounts[depth] * cc;
      // Decrement depth by `exit - 1`; the `-1` corresponds
      // to descending into this header, while we exit `exit` loops afterwards.
      depth -= exit - 1; // don't fuse `-1` to keep `exit` unsigned
      if (depth <= std::countr_zero(vf.indexMask)) {
        vf.l2factor = 0;
        vf.indexMask = 0;
      }
    }
    return c;
  }
  void init(IR::Loop *root, unsigned maxl2VF,
            const llvm::TargetTransformInfo &TTI) {
    clear(); // max_depth = 0;
    iterLoopLevel(root->getSubLoop(), maxl2VF, TTI, 0, 0);
  }
  LoopTreeCostFn(IR::Loop *root, unsigned maxVF,
                 const llvm::TargetTransformInfo &TTI)
    : maxVectorWidth{unsigned(1) << maxVF} {
    init(root, maxVF, TTI);
  }
};

} // namespace poly::CostModeling
