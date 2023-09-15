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
/// ///
} // namespace poly::CostModeling
