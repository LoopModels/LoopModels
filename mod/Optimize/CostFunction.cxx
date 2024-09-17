#include <bits/ranges_algo.h>
#include <iterator>
#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/TinyVector.cxx"
#include "IR/OrthogonalAxes.cxx"
#include "Math/Array.cxx"
#include "Math/Constructors.cxx"
#include "Math/ManagedArray.cxx"
#include "Math/MatrixDimensions.cxx"
#include "Math/Saturated.cxx"
#include "Numbers/Int8.cxx"
#include "Optimize/BBCosts.cxx"
#include "Optimize/MemoryCost.cxx"
#include "Optimize/MicroKernelOptimization.cxx"
#include "Optimize/RegisterLife.cxx"
#include "Optimize/RegisterUse.cxx"
#include "Target/Machine.cxx"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#else
export module CostModeling:CostFunction;
import Arena;
import ArrayConstructors;
import BitSet;
import Int8;
import Invariant;
import IR;
import ManagedArray;
import Optional;
import OrthogonalAxes;
import Pair;
import Saturated;
import StaticArray;
import STL;
import TargetMachine;
import TinyVector;
import Tuple;
import :BasicBlock;
import :MemoryCost;
import :MicroKernel;
import :RegisterLife;
import :RegisterUse;
#endif

#ifdef USE_MODULE
export namespace CostModeling::Hard {
#else
namespace CostModeling::Hard {
#endif

// Here, we define an integer cost function.
// Unlike the smooth function, this one is not differentiable.
// What it gains are:
// 1. Better performance: no need to use slow approximations like `smax`.
// 2. More accurate: not every decision can be represented in a differentiable
// way.
//
// This, however, forces us into discrete space exploration.
// But, the space we actually are able to represent in a differentiable way is
// so small (but must be explored many times for discrete parameters), that
// this doesn't necessarilly mean that we are worse off.

// Our cost function iterates over a loop tree, conceptually recursively.
// Each branch in the tree has

using math::Vector, math::DensePtrMatrix, math::_, math::end;
using numbers::i8, numbers::u8;

// data layout is [deps, permanent]
struct LoopDeps {
  // trailing bit
  uint16_t permanent_ : 1;
  uint16_t deps_ : 15;
  explicit constexpr operator uint16_t() const {
    return std::bit_cast<uint16_t>(*this);
  }

private:
  friend constexpr auto hash_value(LoopDeps d) -> uint64_t {
    return uint64_t(uint16_t(d));
  }
};

// We then additionally need a throughput vs latency estimator, and code for
// handling the tail.
// Standard throughput is fairly trivial/should be a vector sum,
// although we may have some operations not dependent on all loops,
// in which case unrolling the loops they don't depend on will help.
// Thus, it would probably be best to handle these with code
// similar to the memory cost-fun above, ideally we can abstract away the core.
//
/// memcost = I*J*(Ui*Uj*C_{Al} + Uj*C_{yl}) / (Ui*Uj) +
///    I*(C_{xl}*Ui + C_{xs}*Ui) / Ui
/// cthroughput = I*J*(Ui*Uj*C_{t,fma}) / (Ui*Uj) + I*(Ui*C_{t,add}*(Uj-1)) / Ui
/// Ui clatency = I*J*C_{l,fma}/smin(Ui*Uj, C_{l,fma}/C_{t,fma}) +
///    I*C_{l,add}*log2(Uj)
///
/// Here, we define a cost fn that can be optimized to produce
///
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
///
///     for (ptrdiff_t i = 0; i < I; ++i){
///       eltype_t<A> xi = x[i];
///       for (ptrdiff_t j = 0; j < J; ++j)
///         xi += A[i][j] * y[j];
///       x[i] = xi;
///     }
///
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
/// cthroughput = I*J*(Ui*Uj*C_{t,fma}) / (Ui*Uj) + I*(C_{t,add}*(Uj-1)) /
/// Ui clatency = I*J*C_{l,fma}/smin(Ui*Uj, C_{l,fma}/C_{t,fma}) +
///    I*C_{l,add}*log2(Uj)
/// cost = memcost + std::max(cthroughput, clatency)
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
///
///     for (ptrdiff_t i = 0; i < I; ++i){
///       eltype_t<A> yi = y[i];
///       for (ptrdiff_t j = 0; j < J; ++j)
///         x[j] += A[i][j] * yi;
///     }
///
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
/// = 1 - \prod_{d}^{D}\left(1 - \frac{coef_{g,d}U_d}{\prod_{i}^{D}U_i}\right)
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
///
///     for (int m=0; m<M; ++m){
///       for (int n=0; n<N; ++n){
///         auto Cmn = C[m,n];
///         for (int k=0; k<K; ++k)
///           Cmn += A[m,k]*B[k,n];
///         C[m,n] = Cmn;
///       }
///     }
///
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
/// The purpose of this object is to choose unroll factors and loops to
/// vectorize. To this end, we evaluate loop trees from outside->in.
/// All data structures representing loop information must thus be
/// subsettable to represent a branch of the loop tree.
///
/// Register costs are tricky, and spills result in non-locality
/// but we can easily place upper and lower bounds on spill costs,
/// i.e. assume all/none get spilled and thus all/none must be reloaded.
/// Thus, early-stopping is still feasible.
/// The lower bound cost is `max(0, live_register_count - reg_count)`.
/// The upper bound  cost is `live_register_count`.
///
/// We have both intrablock and interblock spill costs.
/// Spill costs are by BB
/// Costs are organized as follows:
/// LoopHeader, contains:
///  - instruction costs
///  - live_register_count for intra-block LB and UB
///  - intrablock costs for all BBs; (interblock costs computed later)
///  - size information for:
///    - each bb
///    - subloops
/// We can iterate over the BBs of a loop, calling sub-loops one at a time.
class LoopTreeCostFn {
  alloc::Arena<> *alloc_;
  Vector<LoopSummary> loop_summaries_;
  // BBCosts
  Vector<BasicBlockCostCounts> cost_counts_;
  Vector<Cost::MemCostSummary> orth_axes_;
  Vector<Pair<Cost::MemCostSummary, DensePtrMatrix<int64_t>>> conv_axes_;
  Vector<CompCost> compute_independence_;
  Vector<IntraBlockRegisterUse> intrablock_reg_;
  Register::UsesAcrossBBs interblock_reg_;
  Cache::CacheOptimizer::DepSummary *leafdepsummary_{nullptr};
  target::MachineCore target_;
  int16_t max_vector_width_;
  int16_t cacheline_bits_;
  u8 register_count_;
  u8 max_depth_{};

