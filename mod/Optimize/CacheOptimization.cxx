#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/BitSets.cxx"
#include "Containers/TinyVector.cxx"
#include "Containers/Tuple.cxx"
#include "Math/Array.cxx"
#include "Math/AxisTypes.cxx"
#include "Math/Constructors.cxx"
#include "Math/Indexing.cxx"
#include "Math/MatrixDimensions.cxx"
#include "Math/MultiplicativeInverse.cxx"
#include "Optimize/LeakyReluCost.cxx"
#include "Optimize/LoopTransform.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#else
export module CacheModel;
import Arena;
import Array;
import ArrayConstructors;
import BitSet;
import Comparisons;
import Invariant;
import LeakyReluCost;
import LoopTransform;
import MultiplicativeInverse;
import STL;
import TargetMachine;
import TinyVector;
import Tuple;
#endif

#ifndef USE_MODULE
namespace CostModeling::Cache {
#else
export namespace CostModeling::Cache {
#endif
using containers::Tuple, containers::TinyVector, math::MutPtrMatrix,
  math::MutDensePtrMatrix, math::PtrMatrix, math::DensePtrMatrix,
  math::MutPtrVector, math::matrix, containers::tie, containers::Add,
  math::MutStridedVector, math::StridedVector, math::DenseDims,
  math::StridedDims, math::Array, math::MutArray, utils::invariant, math::last;

/// Our approach is to consider different strategies from the inside-out.
/// We evaluated conditioned on micro-kernel parameters that determine
/// L1->register costs.
/// Each strategy adds different possible constraints and
/// costs. If the number of constraints equals the number of variables, we
/// solve, and use these to continuesolving as we work our way out. Once we
/// reach the end, we need to optimize the cost function w/ respect to free
/// variables if there are any. We must return all the final costs.
///
/// We may also need to recompute some L1 load bandwidth costs?
/// Or, how to handle packing dramatically reducing costs?
/// TODO: add packing optimization at micro-kernel opt-level
///
///
/// Consider this example:
///
///      for (int n = 0; n < N; ++n){
///        for (int m = 0; m < M; ++m){
///          Cmn = 0f0;
///          for (int k = 0; k < K; ++k)
///            Cmn += A[m,k]*B[k,n];
///          C[m,n] = f(Cmn + x[m]);
///        }
///        for (int i = 0; i < I; ++i){
///          Ein = 0f0;
///          for (int j = 0; j < M; ++j)
///            Ein += D[i,j]*C[j,n];
///          E[i,n] = g(Ein + y[i]);
///        }
///      }
///
/// we have n_r, m_r, k_r, i_r, j_r
/// let n_f, m_f, k_f, i_f, j_f be integer-valued cache-factors, so that
/// n_c = n_f*n_r, m_c = m_f*m_r, k_c = k_f*k_r, i_c = i_f*i_r, j_c = j_f*j_r
///
/// L_i = S_iW_i, where `L_i` is the `i`th cache size, `W_i` is the number of
/// ways of the `i`th cache, and `S_i` is the critical stride, i.e. number of
/// sets*cacheline size. We leave reduction loops as the inner-most. We look
/// directly outside, we have
///
/// C: m_r*n_r
/// x: m_r
/// A: m_r*k_c
/// B: k_c*n_r
/// Options:
/// 1. fit m_r*k_c in L1 across iters, loop over n_r in n_c
/// 2. fit k_c*n_r in L1 across iters, loop over m_r in m_c
/// 3. don't fit, instead stream through L1
///
/// Expanding on the constraints and costs of each:
/// L1 use: m_r*k_c + k_c*n_r + m_r*n_r + m_r
/// We need to avoid overloading any cache-ways, thus options 1 and 2 require:
/// m_r*k_c <= S_1*u_A
/// k_c*n_r <= S_1*u_B
/// m_r*n_r <= S_1*u_C // u_C = 1
/// m_r <= S_1*u_X // u_X = 1
/// u_A + u_B + 1 <= W_1
/// `u_A` and `u_B` are positive integers, equal to the number of ways used.
/// Any heuristic for combining `u_C` and `u_X`? Probably that their sum is
/// still below `1`. The entirety of `m_r*k_c` and `k_c*n_r` are touched on each
/// iteration, thus depending on the order, either can be evicted and replaced.
/// We're assuming/hoping that the `m_r*n_r` and `m_r` are scattered enough to
/// avoid evicting.
/// Options `1` and `2` require the three contraints, option `3` does not.
/// Instead, option `3` has the constraint:
/// m_r*k_c >= S_1*u_A
/// k_c*n_r >= S_1*u_B
/// m_r*n_r >= S_1*u_C // u_C = 1
/// m_r >= S_1*u_X // u_X = 1
/// u_A + u_B + 1 >= W_1
/// That is, we've flipped the inequalities. Option 3, which produces greater
/// bandwidth costs, only makes sense when we get to violate these.
/// The above constraint is unbounded, and thus not yet solveable; we'd just get
/// `k_c = K`.
///
/// L2->L1 bandwidth cost for each of the three is:
/// 1. (M/m_r)(N/n_c)(K/k_c)*(m_r*k_c + m_r + (n_c/n_r)*(k_c*n_r + 2*m_r*n_r))
///    = M*(N/n_c)*K + M*(N/n_c)*(K/k_c) + (M/m_r)*N*K + 2*M*N*(K/k_c)
///          A             x                  B             C
/// 2. (M/m_c)(N/n_r)(K/k_c)*(k_c*n_r + (m_c/m_r)*(m_r*k_c + m_r + m_r*n_r))
///    = M*(N/n_r)*K + M*(N/n_r)*(K/k_c) + (M/m_c)*N*K + 2*M*N*(K/k_c)
///          A             x                  B             C
/// 3. (M/m_r)(N/n_r)(K/k_c)*(m_r*k_c + m_r + k_c*n_r + 2*m_r*n_r)
///    = M*(N/n_r)*K + M*(N/n_r)*(K/k_c) + (M/m_r)*N*K + 2*M*N*(K/k_c)
///          A             x                  B             C
/// NOTE: On many CPUs, the L2->L1 bandwidth is sufficiently high, and the L1
/// size sufficiently small, that option 3. is best. But our approach will
/// probably be to carry all options through to the outermost, unless we can
/// prove an option is guarnateed to be dominated.
/// In case of options 1 and 2, we have 3 constraints and 3 unknowns.
/// Using an integer-relaxation, using equality:
/// u_A = m_r*k_c/S_1
/// u_B = k_c*n_r/S_1
/// m_r*k_c/S_1 + k_c*n_r/S_1 + 1 = W_1
/// k_c*(m_r + n_r)/S_1 = W_1 - 1
/// k_c = S_1*(W_1 - 1)/(m_r + n_r)
/// This is an integer-relaxation-value.
/// Should perhaps floor `u_A` and `u_B` above, and then take
/// k_c = floor(min(S_1*u_A/m_r k_c, S_1*u_B/n_r))
/// In the "violate" case, we don't get any constraints, but have the larger
/// L2->L1 bandwidth cost as a result.
///
/// Then for the next loop and L3->L2 bandwidth, we have...
/// Option 1a:
/// fit k_c*n_c in L2 across iters, loop over m_r in m_c
/// Option 1b:
/// don't fit, instead stream through l2
/// Option 2a:
/// fit m_c*k_c + m_c in L2 across iters, loop over n_r in n_c
/// Option 2b:
/// don't fit, instead stream through l2
/// Option 3a:
/// fit k_c*n_c in L2 across iters, loop over m_r in m_c, n_r in n_c
/// Option 3b:
/// fit m_c*k_c + m_c in L2 across iters, loop over n_r in n_c, m_r in m_c
/// Option 3c:
/// don't fit, instead stream through l2
///
/// Fitting in cache is now more difficult, because we touch the entirety of
/// those arrays we discard, but only part of those that we keep. That means,
/// for the order for n_r in n_c, m_r in m_c where we keep `m_c*k_c + m_c`, we
/// iterate over that `m_c` in pieces. The `m_c*n_r` is also iterated in pieces,
/// thus the new loads will be able to evict the old. The `k_c*n_r`, however, is
/// iterated in its entirety for each `n_r`, making it more recently used than
/// all but the last `m_f` when it comes time to evict. Thus, we keep the space
/// for two of these, so that the older one will be least recently used and
/// evicted. We have:
///
/// m_c*k_c = S_2*u_A2
/// k_c*n_r = S_2*u_B2
/// m_c*n_r = S_2*u_C2
/// m_c     = S_2*u_X2 // u_X2 is probably 1
/// W_2 = u_A2 + 2*u_B2 + u_C2 + u_X2
/// unknowns: m_c, u_A2, u_B2, u_C2, u_X2
/// maybe known: k_c, if we're option 2a
/// Thus, in option 2a, we can solve for `m_c`.
/// In option 3b, we will eventually need to solve.
/// Either way, the L3->L2 bandwidth cost assuming we do fit is:
/// (M/m_c)*(K/k_c)*(N/n_c)[ m_c*k_c + m_c + (n_c/n_r) * (k_c*n_r + m_c*n_r) ]
/// M*K*(N/n_c) + M*(K/k_c)*(N/n_c) + (M/m_c)*K*N + M*(K/k_c)*N
///
/// The `don't fit` options defer. If neither fit, we get the previous level's
/// bandwidth cost. If the inner (`m_c`) tile fits, we'd get:
/// (M/m_c)*(K/k_c)*(N/n_c)[ (n_c/n_r) * (m_c*k_c + m_c + k_c*n_r + m_c*n_r) ]
/// M*K*(N/n_c) + M*(K/k_c)*(N/n_c) + (M/m_c)*K*N + M*(K/k_c)*N
///
/// If, in the end, we've defered all the way, we don't do any packing. This is
/// likely of course when there are no reuse opportunities, or the loop sizes
/// are known at compile time to be too small enough for cache tiling and
/// packing to be profitable.
///
///
/// Note that we cannot frame this as a linear program in general, as we can
/// have products of many arguments. It thus isn't necessarilly quadratic
/// either. Branch-and-bound is probably still useful.
///
/// Implementation ideas/thoughts:
/// We care about the history of unrolling.
/// But we need a tree
/// When we have multiple branches/subloops, we want to merge their impacts...
///
/// Particular arrays that are indexed define a history...
/// Lets try and start a stupid-way
///
/// Note that cache tiles can be placed in different orders outside of the
/// microkernel loop, just like unroll orders can vary.
///
/// Our tiling is also layered based on number of cache-layers?
///
/// The first idea to try, I think, as described above, is to build up a big set
/// of possible strategies...
///
/// We want to be able to use the constraints to simplify as many of the loops
/// as we can. Taking the earlier example, let's assume we are using the
/// following orders: clang-format off
///
///     for (int n_c_b = 0; n_c_b < N; n_c_b += n_c){     // held in L3
///       for (int k_c_b = 0; k_c_b < K; k_c_b += k_c){   // held in L2
///         for (int m_c_b = 0; m_c_b < M; m_c_b += m_c){ // held in L2
///           for (int n_r_b = n_c_b; n_r_b < n_c+n_c_b; n_r_b += n_r){ // L2
///             for (int m_r_b = m_c_b; m_r_b < m_c+m_c_b; m_r_b += m_r){
///               Cmn = C[m_r_b+_(0,m_r),n_r_b+_(0,n_r)];
///               if (k_c_b == 0) Cmn << 0;
///               for (int k_r_b = k_c_b; k_r_b < k_c+k_c_b; k_r_b += k_r){
///                 Cmn += A[m_r_b+_(0,m_r),k_r_b+_(0,k_r)] *
///                        B[k_r_b+_(0,k_r),n_r_b+_(0,n_r)];
///               } // k_r_b
///               Cmn += x[m_r_b+_(0,m_r)];
///               C[m_r_b+_(0,m_r),n_r_b+_(0,n_r)] << f(Cmn);
///             } // m_r_b
///           } // n_r_b
///         } // m_c_b
///       } // k_c_b
///       for (int j_c_b = 0; j_c_b < J; j_c_b += j_c){   // held in L2
///         for (int i_c_b = 0; i_c_b < I; i_c_b += i_c){ // held in L2
///           for (int n_r_b = n_c_b; n_r_b < n_c+n_c_b; n_r_b += n_r){ // L2
///             for (int i_r_b = i_c_b; i_r_b < i_c+i_c_b; i_r_b += i_r){
///               Ein = E[i_r_b+_(0,i_r),n_r_b+_(0,n_r)];
///               if (j_c_b == 0) Ein << 0;
///               for (int j_r_b = j_c_b; j_r_b < j_c+j_c_b; j_r_b += j_r){
///                 Ein += D[i_r_b+_(0,i_r),j_r_b+_(0,j_r)] *
///                        C[j_r_b+_(0,j_r),n_r_b+_(0,n_r)];
///               } // j_r_b
///               Ein += y[i_r_b+_(0,i_r)];
///               E[i_r_b+_(0,i_r),n_r_b+_(0,n_r)] << g(Ein);
///             } // j_c_b
///           } // n_r_b
///         } // i_c_b
///       } // j_c_b
///     } // n_c_b
///
/// Above, "held in" means that given slice is held in memory
///
/// Additionally, let's assume we are
/// 1. streaming L2->L1 (nothing is held in L1)
/// 2. holding `m_c`, `k_c`, `i_c`, and `j_c` in L2
/// 3. holding `n_c` in L3.
///
/// Now, we have the following:
/// Having the `n_c_b` loop fused is only likely to be helpful if
/// `(k_c >= K) && (m_c >= M)`
/// Q: should we really keep `n_r` constant across sub-loops?
/// A: Long term, may want to lift that restriction...
/// Q: What sort of legality check do we need?
/// A: We'll restrict cache-tiling to be within the inner-most reorderable-band.
///
/// Let all of these be integer-valued:
/// `x_r` be reg tile size
/// `x_c` be reg tile size
/// `x_f = x_c/x_r` be reg tile size
///
/// We have the following costs:
/// L1 -> L0 = 2*M*N*(K/k_c - 1) + 2*I*N*(J/j_c - 1)
///               C                   E
///   + 2*M*K + 2*N*K + 2*I*J + 2*N*J
///      pA      pB      pD      pC
/// Most of the `L1 -> L0` costs are accounted for in the microkernel cost
/// calculation, but we have additional loads and stores related to the
/// phi-nodes of the reduction loops for each time we must repeat them.
/// The `p*` costs are the pack + unpack costs of the packed arrays.
/// These are added for every level of the memory hierarchy.
/// L2 -> L1 =
///     M*(N/n_r)*K + M*(N/n_r)*(K/k_c) + (M/m_r)*N*K + 2*M*N*(K/k_c)
///          A             x                  B             C
///   + I*(N/n_r)*J + I*(N/n_r)*(J/j_c) + (I/i_r)*N*J + 2*I*N*(J/j_c)
///          D             y                  C             E
///   + 2*M*K + 2*N*K + 2*I*J + 2*N*J
///      pA      pB      pD      pC
/// Held: none, order n_c, k_c, m_c, [n_r, m_r, k_r]
/// Held: none, order n_c, j_c, i_c, [n_r, i_r, j_r]
/// Because we don't hold in L1, we'd have all the tile factors as
/// denominators. However, the order of `k_r_b` and `j_r_b` being
/// inner-most let us hoist those that don't depend on `k` or `j`
/// out, and thus we get the improved `k_c` and `j_c` denominators.
///
/// The exact costs are, for all-reg (`k_r` and `j_r` are inner-most):
/// A: (M/m_c)(N/n_c)(K/k_c) * (m_c/m_r)(n_c/n_r)(k_c/k_r) * m_r*k_r
/// x: (M/m_c)(N/n_c)(K/k_c) * (m_c/m_r)(n_c/n_r) * m_r
/// B: (M/m_c)(N/n_c)(K/k_c) * (m_c/m_r)(n_c/n_r)(k_c/k_r) * k_r*n_r
///
/// C: (N/n_c)*(n_c/n_r)*n_r*[2(M/m_c)(K/k_c)*(m_c/m_r)*m_r +
///                          (I/i_c)(J/j_c)*(i_c/i_r)(j_c/j_r)*j_r]
/// D: (I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r)(j_c/j_r) * i_r*j_r
/// y: (I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r) * i_r
/// E: 2*(I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r) * i_r*n_r
///
/// If we did hold `k_c` and `j_c` in L1, with `m_r` and `i_r` as
/// inner-most regs, we'd instead have:
/// A: (M/m_c)(N/n_c)(K/k_c) * (m_c/m_r)(n_c/n_r) * m_r*k_c
/// x: (M/m_c)(N/n_c)(K/k_c) * (m_c/m_r)(n_c/n_r) * m_r
/// B: (M/m_c)(N/n_c)(K/k_c) * (n_c/n_r) * k_c*n_r
///
/// C: (N/n_c)*(n_c/n_r)*n_r*[2(M/m_c)(K/k_c)*(m_c/m_r)*m_r +
///                          (I/i_c)(J/j_c)*(j_c/j_r)*j_r]
/// D: (I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r) * i_r*j_c
/// y: (I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r) * i_r
/// E: 2*(I/i_c)(N/n_c)(J/j_c) * (i_c/i_r)(n_c/n_r) * i_r*n_r
///
/// The chief difficulties above are
/// 1. `k` is the inner-most `reg` loop, hence, things that don't depend on it
///    drop the cache-factor component of the cost.
/// 2. That we mave multipliers `2*`; we need to store frequencies with deps.
///
/// L3 -> L2 =
///     M*(N/n_c)*K + M*(N/n_c)*(K/k_c) + (M/m_c)*N*K + 2*M*N*(K/k_c)
///          A             x                  B             C
///   + I*(N/n_c)*J + I*(N/n_c)*(J/j_c) + (I/i_c)*N*J + 2*I*N*(J/j_c)
///          D             y                  C             E
///   + 2*M*K + 2*N*K + 2*I*J + 2*N*J
///      pA      pB      pD      pC
/// Held: k_c, m_c, n_r, order n_c, [k_c, m_c, n_r], m_r, k_r
/// Held: j_c, i_c, n_r, order n_c, [j_c, i_c, n_r], i_r, j_r
/// We would have the denominators `k_c`, `m_c`, `j_c`, `i_c`, and
/// `n_r`, but because `n_r` is the inner-most of these, those that
/// don't depend on it are hoisted out and have `n_c` instead.
///
/// We have only `n_r` reg, making it the inner-most.
///
/// A: (M/m_c)(N/n_c)(K/k_c) * m_c*k_c
/// x: (M/m_c)(N/n_c)(K/k_c) * m_c
/// B: (M/m_c)(N/n_c)(K/k_c) * (n_c/n_r) * k_c*n_r
///
/// C: (N/n_c)*(n_c/n_r)*n_r*[2(M/m_c)(K/k_c)*m_c + (I/i_c)(J/j_c)*j_c]
/// D: (I/i_c)(N/n_c)(J/j_r) * i_c*j_c
/// y: (I/i_c)(N/n_c)(J/j_c) * i_c
/// E: 2*(I/i_c)(N/n_c)(J/j_c) * (n_c/n_r) * i_c*n_r
///
///
///
/// RAM -> L3 =
///     M*(N/n_c)*K + M*(N/n_c)*(K/k_c) + N*K + 2*M*N*(K/k_c)
///          A             x               B             C
///   + I*(N/n_c)*J + I*(N/n_c)*(J/j_c) + N*J + 2*I*N*(J/j_c)
///          D             y               C             E
///   + 2*M*K + 2*N*K + 2*I*J + 2*N*J
///      pA      pB      pD      pC
/// Held: n_c, k_c, m_c, order [n_c, k_c, m_c], n_r, m_r, k_r
/// Held: n_c, j_c, i_c, order [n_c, j_c, i_c], n_r, i_r, j_r
/// Because `m_c` and `i_c` are inner-most, we can hoist out:
/// A: (M/m_c)(N/n_c)(K/k_c) * m_c*k_c
/// x: (M/m_c)(N/n_c)(K/k_c) * m_c
/// B: (N/n_c)(K/k_c) * k_c*n_c
///
/// C: (N/n_c)*n_c*[2(M/m_c)(K/k_c)*m_c + (J/j_c)*j_c]
/// D: (I/i_c)(N/n_c)(J/j_r) * i_c*j_c
/// y: (I/i_c)(N/n_c)(J/j_c) * i_c
/// E: 2*(I/i_c)(N/n_c)(J/j_c) * i_c*n_c
///
///
/// We have the following contraints:
/// We assume LRU (least-recently-used) cache.
///
/// Hold in L2:
/// m_c*k_c <= S_2*u_A2
/// k_c*n_r <= S_2*u_B2
/// m_c*n_r <= S_2*u_C2_0
/// m_c     <= S_2*u_X2 // u_X2 is probably 1
/// W_2 >= u_A2 + 2*u_B2 + u_C2_0 + u_X2
/// i_c*j_c <= S_2*u_D2
/// j_c*n_r <= S_2*u_C2_1
/// i_c*n_r <= S_2*u_E2
/// i_c     <= S_2*u_Y2 // u_Y2 is probably 1
/// W_2 >= u_D2 + 2*u_C2_1 + u_E2 + u_Y2
///
///
/// The `2*` comes because it depends on `n_r`
/// Order: n_c, [k_c, m_c, n_r], m_r, k_r
/// A:            1    1          1    1
/// B:      1     1        1           1
/// C:      1          1   1      1
/// `k_r`, `m_r`, `n_r` make the `k_c`, `m_c`, `n_c` slices.
/// When iterating `n_r`, `B[k_c,n_r]` and `C[m_c,n_r]` get
/// replaced.
/// We just iterated over last `m_r*k_c` tile.
/// Therefore, last touched is all of `B[k_c,n_r]`
/// but only last `C[m_r,n_r]`.
/// Thus, incoming `C[m_r,n_r]` can replace old,
/// which has not been touched for longer.
///
/// Perhaps another way to view it is, we only hold a `m_r*n_r` block
/// of `C`, but based on use-pattern, we need `m_c/m_r` of them?
/// Implement whichever is the easier representation, but that is
/// probably the former.
///
/// Basically, when we replace `n_r`, we look at our last `m_r` to
/// say what we touched most recently, and thus how much
/// space we need.
/// `m_r` was most recent, meaning we last touched
/// `A[m_r, k_c]`, `C[m_r, n_r]`, and `B[k_c, n_r]`
/// `B` was touched in entirety, so we need a copy.
///
/// Simplifying, we have:
/// W_2 >= (m_c*k_c)/S_2 + 2*((k_c*n_r)/S_2) + (m_c*n_r)/S_2 + m_c/S_2
/// W_2 >= (i_c*j_c)/S_2 + 2*((j_c*n_r)/S_2) + (i_c*n_r)/S_2 + i_c/S_2
///
/// Hold in L3:
/// m_c*k_c <= S_3*u_A3
/// k_c*n_c <= S_3*u_B3
/// m_c*n_c <= S_3*u_C3_0
/// m_c     <= S_3*u_X3 // u_X3 is probably 1
/// W_3 >= 2*u_A3 + u_B3 + u_C3_0 + u_X3
/// i_c*j_c <= S_3*u_D3
/// j_c*n_c <= S_3*u_C3_1
/// i_c*n_c <= S_3*u_E3
/// i_c     <= S_3*u_Y3 // u_Y3 is probably 1
/// W_3 >= 2*u_D3 + u_C3_1 + u_E3 + u_Y3
///
/// Order: [n_c, k_c, m_c], n_r, m_r, k_r
/// A:            1    1          1    1
/// B:      1     1        1           1
/// C:      1          1   1      1
///
/// When we replace `m_c`, we swap out both
/// `A[m_c, k_c]` and `C[m_c, n_c]`.
///  `n_r` was the most recent, meaning we last touched:
/// `A[m_c, k_c]`, `C[m_c, n_r]`, and `B[k_c, n_r]`
/// `A` was touched in entirety, so we need a copy.
///
/// W_3 >= 2*((m_c*k_c)/S_3) + (k_c*n_c)/S_3 + (m_c*n_c)/S_3 + m_c/S_3
/// W_3 >= 2*((i_c*j_c)/S_3) + (j_c*n_c)/S_3 + (i_c*n_c)/S_3 + i_c/S_3
///
/// So here we have 5 unnkowns:
/// m_c, k_c, i_c, j_c, n_c
/// And four equations:
/// W_2 >= (m_c*k_c)/S_2 + 2*((k_c*n_r)/S_2) + (m_c*n_r)/S_2 + m_c/S_2
/// W_3 >= 2*((m_c*k_c)/S_3) + (k_c*n_c)/S_3 + (m_c*n_c)/S_3 + m_c/S_3
/// W_2 >= (i_c*j_c)/S_2 + 2*((j_c*n_r)/S_2) + (i_c*n_r)/S_2 + i_c/S_2
/// W_3 >= 2*((i_c*j_c)/S_3) + (j_c*n_c)/S_3 + (i_c*n_c)/S_3 + i_c/S_3
///
/// Can we just pick a value, and propogate through?
/// E.g., iterate over
/// for (int m_c = m_r; m_c < M; m_c += m_r){
///   Solve for k_c in:
///   W_2 >= (m_c*k_c)/S_2 + 2*((k_c*n_r)/S_2) + (m_c*n_r)/S_2 + m_c/S_2
///   W_2 - (m_c*n_r)/S_2 - m_c/S_2 >= (m_c*k_c)/S_2 + 2*((k_c*n_r)/S_2)
///   Now, how do we solve through `cld`?
///   Using `W_2 = 16`, `m_c = 160`, `n_r = 14`, `S_2 = 8192`
///   14 >= (160*k_c)/8192 + 2*((14*k_c)/8192)
///   Every 8192/160 = 51.2, first cld increments
///   Every 8192/14 \approx 585.14, second cld increments twice
///   Thus, 585 yields...
///   16 - 1 - 1 == 12 + 2
///   While 586 exceeds, with 16 - 1 - 1 < 12 + 4.
///   Just take the lazy approach for now, and take steps...
///   Next:
///   W_3 >= 2*((m_c*k_c)/S_3) + (k_c*n_c)/S_3 + (m_c*n_c)/S_3 + m_c/S_3
///   11 >=  2*((160*585)/131072) + (585*n_c)/131072 + (160*n_c)/131072 +
///   160/131072 11 >=  2 + (585*n_c)/131072 + (160*n_c)/131072 + 1 8 >=
///   (585*n_c)/131072 + (160*n_c)/131072 Ratios: S_3 / k_c \approx 224.05; S_3
///   / m_c == 819.2 We get n_C via 6*224 + 2 == 8 then cloest multiple of `n_r`
///   (14) that is <=, yielding: n_c = 1344 Next, we have W_2 >= (i_c*j_c)/S_2 +
///   2*((j_c*n_r)/S_2) + (i_c*n_r)/S_2 + i_c/S_2 W_3 >= 2*((i_c*j_c)/S_3) +
///   (j_c*n_c)/S_3 + (i_c*n_c)/S_3 + i_c/S_3 16 >= (i_c*j_c)/8192 +
///   2*((j_c*14)/8192) + (i_c*14)/8192 + i_c/8192 11 >= 2*((i_c*j_c)/131072) +
///   (j_c*1344)/131072 + (i_c*1344)/131072 + i_c/131072 What to do? Solve
///   numerically, with floating point, and then? What happens if we init with
///   bad values?
/// }
///
/// One idea is to do a "bisection" on values of `n_f`, and then
/// recursively descend into sub-loops in a similar manner.
/// Once we've solved for others, we increase `n_c` to the largest value that
/// satisfies the constraints, and measure full cost.
///
/// iterate 1k, 2k, then...
/// if 1024 cost < 2048 cost 512
/// if 1024 cost > 2048 cost 4096
/// (but values rounded to multiple of nearest `x_r`)
///
/// Question: what do we do about different strategies?
/// Can we smartly anchor the bisection around different thresholds?
///
/// e.g.,
/// n_c = 1022
/// W_1 >= (m_r*k_c)/S_1 + (k_c*n_r)/S_1 + (m_r*n_r)/S_1 + m_r/S_1
/// W_2 >= (m_c*k_c)/S_2 + 2*((k_c*n_r)/S_2) + (m_c*n_r)/S_2 + m_c/S_2
/// W_3 >= 2*((m_c*k_c)/S_3) + (k_c*n_c)/S_3 + (m_c*n_c)/S_3 + m_c/S_3
/// 8 >= (16*k_c)/512 + (k_c*14)/512 + (16*14)/512 + 16/512
/// 16 >= (m_c*k_c)/8192 + 2*((k_c*14)/8192) + (m_c*14)/8192 + m_c/8192
/// 11 >= 2*((m_c*k_c)/131072) + (k_c*1022)/131072 + (m_c*1022)/131072 +
/// m_c/131072 m_c = 512 k_c = 256 k_c = 128 k_c = 192 m_c = 256 m_c = 128 Start
/// working on this implementation; we'll have all the constraints and
/// associated costs and the search will be aware of them, ensuring it has
/// explored both sides...
///
/// Another sort of example to consider is
///
///     for (int n = 0; n < N; ++n){
///       for (int m = 0; m < M; ++m){
///         Cmn = 0f0;
///         for (int k = 0; k < K; ++k)
///           Cmn += A[m,k]*B[k,n];
///         C[m,n] = f(Cmn + x[m]);
///         Fmn = 0f0;
///         for (int l = 0; l < L; ++l)
///           Fmn += D[m,l]*E[l,n];
///         F[m,n] = g(Fmn + y[m]);
///       }
///     }
///
/// How do we handle cache across subloops?
/// A problem is replacement:
/// First inner most loop wants
/// m_r*n_r + m_r*k_c + k_c*n_r
/// Second:
/// m_r*n_r + m_r*l_c + l_c*n_r
/// This loop is of course outright worse than splitting...
/// But what if, e.g. `A == D`? Then, we'd have re-use of
/// the tile could would be similar to incrementing
/// `n_r` once, i.e. reuse `A` but need to load the other
/// two. What to do?
/// If `A != D`, we should have a way to check splitting
/// profitability, or even heuristically assume it is.
/// If `A == D`, perhaps still consider it?
/// How to measure cost?
/// Have dependent loops, that don't necessarilly match loop
/// nestings. Above example:
/// n -> m -> k == l
/// May also have
/// n -> m -> k -> l
/// First example
/// n -> m -> k
///  \-> i -> j
///
/// We build traversal-trees based on constraints
/// Except, then costs get more complicated?
/// E.g., if we have
/// n -> m -> k -> l
/// Then correspondence of these to trip or total traversal counts is less
/// clear. Dep flags vs branching values... Could be replaced with dep vectors
/// and indep vectors. For now, we'll solve heuristically, by choosing the
/// larget of the unknown trip counts and matching tile sizes, so that the costs
/// are the same. I.e., we'll always use n -> m -> k == l We use `lcm(k_r, l_r)`
/// for purpose of cache-factor
///
/// If nothing in common, for split.
/// If something in common, test matching dependent loops/equal tile size
/// TODO: splitting is NOT trivial.
/// Check for weakly connected components?
/// Width of connections between loops that need to be stored/reloaded?
/// How to find the narrowest point?
///
/// Have load and store cost for split. Splits should also handle
/// ---
/// ```cpp
///     for (int n = 0; n < N; ++n){
///       for (int m = 0; m < M; ++m){
///         Cmn = 0f0;
///         Dmn = 0f0;
///         for (int k = 0; k < K; ++k){
///           Cmn += A[m,k]*B[k,n];
///           Dmn += A[m,k]*E[k,n];
///         }
///         C[m,n] = f(Cmn + x[m]);
///         D[m,n] = g(Dmn + y[m]);
///       }
///     }
/// ```
/// These can infuence register tiling decisions, and thus should not be
/// handled downstream of register tiling.
/// Ideally, before redundant load elimination?
///
/// clang-format on
///
/// Let us consider how to correctly handle multiple sub-loops.
/// For now, we will take the approach of "dumping" contents, i.e. assuming each
/// subloop wants to use the full cache.
/// This can be viewed as approximating a loop over the subloops, but where each
/// loop iteration does something different (i.e. evaluate a different subloop).
///
/// Any tile not indexed by a sub-loop or deeper contributes to the cache-fit of
/// all sub-loops, but to the fit-cost of only one of them.
///
/// Our buffer can store arrays sorted by indices; makes dropping as we
/// exit a loop natural.
///
/// Any tile indexed by a subloop or descendent is evicted, unless it is used
/// by the next -- and the next has a matching tile size. If ever evicted (e.g.,
/// not used by all), it would need to be reloaded.
///
/// For handling sub-loops of `i`, there are two possibilities:
/// 1. Fuse & nest: We fuse just the `+= i_c` loops.
/// 2. Fuse & fuse: We fuse the `+= i_c` and `+= i_r` loops.
///
/// ### Fuse & nest:
///
/// The significance of the latter is that it requires also fusing the sub-loop
/// tile sizes.
/// Implications of the former are that we can and must share tiles indexed only
/// by the common loops `i` and those exterior to `i`, but we can solve interior
/// loops indepdently. They will fully iterate inside, so we do not have special
/// considerations there.
/// This also makes dependencies less of a concern, so long as `i` doesn't carry
/// any.
/// When taking this approach, the subloops are marked as effectively always
/// changing.
///
/// clang-format off
///
///     for (int i = 0; i < I; ++i){
///       for (int j0 = 0; j0 < J0; ++j0){ A[i,j0]; B[j0]; C[i]; }
///       for (int j1 = 0; j1 < J1; ++j1){ D[i,j1]; E[j1]; F[i]; }
///       for (int j2 = 0; j2 < J2; ++j2){ G[i,j2]; H[j2]; X[i]; }
///     }
///
/// This can turn into
///
///     for (int i_c_b = 0; ic_b < I; i_c_b += i_c){
///       // change: C[i_c_b+_(0,i_c)];
///       for (int j0_c_b = 0; j0_c_b < J0; j0_c_b += j0_c){
///         // change: B[j0_c_b+_(0,j0_c)];
///         // const:  C[i_c_b+_(0,i_c)];
///         for (int i_r_b = i_c_b; i_r_b < i_c_b+i_c; i_c_b += i_c){
///           // const:  B[j0_c_b+_(0,j0_c)];
///           // change: C[i_r_b+_(0,i_r)];
///           for (int j0_r_b = j0_c_b; j0_r_b < j0_c_b+j0_c; j0_c_b += j0_r){
///             // change: A[i_r_b+_(0,i_r), j0_r_b+_(0,j0_r)];
///             // change: B[j0_r_b+_(0,j0_r)];
///             // const:  C[i_r_b+_(0,i_r)];
///           }
///         }
///       }
///       for (int j1_c_b = 0; j1_c_b < J1; j1_c_b += j1_c){
///         for (int i_r_b = i_c_b; i_r_b < i_c_b+i_c; i_c_b += i_c){
///           for (int j1_r_b = j1_c_b; j1_r_b < j1_c_b+j1_c; j1_c_b += j1_r){
///             A[i_r_b+_(0,i_r), j1_r_b+_(0,j1_r)];
///             B[j1_r_b+_(0,j1_r)];
///             C[i_r_b+_(0,i_r)];
///           }
///         }
///       }
///       for (int j2_c_b = 0; j2_c_b < J2; j2_c_b += j2_c){
///         for (int i_r_b = i_c_b; i_r_b < i_c_b+i_c; i_c_b += i_c){
///           for (int j2_r_b = j2_c_b; j2_r_b < j2_c_b+j2_c; j2_c_b += j2_r){
///             A[i_r_b+_(0,i_r), j2_r_b+_(0,j2_r)];
///             B[j2_r_b+_(0,j2_r)];
///             C[i_r_b+_(0,i_r)];
///           }
///         }
///       }
///     }
///
/// clang-format on
///
/// All we must do is avoid the optimization of reversing `j*_c_b`, as we can't
/// hold anyway.
///
/// ### Fuse & fuse:
/// This involves interleaving the subloops, and lock their cache tile sizes.
/// This allows reuse between subloops, but requires they not carry dependencies
/// either. We do not necessarilly need to fuse all, e.g. we could fuse only the
/// first subloop, and then take a nesting approach from there.
/// TODO: implement this as an option to consider; it is likely to yield better
/// perf in some circumstances.
///
///
struct CacheOptimizer {
  // using S = double;
  // using T = math::MultiplicativeInverse<S>;
  // using T = int;
  struct Loop {
    uint32_t cache_factor_ : 22;
    uint32_t reg_factor_ : 10;
    uint32_t known_trip_ : 1;
    uint32_t trip_count_ : 31;
    // equals known_trip_ ? cld(trip_count_, cache_factor_) :
    //                     trip_count_ / cache_factor_;
    double cache_trip_count_;
    // cumulative counts precede this
    double cumulative_tf_;
    double cumulative_cf_;
    double phi_cost_; ///< cost in cycles of spilling phis
    constexpr Loop(uint16_t reg_factor, bool known_trip, int trip_count,
                   double phi_cost)
      : reg_factor_(reg_factor - 1), known_trip_(known_trip),
        trip_count_(trip_count), phi_cost_(phi_cost) {
      utils::invariant(trip_count_ > 0);
    }
    // for trivial default constructibility
    constexpr Loop() = default;
    constexpr auto reg_factor() const -> uint32_t { return reg_factor_ + 1; }
    constexpr auto maxCacheFactor() const -> int {
      return int(math::cld(trip_count_, reg_factor()));
    }
    constexpr auto setCacheFactor(int cache_factor) -> double {
      utils::invariant(cache_factor > 0);
      int ru = reg_factor(), cfr = cache_factor * ru;
      utils::invariant(cfr < int(trip_count_) + ru);
      cache_factor_ = cache_factor;
      cache_trip_count_ = known_trip_ ? math::cld<int>(trip_count_, cfr)
                                      : double(trip_count_) / cfr;
      return cache_trip_count_;
    }
    // get cumulative trip including this
    [[nodiscard]] constexpr auto cumulativeTripCountInclusive() const
      -> double {
      return cumulative_tf_ * cache_trip_count_;
    }
    [[nodiscard]] constexpr auto cumulativeCacheFactorInclusive() const
      -> double {
      return cumulative_cf_ * cache_factor_;
    }
    constexpr void setCumulative(const Loop &l) {
      cumulative_tf_ = l.cumulativeTripCountInclusive();
      cumulative_cf_ = l.cumulativeCacheFactorInclusive();
    }
    constexpr void initCumulative() {
      cumulative_tf_ = 1.0;
      cumulative_cf_ = 1.0;
    }
  };
  static_assert(sizeof(Loop) == 40);
  static_assert(std::is_trivially_default_constructible_v<Loop> &&
                std::is_trivially_destructible_v<Loop>);
  TinyVector<Loop, 15> unrolls_;
  constexpr auto setCacheFactor(ptrdiff_t depth0, int cache_factor) -> double {
    Loop &l = unrolls_[depth0];
    double tf = l.setCacheFactor(cache_factor);
    if (++depth0 < unrolls_.size()) {
      Loop &li = unrolls_[depth0];
      li.cumulative_cf_ = cache_factor * l.cumulative_cf_;
      li.cumulative_tf_ = tf * l.cumulative_tf_;
    }
    return tf;
  }
  struct PopBack {
    TinyVector<Loop, 15> &unrolls_;
    ~PopBack() { unrolls_.pop_back(); }
  };
  auto pushLoop(LoopSummary loopinfo, int reg_factor, double phi_cost)
    -> PopBack {
    int trip_count = int(loopinfo.estimatedTripCount());
    Loop l{Loop(reg_factor, loopinfo.knownTrip(), trip_count, phi_cost)};
    if (!unrolls_.empty()) l.setCumulative(unrolls_.back());
    else l.initCumulative();
    unrolls_.push_back(l);
    return {unrolls_};
  }
  /// The 5 rows are for each array (dep and indep):
  /// 0. Dep flag.
  /// 1. Fit-count, i.e. how many unique array-index pairs there are.
  /// 2. Cost-count, i.e. how much movement is associated (arrays that are read
  ///    and written count double).
  /// 3. Flags indicating whether we need two copies, based on # cache tiles.
  ///    The mask contains `depth0-1` entries, for iterating over 2..depth0
  ///    cache tiles. `1` is excluded, as no need for duplicates there.
  ///    `depth1` is excluded, as that is handled by `4.`:
  /// 4. Flags indicating whether we need two copies, based on inner-most cache
  ///    loop.
  /// 5. Product of register tile sizes
  /// Additionally, we have, for each cache level:
  /// 0. Max grid size to fit in that cache level.
  /// 1. If some but not all arrays can be made to fit in cache
  ///    via striding accesses, yields those.
  /// `3`, `4`, `5`  are `undef`; we fill them
  /// TODO: Store precomputed inner-most grid values
  struct DepSummary {
    static constexpr ptrdiff_t R = 6;
    static constexpr ptrdiff_t DepInd = 0;
    static constexpr ptrdiff_t FitInd = 1;
    static constexpr ptrdiff_t CostInd = 2;
    static constexpr ptrdiff_t CpyInd = 3;
    static constexpr ptrdiff_t CpyOuterInd = 4;
    static constexpr ptrdiff_t RegSzInd = 5;
    constexpr auto dependent() -> MutArray<uint16_t, DenseDims<R>> {
      return {ptr_, {{}, math::col(ndependent_)}};
    }
    constexpr auto independent() -> MutArray<uint16_t, DenseDims<R>> {
      return {ptr_ + (R * ndependent_), {{}, math::col(nindependent_)}};
    }
    [[nodiscard]] constexpr auto dependent() const
      -> Array<uint16_t, DenseDims<R>> {
      return {ptr_, {{}, math::col(ndependent_)}};
    }
    [[nodiscard]] constexpr auto independent() const
      -> Array<uint16_t, DenseDims<R>> {
      return {ptr_ + (R * ndependent_), {{}, math::col(nindependent_)}};
    }
    [[nodiscard]] constexpr auto numDependent() const -> ptrdiff_t {
      return ndependent_;
    }
    [[nodiscard]] constexpr auto numInependent() const -> ptrdiff_t {
      return nindependent_;
    }
    [[nodiscard]] constexpr auto vectorMask() const -> uint_fast16_t {
      return vector_mask_;
    }
    // the bits are ordered
    // idx depth0-1,..., idx 0
    // [innermost,..., outermost-1]
    // So, in our matmul example,
    // idx = 0 correponds to `m`
    // idx = 1 correponds to `k`
    // excludes actual outer-most
    [[nodiscard]] constexpr auto mustStoreOldDep() const
      -> PtrVector<uint16_t> {
      return dependent()[CpyOuterInd, _];
    }
    [[nodiscard]] constexpr auto mustStoreOldIndep() const
      -> PtrVector<uint16_t> {
      return independent()[CpyOuterInd, _];
    }
    [[nodiscard]] constexpr auto fitCoefDep() const -> PtrVector<uint16_t> {
      return dependent()[FitInd, _];
    }
    [[nodiscard]] constexpr auto fitCoefIndep() const -> PtrVector<uint16_t> {
      return independent()[FitInd, _];
    }
    [[nodiscard]] constexpr auto maxInnerTileStrided() const
      -> std::array<uint16_t, 4> {
      return max_tile_inner_strided_;
    }
    [[nodiscard]] constexpr auto maxInnerTileNoStride() const
      -> std::array<uint16_t, 4> {
      return max_tile_inner_nostride_;
    }
    DepSummary() = delete;
    DepSummary(const DepSummary &) = delete;

