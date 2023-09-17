#pragma once
#include <Math/Array.hpp>

namespace poly::CostModeling {
using math::DensePtrMatrix;

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
/// relaxation that plays well, but less so for binary variables like which loop
/// is vectorized. Additionally, patterns such as replacing gather/scatters with
/// shuffle sequences need special handling, that restricts the branch and bound
/// to powers of 2. To be able to build such a cost model, we need to estimate
/// the number of live variables as a result of unroll factors, in order to
/// impose constraints.
///
/// We use soft constraints for register pressuring, representing the
/// store/reload pair of a spill.
///
/// Furthermore, we also need to consider the possibility of dependency chains.
/// Consider, for example
/// for (ptrdiff_t i = 0; i < I; ++i){
///   eltype_t<A> xi = x[i];
///   for (ptrdiff_t j = 0; j < J; ++j)
///     xi += A[i][j] * y[j];
///   x[i] = xi;
/// }
/// The `j` loop itself has a dependency chain.
/// Two options for addressing this:
/// 1. unrolling `j`, cloning the accumulation registers, and reducing at the
/// end.
/// 2. unrolling the `i` loop.
/// The second option is better, but may not be possible, e.g. if there is no
/// `i` loop or it carries some dependency. Thus, we want our model to unroll
/// `i` when legal, and unroll `j` otherwise.
/// Assuming a throughput of 2 fma/cycle and a latency of 4 cycles, an estimate
/// of the cost as a function of I, J, Ui, and Uj is (ignoring vectorization):
/// 4*I*J/min(Ui*Uj, 2*4) + 4*I*log2(Uj)
/// The first term is latency per fma (because of the dependency chain) * the
/// number of iterations, divided by however many unrolling allows us to have
/// inflight. The second term is for the reduction of the cloned `Uj`
/// accumulators. Each step in the reduction has a latency of 4 cycles, and we
/// need to do `log2(Uj)` steps.
///
/// Note, `y-softplus(l*(y-x))/l` is a good smooth minimum function,
/// monotonic in `x` and differentiable everywhere. `l` controls
/// sharpness. Likewise, `y+softplus(l*(x-y))/l` for `max`.
///
/// Thus, a cost function for the above gemv could be something like
/// memcost = I*J*(Ui*Uj*C_{Al} + Uj*C_{yl}) / (Ui*Uj) +
///    I*(C_{xl}*Ui + C_{xs}*Ui) / Ui
/// cthroughput = I*J*(Ui*Uj*C_{t,fma}) / (Ui*Uj) + I*(Ui*C_{t,add}*(Uj-1)) / Ui
/// clatency = I*J*C_{l,fma}/smin(Ui*Uj, C_{l,fma}/C_{t,fma}) +
///    I*C_{l,add}*log2(Uj)
/// cost = memcost + smax(cthroughput, clatency)
/// or, if the it is easier to solve:
/// cost = memcost + cthroughput + clatency
///
/// We may initially want to add a small cost for loop increment and cmp/branch,
/// to encourage unrolling more generally, plus a cost for unrolling to
/// discourse any excess unrolling when it doesn't provide meaningful benefits
/// (representing the general cost of code size/ filling uop cache -- we
/// definitely want loops to fit in the uop cache of any CPU sporting one!!! ).
///
///
///
/// Note that if we had
/// for (ptrdiff_t i = 0; i < I; ++i){
///   eltype_t<A> yi = y[i];
///   for (ptrdiff_t j = 0; j < J; ++j)
///     x[j] += A[i][j] * yi;
/// }
/// then unrolling the `i` loop doesn't increase OOO,
/// but we can assume that as successive `j` iterations are independent/do not
/// have a dependency chain, this isn't an issue.
/// That is, we only consider reductions across the inner-most loop as requiring
/// cloning of accumulators.
///
  /// On throughput modeling, LLVM seems to generally give a recip throughput of 1 for pipelined instructions, regardless of number of ports. This is actually what we want, as this allows RTs to be additive (e.g., we may have a fma that is able to run on 2 ports (e.g. p0 or p5) and a permute that can only execute on one (e.g. p5); when mixing these instructions, they have the same effective cost -- they use a port -- and the more limited port choices of one isn't a problem so long as others can use what remains.
  /// For our purposes, it isn't worth getting too fancy here. It is worth noting that the baseline model presented here
/// https://arxiv.org/pdf/2107.14210.pdf
  /// performed respectively well when compared to vastly more sophisticated tools; for example, it performed similarly well as llvm-mca on most tested architectures!
/// The baseline model used above for loops was
  /// max(1, (n-1)/i, m_r/m, m_w/w)
/// where
  /// n - the number of instructions in the benchmark (-1 because of assumption that the cmp and branch are macro-fused, meaning the last two instructions count as 1)
/// m_r - number of memory reads
/// m_w - number of memory writes
  /// i - the issue width, e.g. 4 for Intel Skylake CPUs. 
  /// m - number of reads the CPU can do per cycle (2 for all in the article)
  /// w - number of writes the CPU can do per cycle (e.g. 2 for Ice Lake and newer, 1 for older)
  /// Unfortunately, we cannot get the CPU-specific information (`i`,`m`,or`w`) from LLVM.
  /// However, these are largely a matter of scale, and are generally correlated. E.g., Intel's Alderlake's values would be 6, 3, and 2, vs the older Skylake's 4, 2, and 1.
  /// While not all the ratios are equal (`w`'s is 2 instead of 1.5), it is unlikely that many optimization decisions are going to be made differently between them.
/// A possible exception is that we may wish to unroll more for CPUs with more out of order execution abilities.
/// `getMaxInterleaveFactor` is an indicator of whether the pipeline might be very narrow.
///
///
/// ///
/// ///
} // namespace poly::CostModeling