  constexpr auto bbcosts() -> BBCosts {
    return {.cost_counts_ = cost_counts_,
            .orth_axes_ = orth_axes_,
            .conv_axes_ = conv_axes_,
            .compute_independence_ = compute_independence_,
            .intrablock_reg_ = intrablock_reg_,
            .interblock_reg_ = interblock_reg_.liveinfo_,
            .live_counts_ = interblock_reg_.live_counts_.data()};
  }

  constexpr void clear() {
    cost_counts_.clear();
    orth_axes_.clear();
    conv_axes_.clear();
    compute_independence_.clear();
    intrablock_reg_.clear();
    interblock_reg_.clear();
    register_count_ = {};
    max_depth_ = {};
  }

  struct CostLengths {
    ptrdiff_t n_orth_axes_{}, n_conv_axes_{}, n_comp_{}, n_intrablock_reg_{},
      n_live_histories_{};
  };
  [[nodiscard]] constexpr auto costLengths() const -> CostLengths {
    return {.n_orth_axes_ = orth_axes_.size(),
            .n_conv_axes_ = conv_axes_.size(),
            .n_comp_ = compute_independence_.size(),
            .n_intrablock_reg_ = intrablock_reg_.size(),
            .n_live_histories_ = interblock_reg_.liveinfo_.size()};
  }
  [[nodiscard]] constexpr auto BBCostCounts(CostLengths cost_len) const
    -> BasicBlockCostCounts {
    return {.latency_ = u8(0),
            .n_orth_axes_ = u8(orth_axes_.size() - cost_len.n_orth_axes_),
            .n_conv_axes_ = u8(conv_axes_.size() - cost_len.n_conv_axes_),
            .n_comp_ = u8(compute_independence_.size() - cost_len.n_comp_),
            .n_intrablock_reg_ =
              u8(intrablock_reg_.size() - cost_len.n_intrablock_reg_),
            .n_live_histories_ = u8(interblock_reg_.liveinfo_.size() -
                                    cost_len.n_live_histories_)};
  }
  // we initialize vector width first, so costs are scaled correctly
  void initialize_vector_width(IR::Loop *root) {
    uint32_t eltnumbits = 64;
    containers::TinyVector<IR::Loop *, 15> loopstack{root->getSubLoop()};
    for (IR::Node *N = loopstack.front()->getChild();;) {
      if (auto *I = llvm::dyn_cast<IR::Instruction>(N)) {
        if (auto num_bits = I->getType()->getScalarSizeInBits(); num_bits > 1)
          eltnumbits = std::min(eltnumbits, num_bits);
        N = I->getNext();
        while (!N) {
          if (loopstack.empty()) {
            max_vector_width_ =
              int16_t(max_vector_width_ >> (28 - std::countl_zero(eltnumbits)));
            return;
          }
          N = loopstack.pop_back_val()->getNext();
        }
      } else {
        auto *L = llvm::cast<IR::Loop>(N);
        loopstack.push_back(L);
        N = L->getChild();
      }
    }
  }
  struct SubLoopCounts {
    int nsubloops_, idx_;
  };
  // returns idx of pushed loop transform
  auto pushLoop(IR::Loop *L, ptrdiff_t depth1) -> int {
    int sz = loop_summaries_.size();
    bool reorderable = L->getLegality().reorderable_;
    auto [knowntc, tc] = L->getAffineLoop()->tripCount(depth1);
    loop_summaries_.push_back({.reorderable_ = reorderable,
                               .known_trip_ = knowntc,
                               .reorderable_sub_tree_size_ = 0,
                               .num_reduct_ = 0,
                               .num_sub_loops_ = 0,
                               .trip_count_ = tc});
    return sz;
  }
  /// Used for assemblying dep info
  ///
  /// A tricky thing to handle is assignment of memory ops in valleys;
  /// a loads and stores may both be associated with either the preceding or
  /// following loop nest.
  /// It is important that fits are attributed appropriately.
  /// Consider a matmul example
  ///
  ///     for (int n = 0; n < N; ++n){
  ///       for (int m = 0; m < M; ++m){
  ///         Cmn = 0.0;
  ///         for (int k = 0; k < K; ++k)
  ///           Cmn += A[m,k]*B[k,n];
  ///         C[m,n] += Cmn; // load and store
  ///         Fmn = F[m,n];
  ///         F[m,n] = g(Fmn);
  ///         for (int l = 0; l < L; ++l)
  ///           Fmn += D[m,k]*E[k,n];
  ///         G[m,n] = Fmn; // store
  ///       }
  ///     }
  ///
  /// Following the fuse & nest strategy (discussed in CacheOptimization), we
  /// have
  ///
  /// clang-format off
  ///
  ///     for (int n_c_b = 0; n_c_b < N; n_c_b += n_c){
  ///       for (int m_c_b = 0; m_c_b < M; m_c_b += m_c){
  ///         for (int k_c_b = 0; k_c_b < K; k_c_b += k_c){
  ///           // keep: C[m_c_b+_(0,m_c),n_c_b+_(0,n_c)]
  ///           for (int n_r_b = n_c_b; n_r_b < n_c+n_c_b; n_r_b += n_r){
  ///             // keep: A[m_c_b+_(0,m_c),k_c_b+_(0,k_c)]
  ///             for (int m_r_b = m_c_b; m_r_b < m_c+m_c_b; m_r_b += m_r){
  ///               // keep: B[k_c_b+_(0,k_c),n_r_b+_(0,n_r)]
  ///               Cmn = 0;
  ///               if (k_c_b == 0) Cmn << 0;
  ///               for (int k_r_b = k_c_b; k_r_b < k_c+k_c_b; k_r_b += k_r){
  ///                 Cmn += A[m_r_b+_(0,m_r),k_r_b+_(0,k_r)] *
  ///                        B[k_r_b+_(0,k_r),n_r_b+_(0,n_r)];
  ///               } // k_r_b
  ///               C[m_r_b+_(0,m_r),n_r_b+_(0,n_r)] += Cmn;
  ///             } // m_r_b
  ///           } // n_r_b
  ///         } // k_c_b
  ///         for (int l_c_b = 0; l_c_b < K; l_c_b += l_c){
  ///           for (int n_r_b = n_c_b; n_r_b < n_c+n_c_b; n_r_b += n_r){
  ///             for (int m_r_b = m_c_b; m_r_b < m_c+m_c_b; m_r_b += m_r){
  ///               Fmn = F[m_r_b+_(0,m_r),n_r_b+_(0,n_r)];
  ///               F[m_r_b+_(0,m_r),n_r_b+_(0,n_r)] << g(Fmn);
  ///               if (l_c_b == 0) Fmn << 0;
  ///               for (int l_r_b = l_c_b; l_r_b < l_c+l_c_b; l_r_b += l_r){
  ///                 Fmn += A[m_r_b+_(0,m_r),l_r_b+_(0,l_r)] *
  ///                        B[l_r_b+_(0,l_r),n_r_b+_(0,n_r)];
  ///               } // l_r_b
  ///               G[m_r_b+_(0,m_r),n_r_b+_(0,n_r)] << Fmn;
  ///             } // m_r_b
  ///           } // n_r_b
  ///         } // l_c_b
  ///       } // m_c_b
  ///     } // n_c_b
  ///
  ///
  ///
  /// clang-format on
  ///
  ///
  /// TODO: ensure that problems like this can be split more fully.
  ///
  /// Cache optimization needs to be changed, to recognize that
  /// sub-loops are similar to iterations of a loop at that level,
  /// possibly dumping content.
  ///
  /// The load-from and store-to `C[m,n]` should be attributed to the previous
  /// `DepSummary`, while the load-from and store-to `F` should be attributed to
  /// the following.
  /// Approach: search dependence tree for uses
  /// Attribute fit to all associated trees.
  /// Attribute cost to the first associated tree.
  /// We shall have a current and next `DepSummaryMeta`.
  ///
  /// For now, we aggregate all matching deps. We could consider not
  /// aggregating, and having per-array meta info.
  class DepSummaryMeta {
    using V = std::array<uint16_t, 2>;
    using DS = Cache::CacheOptimizer::DepSummary;
    static_assert(sizeof(Pair<uint16_t, V>) == 6);
    // dict::Binary<uint16_t,Pair<uint16_t,uint16_t>> c_;
    dict::Binary<uint16_t, V> a_{}, b_{}, *prev_,
      *next_; ///< Matches the first three rows of `CacheOptimizer::DepSummary`.
    DS *ds_{nullptr};
    static void update(dict::Binary<uint16_t, V> *d, uint16_t deps,
                       uint16_t costbits, uint16_t fitbits) {
      V &costs = (*d)[deps];
      costs[0] += costbits;
      costs[1] += fitbits;
    }