    /// Receives the two blocks, must fill them correctly.
    ///
    /// @param f(dependent, infependent) - receives the two arrays as inputs,
    ///        and is in charge of initializing them.
    static auto create(alloc::Arena<> *alloc, ptrdiff_t depth0,
                       ptrdiff_t ndependent, ptrdiff_t nindependent,
                       const auto &f)
      -> DepSummary *requires(
        std::invocable<decltype(f), MutArray<uint16_t, DenseDims<3>>,
                       MutArray<uint16_t, DenseDims<3>>>) {
        DepSummary *ds = alloc->allocate<DepSummary>(
          R * sizeof(uint16_t) * (ndependent + nindependent) +
          sizeof(DepSummary));
        ds->ndependent_ = ndependent;
        ds->nindependent_ = nindependent;
        ds->next_ = nullptr;
        f(ds->dependent()[_(0, 3), _], ds->independent()[_(0, 3), _]);
        ds->fillCountDeps(depth0);
        return ds;
      }

    ///
    ///
    /// @param f(ptr, ndeps, depth0) - receives the pointer;
    ///       must fill it correctly
    static auto create(alloc::Arena<> *alloc, ptrdiff_t depth0, ptrdiff_t ndeps,
                       const auto &f)
      -> DepSummary *requires(requires(decltype(f) ff, uint16_t *p,
                                       ptrdiff_t ndep, ptrdiff_t d0) {
        { ff(p, ndep, d0) } -> std::same_as<ptrdiff_t>;
      }) {
        DepSummary *ds = alloc->template allocate<DepSummary>(
          (R * sizeof(uint16_t) * ndeps) + sizeof(DepSummary));
        ds->ndependent_ = f(ds->ptr_, ndeps, depth0);
        ds->nindependent_ = ndeps - ds->ndependent_;
        ds->next_ = nullptr;
        ds->fillCountDeps(depth0);
      }

    constexpr void setNext(DepSummary *next) {
      next_ = next;
    }
    constexpr auto getNext() const -> DepSummary * { return next_; }

    // // c is depidx
    // // i is the loop we make inner-most
    // // returns the rotated dep mask for `getFreq`.
    // constexpr auto rotatedDepMask(ptrdiff_t c, ptrdiff_t i,
    //                               ptrdiff_t depth0) -> uint32_t {
    //   utils::invariant(i > 0);
    //   utils::invariant(i <= depth0);
    //   ptrdiff_t b = c - ndependent_;
    //   bool isdependent = b < 0;
    //   uint32_t d{isdependent ? dependent()[0, c] : independent()[0, b]},
    //     depi = (d >> i) & 1, depl = d & ((1 << i) - 1),
    //     depu = (d & (~((1 << (i + 1)) - 1))) >> 1,
    //     dr = (d << (depth0 + 1)) | ((depi << depth0) | d) | depl | depu;
    //   return dr;
    // }

    static auto
    maxSatisfactoryValue(PtrVector<int> sizes, PtrVector<uint16_t> counts,
                         math::MultiplicativeInverse<int64_t> stride, int ways,
                         int64_t maxcf) -> int {
      if (ways <= 0) return 0;
      // (cld(coefs.num * x, stride) * (coefs.double + 1)).sum() <= ways
      //
      // we find the max intege value via first finding the floating
      // point solution
      // x = ways * stride / (coefs.num * (coefs.double + 1)).sum()
      int64_t a = sizes * counts.t();
      if (!a) return maxcf;
      a = int64_t(ways * double(int64_t(stride)) / double(a));
      invariant(a >= 0);
      if (!a) return 0;
      int64_t maxcf_rw = ways;
      for (auto [s, c] : std::views::zip(sizes, counts))
        maxcf_rw -= cld(int64_t(c) * s * maxcf, stride);
      invariant(maxcf_rw < ways);
      if (maxcf_rw >= 0) return maxcf;
      // d is an over-estimate; we calculate how many it uses, which
      // versus `ways` tells us how mmany we must remove.
      // While scanning, we also accumulate the top two contendors
      // for decrementing.
      for (;;) {
        int64_t excess_ways = -ways, largest = 0;
        // NOTE: we previously had `count` mean an actual count, and placed it
        // outside the `cld`, however, it has since been replaced with a
        // bitcount. We may wish to add some form of count again, so that we can
        // give each individual array at least one way.
        // As is, we have to be careful about placement of arrays when packing,
        // which may not always be possible in complicated programs.
        //
        // int count_largest = 0;
        for (auto [s, c] : std::views::zip(sizes, counts)) {
          if (!s) continue;
          int64_t sz = int64_t(c) * s, prod = sz * a;
          excess_ways += cld(prod, stride);
          int64_t z = ((prod / stride) * stride) / sz;
          largest = std::max(largest, z);
          // count_largest = z == largest ? count_largest + c : c;
        }
        if (excess_ways <= 0) return a;
        if (!largest) return 0;
        if (excess_ways == 1) return largest;
        a = largest - (a == largest);
      }
    }
    static auto maximalSatisfactoryValueOuter(
      PtrVector<int> sizes, PtrVector<uint16_t> counts,
      math::MultiplicativeInverse<int64_t> stride,
      PtrVector<uint16_t> must_store, int64_t maxcf, int d, int w) -> int {
      if (w <= 0) return 0;
      // (cld(coefs.num * x, stride) * (1 + coefs.double)).sum() <= ways
      // similar to...
      // ways = \sum ((1 + coefs.double)*(coefs.num * x) / stride )
      // ways * stride = x * \sum ((1 + coefs.double)*(coefs.num) )
      // x = ways * stride / \sum ((1 + coefs.double)*(coefs.num) )
      //
      // Thus, we find the max intege value via first finding the floating
      // point solution
      // x = ways * stride / (coefs.num * (1 + coefs.double)).sum()
      int64_t a = 0, maxcf_rw = w;
      for (auto [s, c, m] : std::views::zip(sizes, counts, must_store)) {
        int64_t sz = int64_t(c) * s * (1 + ((m >> d) & 1));
        a += sz;
        maxcf_rw -= cld(sz * maxcf, stride);
      }
      if (!a) return maxcf;
      a = int64_t(w * double(int64_t(stride)) / double(a));
      utils::invariant(a >= 0);
      if (!a) return 0;
      if (maxcf_rw >= 0) return maxcf;
      // d is an over-estimate; we calculate how many it uses, which
      // versus `ways` tells us how mmany we must remove.
      // While scanning, we also accumulate the top two contendors
      // for decrementing.
      for (;;) {
        int64_t excess_ways = -w, largest = 0;
        for (auto [s, c, m] : std::views::zip(sizes, counts, must_store)) {
          if (!s) continue;
          int64_t sz = int64_t(c) * s * (1 + ((m >> d) & 1)), prod = sz * a;
          excess_ways += cld(prod, stride);
          int64_t z = ((prod / stride) * stride) / sz;
          largest = std::max(largest, z);
        }
        if (excess_ways <= 0) return a;
        if (!largest) return 0;
        if (excess_ways == 1) return largest;
        a = largest - (a == largest);
      }
    }
    void maxSatValueOutermost(PtrVector<int> szIndep, PtrVector<int> szDep,
                              int maxcf, target::MachineCore::Cache c,
                              MutPtrVector<int> gc) const {
      PtrVector<uint16_t> msoi{mustStoreOldIndep()}, msod{mustStoreOldDep()};
      for (ptrdiff_t d = 0, depth0 = gc.size(); d < depth0; ++d) {
        int w = c.associativty_;
        for (auto [sz, cnt, m] : std::views::zip(szIndep, fitCoefIndep(), msoi))
          w -= cld((sz * int64_t(cnt)) << ((m >> d) & 1), c.stride_);
        gc[d] = maximalSatisfactoryValueOuter(szDep, fitCoefDep(), c.stride_,
                                              msod, maxcf, d, w);
      }
    }
    void maxSatVictimValue(DensePtrMatrix<int> szIndep,
                           MutDensePtrMatrix<int> szDep,
                           target::MachineCore::Cache c,
                           MutArray<int, StridedDims<2>> grid, int gin) const {
      // utils::invariant(grid.numRow());
      PtrVector<uint16_t> sizesDepReg{dependent()[5, _]},
        sizesIndepReg{independent()[5, _]}, counts{fitCoefDep()};
      utils::invariant(ptrdiff_t(grid.numCol()) + 1 == szDep.numRow());
      for (ptrdiff_t d = 0, d0 = ptrdiff_t(grid.numCol()); d < d0; ++d) {
        // offsets iterate through previous cache sets (offset < d), and sets
        // max allowed cache factor based on their value. offset == d indicates
        // no sub-blocks fit in a previous cache level, and thus no sub-blocks
        // can be removed from the victim cache. We choose the max of these
        // values for the grid.
        int ways = c.associativty_;
        // keep iterating until we find an improvement
        for (ptrdiff_t i = 0; i < szIndep.numCol(); ++i) {
          int64_t cnt = fitCoefIndep()[i], sz = szIndep[d, i];
          sz -= d > 0 ? szIndep[d - 1, i] : sizesIndepReg[i];
          ways -= cld(sz * cnt, c.stride_);
        }
        for (ptrdiff_t i = 0; i < szDep.numCol(); ++i)
          szDep[d, i] -= d > 0 ? szDep[d - 1, i] : sizesDepReg[i];
        int maxcf = d > 0 ? grid[0, d - 1] : gin;
        grid[1, d] =
          maxSatisfactoryValue(szDep[d, _], counts, c.stride_, ways, maxcf);
        for (ptrdiff_t i = 0; i < szDep.numCol(); ++i)
          szDep[d, i] += d > 0 ? szDep[d - 1, i] : sizesDepReg[i];
      }
    }
    // Two rows from grid, as we may subtract prev in case of victim-like cache.
    // We define victim caches as either exclusive caches, or non-inclusive
    // caches where loading data does not automatically insert it into the cache
    // (e.g. Skylake-X's L3).
    void maxSatVictimValueOutermost(DensePtrMatrix<int> szIndep,
                                    MutDensePtrMatrix<int> szDep,
                                    target::MachineCore::Cache c,
                                    MutArray<int, DenseDims<2>> grid,
                                    ptrdiff_t d0, ptrdiff_t ic) const {
      PtrVector<uint16_t> msoi{mustStoreOldIndep()}, msod{mustStoreOldDep()},
        counts{fitCoefDep()}, sizesDepReg{dependent()[5, _]},
        sizesIndepReg{independent()[5, _]};
      MutPtrVector<int> sizes{szDep[last, _]};
      int maxcf = grid[0, ic + d0 - 2];
      if (!maxcf) {
        grid[1, _(0, d0) + (d0 - 1 + ic)].zero();
        return;
      }
      for (ptrdiff_t d = 0, dm = d0 - 1, a = dm - 1; d < d0; ++d) {
        int ways = c.associativty_;
        for (ptrdiff_t i = 0; i < szIndep.numCol(); ++i) {
          uint_fast16_t m = msoi[i];
          int64_t cnt = fitCoefIndep()[i],
                  sz = szIndep[dm, i] << ((m >> d) & 1);
          sz -= (a >= 0) ? szIndep[a, i] : sizesIndepReg[i];
          ways -= cld(sz * cnt, c.stride_);
        }
        for (ptrdiff_t i = 0; i < sizes.size(); ++i) {
          int &sz = sizes[i];
          if ((msod[i] >> d) & 1) sz <<= 1; // scale on first iter
          sz -= a >= 0 ? szDep[a, i] : sizesDepReg[i];
        }
        // Because we handle mask-scaling here, we can call the non-outer
        // version
        // max value: d0-1 + d0-1 + 2 = 2d0
        // grid size= 2d0 + 1
        grid[1, d + (dm + ic)] =
          maxSatisfactoryValue(sizes, counts, c.stride_, ways, maxcf);
        utils::invariant(grid[1, d + (dm + ic)] <= grid[1, dm + ic - 1]);
        // undo adjustment
        for (ptrdiff_t i = 0; i < sizes.size(); ++i) {
          int &sz = sizes[i];
          sz += a >= 0 ? szDep[a, i] : sizesDepReg[i];
          if ((msod[i] >> d) & 1) sz >>= 1;
        }
      }
    }
    int remainingWaysIndep(target::MachineCore::Cache c,
                           PtrVector<int> sizes) const {
      int ways = c.associativty_;
      for (auto [size, count] : std::views::zip(sizes, fitCoefIndep()))
        ways -= cld(int64_t(size) * count, c.stride_);
      return ways;
    }
    void maxSatValue(DensePtrMatrix<int> szIndep, DensePtrMatrix<int> szDep,
                     int maxcf, target::MachineCore::Cache c,
                     MutPtrVector<int> grid, ptrdiff_t ic) const {
      for (ptrdiff_t d = 0, D = grid.size() - ic; d < D; ++d) {
        int ways = remainingWaysIndep(c, szIndep[d, _]);
        grid[d + ic] = maxSatisfactoryValue(szDep[d, _], fitCoefDep(),
                                            c.stride_, ways, maxcf);
        utils::invariant(grid[d + ic] <= grid[d + ic - 1]);
      }
    }