  public:
    DepSummaryMeta() : prev_{&a_}, next_{&b_} {}
    // Rather than maintain correctness of `prev_` and `next_`, delete methods.
    // If we ever do want to copy and move, we can define them to do the correct
    // thing then.
    DepSummaryMeta(const DepSummaryMeta &) = delete;
    DepSummaryMeta(DepSummaryMeta &&) = delete;
    void pushAddr(IR::Addr *A) {
      // TODO: when offset load/store support is added (i.e., A[i], A[i+1], etc,
      // handling, also update this to use those data structures; multiple
      // offset addresses)
      //
      // For now, we do not consider stores to occupy cache space.
      // This seems to be supported by load vs copy memory bandwidth tests,
      // but not write-bandwidth tests.
      // We assume generally that we have more loads than stores.
      // It is also common for stores will alias a load; we'll need to implement
      // tracking of indidual arrays to better support that.
      // TODO: track individual arrays in `DepSummaryMeta` to better represent
      // costs, would need to compare combined area of their iteration spaces.
      uint16_t costbits = A->getType()->getScalarSizeInBits(),
               fitbits = A->isLoad() ? costbits : 0, deps = A->loopMask();
      bool b = A->fromBehind(), f = A->fromFront();
      // TODO: be smarter about alloting non-hoisted?
      if (f || !b) update(prev_, deps, costbits, fitbits);
      if (b) update(next_, deps, costbits, fitbits);
    }
    auto pushDepSummary(Arena<> *alloc, ptrdiff_t depth0)
      -> Cache::CacheOptimizer::DepSummary * {
      dict::Binary<uint16_t, V> *p = prev_;
      uint16_t *lb = std::ranges::lower_bound(p->keys(), 1 << depth0);
      ptrdiff_t ndeps = p->size(),
                nindependent = std::distance(p->keys().begin(), lb);
      // *lb >= (1<<depth0)
      const auto &f = [=](MutArray<uint16_t, DenseDims<3>> dependent,
                          MutArray<uint16_t, DenseDims<3>> independent) {
        auto keys = p->keys();
        auto vals = p->values();
        for (ptrdiff_t j = 0, k = 0; k < 2; ++k) {
          MutArray<uint16_t, DenseDims<3>> ds = k ? dependent : independent;
          ptrdiff_t D = ptrdiff_t(ds.numCol());
          for (ptrdiff_t i = 0; i < D; ++i) {
            ds[DS::DepInd, i] = keys[i + j];
            auto [cc, fc] = vals[i + j];
            ds[DS::CostInd, i] = cc;
            // In case of all-stores, set fit-coef to cost-coef
            // TODO: maybe we can use non-temporal stores?
            ds[DS::FitInd, i] = fc ? fc : cc;
          }
          j = D;
        }
      };
      DS *ds = DS::create(alloc, depth0, ndeps - nindependent, nindependent, f);
      if (ds_) ds_->setNext(ds);
      ds_ = ds;
      prev_->clear();
      dict::Binary<uint16_t, V> *tmp = prev_;
      prev_ = next_;
      next_ = tmp;
      return ds;
    }
  };
  // For register cost computation, some possible strategies include
  // --- Stack of spills ---
  // Chief problem is that this doesn't track lifetimes.
  // L - BB_0 - defines `x`
  //   - SubLoop_0 - doesn't use `x`
  //   - BB_1
  //   - SubLoop_1 - last use of `x`
  //   - BB_2
  //   - SubLoop_2 - no need to spill `x`
  //   - BB_3
  //
  // Example: `SubLoop_0` is lightweight and doesn't need to spill `x`,
  // but `SubLoop_2` is heavy-weight and spills. We'd want to keep
  // `x` alive through to use `SubLoop_1`, without paying a spill cost.
  //
  // --- Vector of spills ---
  // Solution: store individual spill-sets for each BB
  // and update the one stored in our stack each time we pop a level.
  //
  //
  // OL is the outerloop; we don't bother with toplevel
  template <bool TTI>
  void initialize(IR::Loop *root, target::Machine<TTI> target) {
    invariant(root->getCurrentDepth() == 0);
    initialize_vector_width(root);
    // TODO: build `BBCosts`!!!
    // number of remaining uses for each instruction
    dict::map<IR::Value *, ptrdiff_t> remaining_uses;
    int depth1 = 1; // current depth
    // Uses across BBs are a binary tree, starting at the last BB
    // representing fusion as we move forward; remaining usese don't change
    // TODO: `addUsers` should update all future `bb_state`s so that
    // `interblock_` uses are correct.
    IR::Loop *L = root->getSubLoop(); // current loop
    int nBB = L->getNumBBs();
    Register::BBState bb_state{nBB};

    Register::FutureUses futureuses{.mask_use_sets_ = {},
                                    .max_blk_idx_ = nBB - 1};
    // pairs of count, idx for loop header
    containers::TinyVector<SubLoopCounts, 15> subloop_counts{
      {.nsubloops_ = 0, .idx_ = pushLoop(L, depth1)}};
    IR::Node *V = L->getChild();
    DepSummaryMeta dsm{};
    //
    // iterate over instructions
    // For registers, we have
    // - `currentUse` incrementing and decrementing based on use level
    // - `checkpointCost` whenever exiting a loop (if empty) or decreasing cost,
    //   we add a checkpoint. Costs correspond to cumulative trip count.
    // We add checkpoint to the outermost loop we can.
    // Hoisting out of the cost calculation is limited by loop dependencies of
    // the instruction. We may also need to `markPermanent` to indicate whether
    // considering them for reordering is applicable.
    //
    // Goals:
    // - track trend of prev cleared, to see if we've hit a peak
    // (increasing->deceasing)
    // - mark whether a uf is permanent, i.e. we pay full cost, or not
    //   - pay full cost for anything used in another loop, deeper or shallower
    // - if used by a deeper loop...
    // - if used by a shallower loop...
    // - need maybe spill points
    //
    // So, plan is to use topidx to define bb ranges
    // For an instr, if any users are outside the bb range -> permanent
    // For each loop, we track permanent, temp, and outer spillable separately.
    // On starting a loop, we add existing costs as spillable.
    // We then start tracking that loop's costs on a clean slate.
    bool reg_pres_decreasing{false};
    ptrdiff_t loop_descent1 = 0; // set to depth0 when descending
    CostLengths cost_len{};
    for (;;) {
      // Descend into loop `L`
      // FIXME: handle predicates
      IR::Instruction *I{nullptr};
      bool is_store{false};
      if (auto *SL = llvm::dyn_cast<IR::Loop>(V)) {
        // we descend into `L`
        endBlock(bb_state, futureuses, cost_len, depth1, reg_pres_decreasing);
        L = SL;
        V = SL->getChild();
        max_depth_ = u8(std::max(int(max_depth_), ++depth1));
        reg_pres_decreasing = false;
        cost_len = costLengths();
        ++subloop_counts.back().nsubloops_;
        subloop_counts.push_back(
          {.nsubloops_ = 0, .idx_ = pushLoop(L, depth1)});
        if (loop_descent1) {
          updateLeafDepSummary(dsm, loop_descent1);
          loop_descent1 = 0;
        }
      } else if (auto *A = llvm::dyn_cast<IR::Addr>(V)) {
        addAddrCost(A, depth1, target, cost_len.n_orth_axes_,
                    cost_len.n_conv_axes_);
        dsm.pushAddr(A);
        I = A;
        V = A->getNext();
        is_store = A->isStore();
        if (is_store) {
          if (IR::Instruction *lastuse = futureuses.useOperand(
                remaining_uses, bb_state, depth1, A->getStoredVal())) {
            if (!reg_pres_decreasing) {
              bb_state.checkpoint();
              reg_pres_decreasing = true;
            }
            bb_state.free(lastuse);
          }
        } else {
          // `addUsers` keeps track of instr spills;
          reg_pres_decreasing = false;
        }
      } else if (auto *PN = llvm::dyn_cast<IR::Phi>(V)) {
        I = PN;
        V = PN->getNext();
        // T = A->getType();
        // For a `Phi`, we have two operands, but potentially many users.
        // Consider the case:
        // x = foo();
        // for (..) phi(x,...)
        // for (..) phi(x,...)
        // for (..) phi(x,...)
        // `x` must be reloaded at each of these points, but is then treated as
        // a last-use at the same level.
        // When something is a `phi`'s first arg, it is treated as being used by
        // the previous BB.
        // Similar to `addUsers`, there are four possibilities:
        //  - Either the first or second arg of a phi
        //  - Either an accumulate or join phi
        // v = foo(); // blk?
        // for (int i = 0; i < I; ++i){
        //   w = phi(v, y); // accum phi - uidx?
        //   x = bar(w);
        //   y = qux(x); // blk?
        // }
        // z = phi(v, y); // join phi - uidx?
        if (auto *op = futureuses.useOperand(remaining_uses, bb_state, depth1,
                                             PN->getOperand(PN->isJoinPhi()),
                                             PN->isAccumPhi())) {
          // we only free if `isJoinPhi()`; accumPhi allocated to previous
          // block, and is live through end. Thus, cost should be included
          // in the last checkpoint.
          if (PN->isJoinPhi()) bb_state.free(op);
        } else reg_pres_decreasing = false;
      } else if (auto *C = llvm::dyn_cast<IR::Compute>(V)) {
        addCompCost(C, target, cost_len.n_comp_);
        I = C;
        V = C->getNext();
        // T = A->getType();
        reg_pres_decreasing = futureuses.consumeOperands(
          remaining_uses, bb_state, C, reg_pres_decreasing);
      }
      if (I && !is_store) { // stores have no users
        // means we have users
        invariant(I->getCurrentDepth() == depth1);
        IR::Users &users = I->getUsers();
        auto [usedOutsideBB, m, numUsers] = futureuses.addUsers(
          users, I->loopMask(), bb_state, depth1, bb_state.getBlkIdx());
        remaining_uses[I] = numUsers;
        if (usedOutsideBB || IR::Phi::classof(I)) bb_state.defPerennialVar(m);
        else bb_state.defEphemeralVar(m);
      }
      // advance
      // ptrdiff_t n_bb_end = 0;
      while (!V) {
        SubLoopCounts num_sub_loops_count = subloop_counts.pop_back_val();
        // we've reached the end of a loop, so we pop up
        ptrdiff_t sts = exitLoop(bb_state, futureuses, target, cost_len, depth1,
                                 L, num_sub_loops_count, reg_pres_decreasing);
        // We have more because...
        loop_descent1 = loop_descent1 ? loop_descent1 : depth1;
        if (!--depth1) return updateLeafDepSummary(dsm, loop_descent1);
        loop_summaries_[subloop_counts.back().idx_]
          .reorderable_sub_tree_size_ += sts;
        cost_len = costLengths();
        // interblock_reg_
        // live_counts_
        V = L->getNext();
        L = L->getLoop();
      }
    }
  }
  template <bool TTI>
  auto exitLoop(Register::BBState &bb_state, Register::FutureUses &futureuses,
                target::Machine<TTI> target, CostLengths cost_len,
                ptrdiff_t depth1, IR::Loop *L,
                SubLoopCounts num_sub_loops_count, bool reg_pres_decreasing)
    -> ptrdiff_t {
    // we end block here, as we are about to add
    // more compute costs that are categorized as part of `n_reduct` rather than
    // `n_comp`.
    endBlock(bb_state, futureuses, cost_len, depth1, reg_pres_decreasing);
    ptrdiff_t compute = compute_independence_.size();
    for (auto *P = llvm::dyn_cast_or_null<IR::Phi>(L->getNext()); P;
         P = llvm::dyn_cast_or_null<IR::Phi>(P->getNext())) {
      reductionLatency(P->getOperand(0), cost_counts_, target,
                       max_vector_width_);
      if (auto *C = llvm::dyn_cast<IR::Compute>(P->getOperand(1)))
        addCompCost(C, target, compute);
    }
    ptrdiff_t num_reduct = compute_independence_.size() - compute;
    auto [nsubloops, idx] = num_sub_loops_count;
    LoopSummary &ls = loop_summaries_[idx];
    ls.num_sub_loops_ = nsubloops;
    ls.num_reduct_ = num_reduct;
    return ls.reorderableTreeSize();
  }
  void endBlock(Register::BBState &bb_state, Register::FutureUses &futureuses,
                CostLengths cost_len, ptrdiff_t depth1,
                bool reg_pres_decreasing) {
    // inter block
    futureuses.incrementBlock(interblock_reg_, bb_state.getBlkIdx());
    // intra block, TODO: check point conditionally?
    if (!reg_pres_decreasing) bb_state.checkpoint();
    for (auto RA = bb_state.ephemeral().begin(),
              ER = bb_state.perennial().begin(),
              ERE = bb_state.perennial().end();
         ER != ERE; ++ER, ++RA)
      intrablock_reg_.emplace_back(alloc_, *RA, *ER, depth1);
    cost_counts_.push_back(BBCostCounts(cost_len));
    bb_state.incBB();
  }
  void updateLeafDepSummary(DepSummaryMeta &dsm, ptrdiff_t depth1) {
    Cache::CacheOptimizer::DepSummary *ds =
      dsm.pushDepSummary(alloc_, --depth1);
    if (!leafdepsummary_) leafdepsummary_ = ds;
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
  template <bool TTI>
  void addAddrCost(IR::Addr *A, ptrdiff_t depth1, target::Machine<TTI> target,
                   ptrdiff_t orth_offset, ptrdiff_t conv_offset) {
    IR::OrthogonalAxes oa = A->calcOrthAxes(depth1);
    IR::Addr::Costs rtl =
      A->calcCostContigDiscontig(target, max_vector_width_, cacheline_bits_);
    if (!oa.conv_axes_) {
      // check for duplicate
      if (auto o = std::ranges::find_if(
            orth_axes_[_(orth_offset, end)],
            [=](const auto &oai) -> bool { return oai.orth_ == oa; });
          o != orth_axes_.end())
        o->loadstowcost_[A->isStore()] += rtl;
      else orth_axes_.emplace_back(memCostArray(A, rtl), oa);
    } else if (auto c =
                 std::ranges::find_if(conv_axes_[_(conv_offset, end)],
                                      [=](auto cai) -> bool {
                                        return (cai.first.orth_ == oa) &&
                                               (cai.second == A->indexMatrix());
                                      });
               c != conv_axes_.end())
      c->first.loadstowcost_[A->isStore()] += rtl;
    else
      conv_axes_.emplace_back(Cost::MemCostSummary{memCostArray(A, rtl), oa},
                              A->indexMatrix());
  }
  template <bool TTI>
  void addCompCost(IR::Compute *C, target::Machine<TTI> target,
                   ptrdiff_t comp_offset) {
    uint16_t dep = C->loopMask();
    auto ic = C->getCost(target, max_vector_width_).getValue();
    uint16_t cost = ic ? *ic : std::numeric_limits<uint16_t>::max();
    if (!cost) return;
    if (auto c =
          std::ranges::find_if(compute_independence_[_(comp_offset, end)],
                               [=](const auto &ci) { return ci.mask_ != dep; });
        c != compute_independence_.end())
      c->cost_ = math::add_sat(c->cost_, cost);
    else compute_independence_.emplace_back(cost, dep);
  }
  static constexpr auto memCostArray(IR::Addr *A, IR::Addr::Costs c)
    -> std::array<IR::Addr::Costs, 2> {
    return {
      A->isStore() ? IR::Addr::Costs{} : c,
      A->isStore() ? c : IR::Addr::Costs{},
    };
  }
  /// Fill the `Cache::CacheOptimizer::DepSummary` using the aggregated mem-cost
  /// info. When between two leaves, all loads are allocated to the next, and
  /// stows to the previous.
  /// It also includes first costs.
  /// TODO: first cost calculation, and striding optimization
  /// we may be able to repeatedly re-access costs.
  /// For inner-most loop, we may have multiple fits and costs
  /// TODO: add ArrayTransform to MicroKernelOptimization to track.
  /// For array transforms, should calc total orth and conv subtree sizes.
  /// When strided, we iterate repeatedly, `x = cache_bits/elt_bits` times.
  /// We must have inner-most cache factor be a multiple of `x`.
  /// We can effectively divide cache-consumption of arrays we exclude by `x`,
  /// as we only need to consider 1/x iterations at a time before a full
  /// passover of the strided arrays.
  /// However, for non-strided arrays we wish to include, we must still consider
  /// the cost. Therefore, these must be excluded.
  /// We thus have up to 2 rows of cost:
  /// None-strided
  /// Strideable-strided
  ///
  /// In theory, we could also deliberately stride some but not others to give a
  /// chance for a few to fit, but that'd add complexity and seems unlikely; we
  /// should get a motivating example before considering it.
  ///
  /// We may have
  /// for (n : _(0,N))
  ///   for (m : _(0,M))
  ///     for (k : _(0,K))
  ///       C[m,n] = f(A[m,k],B[k,n],C[m,n],w[k])
  ///
  /// Blocks of B and w can be kept in L1 while iterating
  /// over blocks of A and C.
  /// If `n` is vectorized, striding `B` isn't an option,
  /// but striding `w` is.
  /// We can check that in cache cost fun...
  ///
  ///
  /// Perhaps, should fill `fillDepSummaries` through filling a buffer during
  /// `initialize`, and then filling deps on each decrease->increase in depth
  /// change plus final exit?

public:
  struct OptResult {
    double opt_value_;
    PtrVector<LoopTransform> trfs_;
  };
  auto optimize() -> OptResult {
    ptrdiff_t len = size();
    MutPtrVector<LoopTransform> trfs{math::vector<LoopTransform>(alloc_, len)};
    auto s = alloc_->scope();
    SubCostFn fn{.alloc_ = alloc_,
                 .corewidth_ = target_.getCoreWidth(),
                 .unroll_ = {},
                 .leafdepsummary_ = leafdepsummary_,
                 .caches_ = target_.cacheSummary(),
                 .cachelinebits_ = cacheline_bits_,
                 .register_count_ = int(register_count_),
                 .l2maxvf_ = std::countr_zero(unsigned(max_vector_width_)),
                 .max_depth_ = int(max_depth_)};
    SubCostFn::OptResult state{
      .loop_summaries_ = {.loop_summaries_ = loop_summaries_, .trfs_ = trfs},
      .bb_costs_ = bbcosts(),
      .best_cost_ = std::numeric_limits<double>::max(),
      .phi_costs_ = alloc_->template allocate<double>(len)};
    return {.opt_value_ = fn.optimize(state).best_cost_, .trfs_ = trfs};
  }
  // There is a valid question over costs to apply, and the degree we
  // should be willing to spill registers.
  // E.g., spilling in relatively outer loops that doesn't touch
  // interior loops seems like it ought to be okay.
  //
  // I think the approach should be based on early stopping.
  // What we need are
  // 1. To hoist out register costs, but with trip cost multipliers
  //    that correspond to the depth to which they apply. For example
  //
  //        for (ptrdiff_t n = 0; n < N; ++n){
  //          for (ptrdiff_t m = 0; m < M; ++m){
  //            Cmn = 0.0;
  //            for (ptrdiff_t k = 0; k < K; ++k)
  //               Cmn += A[m*K + k]*B[k*N + n];
  //            C[m*N + n] = Cmn;
  //          }
  //        }
  //
  //    the `Cmn` register cost should be applied to the `m` loop,
  //    but with trip count weight of the `k` loop (i.e. `N*M*K`).
  //    Thus, early stop checks would terminate at excessive `C[m,n]`
  //    unrolling.
  // 2. Early stopping ought to have some concept of things not getting
  //    better, e.g. (most basically) if the register pressure cost is
  //    already more extreme than the best cost so far, no amount of
  //    magical improvement from the other parts of the code is going
  //    to be enough to compensate.
  //    This can be improved by having tighter lower bounds on the remaining
  //    computation cost than `0.0`. These lower bounds should be added
  //    before considering whether to terminate a loop increasing register
  //    costs early.
  // 3. Unrolling some loops doesn't increase register cost, e.g. `k` above.
  //    We need to have some model/recording of whether or not there is
  //    some feature of a loop such that unrolling is expected to increase
  //    performance, or how much, so we can compare to lower bounds.
  //    We need some way to terminate.
  //
  // this is a vector fun, where indexing may do non-trivial computation
  // also, mapping from this vector to loop position isn't trivial either
  // hence, we use a 2 x max_depth matrix that we copy into as we descend
  // (and pop from as we ascend). Row `0` is for inverse values,
  // and row `1` for direct values.
  // Inverses are favored as our costs fns use them more often.
  //
  // We iterate over loops in depth-first pre-order.
  template <bool TTI>
  LoopTreeCostFn(alloc::Arena<> *alloc, IR::Loop *root,
                 target::Machine<TTI> target, int loop_count)
    : alloc_(alloc), target_(target),
      max_vector_width_(target.getVectorRegisterByteWidth()),
      cacheline_bits_(target.cachelineBits()),
      register_count_(u8(target.getNumberOfVectorRegisters())) {
    // TODO: use smallest element size to scale down vector width
    loop_summaries_.reserve(loop_count);
    initialize(root, target);
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
    return loop_summaries_.begin()->reorderableTreeSize();
  }
};

#ifndef NDEBUG
template void LoopTreeCostFn::initialize<true>(IR::Loop *,
                                               target::Machine<true>);
#endif

} // namespace CostModeling::Hard