    static auto getRegSize(const LoopTransform trfs[15], uint_fast16_t deps)
      -> int {
      int size = 1;
      for (int64_t j : containers::BitSet64::fromMask(deps))
        size *= trfs[j].reg_factor();
      return size;
    }
    using Cache = target::MachineCore::Cache;

    void initRegTileSizes(const TinyVector<Cache, 4> &caches,
                          LoopSummary loopinfo, LoopTransform trf,
                          LoopSummaries ls, int cachelinebits) {
      // forrward to static function to avoid bugs of using `this` in place of
      // `cur`.
      initRegTileSizes(this, caches, loopinfo, trf, ls, cachelinebits);
      invariant(nonzeroInnerCandidates());
    }
    // bits: [0, ..., nostride, stride]
    constexpr auto nonzeroInnerCandidates() const -> unsigned {
      bool stride = false, nostride = false;
      for (ptrdiff_t i = 0; i < 4; ++i) {
        stride |= max_tile_inner_strided_[i] != 0;
        nostride |= max_tile_inner_nostride_[i] != 0;
      }
      return (unsigned(nostride) << 1) | unsigned(stride);
    }
    constexpr auto log2firstCaceStride() const -> uint32_t { return l2stride_; }

  private:
    // TODO: Must be called prior to optimization
    /// Initialize the `DepSummary` chain
    static void initRegTileSizes(DepSummary *cur,
                                 const TinyVector<Cache, 4> &caches,
                                 LoopSummary loopinfo, LoopTransform trf,
                                 LoopSummaries ls, int cachelinebits) {
      // looptrfs marks which loops are vectorized, important for striding, and
      // lets us fill the `unrolls` correctly
      ptrdiff_t depth0 = 0;
      LoopTransform trfs[15];
      int subloopcnts[15];
      trfs[0] = trf;
      bool vectorized = trf.l2vector_width_, init = false;
      // bits: 0..., inner, ..., outer
      uint_fast16_t vector_mask = 0;
      for (;;) {
        if (init) {
          trfs[depth0] = ls.trfs_.front();
          vectorized = trfs[depth0].l2vector_width_;
          tie(loopinfo, ls) = ls.popFront();
        } else init = true;
        ptrdiff_t nsubloops = loopinfo.numSubLoops();
        vector_mask |= (vectorized << depth0);
        if (!nsubloops) { // we're at a leaf; fill unrolled-sizes
          MutArray<uint16_t, DenseDims<R>> indep{cur->independent()};
          cur->vector_mask_ = vector_mask;
          std::array<int, 4> ways{};
          for (int i = 0; i < caches.size(); ++i)
            ways[i] = caches[i].associativty_;
          for (ptrdiff_t c = 0; c < cur->nindependent_; ++c) {
            int64_t sz = getRegSize(trfs, indep[DepInd, c]);
            for (int i = 0; i < caches.size(); ++i)
              ways[i] -= cld(sz * indep[FitInd, c], caches[i].stride_);
            indep[RegSzInd, c] = sz;
          }
          // We must always pay the full cost of independent arrays
          MutArray<uint16_t, DenseDims<R>> dep{cur->dependent()};
          unsigned stride = std::numeric_limits<unsigned>::max();
          for (ptrdiff_t i = 0; i < cur->ndependent_; ++i) {
            uint_fast16_t d = dep[DepInd, i];
            int sz = getRegSize(trfs, d);
            dep[RegSzInd, i] = sz;
            // can't keep if it depends on the second from outermost
            unsigned keep = !((d >> (depth0 - 1)) & 1), isvec = vector_mask & d;
            // if keep, isvec determines whether we can't stride.
            // Bits: [0, ..., 0, nostride, canstride]
            if (!(keep & (!isvec))) continue;
            // estimate stride; TODO: improve estimate via propogating better
            // information here? Currently, we only have `fit_coef`, the total
            // number of bits.
            // Currently, e.g., would interpret two 32-bit loads as equivalent
            // to one 64-bit load. The current approach is at least
            // 1. Correct when there is only 1 array.
            // 2. Conservative, otherwise.
            uint32_t bits_per_elem =
              std::min(uint32_t(64), uint32_t(dep[FitInd, i]));
            stride = std::min(stride,
                              unsigned(cachelinebits >>
                                       (31 - std::countl_zero(bits_per_elem))));
          }
          // handles numeric_limits<unsigned>::max case.
          int l2stride = cur->l2stride_ = std::countr_zero(stride);
          int maxcf = math::cld(loopinfo.estimatedTripCount(),
                                ptrdiff_t(trfs[depth0].reg_factor()));
          cur->maxSatisfactoryValueInner(caches, l2stride, ways, maxcf,
                                         vector_mask, depth0);
          // exit loops
          for (;;) {
            vector_mask &= ~(1 << depth0);
            // We shouldn't have multiple disjoint sets -- they should always be
            // optimized separately -- so finishishing the outer-most loop means
            // that we are done.
            if (!depth0) return;
            int &cnt = subloopcnts[--depth0];
            utils::invariant(cnt > 0);
            if (--cnt) break;
          }
          cur = cur->getNext();
        } else {
          // we will descend more
          subloopcnts[depth0++] = nsubloops;
        }
      }
      // TODO: fit inner grid sizes
    }
    void maxSatisfactoryValueInner(const TinyVector<Cache, 4> &caches,
                                   int l2stride, std::array<int, 4> ways,
                                   int64_t maxcf, uint_fast16_t vector_mask,
                                   ptrdiff_t depth0) {
      ptrdiff_t ncache = caches.size();
      unsigned maskon = 0;
      // extra ways are init to 0
      for (ptrdiff_t i = 0; i < 4; ++i) {
        invariant(ways[i] >= 0);
        bool g = ways[i] > 0 && i < ncache;
        maskon |= (unsigned(g) << i);
        max_tile_inner_strided_[i] = 0;
        max_tile_inner_nostride_[i] = 0;
      }
      invariant(maskon);
      // if (!maskon) return;
      math::Array<uint16_t, DenseDims<R>> dep{dependent()};
      PtrVector<uint16_t> sizes{dep[RegSzInd, _]}, counts{dep[FitInd, _]},
        deps{dep[DepInd, _]};
      // bool canstride = flag & 1, nostride = flag & 2;
      // (cld(coefs.num * x, stride) * (coefs.double + 1)).sum() <= ways
      //
      // we find the max intege value via first finding the floating
      // point solution
      // x = ways * stride / (coefs.num * (coefs.double + 1)).sum()
      std::array<int, 4> best_possible_stride = ways,
                         best_possible_nostride = ways;
      int64_t totalmemstride = 0, totalmemnostride = 0;
      bool keptvec = false, keptnovec = false;
      for (auto [s, c, d] : std::views::zip(sizes, counts, deps)) {
        bool keep = !((d >> (depth0 - 1)) & 1), isvec = vector_mask & d;
        keptvec |= (keep && isvec);
        keptnovec |= (keep && !isvec);
        // if !keep, we do not stride; cost is / (cache line size/eltsize)
        // if keep && !isvec, we can stride
        // if keep && isvec, we cannot stride
        int64_t mem = c * s;
        totalmemnostride += mem;
        totalmemstride += (!keep || isvec) ? mem >> l2stride : mem;
        for (ptrdiff_t i = 0; i < 4; ++i) {
          best_possible_stride[i] -= c >> l2stride;
          best_possible_nostride[i] -= c;
        }
      }
      // no need to stride if we set maxcf to nostride
      if (!totalmemstride)
        return fillMasked(max_tile_inner_nostride_, maxcf, maskon);
      // as an optimization, we skip doing both strided and not strided if not
      // necessary.
      // It is only necessary if `keptvec && keptnovec`
      // We do masknostride if none are kept.
      utils::invariant(maskon != 0);
      unsigned masknostride = (keptvec || !keptnovec) ? maskon : 0,
               maskstride = keptnovec ? maskon : 0;
      utils::invariant(masknostride | maskstride);
      // If we have a victim cache we do want to handle `nostride`, as then we
      // need to set this smaller value for fitting. Similarly, if some
      // architectures can do more loads/cycle when loading from the same
      // cacheline (not yet supported).
      if (!masknostride && std::ranges::any_of(
                             caches, [](Cache c) -> bool { return c.victim_; }))
        masknostride = maskon;
      std::array<int64_t, 4> astride, anostride;
      {
        double totalmemstrided = totalmemstride,
               totalmemnostrided = totalmemnostride;
        unsigned fitstride = 0, fitnostride = 0;
        for (ptrdiff_t i = 0; i < ncache; ++i) {
          // # remainig ways * mem per way
          double mem = ways[i] * double(int64_t(caches[i].stride_));
          // `x` should be a multiple of `1<<l2stride`
          int64_t x = int64_t(mem / totalmemstrided) & (-1 << l2stride);
          int64_t y = int64_t(mem / totalmemnostrided);
          utils::invariant(x >= 0);
          utils::invariant(y >= 0);
          astride[i] = x;
          anostride[i] = y;
          bool fitx = (x > 0) | (best_possible_stride[i] >= 0);
          bool fity = (y > 0) | (best_possible_nostride[i] >= 0);
          fitstride |= (unsigned(fitx) << i);
          fitnostride |= (unsigned(fity) << i);
        }
        maskstride &= fitstride;
        masknostride &= fitnostride;
        invariant(maskstride || masknostride);
        if (!(maskstride || masknostride)) return;
      }
      std::array<int, 4> maxcf_rw_stride = ways, maxcf_rw_nostride = ways;
      for (auto [s, c, d] : std::views::zip(sizes, counts, deps)) {
        bool keep = !((d >> (depth0 - 1)) & 1), isvec = vector_mask & d;
        int64_t mem = s * maxcf,
                memstride = (!keep || isvec) ? mem >> l2stride : mem;
        for (ptrdiff_t i = 0; i < ncache; ++i) {
          maxcf_rw_stride[i] -= cld(c * memstride, caches[i].stride_);
          maxcf_rw_nostride[i] -= cld(c * mem, caches[i].stride_);
        }
      }
      {
        unsigned incompletestride = 0, incompletenostride = 0;
        for (ptrdiff_t i = 0; i < 4; ++i) {
          unsigned m = 1 << i;
          if ((maskstride & m) && (maxcf_rw_stride[i] >= 0))
            max_tile_inner_strided_[i] = maxcf;
          else incompletestride |= m;
          if ((masknostride & m) && (maxcf_rw_nostride[i] >= 0))
            max_tile_inner_nostride_[i] = maxcf;
          else incompletenostride |= m;
        }
        maskstride &= incompletestride;
        masknostride &= incompletenostride;
        if (!(maskstride || masknostride)) return;
      }
      // d is an over-estimate; we calculate how many it uses, which
      // versus `ways` tells us how mmany we must remove.
      // While scanning, we also accumulate the top two contendors
      // for decrementing.
      for (;;) {
        std::array<int64_t, 4> excess_ways, excess_ways_stride, largest{},
          largest_stride{};
        for (ptrdiff_t i = 0; i < 4; ++i) {
          excess_ways[i] = -ways[i];
          excess_ways_stride[i] = -ways[i];
        }
        for (auto [s, c, d] : std::views::zip(sizes, counts, deps)) {
          if (!s) continue;
          bool keep = !((d >> (depth0 - 1)) & 1), isvec = vector_mask & d;
          int64_t sz = int64_t(s) * c;
          for (ptrdiff_t i = 0; i < ncache; ++i) {
            auto x = caches[i].stride_;
            if (masknostride & (1 << i)) {
              int64_t prod = sz * anostride[i];
              utils::invariant(anostride[i] <= maxcf);
              excess_ways[i] += cld(prod, x);
              int64_t z = ((prod / x) * x) / sz;
              utils::invariant(z <= maxcf);
              largest[i] = std::max(largest[i], z);
            }
            if (maskstride & (1 << i)) {
              int64_t prod = sz * astride[i];
              utils::invariant(astride[i] <= maxcf);
              prod = (!keep || isvec) ? prod >> l2stride : prod;
              excess_ways_stride[i] += cld(prod, caches[i].stride_);
              int64_t z = (((prod / x) * x) / sz) & (-1 << l2stride);
              utils::invariant(z <= maxcf);
              largest_stride[i] = std::max(largest_stride[i], z);
            }
          }
        }
        unsigned incompletestride = 0, incompletenostride = 0;
        for (ptrdiff_t i = 0; i < 4; ++i) {
          incompletenostride |=
            update_masked_iter(masknostride, i, largest, excess_ways, anostride,
                               max_tile_inner_nostride_);
          incompletestride |= update_masked_iter(maskstride, i, largest_stride,
                                                 excess_ways_stride, astride,
                                                 max_tile_inner_strided_);
        }
        maskstride &= incompletestride;
        masknostride &= incompletenostride;
        if (!(maskstride || masknostride)) return;
      }
    }
    static constexpr auto update_masked_iter(
      unsigned mask, ptrdiff_t i, const std::array<int64_t, 4> &largest,
      std::array<int64_t, 4> &excess_ways, std::array<int64_t, 4> &a,
      std::array<uint16_t, 4> &max_tile) -> unsigned {
      if (mask & (1 << i)) {
        if (excess_ways[i] <= 0) {
          max_tile[i] = a[i];
          return 0;
        }
        if (!largest[i]) {
          max_tile[i] = 0;
          return 0;
        }
        if (excess_ways[i] == 1) {
          max_tile[i] = largest[i];
          return 0;
        }
        a[i] = largest[i] - (a[i] == largest[i]);
        return (1 << i);
      }
      return 0;
    }
    void fillCountDeps(ptrdiff_t depth0) {
      MutArray<uint16_t, DenseDims<R>> dep{dependent()}, indep{independent()};
      std::array<PtrVector<uint16_t>, 2> deps{dep[DepInd, _], indep[DepInd, _]};
      for (ptrdiff_t i = 0; i < 2; ++i) {
        MutArray<uint16_t, DenseDims<R>> countdeps{i == 0 ? dep : indep};
        for (ptrdiff_t c = 0; c < countdeps.numCol(); ++c) {
          uint_fast16_t d = countdeps[0, c], m = 0, o = 0;
          for (ptrdiff_t j = depth0;;) {
            o = (o << 1) | checkRequiresOldOuter(deps, d, j);
            if (!--j) break;
            m = (m << 1) | checkRequiresOld(deps, depth0 - j, d);
          }
          countdeps[3, c] = m;
          countdeps[4, c] = o;
        }
      }
    }

    // do we need to keep the old op around?
    // When iterating on results later, we use call with `reg == depth0-1`
    // first, and with `reg == 1` last.
    static auto checkRequiresOld(std::array<PtrVector<uint16_t>, 2> deps,
                                 ptrdiff_t reg, uint32_t d) -> bool {
      utils::assume(reg > 0);
      uint32_t reg_mask{uint32_t((1 << reg) - 1)}, br = reg_mask & d,
                                                   bc = d >> reg;
      // Using the matmul example, when we have
      //   innermost  outermost
      //   cache      reg
      //      k m     n
      // A: [ 1 1 ] [ 0 ]
      // B: [ 1 0 ] [ 1 ]
      // C: [ 0 1 ] [ 1 ]
      // `A` has some accessed less recently than `B`.
      // because we need:
      // 1. There to be another dep that doesn't depend on most rapidly
      //    changing ind (`m`, above).
      // 2. That dep to have an ind that changes more slowly.
      // 3. That dep to have an ind that changes at least as rapidly.
      //   innermost  outermost
      //   cache      reg
      //      k     m n
      // A: [ 1 ] [ 1 0 ]
      // B: [ 1 ] [ 0 1 ]
      // C: [ 0 ] [ 1 1 ]
      //
      //  What about
      // A: [ 1 1 1 0 1 ] [ 0 ]
      // B: [ 0 1 0 0 1 ] [ 1 ]
      // `A` again needs to be held
      if (bc < 1) return false;
      const auto f = [=](uint32_t a) -> bool {
        uint32_t ar = reg_mask & a, ac = a >> reg;
        if (ac == bc) return false;
        if (std::countl_zero(ar) <= std::countl_zero(br)) return false;
        return checkCacheDep(ac, bc);
      };
      return std::ranges::any_of(deps[0], f) || std::ranges::any_of(deps[1], f);
    }
    static auto checkRequiresOldOuter(std::array<PtrVector<uint16_t>, 2> deps,
                                      uint32_t b, ptrdiff_t inner) -> bool {
      //   cache      reg
      //      k m n
      // A: [ 1 1 0 ] [ ]
      // B: [ 1 0 1 ] [ ]
      // C: [ 0 1 1 ] [ ]
      // Then it depends on the ordering of the cache tiles
      // Placing `m` as the inner-most, we effectively have
      //  f-iters  | len/c iters
      //      k  n | m
      // A: [ 1 0 ]  1
      // B: [ 1 1 ]  0
      // C: [ 0 1 ]  1
      // So that `A` needs `2*`, to avoid evicting `B`.
      // With `k` as inner
      //  f-iters  | len/c iters
      //      m  n | k
      // A: [ 1 0 ]  1
      // B: [ 0 1 ]  1
      // C: [ 1 1 ]  0
      // `A` again needs to be held, to avoid evicting `C`.
      if (b < 1) return false;
      uint32_t lon = 1 << inner, loff = ~lon;
      if ((b & lon) == 0) return false;
      uint32_t bloff = b & loff;
      const auto f = [=](uint32_t a) -> bool {
        if ((a == b) || (a & lon)) return false;
        return checkCacheDep(a & loff, bloff);
      };
      return std::ranges::any_of(deps[0], f) || std::ranges::any_of(deps[1], f);
    }
    static void fillMasked(std::array<uint16_t, 4> &a, uint16_t x,
                           unsigned maskon) {
      for (ptrdiff_t i = 0; i < 4; ++i)
        if (maskon & (1 << i)) a[i] = x;
    }

    ptrdiff_t ndependent_, nindependent_;
    uint32_t vector_mask_, l2stride_;
    DepSummary *next_;
    // strided values are larger than non-strided, so
    // non-stride idx is `0`, strided `1`; smaller values should have smaller
    // idx for use in scanning.
    std::array<uint16_t, 4>
      max_tile_inner_strided_; ///< Max inner-most tile sizes for each cache
                               ///< level, striding all strideable arrays that
                               ///< are kept in the cache. This means we must
                               ///< stream any vectorized arrays kept in cache.
    std::array<uint16_t, 4>
      max_tile_inner_nostride_; ///< Max inner-most tile sizes for each level,
                                ///< without striding any arrays. Thus, no
                                ///< arrays kept in cache must be streamed.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
    // NOLINTNEXTLINE(modernize-avoid-c-arrays) // FAM
    uint16_t ptr_[];
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  };

  using Cache = target::MachineCore::Cache;
  // 4 is current greatest, on some Broadwell chips, as well as Lion Cove
  containers::TinyVector<Cache, 4> caches_;
  int cachelinebits_;
  alloc::Arena<> alloc_;
  // Constraint as function of the innermost loop.
  // This is used for indicating both the boundaies around which we
  // increment the number of ways used.
  struct InnerMostConstraint {
    struct Cost {
      double tf_{0.0}, cnst_{0.0};
      constexpr auto operator()(double trip_factor) const -> double {
        return (tf_ * trip_factor) + cnst_;
      }

    private:
      friend constexpr auto operator*(Cost d, double x) -> Cost {
        return {.tf_ = d.tf_ * x, .cnst_ = d.cnst_ * x};
      }
      friend constexpr auto operator*(double x, Cost d) -> Cost {
        return {.tf_ = d.tf_ * x, .cnst_ = d.cnst_ * x};
      }
    };
    struct Cost3 {
      double ctf_{0.0}, cf_{0.0}, tf_{0.0}, cnst_{0.0};
      constexpr auto operator()(double cache_factor, double trip_factor) const
        -> double {
        return ((cache_factor * ctf_ + tf_) * trip_factor + cnst_) +
               (cache_factor * cf_);
      }
      constexpr auto operator+=(Cost3 c) -> Cost3 & {
        ctf_ += c.ctf_; // cache_factor * trip_factor
        cf_ += c.cf_;   // cache_factor
        tf_ += c.tf_;   // trip_factor
        cnst_ += c.cnst_;
        return *this;
      }
      void addDependent(Cost c) {
        ctf_ += c.tf_;
        cf_ += c.cnst_;
      }
      void addIndependent(Cost c) {
        tf_ += c.tf_;
        cnst_ += c.cnst_;
      }
      void add(Cost c, bool isdependent) {
        isdependent ? addDependent(c) : addIndependent(c);
      }

    private:
      friend constexpr auto operator*(Cost3 d, double x) -> Cost3 {
        return {.ctf_ = d.ctf_ * x,
                .cf_ = d.cf_ * x,
                .tf_ = d.tf_ * x,
                .cnst_ = d.cnst_ * x};
      }
      friend constexpr auto operator*(double x, Cost3 d) -> Cost3 {
        return {.ctf_ = d.ctf_ * x,
                .cf_ = d.cf_ * x,
                .tf_ = d.tf_ * x,
                .cnst_ = d.cnst_ * x};
      }
      friend constexpr auto operator+(Cost3 x, Cost3 y) -> Cost3 {
        return {.ctf_ = x.ctf_ + y.ctf_,
                .cf_ = x.cf_ + y.cf_,
                .tf_ = x.tf_ + y.tf_,
                .cnst_ = x.cnst_ + y.cnst_};
      }
    };
    // 4 quadrants:
    //                    #reg_loops  cache_loops
    // indep of innermost
    // dep on innermost
    // #cache-tiled goes from _(0,depth1), i.e. [0, depth1)
    // outer-most contains `depth1`, but is `depth0` instances,
    // with depth0-indexed loops from `_(1, depth1)`, i.e.
    // skip outer-most. These correspond to which cache-loop
    // we place inner-most among cache-loops.
    // They are ordered outer, inner (skipping the outer-most)
    // so `coefs_[_, 2*depth0]` places the inner-most loop
    // as the inner-most cache loop, and `coefs_[_, 2*depth0-1]` places the
    // second-from-innermost as the inner-most cache loop.
    //
    // as col# increases, so does size, while cost decreases
    //
    // # deps x depth1, each col gives sizes for fitting col idx + 1

  public:
    [[nodiscard]] constexpr auto numDeps() const -> ptrdiff_t {
      return num_dependent_ + num_independent_;
    }
    [[nodiscard]] constexpr auto numDependent() const -> ptrdiff_t {
      return num_dependent_;
    }
    [[nodiscard]] constexpr auto numIndependent() const -> ptrdiff_t {
      return num_independent_;
    }
    [[nodiscard]] constexpr auto depth0() const -> ptrdiff_t { return depth0_; }
    [[nodiscard]] constexpr auto chainLength() const -> ptrdiff_t {
      return chain_len_;
    }
    // bits: [0, ..., nostride, stride]
    [[nodiscard]] constexpr auto innerTileFactorFlag() const -> unsigned {
      return inner_tile_factor_flag_;
    }
    auto streamCost() -> Cost & { return stream_cost_; }
    /// ((tf_ * cache_factor) + cnst_) * trip_factor;
    [[nodiscard]] auto streamCost(double cache_factor, double trip_factor) const
      -> double {
      return stream_cost_(cache_factor) * trip_factor;
    }

    /// `chain_len` x `depth1` matrix.
    /// - Rows: which cache loop we make inner-most; `0` not eligible
    /// - Cols: How many cache-loops, 1,..,depth1
    /// Cost3 reduces cost to function
    auto cost() -> MutDensePtrMatrix<Cost3> {
      return {costPtr(),
              {math::row(chain_len_),
               math::col(depth0_ + std::popcount(inner_tile_factor_flag_))}};
    }
    /// depth0 x numDependent()
    /// They exclude the inner-most loop; that value is applied later in
    /// inner-optimization
    /// Rows are for number of tiling loops, first idx corresponds to 2.
    auto cacheFitDep() -> MutDensePtrMatrix<int> {
      return {cacheFitPtr(), {math::row(depth0_), math::col(numDependent())}};
    }
    /// depth0 x numIndependent()
    /// They exclude the inner-most loop; that value is applied later in
    /// inner-optimization
    /// Rows are for number of tiling loops, first idx corresponds to 2.
    auto cacheFitIndep() -> MutDensePtrMatrix<int> {
      return {cacheFitPtr() + (numDependent() * depth0_),
              {math::row(depth0_), math::col(numIndependent())}};
    }
    // # deps, 1 bits for which deps placed first require a 2x for fitting all
    // in cache, as we consider placing any in middle.
    // # [dependent + 1] x [1 + depth0 + depth0]
    // Columns:
    // [0, depth0): # of cache tiles
    // [depth0, depth0+depth0): # which do we place as inner-most?
    // For cost calculation, we have freq vs size
    // We have costs...
    // Costs are frequency * size
    // Frequency: trip_count - out_of_band_tc + 1
    // `trip_count` and `out_of_band_tc` may be functions of innermost
    // `cache_trip_count_`
    // `trip_count` may be a function of innermost `cache_factor_`
    //
    // Streaming (no-fit) frequency is product of all trip factors and all cache
    // factors. None of the fit-based costs include cache-factor
    InnerMostConstraint(alloc::Arena<> *alloc_, ptrdiff_t depth0,
                        ptrdiff_t ndependent, ptrdiff_t nindependent,
                        ptrdiff_t chain_len, unsigned inner_tile_factor_flag)
      : depth0_(depth0), num_dependent_(ndependent),
        num_independent_(nindependent), chain_len_(chain_len),
        inner_tile_factor_flag_(inner_tile_factor_flag) {
      data_ = alloc_->allocate<sizeof(double)>(bytesRequired());
    }

  private:
    void *data_;
    ptrdiff_t depth0_, num_dependent_, num_independent_, chain_len_;
    unsigned inner_tile_factor_flag_;
    // tf needs inner-most cache factor as a multiple
    // cnst does not.
    // Both need the inner-most cache trip count.
    Cost stream_cost_{.tf_ = 0.0, .cnst_ = 0.0};
    // auto costOffset() const -> ptrdiff_t {
    //   return sizeof(double) * (num_dependent_ + 1);
    // }
    // auto cacheFitOffset()const -> ptrdiff_t {
    //   return costOffset() +
    //          sizeof(Cost) * (num_dependent_ + 1) * depth0_ * (depth0_ + 1);
    // }
    [[nodiscard]] constexpr auto cacheFitOffset() const -> ptrdiff_t {
      return ptrdiff_t(sizeof(Cost3)) * chain_len_ *
             (depth0_ + std::popcount(inner_tile_factor_flag_));
    }
    [[nodiscard]] constexpr auto bytesRequired() const -> ptrdiff_t {
      return cacheFitOffset() +
             (ptrdiff_t(sizeof(int)) * numDeps() * (depth0_ + 1));
    }
    // auto costPtr() -> Cost * {
    //   return reinterpret_cast<Cost *>(static_cast<char *>(data_) +
    //                                   costOffset());
    // }
    [[nodiscard]] auto costPtr() const -> Cost3 * {
      return reinterpret_cast<Cost3 *>(static_cast<char *>(data_));
    }
    [[nodiscard]] auto cacheFitPtr() const -> int * {
      return reinterpret_cast<int *>(static_cast<char *>(data_) +
                                     cacheFitOffset());
    }
  };

  static auto checkCacheDep(uint32_t ac, uint32_t bc) -> bool {
    if (std::countl_zero(ac) > std::countl_zero(bc)) return false;
    uint32_t acs = ac, bcs = bc;
    for (;;) {
      uint32_t acrz = std::countr_zero(acs), bcrz = std::countr_zero(bcs);
      if (bcrz != acrz) return bcrz > acrz;
      acs >>= ++acrz;
      bcs >>= ++bcrz;
      if (bcs == 0) return false;
    }
  }
  /// fill cache fits with sizes (product of cache tile sizes) and the
  /// `fit_coef`.
  static void fillTileSizes(MutStridedVector<int> tile_size,
                            const TinyVector<Loop, 15> &unrolls, uint16_t deps,
                            uint32_t cpy_mask, ptrdiff_t depth0, int size) {
    for (ptrdiff_t reg = depth0; reg;) {
      // column index is # cache - 1, from 0...depth0-1
      if ((deps >> (--reg)) & 1) size *= unrolls[reg].cache_factor_;
      // we don't copy if the associated loop isn't actually unrolled
      // n,m,k
      // A[m,k]*B[k,n]
      // reg==1: reg = [n_r], cache = [m_c,k_c]
      // size = reg_size * m_c
      // something that doesn't depend on `m`, but does depend on `n`,
      // is a candidate for `cpy_mask`.
      // Commented out below is an alternate implementation, that checks for the
      // inner-most ind exterior to `reg` that it is dependent on.
      // However, this shouldn't be possible; we only need an extra
      // copy when changing rapidly, i.e. it's the very next ind that
      // we depend on, so using `reg - 1` should be correct.
      // See `checkRequiresOld` for more details.
      // int sz = size;
      // if (cpy_mask & 1) {
      //   if (reg) {
      //     // m corresponds to only the exterior loops
      //     uint32_t m = deps & ~((1 << reg) - 1);
      //     if (m && (unrolls[31 - std::countl_zero(m)].cache_factor_ > 1))
      //       sz <<= 1;
      //   } else sz <<= 1;
      // }
      // tile_size[depth0 - 1 - reg] = sz;
      bool cpy =
        (cpy_mask & 1) && (!reg || (unrolls[reg - 1].cache_factor_ > 1));
      tile_size[depth0 - 1 - reg] = size << (cpy);
      cpy_mask >>= 1;
    }
    // for (ptrdiff_t reg = depth0; reg;) {
    //   // column index is # cache - 1, from 0...depth0-1
    //   cache_fit[depth0 - reg] = size * (1 + int(oldcopy & 1));
    //   oldcopy >>= 1;
    //   if ((deps >> (--reg)) & 1) size *= unrolls[reg].cache_factor_;
    // }
    // cache_fit[depth0] = size;
  }

  /// deps go `outer->inner`
  /// for a bitfield, that means outer occupies the right-most bits
  /// [0-padding..., inner, ..., outer]
  /// This produces an updated-dep-mask for the purpose of cache-optimization.
  /// outer->inner:
  /// [ n, m, k]
  /// reg = 1, i.e. reg = [n], returns [m_c, k_c, n_r]
  /// reg = 2, i.e. reg = [n, m], returns [k_c, n_r, m_r]
  static constexpr auto rotateDepMask(uint32_t deps, uint32_t reg,
                                      uint32_t cache) -> uint32_t {
    uint32_t c = deps >> reg, r = ((1 << reg) - 1) & deps;
    return c | (r << cache);
  }
  // assumes dep `dr` has been rotated to reflect position within loop-nest,
  // i.e., if we have `n_c, m_c, k_c, n_r, m_r, k_r`
  // then `[n,m,k]` should be rotated to reflect the subset
  // E.g., for `n_r`, we should have
  // `[m_c, k_c, n_r]`, as `n_r` is the inner-most loop of the tile.
  // Note, bits are in reverse order, i.e. index 0 is right-most.
  // `idx_depth` refers to num-reg
  static auto getFreq(const containers::TinyVector<double, 29> &freqs,
                      ptrdiff_t depth0, uint32_t dr, ptrdiff_t nct,
                      ptrdiff_t inner_idx, ptrdiff_t chain_len)
    -> InnerMostConstraint::Cost {
    // dr is [0..., cache tiles..., loops over cache tiles...]
    // if depth1 = 3, nct will = 0...2, corresponding to 1..3 cache tiles
    // We peel off nct+1 cache tiles:
    // 0 + 31 - 4 = 27
    // 2 + 31 - 4 = 29
    // Note, we shift out 1, because `nct = 0` corresponds to 1 cache tile,
    // e.g. in the matmul example
    //     for n, m, k
    //       C[m,n] += A[m,k] * B[k,n]
    // we have tile sizes of
    // C: m_r x n_r; dr = 011011
    // A: m_r x k_c; dr = 110110
    // B: k_c x n_r; dr = 101101
    // fitting uses `k_c`, but the movement is across `m_r` tiles.
    // Hoisting means not depending on `m`, i.e. we can hoist `B`'s
    // strip when `nct = 0`. We can see this because
    // 0...0101101 << 27 == 011010...0
    dr <<= (nct + 31 - 2 * depth0);
    // we can hoist it out of lz loops
    uint32_t lz = std::countl_zero(dr);
    // freqs is [ loops over cache tiles..., cache tiles... ]
    // nct = 0: 6 - 2 - 0 = 4
    // nct = 2: 6 - 2 - 2 = 2
    // freqs = [N/n_c, N/n_c*M/m_c, N/n_c*M/m_c*K/k_c,
    //         N/n_c*M/m_c*K/k_c*n_f, N/n_c*M/m_c*K/k_c*n_f*m_f]
    ptrdiff_t idx = (2 * depth0) - nct - lz;

    double f = freqs[idx], tf = idx >= inner_idx ? f : 0.0,
           cnst = idx >= inner_idx ? 0.0 : f;
    // depband is the width of the band of deps, e.g. if we have `[a, b, c, d]`
    // and depend on `a`, `b`, and `d`, the band is `[a, b]`, so depband = 2.
    // Here, we subtract the frequency saved through order-reversals
    uint32_t depband = std::countl_one(dr <<= lz);
    // We only subtract for reversal if we don't have a subloop.
    // Otherwise, the subloop prevents keeping it in cache.
    utils::invariant(depth0 >= chain_len);
    if (ptrdiff_t i = idx - depband; i >= depth0 - chain_len) {
      // Example:
      // freq = a*b*c*d
      // band = c,d
      // so every a*b, the direction reverses
      // we wish to subtract `a*b`
      // but must add `a` if a change in `a` forces a reload
      // i = 1
      if (i >= inner_idx) tf -= freqs[i];
      else cnst -= freqs[i];
      // dr<<depband = [d,c,0...]
      i -= std::countl_zero(dr << depband);
      if (i >= inner_idx) tf += freqs[i];
      else if (i >= 0) cnst += freqs[i];
      else cnst += 1.0;
    }
    return {.tf_ = tf, .cnst_ = cnst};
  }
  // builds a matrix that is similar to a series of univariate polynomials
  // We can use this to build yet another matrix, with cols corresponding to
  // cols of `InnerMostConstraint`, and row per memory level.
  // Each entry is the maximum inner-most loop cache-tile size that allows the
  // corresponding polynomial to fit within that cache.
  // We then use those to try different inner-most loop cache sizes to
  // pick the lowest-cost.
  // TODO: add coefs to `deps`
  // TODO: we need to also store cost for all-failed! I.e., reg-tile only!
  //       probably storable in some compressed way, as we don't apply the
  //       inner-most here.
  // TODO: fix cost calculation. It needs to consider the inner-most reg.
  //       Cost calculation has these components:
  //       1. coef (load, store, array count)
  //       2. tile size
  //       3. tile frequency
  // Tile frequency deceases while size increases. Hence it may make sense to
  // build the frequency component backwads w/ respect to the order we build
  // size.
  [[nodiscard]] auto innerConstraint(DepSummary &countdeps, ptrdiff_t chain_len)
    -> InnerMostConstraint {
    utils::invariant(unrolls_.size() > 1);
    utils::invariant(chain_len > 0);
    ptrdiff_t depth1 = unrolls_.size(), depth0 = depth1 - 1;
    // number of cols is 2depth0
    // this comes from any but the inner-most loop being unrolled (depth0+1)
    // e.g., if we have [n,m,k] (outer<->inner), then we have
    // reg = [m,n], [m], in order
    // and then placing any but the outermost as the inner-most cache
    // i.e., no reg, w/ `k` and `m` as unroll options
    // Order is outer-to-inner
    ptrdiff_t ndependent = countdeps.numDependent(),
              nindependent = countdeps.numInependent();
    // doesn't contain inner-loop
    containers::TinyVector<double, 29> freqs{};
    {
      double freq = 1.0;
      for (ptrdiff_t i = 0; i++ < depth0;)
        freqs.push_back((freq = unrolls_[i].cumulative_tf_));
      freqs.push_back(freq);
      for (ptrdiff_t i = 0; i++ < depth0;)
        freqs.push_back((freq * unrolls_[i].cumulative_cf_));
    }
    unsigned inner_tile_factor_flag = countdeps.nonzeroInnerCandidates();
    utils::invariant(inner_tile_factor_flag);
    InnerMostConstraint imc{&alloc_,      depth0,    ndependent,
                            nindependent, chain_len, inner_tile_factor_flag};
    // stridestream gives the cost of streaming `keep && isvec` variables when
    // striding, which ideally wouldn't be streamed.
    uint_fast16_t vector_mask = countdeps.vectorMask();
    double stridestream = 0.0; // corresponds to `.tf_`
    InnerMostConstraint::Cost stream{};
    // fill `imc.streamCost()`, `imc.cacheFit(Ind/D)ep()`, and
    // `imd.mustStoreOld()`
    MutArray<uint16_t, DenseDims<6>> dependent{countdeps.dependent()};
    for (ptrdiff_t i = 0; i < ndependent; ++i) {
      uint32_t deps{dependent[DepSummary::DepInd, i]},
        cost_coef{dependent[DepSummary::CostInd, i]},
        cpy_mask{dependent[DepSummary::CpyInd, i]};
      // int size = getRegSize(unrolls_, deps);
      // keep - do we keep it in the deepest level?
      bool keep = !((deps >> (depth0 - 1)) & 1), isvec = vector_mask & deps;
      int size = dependent[DepSummary::RegSzInd, i];
      double c = freqs.back() * cost_coef * size;
      stream.tf_ += c;
      if (keep & isvec) stridestream += c;
      fillTileSizes(imc.cacheFitDep()[_, i], unrolls_, deps, cpy_mask, depth0,
                    size);
    }
    MutArray<uint16_t, DenseDims<6>> independent{countdeps.independent()};
    for (ptrdiff_t c = 0; c < nindependent; ++c) {
      uint32_t deps{independent[DepSummary::DepInd, c]},
        cost_coef{independent[DepSummary::CostInd, c]},
        cpy_mask{independent[DepSummary::CpyInd, c]};
      // int size = getRegSize(unrolls_, deps);
      int size = independent[DepSummary::RegSzInd, c];
      stream.cnst_ +=
        freqs[depth0 + 32 - std::countl_zero(deps)] * cost_coef * double(size);
      fillTileSizes(imc.cacheFitIndep()[_, c], unrolls_, deps, cpy_mask, depth0,
                    size);
    }
    imc.streamCost() = stream;
    imc.cost().zero();
    // `i` iterates from depth0..1, over the loop we make inner-most
    for (ptrdiff_t l = 0; l < chain_len;) {
      ptrdiff_t i = depth0 - l++;
      if (inner_tile_factor_flag & 2) {
        // `-0.0` is an additive identity, `0.0` is not.
        // `-fno-signed-zeros` makes this unnecessary.
        imc.cost()[i - 1, 0].add(
          InnerMostConstraint::Cost{.tf_ = stridestream, .cnst_ = -0.0}, true);
      }
      // `k` iterates from 0..depth0, 1+k == number of cache tiles
      // we're calculating the cost of.
      // Different rotations give us potentially different costs,
      // due to different rotation-savings.
      ptrdiff_t inner_idx = depth0 - (i != depth0);
      for (ptrdiff_t c = 0, ndep = ndependent + nindependent; c < ndep; ++c) {
        ptrdiff_t b = c - ndependent;
        bool isdependent = b < 0;
        MutArray<uint16_t, math::StridedRange<6>> col =
          isdependent ? dependent[_, c] : independent[_, b];
        uint32_t d{col[DepSummary::DepInd]},
          cost_coef{col[DepSummary::CostInd]},
          cpy_mask{col[DepSummary::CpyInd]},
          depi = (d >> i) & 1, depl = d & ((1 << i) - 1),
          depu = (d & (~((1 << (i + 1)) - 1))) >> 1,
          dr = (d << (depth0 + 1)) | ((depi << depth0) | d) | depl | depu;
        StridedVector<int> sizes{isdependent ? imc.cacheFitDep()[_, c]
                                             : imc.cacheFitIndep()[_, b]};
        // First, we handle inner
        ptrdiff_t o = 0;
        {
          InnerMostConstraint::Cost cost{
            getFreq(freqs, depth0, dr, 0, inner_idx, chain_len) *
            (cost_coef * col[DepSummary::RegSzInd])};
          if ((inner_tile_factor_flag & 2)) {
            // stride, and either independent, !keep, or !isvec
            // The dependent, keep, isvec cases were added to streamcost
            if (!isdependent || ((d >> (depth0 - 1)) & 1) || !(vector_mask & d))
              imc.cost()[i - 1, 0].add(cost, isdependent);
            ++o; // o = 1;
          }
          if (inner_tile_factor_flag & 1) // nostride
            imc.cost()[i - 1, o++].add(cost, isdependent);
        }
        // k + 1 = # number of cache tiles
        for (ptrdiff_t k = 0; k < depth0; ++k) {
          // Following bit order, dr now contains
          // [0..., deps_cache_loops..., reordered deps...]
          // to move the inner-most loop left
          // see `fillTileSizes` for use of `cpy_mask`
          // if it was doubled there, we halve-it here.
          int size = sizes[k, c] >> (cpy_mask & 1);
          cpy_mask >>= 1;
          InnerMostConstraint::Cost cost{
            getFreq(freqs, depth0, dr, 1 + k, inner_idx, chain_len) *
            (cost_coef * size)};
          imc.cost()[i - 1, o + k].add(cost, isdependent);
        }
      }
      ptrdiff_t j = i--;
      if (l == chain_len) break;
      // Update `freqs` according to pattern:
      // `e`: [a, a*b, a*b*c, a*b*c*d, a*b*c*d*e]
      // `d`: [a, a*b, a*b*c, a*b*c*e, a*b*c*d*e]
      // `c`: [a, a*b, a*b*d, a*b*d*e, a*b*c*d*e]
      // `b`: [a, a*c, a*c*d, a*c*d*e, a*b*c*d*e]
      freqs[i] = freqs[i - 1] * unrolls_[j].cache_factor_;
    }
    return imc;
  }
  // swaps i, j
  // static constexpr auto bitswap(uint32_t x, uint32_t i,
  //                               uint32_t j) -> uint32_t {
  //   // Implementation:
  //   // if `xi` and `xj` are both set or not-set, swapping is a no-op.
  //   // Otherwise, 1-bits in an xor will swap them.
  //   uint32_t mi = 1 << i, mj = 1 << j, xi = x & mi, xj = x & mj;
  //   bool doswap = !xi != !xj;
  //   // bool doswap = std::popcount(xi | xj) == 1;
  //   // return (!xi != !xj) ? x ^ (mi | mj) : x;
  //   return doswap ? x ^ (mi | mj) : x;
  // }
  //

  /// Each row corresponds to a cache level
  /// Each column corresponds to some tiling behavior.
  /// The values are the maximum inner-most tile factor that will fit.
  ///
  /// Within a row, the values should be decreasing, i.e. each successive tiling
  /// strategy requires a smaller tile factor.
  /// Each tiling strategy is ordered from highest to lowest cost, given equal
  /// tile factors.
  ///
  /// The trade off is high cost corresponds with larget tile factors,
  /// low cost requires small tile factors.
  ///
  /// Tiling strategies are:
  /// 1 strided tile (optional)
  /// 1 tile without striding (optional)
  /// 2 tiles
  /// 3 tiles
  /// ...
  /// depth1 tiles
  ///
  /// We must have at least one of the 1-tile strategies.
  auto fitGrid(const DepSummary &deps, InnerMostConstraint imc)
    -> DensePtrMatrix<int> {
    // we create a grid of cache-tile sizes for the inner-most loop
    // the grid is #cache x 2depth0
    // Each element of the grid is the maximum tile size that causes the tiles
    // corresponding to column to fit into the row's corresponding cache.
    // First depth0 columns are for 1->depth0 cache tiles.
    // Remaining `depth0` cols cache tile all loops, with loop
    // 1+colidx-depth0 moved to inner-most of the cache-tiles.
    // Note:
    // 1. The outer-most loop, loop idx 0, cannot be moved to inner-most,
    //    as it is the outermost register tile.
    // 2. Entries of `0` mean we cannot fit; valid cache-factors are >0.
    unsigned itfs_flag = imc.innerTileFactorFlag();
    // `d0o = d0 + ic - 1` makes sense because we have `d0 - 1` entries
    // in the grid after excluding the first and the last.
    // The first has `ic` and the last has `d0`.
    // `d0o` gives the start of the last.
    ptrdiff_t d0 = imc.depth0(), ic = std::popcount(itfs_flag), o = ic - 1,
              d0o = d0 + o, d0d0 = d0 + d0o;
    MutDensePtrMatrix<int> grid{
      matrix<int>(&alloc_, math::row(caches_.size()), math::col(d0d0))};
    int maxcf = unrolls_.back().maxCacheFactor();
    utils::invariant(!caches_.front().victim_);
    utils::invariant(itfs_flag);
    if (itfs_flag & 1) {
      // Striding allows for larger tile factors, but may have higher cost.
      std::array<uint16_t, 4> t = deps.maxInnerTileStrided();
      for (ptrdiff_t cache_idx = 0; cache_idx < caches_.size(); ++cache_idx)
        grid[cache_idx, 0] = t[cache_idx];
    }
    if (itfs_flag & 2) {
      std::array<uint16_t, 4> t = deps.maxInnerTileNoStride();
      for (ptrdiff_t cache_idx = 0, i = itfs_flag & 1;
           cache_idx < caches_.size(); ++cache_idx)
        grid[cache_idx, i] = t[cache_idx];
    }
    DensePtrMatrix<int> szIndep{imc.cacheFitIndep()};
    MutDensePtrMatrix<int> szDep{imc.cacheFitDep()};
    for (ptrdiff_t cidx = 0, ncache = caches_.size(); cidx < ncache; ++cidx) {
      Cache c = caches_[cidx];
      if (!c.victim_) {
        deps.maxSatValue(szIndep, szDep, maxcf, c, grid[cidx, _(0, d0o)], ic);
        deps.maxSatValueOutermost(szIndep[d0 - 1, _], szDep[d0 - 1, _], maxcf,
                                  c, grid[cidx, _(d0o, d0d0)]);
      } else {
        // we use `g[0,nostride]` for inner size to add
        utils::invariant(itfs_flag & 2);
        MutArray<int, DenseDims<2>> g{grid[cidx - 1 + _(0, 2), _]};
        deps.maxSatVictimValue(szIndep, szDep, c, g[_, _(ic, d0o)],
                               g[0, itfs_flag == 3]);
        deps.maxSatVictimValueOutermost(szIndep, szDep, c, g, d0, ic);
      }
    }
    return grid;
  }
  /// The permutation we set is...
  /// n, m, k, j, i
  /// inner = idx of inner-most, e.g.
  /// 1 -> m
  /// Permutation: 0, 2, 3, 4, 1
  /// 2 -> k
  /// Permutation: 0, 1, 3, 4, 2
  /// 3 -> j
  /// Permutation: 0, 1, 2, 4, 3
  /// 4 -> i
  /// Permutation: 0, 1, 2, 3, 4
  /// This gives the `idx` of the cache tile's new position.
  struct InnerPerm {
    uint16_t inner_;
    // cannot be used from inner-most; there we know the answer is inner
    constexpr auto perm(int d0) const -> int { return d0 > inner_ ? --d0 : d0; }
  };
  struct Best {
    LeakyReluCost cost_;
    int cache_factor_;
    InnerPerm perm_;
    uint16_t flag_;
    constexpr void update(Best other) {
      if (other.cost_ < cost_) *this = other;
    }

  private:
    friend constexpr auto operator==(Best a, Best b) -> bool {
      return a.cost_ == b.cost_;
    }
    friend constexpr auto operator==(Best b, LeakyReluCost c) -> bool {
      return static_cast<double>(b.cost_) == static_cast<double>(c);
    }
    friend constexpr auto operator<=>(Best b, double c)
      -> std::partial_ordering {
      return static_cast<double>(b.cost_) <=> c;
    }
    friend constexpr auto operator<=>(Best b, LeakyReluCost c)
      -> std::partial_ordering {
      return static_cast<double>(b.cost_) <=> static_cast<double>(c);
    }
    friend constexpr auto operator<=>(double c, Best b)
      -> std::partial_ordering {
      return c <=> static_cast<double>(b.cost_);
    }
    friend constexpr auto operator<=>(Best b, Best c) -> std::partial_ordering {
      return b.cost_ <=> c.cost_;
    }
  };
  static_assert(sizeof(Best) == 24);
  /// @param deps `Tuple` consists of `deps`, `fit_coef`, and `cost_coef`.
  /// `fit_coef` is used for determining whether arrays fit, while `cost_coef`
  /// is for bandwidth costs. These two may not be equal, e.g. if we both load
  /// and store from an array, it contributes once to `fit_coef` but twice to
  /// `cost_coef`.
  /// Returns:
  /// double: best cost
  /// int: best cache factor for the inner-most loop
  /// int: best choice for the inner-most cache loop, offset by `1`.
  auto optInnerMost(DepSummary *deps_ptr, ptrdiff_t chain_len) -> Best {
    DepSummary &deps{*deps_ptr};
    auto scope = alloc_.scope();
    InnerMostConstraint imc{innerConstraint(deps, chain_len)};
    // #cache x depth1, giving maximal inner-most loop cache factor
    // that will result in col#+1 loops fitting in that cache.
    // We now explore each of these, to determine which has the
    // lowest cost. We then return that cost and unroll factor.
    DensePtrMatrix<int> grid{fitGrid(deps, imc)};
    // For a given value, we can use the grid to determine which
    // cache levels the blocked sets fit in.
    // cost per `depth0` choice of inner-most
    MutPtrVector<LeakyReluCost> costs{
      math::vector<LeakyReluCost>(&alloc_, chain_len)};
    unsigned itf_flag = imc.innerTileFactorFlag(),
             itfc = std::popcount(itf_flag);
    int best_cf = 0, best_inner = 0;
    ptrdiff_t d0 = imc.depth0(), ncolg = ptrdiff_t(grid.numCol()),
              inneroff = itfc - 1, d0o = d0 + inneroff;
    utils::assume(d0 > 0);
    LeakyReluCost best_cost{.max_cost_ =
                              std::numeric_limits<double>::infinity()};
    Loop inner{unrolls_.back()}; // copy
    DensePtrMatrix<InnerMostConstraint::Cost3> costmap{imc.cost()};
    // this flag indicates which cache levels have non-zero grid entries
    // the bits are backwards from normal:
    // [0,...,0,outermost,...,innermost]
    uint16_t cache_filled_flag = 0;
    // `i` iterates over cache level
    for (ptrdiff_t i = 0; i < grid.numRow(); ++i) {
      // j-loop over tiles to set
      for (ptrdiff_t j = 0; j < ncolg; ++j) {
        // `j` iterates over which loop
        int cf = grid[i, j];
        if (!cf) continue;
        // check whether we have stride, and are less then that
        // if so, and we don't have no-stride, or are > no-stride
        // then reduce `cf` to be divisible by stride.
        if ((j >= itfc) && (itf_flag & 1)) {
          for (ptrdiff_t k = 0; k < i; ++k) {
            if ((cf < grid[k, 0]) && ((itf_flag == 1) || (cf > grid[k, 1]))) {
              cf &= -1 << deps_ptr->log2firstCaceStride();
              break;
            }
          }
        }
        uint16_t cacheflag = 0;
        // cache_filled_flag |= (1u << std::min(j, d0));
        double trip_factor = inner.setCacheFactor(cf), cache_factor = cf;
        costs.zero();
        ptrdiff_t cl = caches_.size();
        utils::assume(cl > 0);
        // Implementation note: `cl` is decremented at the end of the first loop
        // and start of the second. Within the first loop, we use `cl - 1`;
        // postponing the decrementation to the end allows us to break in the
        // none-fit condition, and start from the same `cl`.
        do {
          // double ibw = caches_[cl].inv_next_bandwidth_;
          // this means that at least one is still d0
          // This section is for tiling all loops, so
          // we consider last `d0` cols of grid.
          uint32_t nofit = 0;
          PtrVector<int> g{grid[cl - 1, _]};
          double ibw = caches_[cl - 1].inv_next_bandwidth_;
          for (ptrdiff_t k = 0; k < chain_len; ++k) {
            nofit <<= 1;
            if (cf <= g[k + d0o])
              costs[k] += costmap[k, d0o](cache_factor, trip_factor) * ibw;
            else nofit |= 1;
          }
          if (nofit == (1u << d0) - 1) break;
          // set outer-most flag
          cacheflag |= 1u << d0;
          if (!nofit) continue;
          // handle those that don't fit
          // if none of them fit, decrement nctidx
          ptrdiff_t iidx = chain_len; // innermost idx
          do {
            uint32_t shift = std::countr_zero(nofit) + 1;
            iidx -= shift;
            nofit >>= shift;
            ptrdiff_t cfidx = d0o - 1;
            while (cfidx >= 0 && cf > g[cfidx]) --cfidx;
            if (cfidx >= 0) {
              cacheflag |= 1u << std::max(0z, cfidx - inneroff);
              costs[iidx] +=
                costmap[iidx, cfidx](cache_factor, trip_factor) * ibw;
            } else
              costs[iidx] += imc.streamCost(cache_factor, trip_factor) * ibw;
          } while (nofit);
        } while (--cl);
        if (cl) {
          ptrdiff_t nctidx = d0o - 1;
          for (; cl--;) {
            while (nctidx >= 0 && cf > grid[cl, nctidx]) --nctidx;
            double ibw = caches_[cl].inv_next_bandwidth_;
            if (nctidx >= 0) {
              cacheflag |= 1u << std::max(0z, nctidx - inneroff);
              // If we've selected no-stride, while stride is an option
              // then set to stride if we can't fit w/out stride in l1 cache.
              if ((itf_flag == 3) && (nctidx == 1) && cl && (cf > grid[0, 1]))
                nctidx = 0;
              for (ptrdiff_t k = 0; k < chain_len; ++k)
                costs[k] += costmap[k, nctidx](cache_factor, trip_factor) * ibw;
            } else costs += imc.streamCost(cache_factor, trip_factor) * ibw;
          }
        }
        double phi_reload_cost = phiSpillCost(inner) * (1.0 / LeakyReluCost::a);
        for (ptrdiff_t k = chain_len; k--;) {
          LeakyReluCost c = costs[k] + phi_reload_cost;
          if (c < best_cost) {
            invariant(static_cast<double>(c) > 0.0);
            best_cost = c;
            best_cf = cf;
            best_inner = int(k);
            cache_filled_flag = cacheflag;
          }
        }
      }
    }
    InnerPerm ip{uint16_t(best_inner + unrolls_.size() - chain_len)};
    // Contribution of remaining loops is constant as a function of inner-most
    // cache-factor, so we hoist it out.
    // TODO: Alternative implementation could add it in `cacheOptEntry` upon
    // returning, hoisting out these calculations further.
    best_cost += remainingPhiSpillCost() * (1.0 / LeakyReluCost::a);
    return {best_cost, best_cf, ip, cache_filled_flag};
  }
  // use `l` instead of the deepest
  auto remainingPhiSpillCost() -> double {
    double c = 0.0;
    for (ptrdiff_t i = 0; i < unrolls_.size() - 1; ++i)
      c += phiSpillCost(unrolls_[i]);
    return c;
  }
  static auto phiSpillCost(const Loop &l) -> double {
    if (!l.phi_cost_) return 0.0;
    // For each trip factor - 1, we need to store and then reload
    // all the `phi` elements.
    double tf = l.cache_trip_count_;
    if (tf <= 1.0) return 0.0;
    double c = l.phi_cost_ * l.cumulative_tf_ * l.cumulative_cf_;
    return ((tf * c) - c);
  }
  // auto doesFitLast(PtrVector<uint16_t> deps, Cache cache, int inner) -> bool
  // {
  //   // tiled_mask{uint32_t(((1 << depth) - 1) & ~untiled_mask)};
  //   int ways = cache.associativty_;
  //   for (uint32_t d : deps) {
  //     int size = 1;
  //     for (int64_t i : containers::BitSet64::fromMask(d))
  //       size *= unrolls_[i].reg_factor_ * unrolls_[i].cache_factor_;
  //     int nw = cld(size, cache.stride_);
  //     if (checkRequiresOldOuter(deps, d, inner)) nw <<= 1;
  //     ways -= nw;
  //     if (ways < 0) return false;
  //   }
  //   return true;
  // }
  // auto doesFit(PtrVector<uint16_t> deps, Cache cache, int reg) -> bool {
  //   utils::assume(reg > 0);
  //   uint32_t reg_mask{uint32_t((1 << reg) - 1)};
  //   // tiled_mask{uint32_t(((1 << depth) - 1) & ~untiled_mask)};
  //   int ways = cache.associativty_;
  //   for (uint32_t d : deps) {
  //     uint32_t r = reg_mask & d, c = d >> reg;
  //     int size = 1;
  //     for (int64_t i : containers::BitSet64::fromMask(r))
  //       size *= unrolls_[i].reg_factor_;
  //     for (int64_t i : containers::BitSet64::fromMask(c))
  //       size *= unrolls_[i + reg].reg_factor_ * unrolls_[i +
  //       reg].cache_factor_;
  //     int nw = cld(size, cache.stride_);
  //     if (checkRequiresOld(deps, reg, d)) nw <<= 1;
  //     ways -= nw;
  //     if (ways < 0) return false;
  //   }
  //   return true;
  // }
  // This must be popped and returned by `cacheOptEntry` to track
  // mvovement through it.
  // Dependent and independent of the inner-most loop are sorted;
  // two successive `ndeps_*` subsets yield dependent and independent,
  // respectively.
  static constexpr ptrdiff_t NumBounds = 3;
  static constexpr ptrdiff_t NB = (2 * NumBounds) + 1;
  // The basic plan here is that this does a sort of bisection. We assume
  // that it is roughly unimodal. It is not really unimodal, but as long
  // as the appoximation is decent, we should still be able to land on the
  // optimal solution.
  // We keep 7 points:
  // lb0, lb1, lb2, best, ub0, ub1, ub2
  // Initially,
  // lb0 = lb1 = lb2 = 1
  // ub0 = ub1 = ub2 = cld(trip_count, reg_factor)
  //
  // These are sorted by cost value.
  // We also track their costs. Whenever we have two modes, we split.
  // We also get a flag indicating which depths both fit and didn't,
  // to possibly inform which direction to explore.
  //
  // We optimize over all choices for which loop to reorder to inner-most.
  //
  // We have two layers per level:
  // Entry point, pops off `loopinfo`, sets up problem and bounds
  // Bisection; calls entry or `optInnerMost`, as appropriate.
  //
  // TODO: Need to store state, like micro kernel opt does.
  // This state must include non-leaf `cache_factor`s (`int`s), and leaf
  // cache-factor per depth-unroll-vectors.
  // TODO: need to update `optInnerMost` for taking separate dep matrices
  // TODO: figure out plan of cost evaluation, and sub-loop iteration
  //
  // Returns best from its sub-branch
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOptBisect(LoopSummaries ls, double *phi_costs, DepSummary *ds,
                 ptrdiff_t chain_len, ptrdiff_t nsubloops,
                 std::array<Best, NB> bounds, LoopTransform *best_trf) -> Best {
    Best best{bounds[3]};
    for (;;) {
      // costs[3] is best
      // perhaps decision should be based on gap, i.e. avoid under-exploring?
      int b2 = bounds[2].cache_factor_, b3 = bounds[3].cache_factor_,
          b4 = bounds[4].cache_factor_, d0 = b3 - b2, d1 = b4 - b3;
      if ((d0 <= 1) && (d1 <= 1)) return best;
      double c2 = static_cast<double>(bounds[2].cost_),
             c3 = static_cast<double>(bounds[3].cost_),
             c4 = static_cast<double>(bounds[4].cost_);
      utils::invariant((c3 <= c2) && (c3 <= c4));
      bool large_diff = (d0 > 3 * (d1 >> 2)) || (3 * (d0 >> 2) < d1),
           upper = large_diff ? d1 > d0 : c2 > c4;
      int b = upper ? b4 : b2, cache_factor = (b & b3) + ((b ^ b3) >> 1);
      Best nb =
        cacheOptCost(ls, phi_costs, ds, chain_len, nsubloops, cache_factor,
                     static_cast<double>(best.cost_), best_trf);
      best.update(nb);
      // midpoint rounds down
      // upper: b2, b3, cache_factor, b4
      // !upper: b2, cache_factor, b3, b4
      if (nb < c3) {
        if (!upper) {
          // we don't lose focus on smallest values; can ignore cff
          for (ptrdiff_t i = 6; i > 3; --i) bounds[i] = bounds[i - 1];
          bounds[3] = nb;
        } else if (bounds[2].flag_ == bounds[3].flag_) {
          // `upper`, so we shift focus on cache factor, losing site of `b2`
          // If `b2` contained a `1` that b3 did not, we do not want to lose it.
          // Hence, we check that flags match to go down this path.
          for (ptrdiff_t i = 0; i < 3; ++i) bounds[i] = bounds[i + 1];
          bounds[3] = nb;
        } else
          best = bisectSplit(ls, phi_costs, ds, chain_len, nsubloops, best_trf,
                             best, upper, nb, bounds);
      } else if (upper && nb <= c4) {
        // `b3` remains the center, so we do not lose sight of b2
        for (ptrdiff_t i = 6; i > 4; --i) bounds[i] = bounds[i - 1];
        bounds[4] = nb;
      } else if (!upper && nb <= c2 && bounds[2].flag_ == bounds[3].flag_) {
        // We would lose sight of `b2`, as we maintain focus on `b3`
        // and insert `cache_factor` to b3's left. Hence, we check flags.
        for (ptrdiff_t i = 0; i < 2; ++i) bounds[i] = bounds[i + 1];
        bounds[2] = nb;
      } else
        best = bisectSplit(ls, phi_costs, ds, chain_len, nsubloops, best_trf,
                           best, upper, nb, bounds);
    }
  }

  constexpr auto complete(const std::array<Best, NB> &bounds) -> bool {
    int center = bounds[3].cache_factor_;
    return ((center - bounds[2].cache_factor_) <= 1) &&
           ((bounds[4].cache_factor_ - center) <= 1);
  }
  auto bisectSplit(LoopSummaries ls, double *phi_costs, DepSummary *ds,
                   ptrdiff_t chain_len, ptrdiff_t nsubloops,
                   LoopTransform *best_trf, Best best, bool upper, Best current,
                   std::array<Best, NB> &bounds) -> Best {
    std::array<Best, NB> btmp =
      upper ? splitUpUpper(bounds, current) : splitUpLower(bounds, current);
    bounds =
      upper ? splitLowUpper(bounds, current) : splitLowLower(bounds, current);

    if (!complete(btmp)) {
      if (complete(bounds)) bounds = btmp;
      else if (btmp[3] == best)
        best.update(cacheOptBisect(ls, phi_costs, ds, chain_len, nsubloops,
                                   btmp, best_trf));
    }
    return best;
  }
  static constexpr auto splitUpUpper(std::array<Best, NB> a, Best x)
    -> std::array<Best, NB> {
    a[0] = a[1] = a[2] = x;
    if (x >= a[4]) {
      a[3] = a[4];
      a[4] = a[5];
      a[5] = a[6];
    } else a[3] = x;
    return a;
  }
  template <typename T>
  static constexpr auto splitLowUpper(std::array<T, NB> a, T x)
    -> std::array<T, NB> {
    a[4] = a[5] = a[6] = x;
    if (x < a[3]) {
      a[0] = a[1];
      a[1] = a[2];
      a[2] = a[3];
      a[3] = x;
    }
    return a;
  }

  template <typename T>
  static constexpr auto splitUpLower(std::array<T, NB> a, T x)
    -> std::array<T, NB> {
    a[0] = a[1] = a[2] = x;
    if (x < a[3]) {
      a[6] = a[5];
      a[5] = a[4];
      a[4] = a[3];
      a[3] = x;
    }
    return a;
  }
  template <typename T>
  static constexpr auto splitLowLower(std::array<T, NB> a, T x)
    -> std::array<T, NB> {
    a[4] = a[5] = a[6] = x;
    if (x >= a[2]) {
      a[3] = a[2];
      a[2] = a[1];
      a[1] = a[0];
    } else a[3] = x;
    return a;
  }
  constexpr auto depth1() const -> ptrdiff_t { return unrolls_.size(); }
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOptCost(LoopSummaries ls, double *phi_costs, DepSummary *ds,
               ptrdiff_t chain_len, ptrdiff_t nsubloops, int cache_factor)
    -> Tuple<Best, LoopSummaries, DepSummary *, int> {
    unrolls_.back().setCacheFactor(cache_factor);
    utils::assume(nsubloops > 0);
    // Best best{0.0,cache_factor,{},0xffff};
    LeakyReluCost cost{};
    int sub_tree_size = 0;
    uint16_t cuf = 0xffff;
    InnerPerm ip{};
    for (ptrdiff_t i = 0; i < nsubloops; ++i) {
      auto [loopinfo, loopsmrs] = ls.popFront();
      LoopTransform &trf = ls.trfs_.front();
      Best btmp;
      tie(btmp, ls, ds, Add(sub_tree_size)) = cacheOptEntry(
        loopinfo, trf.reg_factor(), loopsmrs, phi_costs, ds, chain_len);
      cost += btmp.cost_;
      ip = btmp.perm_;
      cuf &= btmp.flag_;
      // Note, if we have multiple nsubloops, then inner_ must be inside
      invariant(nsubloops == 1 || (ip.inner_ >= depth1()));
      trf.cache_unroll_factor_ = btmp.cache_factor_ - 1;
      // we've returned from `cacheOptEntry`, so we're up one level
      // thus, our depth1 was the previous level's depth0
      trf.cache_permutation_ = ip.perm(depth1());
    }
    return {Best{cost, cache_factor, ip, cuf}, ls, ds, sub_tree_size};
  }
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOptCost(LoopSummaries ls, double *phi_costs, DepSummary *ds,
               ptrdiff_t chain_len, ptrdiff_t nsubloops, int cache_factor,
               double bestc, LoopTransform *best_trf) -> Best {
    auto [best, lsr, _, __] =
      cacheOptCost(ls, phi_costs, ds, chain_len, nsubloops, cache_factor);
    if (best < bestc)
      std::memcpy(best_trf, ls.trfs_.data(),
                  ls.trfs_.size() * sizeof(LoopTransform));
    return best;
  }

  // The functions are recursive. They take `best_cost` explored thus far as
  // inputs, but must return the best cost they were able to find on their
  // subtree. It is the caller's responsibility to update their `best_cost`
  // accordingly.
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOptEntry(LoopSummary loopinfo, int reg_factor, LoopSummaries ls,
                double *phi_costs, DepSummary *ds, ptrdiff_t chain_len)
    -> Tuple<Best, LoopSummaries, DepSummary *, int> {
    ptrdiff_t nsubloops = loopinfo.numSubLoops();
    MutPtrVector<LoopTransform> best_trfs = ls.trfs_;
    int trip_count = int(loopinfo.estimatedTripCount());
    double phi_cost = *(phi_costs++);
    PopBack pb = pushLoop(loopinfo, reg_factor, phi_cost);
    if (!nsubloops) {
      auto [c, cf, ip, cff] = optInnerMost(ds, chain_len);
      return {Best{c, cf, ip, uint16_t(cff >> 1)}, ls, ds->getNext(), 1};
    }
    chain_len = nsubloops == 1 ? chain_len + 1 : 1;
    utils::assume(loopinfo.reorderable());
    int ub = math::cld(trip_count, reg_factor);
    // NOTE: overwrites `ls.trfs_`
    auto [l, lsr, ds_ret, sts] =
      cacheOptCost(ls, phi_costs, ds, chain_len, nsubloops, 1);
    if (ub <= 1) return {l, lsr, ds_ret, sts + 1};
    MutPtrVector<LoopTransform> trfs =
      math::vector<LoopTransform>(&alloc_, sts);
    std::memcpy(trfs.data(), best_trfs.data(), sts * sizeof(LoopTransform));
    LoopSummaries lstmp{ls.loop_summaries_, trfs};
    LoopTransform *btrfs = ls.trfs_.data();
    Best u = cacheOptCost(lstmp, phi_costs, ds, chain_len, nsubloops, ub,
                          static_cast<double>(l.cost_), btrfs);
    Best best = l < u ? l : u;
    if (ub == 2) return {best, lsr, ds_ret, sts + 1};
    // cacheOptBisect
    l.flag_ |= 1; // encourage searching down.
    std::array<Best, NB> bounds{l, l, l, best, u, u, u};
    best =
      cacheOptBisect(lstmp, phi_costs, ds, chain_len, nsubloops, bounds, btrfs);
    best.flag_ >>= 1;
    return {best, lsr, ds_ret, sts + 1};
  }
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOpt(LoopSummary loopinfo, LoopTransform trf, LoopSummaries ls,
           double *phi_costs, DepSummary *ds) -> Pair<Best, DepSummary *> {
    ds->initRegTileSizes(caches_, loopinfo, trf, ls, cachelinebits_);
    auto opt = cacheOptEntry(loopinfo, trf.reg_factor(), ls, phi_costs, ds, 0);
    Best b = opt.template get<0>();
    return {b, opt.template get<2>()};
  }
  auto // NOLINTNEXTLINE(misc-no-recursion)
  cacheOpt(LoopSummaries ls, double *phi_costs, DepSummary *ds)
    -> Pair<Best, DepSummary *> {
    auto [loopinfo, loopsmrs] = ls.popFront();
    auto [b, dsret] =
      cacheOpt(loopinfo, ls.trfs_.front(), loopsmrs, phi_costs, ds);
    ls.trfs_.front().cache_unroll_factor_ = b.cache_factor_ - 1;
    return {b, dsret};
  }
};

} // namespace CostModeling::Cache

#ifndef NDEBUG
// for GDB
namespace containers {
template ::CostModeling::Cache::CacheOptimizer::Cache &
TinyVector<::CostModeling::Cache::CacheOptimizer::Cache, 4>::operator[](
  ptrdiff_t);
template ::CostModeling::Cache::CacheOptimizer::Loop &
TinyVector<::CostModeling::Cache::CacheOptimizer::Loop, 15>::operator[](
  ptrdiff_t);
} // namespace containers
#endif
