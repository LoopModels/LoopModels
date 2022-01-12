#pragma once

#include "./bitsets.hpp"
#include "./graphs.hpp"
#include "./ir.hpp"
#include "./loops.hpp"
#include "./math.hpp"
#include "symbolics.hpp"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

struct ShorterCoef {
    bool
    operator()(std::pair<Vector<size_t, 0>,
                         std::vector<std::tuple<Int, size_t, SourceType>>> &x,
               std::pair<Vector<size_t, 0>,
                         std::vector<std::tuple<Int, size_t, SourceType>>> &y) {
        return length(x.first) < length(y.first);
    }
};

/*
template <typename T> struct Stride {
    std::vector<std::pair<Vector<size_t, 0>, T>> strides;

    T &operator[](size_t i) { return strides[i]; }
};
*/
/// gives stridesX - stridesY;
/*
struct IndexDelta {
    std::vector<std::pair<Stride<Int>,
                          std::vector<std::tuple<Stride<std::pair<Int, Int>>,
                                                 size_t, SourceType>>>>
        strides; // strides of `X` and `Y`
    // { src {                  aff terms of src { ids mulled }, coef }, srcId,
    // type  }
    std::vector<std::tuple<std::vector<std::pair<Vector<size_t, 0>, Int>>,
                           size_t, SourceType>>
        diffsBySource;
    std::vector<std::pair<Vector<size_t, 0>,
                          std::vector<std::tuple<Int, size_t, SourceType>>>>
        diffsByStride; // sorted by length
    bool isStrided;
    bool isLinear;

    void checkStrided() { // O(N^2) check, but `N` should generally be small.
        // Assumes that `diffsByStride` is sorted by length of the
        // `Vector<size_t,0>`s. Assumes that the contents of the
        // `Vector<size_t,0>`s are sorted.
        isStrided = true;
        for (size_t i = 1; i < length(diffsByStride); ++i) {
            Vector<size_t, 0> stride0 = diffsByStride[i - 1].first;
            Vector<size_t, 0> stride1 = diffsByStride[i].first;
            size_t j = 0;
            for (size_t k = 0; k < length(stride0); ++k) {
                if (j == length(stride1)) {
                    isStrided = false;
                    return;
                }
                // stride1 must be a super set of stride0, so we keep
                // incrementing `j` while trying to find a match.
                while (stride0[k] != stride1[j]) {
                    ++j;
                    if (j ==
                        length(stride1)) { // if we reach the end, that means
                                           // there was an element in `stride0`
                                           // absent in `stride1`.
                        isStrided = false;
                        return;
                    }
                }
                ++j;
            }
        }
    }
    void checkLinear() {
        isLinear = true;
        for (size_t i = 0; i < length(diffsBySource); ++i) {
            SourceType srcTyp = std::get<2>(diffsBySource[i]);
            if (!((srcTyp == CONSTANT) | (srcTyp == LOOPINDUCTVAR))) {
                isLinear = false;
                return;
            }
        }
    }
    void fillStrides() {
        for (size_t i = 0; i < length(diffsBySource); ++i) {
            auto [coefpairs, srcId, srcTyp] = diffsBySource[i];
            for (size_t j = 0; j < length(coefpairs); ++j) {
                auto [stride, mul] = coefpairs[j];
                std::tuple<Int, size_t, SourceType> newSource =
                    std::make_tuple(mul, srcId, srcTyp);
                bool found = false;
                for (size_t k = 0; k < length(diffsByStride); ++k) {
                    auto [stridek, terms] = diffsByStride[k];
                    if (stridek == stride) {
                        terms.emplace_back(newSource);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::vector<std::tuple<Int, size_t, SourceType>> newSources{
                        newSource};
                    diffsByStride.emplace_back(
                        std::make_pair(stride, newSources));
                }
            }
        }
        std::sort(diffsByStride.begin(), diffsByStride.end(), ShorterCoef());
        checkStrided();
        checkLinear();
    }
    void emplace_back(std::tuple<std::vector<std::pair<Vector<size_t, 0>, Int>>,
                                 size_t, SourceType> &&x) {
        diffsBySource.emplace_back(x);
        return;
    }
    //
std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType>&
    // operator[](size_t i){ return diffs[i]; }
};
// size_t length(IndexDelta &d){ return d.diffs.size(); }

std::vector<std::pair<Vector<size_t, 0>, Int>>
difference(ArrayRef x, size_t idx, ArrayRef y, size_t idy) {
    VoV<size_t> pvcx = x.programVariableCombinations(idx);
    Vector<Int, 0> coefx = x.coef(idx);
    VoV<size_t> pvcy = y.programVariableCombinations(idy);
    Vector<Int, 0> coefy = y.coef(idy);
    BitSet64 matchedx; // zero initialized by default constructor
    std::vector<std::pair<Vector<size_t, 0>, Int>> diffs;
    for (size_t i = 0; i < length(pvcy); ++i) {
        bool matchFound = false;
        Int coefi = coefy(i);
        Vector<size_t, 0> argy = pvcy(i);
        for (size_t j = 0; j < length(pvcx); ++j) {
            Vector<size_t, 0> argx = pvcx(j);
            if (argx == argy) {
                Int deltaCoef = coefx(j) - coefi;
                if (deltaCoef) { // only need to push if not 0
                    diffs.emplace_back(std::make_pair(argy, deltaCoef));
                }
                matchedx.set(j);
                matchFound = true;
                break;
            }
        }
        if (!matchFound) {
            diffs.emplace_back(std::make_pair(argy, -coefi));
        }
    }
    for (size_t j = 0; j < length(pvcx); ++j) {
        if (!matchedx[j]) {
            diffs.emplace_back(std::make_pair(pvcx(j), coefx(j)));
        }
    }
    return diffs;
}

std::vector<std::pair<Vector<size_t, 0>, Int>> mulCoefs(ArrayRef x, size_t idx,
                                                        Int factor) {
    VoV<size_t> pvcx = x.programVariableCombinations(idx);
    Vector<Int, 0> coefx = x.coef(idx);
    std::vector<std::pair<Vector<size_t, 0>, Int>> diffs;
    for (size_t j = 0; j < length(pvcx); ++j) {
        diffs.emplace_back(std::make_pair(pvcx(j), factor * coefx(j)));
    }
    return diffs;
}
*/

// for (m = 0; m < M; ++m){
//   for (n = 0; n < N; ++n){
//     A(m,n) /= U(n,n);
//     for (k = 0; k < N - n - 1; ++k){
//       A(m, k + n + 1) -= A(m,n) * U(n, k + n + 1);
//     }
//   }
// }
//
//
// for (m = 0; m < M; ++m){
//   for (k = 0; k < N; ++k){
//     for (n = 0; n < N - k - 1; ++n){
//       A(m, k + n + 1) -= A(m,n) * U(n, k + n + 1);
//     }
//   }
// }
// k* = k + n + 1
//
// for (m = 0; m < M; ++m){
//   for (k = 0; k < N; ++k){
//     for (n = 0; n < N - k - 1; ++n){
//       A(m, k + n + 1) -= A(m, n) * U(n, k + n + 1);
//     }
//   }
// }
//
// k* = n
// n* = k + n + 1
// bounds on n*:
// 0 <= k < N - n - 1 => n < N - k - 1
// 0 <= n < N
// k + n + 1
//
// 0 <= n < N
// 0 <= k < N - n - 1
// k + n + 1
//
// n + 1 <= n + k + 1 < N
// k* + 1 <= n* < N
// k* <= n* - 1 => k* < n* # nsw
// # also need k* lower bound
// 0 <= n => 0 <= k*
// 0 <= k* < n*
//
// n + 1 <= k* => 1 <= k* < N => 0 <= k* < N
//
//   for (n* = 0; n* < N; ++n*){
//     for (n = 0; n < N - k - 1; ++n){
//       A(m, n*) -= A(m,k*) * U(k*, n*);
//     }
//   }
//
// we want to change the visiting pattern, so we swap symbols
//   for (n = 0; n < N; ++n){
//     for (k = 0; k < N - n - 1; ++k){
//       A(m, k + n + 1) -= A(m, k) * U(k, n + k + 1);
//     }
//   }
//
// k + n + 1 -> k*, n*
// 1) k + n + 1 -> k*
// [1 1 1] [k       [1 0 0] [k*
//          n     ~          n*
//          1]               0]
// 0 <= n < N - k - 1
//
// 0 <= k < N
// k < N - n - 1 => 0 <= k < N - n - 1
// k + n + 1
//
// k* = k + n + 1; k = k* - n - 1
// k < N => k* < N + n + 1;
// k < N - n - 1 => k* < N; intersect, tighter bound wins
// conveniently, tighter bound excludes `n`
// bound would be:
// for (k* = n + 1; k* < N; ++k*)
// because `n` is inside, we have to set `n` to minimum value
// for (k* = 1; k* < N; ++k*) // maybe we can safely set k=0 if inner loop
// doesn't iterate on that condition?
//
// k < N => k* - n - 1 < N => n > k* - 1 - N
//
//
// for (n = 0; n < N - 1 - (k* - n - 1); ++n)
//
// 0 <= k < N
// 0 <= n < N - k - 1
// 0 <= k < N - k - 1
// n* = k + n + 1; k = n* - k* - 1
// k* = n        ; n = k*
//
// for (n* = k* + 1; n* < N + k* + 1; ++n){ // intersect should fix upper bound
//   for (k* = 0; k* < N - 1 - (n* - k* - 1); ++k*){ // oof
//
//   }
// }
// we have
// n* < N + k* + 1
// k* < N - 1 - n* + k* + 1 => n* < N
// k* < N
//
// n* - k* - 1 < N
// n* < N + k* + 1
//
//
//
// 0 <= n < N - k - 1
// 0 <= k < N
// k + n + 1
//
// 0 <= n < N
// 0 <= k < N - n - 1
// k + n + 1
//
// n + 1 <= n + k + 1 < N
// n + 1 <= k* < N
// 0 <= n < k*
//
// n + 1 <= k* => 1 <= k* < N => 0 <= k* < N
//
//
//
//
// Looking for register tiling, we see we have
// m
// n
// k + n + 1
// if we want three independent loops, so that we can hoist and have
// one reduce costs of others, we need to diagonalize these somehow
// [ 1 0 0   [ 0
//   0 1 0     0
//   0 1 1 ]   1 ]
//
// [ 1 0 0 0  [ m
//   0 1 0 0    n
//   0 1 1 1    k
//   ]          1 ]
//
// [ 1 0 0 0  [ m*      [ 1 0 0 0  [ m
//   0 1 0 0    n*    =   0 1 0 0    n
//   0 0 1 0    k*        0 1 1 1    k
//   ]          1 ]      ]          1 ]
//   m* = m
//   n* = n
//   k* = n + k + 1
// Then
// 1) rotate as necessary to get register tiling
// 2) check to make sure that deps are still satisfied
//
// first, the substitution:
// for (m = 0; m < M; ++m){
//   for (n = 0; n < N; ++n){
//     A(m,n) /= U(n,n); // let k* = k + n + 1; k = k* - n - 1
//     for (k* = n + 1; k < N; ++k){
//       A(m, k*) -= A(m,n) * U(n, k*);
//     }
//   }
// }
// now, the loop swap to move the reduction to the middle:
// for (m = 0; m < M; ++m){
//   for (k* = 0; k* < N; ++k*){
//     A(m,k*) /= U(k*,k*);// renamed to focus on other loop
//     for (n = 0; n < k*; ++n){
//       A(m, k*) -= A(m,n) * U(n, k*);
//     }
//   }
// }
// lets consider trying to do loop swap at same time as redefinition
//
//  [ 1 0 0 0   [m*
//    0 0 1 0    n*
//    0 1 0 0 ]  k*
//               1]

// Partitions array accesses into non-overlapping strides
// two array refs of:
// A(i,j,k); // []*i + [M]*j + [M*N]*k;
// A(i,j,i); // []*i + [M]*j + [M*N]*i;
// a0 - b0 = a1x - b1x + a2x - b2x + ...
// Two ways to prove independence:
// 1. no integer solution: independent A(2i) & A(2i + 1)
// 2. integer solution is out of loop's iteration space
//
// cmp:
// A(i, j, k)
// A(i, j, i)
// A(i+1, j, i) // won't overlap with above
// 1, M, M*N = strides(A);
// stride of `i` == (1 + M*N)
// `A` is basically a matrix, j * M + (1 + M*N) * i
// do we want to represent as the matrix, or flatten to affine indexed vector?
// [] :  []*i + [M,N]*k
//       [[], [M,N]]*i
// [] : [ ... ]
// [M] : j
//       j
//
// A(1, foo(j))
// A(2, bar(j))
// A(i*i, ...)
//
// We should go ahead and implement loop splitting to break up dependencies /
// handle changing dep direction by breaking it up along that change.
//
// After failing to rule out a dependency (and thus adding a dep to the graph)
// what we need to do later on is...
// 1) symbolic distance vectors
// 2) Check direction of the dependency
// 3) Recheck that our transforms don't violate the dep, but maybe don't bother
// caching for this*
//
// Focusing on the symbolic distance vectors,
// for each loop, we have a symbolic expression giving dependency distance
// These can be used to...
// 1) answer questions about order
// 2) yield all the dependencies, with their levels
//    - gives guidelines to legal optimizations/strategy
//
// for (iter = 0; iter < I; ++iter){
//    S0: update!(A)
//    S1: lu!(A)
//    S2: ldiv!(A, Y)
// }
// iter loop adds dependencies:
// RTW S1 -> S1 0 < iter < I
// WTR S1 -> S1
//
// The symbolic distance vectors are to be indexed by loop,
// but, they should include everything relevent
// Which is another example where the stride partitioning can be helpful
// for reducing what is relevant
//
// A(i + 1, j) = A(i, j) + B
// i: i + 1 - i = 1
// j: j - j     = 0
// []  :  i + 1, i
// [M] :   j,    j
//
//
//
// should produce two strides:
// [] :  []*i + [M*N]*k;
//       [[] + [M*N]] * i; == []*i + [M*N]*i;
// [M]:  j;
//       j;
//
// so, we want to partition arrays in sets, as the partitioning
// A1: A(i,j,k)
// may be different when compared to
// A2: A(i,j,i)
// vs when compared to
// A3: A(i,j,k+K)
//
// M, N, L = size(A);
// 1, M, M*N = strides(A);
// For A1 vs A3
// []    : i v i
// [M]   : j v j
// [M,N] : k v k+K
//
// assert(2K <= L);
// assert(K > 0);
//
// for (m = 0; m < M; ++m){
//   for (n = 0; n < N1; ++n){
//     for (k = 0; k < K; ++k){
//
//     }
//   }
// }
// (0 <= m < M);
// (0 <= n < N1) * M;
// (0 <= k < K) * M*N;
//
// if N1 > N, then
// M*(N1-1) might cause overlap with M*N
// so, we need N1 <= N; scalar evolution analysis?
//
// for A1 vs A2
// []   : []*i + [M*N]*k v [[],[M,N]]*i
// [M]  : j v j
//
//
// A(i,j,i)

// should be O(N), passing through both arrays just once;
Polynomial::Multivariate<intptr_t> upperBound(Source indSrc, RektM loopvars) {
    // std::numeric_limits<Int>::max(): not making assumptions on returned
    // value.
    if (indSrc.typ == SourceType::LoopInductionVariable) {
        return Polynomial::loopToAffineUpperBound(getCol(loopvars, indSrc.id));
    } else {
        return Polynomial::Multivariate<intptr_t>(
            Polynomial::MultivariateTerm<intptr_t>(
                std::numeric_limits<Int>::max()));
    }
}
Polynomial::Multivariate<intptr_t> upperBound(Source indSrc,
                                              RectangularLoopNest loop) {
    return upperBound(indSrc, getUpperbound(loop));
}
std::pair<Int, Polynomial::Multivariate<intptr_t>>
getBounds(Source indSrc, RectangularLoopNest loop) {
    return std::make_pair(0, upperBound(indSrc, getUpperbound(loop)));
}
std::pair<Int, Polynomial::Multivariate<intptr_t>>
getBounds(Source indSrc, RectangularLoopNest loop, std::vector<Stride> &) {
    return std::make_pair(0, upperBound(indSrc, getUpperbound(loop)));
}

/*
std::pair<Int,Polynomial::Multivariate<intptr_t>> getBounds(RectangularLoopNest
&loop, std::vector<Stride> &axes){
std::vector<std::pair<Int,Polynomial::Multivariate<intptr_t>>> bounds;
    bounds.reserve(axes.size());
    // for (auto it = axes.begin();
    return std::make_pair(0,upperBound(indSrc, loop));
}
*/

// get as function of strides
Polynomial::Multivariate<intptr_t> getAsStrideFun() {}

// we want to return upper bounds as a function of indices
// previous loop ids are (1 << 16 + i), for stride id 0, ..., nloops-1
// previous stride ids are (1 << 24 + i), for stride id 0, ..., nstrides-1
std::pair<Polynomial::Multivariate<intptr_t>,
          Polynomial::Multivariate<intptr_t>>
getBounds(Source indSrc, TriangularLoopNest tri, std::vector<Stride> &axes) {
    // TODO: implement me
    Polynomial::Multivariate<intptr_t> L(0);
    Polynomial::Multivariate<intptr_t> U = upperBound(indSrc, getRekt(tri));
    TrictM A = getTrit(tri);
    size_t l = indSrc.id;

    // As this function is used for checking dependencies, we aggressively try
    // and define bounds as a function of the values along other axes.
    //
    // For example, if we have a case such as
    // for (i = 0; i < M; ++i){
    //   for (j = 0; j < i; ++j){
    //     B(i,j) = B(j,i)
    //   }
    // }
    // TODO: what if? (M, N, K are constants, i & j are induction variables)
    // for (i = 0; i < A; ++i){; for (j = 0; j < B; ++j){
    //     A(..., M0*i + N0*j + K0, ...)
    // };};
    //
    // x1 = M*i + N*j + K1 // [M/N/K]1 = [M/N/K]0 * stride
    // We have multiple induction variables, so we're unlikely to be able
    // to do a simple substituition and replace indices with stride positions.
    // Strategies to explore:
    // 1. Do symbolic simplification.
    // 2. See how restrictive each interval is on the reduction variables,
    // eliminate appropriately if compromises must be made and only some can be
    // dropped before switching to interval arithmetic for comparisons.
    //
    //
    // where `stride(A,2) == M`, such that we have
    // source: B[j + M*i]
    // target: B[i + M*j]
    // Additionall, `A`, the matrix defining loop bounds is:
    // [  1 0   [ i   < [ M
    //   -1 1 ]   j ]     0 ]
    // Note that for relating loop values to strides, the relationships are
    // trivially invertible here, as they are merely the identity function.
    // Thus, for the srouce we can easily go from `j` to `x0` and `i` to `x1`.
    // Similarly, for the target, we can go from `i` to `x0` and `j` to `x1`.
    //
    // for the source, we have:
    // when `indSrc == i`:
    // lowerBound_s_i = [j] = [x0] # lower bounds are inclusive
    // upperBound_s_i = [M]        # upper bounds are exclusive
    //
    //
    // when `indSrc == j`:
    // lowerBound_s_j = [0]          # inclusive
    // upperBound_s_j = [i] = [x1]   # exclusive
    //
    // Meanwhile, for the target, we have
    // when `indSrc == i`:
    // lowerBound_t_i = [j] = [x1]   # inclusive
    // upperBound_t_i = [M]          # exclusive
    //
    // when `indSrc == j`:
    // lowerBound_t_j = [0]          # inclusive
    // upperBound_t_j = [i] = [x0]   # exclusive
    //
    // Thus, were we to compare `i` for the source with `j` for the target, we
    // see (inclusive) lowerBound_s_i = [x0] >= [x0] = upperBound_t_j
    // (exclusive) meaning no overlap, and we reject dependence. Alternatively,
    // were we to compare `j` for the source with `i` for the target:
    // upperbound_s_j = [x1 - 1] < [x1] = lowerBound_t_i meaning, again, no
    // overlap and we reject dependence.
    //
    // if not invertible, we set to extreme bound instead.
    // visit all previous inds
    //
    // We take the approach of first calculating bounds in terms of loop
    // variables then, we try to translate the loop variables into strides.
    //
    // for (size_t i = 0; i < size(A,1); ++i){
    for (size_t i = 0; i < l; ++i) {
        Int Ail = A(i, l); // coefficient on other loop
        // if ((Ail == 0) | (i == l)){ continue; }
        if (Ail == 0) {
            continue;
        } else if (Ail > 0) {
            // Aij * i + l < M
            // Upper bound:
            // l < M - Aij * i
            //
            //
            //
            //
        } else { // Ail < 0
            // Aij * i + l < M
            // Upper bound:
            // l < M - Aij * i
            //
            //
        }
        // first, define as function of outer loop
        // then...
        //
    }
    // second loop, these each define separate equations
    for (size_t i = l + 1; i < size(A, 1); ++i) {
        Int Ail = A(i, l); // coefficient on this loop
        if (Ail == 0) {
            continue;
        } else if (Ail > 0) {
            // i + Aij * l < M
            // Upper bound:
            // l < (M - i) / Aij
            // must check entire strip A(0:i-1, i)
            //
            //
            //
            //
            //
        } else { // if Ail < 0
            // i + Aij * l < M
            // Lower bound:
            // (M - i) / Aij < l
            // must check entire strip A(0:i-1, i)
            //
            //
            //
            //
        }
    }
    return std::make_pair(std::move(L), std::move(U));
}
// x y
// T T T
// T F F
// F T F
// F F T
// only returns true if guaranteed

bool maybeLess(Function const &fun,
               Polynomial::Multivariate<intptr_t> const &diff) {
    if (diff.isZero()) {
        return false;
    }
    ValueRange r(0);
    for (auto it = diff.terms.begin(); it != diff.terms.end(); ++it) {
        r += valueRange(fun, *it);
    }
    return r.lowerBound < 0;
}
bool maybeLess(std::vector<ValueRange> const &x) {
    intptr_t lowerBound = 0;
    for (auto it = x.begin(); it != x.end(); ++it) {
        lowerBound += it->lowerBound;
    }
    return lowerBound < 0;
}

bool maybeLess(Function const &fun, Polynomial::Multivariate<intptr_t> const &x,
               Polynomial::Multivariate<intptr_t> const &y) {
    /*
    std::vector<ValueRange> diff;
    auto itx = x.begin(); auto itxe = x.end();
    auto ity = y.begin(); auto itye = y.end();
    while ((itx != itxe) & (ity != itye)) {
        if ((itx -> prodIDs) == (ity -> prodIDs)){
            if ((itx -> coefficient) != (ity -> coefficient)){
                intptr_t coefOld = itx -> coefficient;
                (itx -> coefficient) = coefOld - (ity -> coefficient);
                diff.push_back(valueRange(fun, *itx));
                (itx -> coefficient) = coefOld;
            }
            ++itx;
            ++ity;
        } else if (lexicographicalLess(*itx, *ity)){

            ++itx;
        } else {

            ++ity;
        }
    }
    // clean up x
    for (; itx != itxe; ++itx){

    }
    // clean up y
    for (; ity != itye; ++ity){

    }
    */
    // TODO: improve on this, by taking advantage of the cached difference info
    Polynomial::Multivariate<intptr_t> diff = x - y;
    return maybeLess(fun, diff);
}
/*
bool maybeLess(Function &fun, Polynomial::Multivariate<intptr_t> &&x,
Polynomial::Multivariate<intptr_t> &y) { x -= y; return maybeLess(fun, x);
}
*/
bool maybeLess(Function const &fun, Stride const &x,
               Polynomial::Multivariate<intptr_t> const &y) {
    for (auto it = x.begin(); it != x.end(); ++it) {
        if (maybeLess(fun, std::get<0>(*it), y)) {
            return true;
        }
    }
    return false;
}

template <typename I>
void pushMatchingStride(ArrayRef &ar, std::vector<Stride> const &strides,
                        I itsrc) {
    Source src = std::get<1>(*itsrc);
    for (size_t j = 0; j < strides.size(); ++j) {
        const Stride &s = strides[j];
        for (auto it = s.begin(); it != s.end(); ++it) {
            if (src == std::get<1>(*it)) {
                ar.indToStrideMap.push_back(j);
                return;
            }
        }
    }
}
/*
  void erase(std::vector<BitSet64> &x, size_t i){
    x.erase(x.begin() + i);
    for (size_t j = 0; j < x.size(); ++j){
        x[j].erase(i);
    }
    return;
}
*/

bool mayOverlap(
    Function const &fun, std::vector<Stride> const &strides,
    std::vector<Polynomial::Multivariate<intptr_t>> const &upperBounds,
    size_t j, size_t k) {
    return maybeLess(fun, strides[k], upperBounds[j]) &&
           maybeLess(fun, strides[j], upperBounds[k]);
}

void recheckStrides(
    Function const &fun, std::vector<Stride> &strides,
    std::vector<Polynomial::Multivariate<intptr_t>> &upperBounds, size_t j) {
    size_t eraseInds[64];
    size_t eraseCount;
    size_t jDec;
    while (true) {
        eraseCount = 0;
        jDec = 0;
        for (size_t k = 0; k < strides.size(); ++k) {
            if (k == j) {
                continue;
            }
            // if (recheck[k][j]) {
            // means that stride of the `k`th was previously
            // >= than `j`th upper bound. We must now check
            // if that is still true
            if (mayOverlap(fun, strides, upperBounds, j, k)) {
                // so it may now be less, that means we must
                // combine `k` with `j`, and then remove
                // `k`.
                strides[j] += strides[k];
                upperBounds[j] += upperBounds[k];
                eraseInds[++eraseCount] = k;
                jDec += k < j;
            }
            // }
        }
        for (size_t k = 0; k < eraseCount; ++k) {
            size_t del = eraseInds[eraseCount - 1 - k];
            strides.erase(strides.begin() + del);
            upperBounds.erase(upperBounds.begin() + del);
        }
        if ((eraseCount == 0) | (strides.size() <= 1)) {
            return;
        }
        j -= jDec;
    }
}

void partitionStrides(Function const &fun, ArrayRef ar, RektM loopnest) {
    // std::vector<std::pair<Vector<size_t, 0>, T>> strides;
    size_t Ninds = length(ar.inds);
    std::vector<Stride> &strides = ar.axes;
    strides.reserve(Ninds);
    std::vector<Polynomial::Multivariate<intptr_t>> &upperBounds =
        ar.upperBounds;
    upperBounds.reserve(Ninds);
    // std::vector<Polynomial::Multivariate<intptr_t>> strideSums;
    // strideSums.reserve(Ninds);
    // TODO: factor out rechecking for stride combining.
    // std::vector<BitSet64> recheck(Ninds);
    // std::vector<BitSet64> recheck; recheck.reserve(Ninds);
    // std::vector<size_t> recheck;
    auto ite = ar.inds.end();
    for (auto it = ar.inds.begin(); it != ite; ++it) {
        std::pair<Polynomial::Multivariate<intptr_t>, Source> ind = *it;
        Polynomial::Multivariate<intptr_t> &a = std::get<0>(ind);
        Source indSrc = std::get<1>(ind);

        Polynomial::Multivariate<intptr_t> ubi = upperBound(indSrc, loopnest);
        // auto [srcId, srcTyp] = ar.indTyps[i];
        bool overlaps = false;
        // BitSet64 upperBoundGreaterOrEqualToStride;
        for (size_t j = 0; j < length(strides); ++j) {
            // check if strides[j] is guaranteed not to overlap with affine .*
            // coefs if it might, we combine strides, and then keep iterating if
            // it might, and overlaps already set to true, keep collapsing
            // but...what about A(i,foo(j),k) // we can tell foo(j) doesn't
            // alias `i`, but we give up on `k`
            // we can check with a simplified form of the SIV Diophantine test,
            // because constant offsets for the same array ref are...the same.
            Stride &b = strides[j];
            // actually, instead, we'll be super lazy here.
            // splitting into strides is an optimization, making future analysis
            // easier. Failing to split doesn't cause incorrect answers
            // (but splitting when not legal might). Thus it's okay if we
            // miss some cases that would've been legal.
            //
            // We require every stride to be larger than the upper bound
            if (indSrc.typ != SourceType::LoopInductionVariable) {
                if (maybeLess(fun, b, ubi)) {
                    // if !, then `b` definitely >= than `ubi`
                    // we require the stride to be larger than the sum of the
                    // upper bounds
                    if (maybeLess(fun, a, upperBounds[j])) {
                        b.add_term(a, indSrc);
                        upperBounds[j] += ubi;
                        // now we need to revalidate, possibly collapsing
                        // strides.
                        recheckStrides(fun, strides, upperBounds, j);
                        overlaps = true;
                        break;
                    }
                }
            }
            // auto [g, ai, bi] = gcd(a, b.gcd);
            // Thus, we have
            // g = gcd(a, b);
            // i = k * b / g;
            // j = k * a / g;
            // auto [adg, bdg] = intersectionDifference(a, b);
            //
            // auto g = symbolicGCD(a, b);
        }
        if (!overlaps) {
            strides.emplace_back(a, indSrc);
            upperBounds.emplace_back(std::move(ubi));
        }
    }
    // now that strides should be settled, we'll fill the `indToStridemap`
    for (auto it = ar.inds.begin(); it != ite; ++it) {
        pushMatchingStride(ar, strides, it);
    }
    return;
}

template <typename L> void partitionStrides(ArrayRef ar, L loopnest) {
    partitionStrides(ar, getUpperbound(loopnest));
}

bool mayOverlap(Function &fun, ArrayRef &x, ArrayRef &y, size_t i, size_t j) {
    return maybeLess(fun, x.axes[j], y.upperBounds[i]) &&
           maybeLess(fun, y.axes[i], x.upperBounds[j]);
}
bool mayOverlap(
    Function &fun, std::vector<std::pair<Stride, Stride>> &strides,
    std::vector<std::pair<Polynomial::Multivariate<intptr_t>,
                          Polynomial::Multivariate<intptr_t>>> &upperBounds,
    size_t i, size_t j) {
    return (maybeLess(fun, strides[j].first, upperBounds[i].first) &&
            maybeLess(fun, strides[i].first,
                      upperBounds[j].first)) // does first overlap?
           || (maybeLess(fun, strides[j].second, upperBounds[i].second) &&
               maybeLess(fun, strides[i].second,
                         upperBounds[j].second)) // does second
           || (maybeLess(fun, strides[j].first, upperBounds[i].second) &&
               maybeLess(fun, strides[i].second,
                         upperBounds[j].first)) // does first overlap
           || (maybeLess(fun, strides[j].second, upperBounds[i].first) &&
               maybeLess(fun, strides[i].first,
                         upperBounds[j].second)); // with second?
}

void recheckStrides(
    Function &fun, std::vector<std::pair<Stride, Stride>> &strides,
    std::vector<std::pair<Polynomial::Multivariate<intptr_t>,
                          Polynomial::Multivariate<intptr_t>>> &upperBounds,
    size_t j) {
    // index `j` has been updated, so check `j` vs all others
    size_t eraseInds[64];
    size_t eraseCount;
    size_t jDec;
    while (true) {
        eraseCount = 0;
        jDec = 0;
        for (size_t k = 0; k < strides.size(); ++k) {
            if (k == j) {
                continue;
            }
            // if (recheck[k][j]) {
            // means that stride of the `k`th was previously
            // >= than `j`th upper bound. We must now check
            // if that is still true
            if (mayOverlap(fun, strides, upperBounds, k, j)) {
                // if (mayOverlap(fun, strides, upperBounds, j, k)) {
                // so it may now be less, that means we must
                // combine `k` with `j`, and then remove
                // `k`.
                strides[j].first += strides[k].first;
                strides[j].second += strides[k].second;
                upperBounds[j].first += upperBounds[k].first;
                upperBounds[j].second += upperBounds[k].second;
                eraseInds[++eraseCount] = k;
                jDec += k < j;
            }
        }
        for (size_t k = 0; k < eraseCount; ++k) {
            size_t del = eraseInds[eraseCount - 1 - k];
            strides.erase(strides.begin() + del);
            upperBounds.erase(upperBounds.begin() + del);
        }
        if ((eraseCount == 0) | (strides.size() <= 1)) {
            return;
        }
        j -= jDec;
    }
}

std::vector<std::pair<Stride, Stride>> pairStrides(Function &fun, ArrayRef &arx,
                                                   ArrayRef &ary) {
    // If we're here, then the array ref ids were not equal.
    //
    // What if `tx.loopNestId != ty.loopNestId` ??
    // bool sameLoopNest = tx.loopNestId == ty.loopNestId;
    // If not the same loop nest,
    // for (size_t i = 0, i < N; ++i){
    //   for (size_t j = 0, j < i; ++j){
    //     A[i,j] = ... // write to lower triangle
    //   }
    // }
    // for (size_t i = 0, i < N; ++i){
    //   for (size_t j = 0, j < i; ++j){
    //     A[j,i] = ... // write to upper triangle
    //   }
    // }
    // above two nests are independent
    // the correct line up is by stride value (`i0 === j1` and `j0 ==== i1`).
    // Similarly for
    // for (size_t i = 0; i < N; ++i){
    //   for (size_t j = 0, j < i; ++j){
    //     A[i,j] = ... // write to lower triangle
    //     A[j,i] = ... // write to upper triangle
    //   }
    // }
    //
    // We care more about the stride than the actual loop.
    // What matters for checking dependence is what can be reached,
    // NOT what is actually doing the reaching.
    // Hence, we care about strides and their upper bounds.

    // First step: match strides
    // for example: x: A(i,j,k), y: A(i,j,j)
    // x strides: [[]*i, [M]*j, [M*N]*k] // 3 strides
    // y strides: [[]*i, [[M] + [M*N]]*j] // 2 strides
    // For comparison, we should get 2 strides:
    // 1. []:  i        vs  i
    // 2. [M]: j + N*k  vs  [[] + [N]]*j
    //
    // Another example to consider:
    // x: A(i,j), y: A(j,i)
    // 1. []:  i vs j
    // 2. [M]: j vs i
    //
    // A simple example (we shouldn't reach here in this case):
    // x: A(i,j), y: A(i,j)
    // 1. []:  i vs i
    // 2. [M]: j vs j
    std::vector<std::pair<Stride, Stride>> strideCmp;
    std::vector<std::pair<Polynomial::Multivariate<intptr_t>,
                          Polynomial::Multivariate<intptr_t>>>
        upperBoundCmp;
    /*
    std::pair<intptr_t,size_t> foundX[64];
    std::pair<intptr_t,size_t> foundY[64];
    for (size_t i = 0; i < 64; ++i){
        foundX[i] = std::make_pair(-1, std::numeric_limits<size_t>::max());
        foundY[i] = std::make_pair(-1, std::numeric_limits<size_t>::max());
    }
    */
    intptr_t foundX[64];
    intptr_t foundY[64];
    for (size_t i = 0; i < 64; ++i) {
        foundX[i] = -1;
        foundY[i] = -1;
    }
    for (size_t i = 0; i < arx.axes.size(); ++i) {
        for (size_t j = 0; j < ary.axes.size(); ++j) {
            if (mayOverlap(fun, arx, ary, i, j)) {
                intptr_t pidX = foundX[i];
                intptr_t pidY = foundY[j];
                if ((pidX == -1) & (pidY == -1)) {
                    // foundX[i] = std::make_pair(intptr_t(j),
                    // strideCmp.size()); foundY[j] =
                    // std::make_pair(intptr_t(i), strideCmp.size());
                    foundX[i] = strideCmp.size();
                    foundY[j] = strideCmp.size();
                    strideCmp.push_back(
                        std::make_pair(arx.axes[i], ary.axes[j]));
                } else if (pidX == -1) {
                    // pidY gives ind of `X` it previously matched with
                    foundX[i] = pidY;
                    strideCmp[pidY].first += arx.axes[i];
                    strideCmp[pidY].second += ary.axes[j];
                    // need to check pidY for conflicts
                    recheckStrides(fun, strideCmp, upperBoundCmp, pidY);
                } else if (pidY == -1) {
                    // pidX gives ind of `Y` it previously matched with
                    foundY[j] = pidX;
                    strideCmp[pidX].first += arx.axes[i];
                    strideCmp[pidX].second += ary.axes[j];
                    // need to check pidX for conflicts
                    recheckStrides(fun, strideCmp, upperBoundCmp, pidX);
                } else if (pidX == pidY) {
                    strideCmp[pidX].first += arx.axes[i];
                    strideCmp[pidX].second += ary.axes[j];
                    // need to check pidX for conflicts
                    recheckStrides(fun, strideCmp, upperBoundCmp, pidX);
                } else {
                    // we combine `pidX`, `pidY`, and the current ind
                    // into `min(pidX, pidY)`, and then have to remove
                    // `max(pidX, pidY)`.
                    // erase cost is proportional in number that must
                    // be shifted.
                    intptr_t s = std::min(pidX, pidY);
                    intptr_t l = std::max(pidX, pidY);
                    strideCmp[s].first += arx.axes[i];
                    strideCmp[s].second += ary.axes[j];
                    strideCmp[s].first += strideCmp[l].first;
                    strideCmp[s].second += strideCmp[l].second;
                    // need to remove pidY
                    strideCmp.erase(strideCmp.begin() + l);
                    upperBoundCmp.erase(upperBoundCmp.begin() + l);
                    // need to check pidX for conflicts
                    recheckStrides(fun, strideCmp, upperBoundCmp, s);
                }
                break;
            }
        }
        if (foundX[i] == -1) {
            // did not find a match with `j`
            // thus, we append it by itself with a 0 stride for `j`.
            foundX[i] = strideCmp.size();
            strideCmp.push_back(std::make_pair(arx.axes[i], Stride()));
        }
    }
    for (size_t j = 0; j < ary.strides.size(); ++j) {
        if (foundY[j] == -1) {
            // we're done, so no need to update `foundY`.
            // foundY[j] = strideCmp.size();
            strideCmp.push_back(std::make_pair(Stride(), ary.axes[j]));
        }
    }
    return strideCmp;
}

enum DependenceType { Independent, LoopIndependent, LoopCarried };

template <typename T> ValueRange differenceRange(Function &fun, T it, T ite) {
    ValueRange r(0);
    for (; it != ite; ++it) {
        r += valueRange(fun, *it);
    }
    return r;
}
ValueRange differenceRange(Function &fun, Polynomial::Multivariate<intptr_t> &x,
                           Polynomial::Multivariate<intptr_t> &y) {
    Polynomial::Multivariate<intptr_t> diff = x - y;
    return differenceRange(fun, diff.begin(), diff.end());
}
// ValueRange differenceRangeConst(Function &fun,
// std::pair<Polynomial::Multivariate<intptr_t>,Source> &x) {
//     return differenceRange(fun, x.first.begin(), x.first.end()) *
//     valueRange(fun, x.second.id);
// }

DependenceType zeroInductionVariableTest(Function &fun, Stride &x, Stride &y) {
    // strides are equal => LoopIndependent
    if ((x.stride == y.stride)) {
        // both same constant
        return LoopIndependent;
    }
    Stride d = x - y;
    ValueRange r(0);
    for (auto it = d.begin(); it != d.end(); ++it) {
        auto [a, s] = *it;
        r += differenceRange(fun, a.begin(), a.end()) * valueRange(fun, s.id);
    }
    if ((r.lowerBound == 0) & (r.upperBound == 0)) {
        return LoopIndependent; // strides are equal
    }
    if ((r.lowerBound <= 0) & (r.upperBound >= 0)) {
        return LoopCarried; // bounds encompass zero, failed to prove
                            // independence
    } else {
        return Independent; // there is a difference => independent
    }
}
auto getFirstLoopStride(Stride &x) {
    auto it = x.begin();
    for (; it != x.end(); ++it) {
        if ((it->second).typ == SourceType::LoopInductionVariable) {
            return it;
        }
    }
#ifndef DONOTBOUNDSCHECK
    assert(it != x.end());
#endif
    return it;
    // return x.stride[0].first;
}

// for the case of
// for (i = 0; i < M; ++i){
//   for (j = 0; j < i; ++j){
//     A(i,j) = A(j,i)
//   }
// }
// Dependency checks will look at...
// stride 0: 0:M-1 vs 0:M-2 -- alias
// stride 1: 0:l0-1 vs l0+1:M-1
// So, in we need to add symbols associated with the LoopInductionVariables to
// the Polynomial::Multivariate<intptr_t>s Perhaps indicated by values >= 2^16
// (we're using uint32_fast for var ids).
template <typename LX, typename LY>
DependenceType singleInductionVariableTest(Function &fun, Stride &x, Stride &y,
                                           LX loopNestX, LY loopNestY) {
    // need to solve diophantine equations
    Stride delta = x - y;
    if (delta.size()) {
        if (delta.isConstant()) { // 1 constant term (# of affine const terms is
                                  // 0 or 1)
                                  // strong
            auto a = getFirstLoopStride(x);
            // auto [g, xf, yf] = gcd(delta.stride.begin() -> first, a);
            // auto [d, r] = xf.divRem(yf);
            auto [d, r] = (delta.axes.begin()->first).divRem(a->first);
            if (r.isZero()) {
                if (absLess(fun, d, upperBound(a->second, loopNestX)) &&
                    absLess(
                        fun, d,
                        upperBound(getFirstLoopStride(y)->second, loopNestY))) {
                    return Independent;
                } else {
                    // must check loop bounds
                    return LoopCarried;
                }
            } else if (((a->first) % r).isCompileTimeConstant()) {
                // r is not zero, but a % r is a constant, indicating
                // that we have a constant offset.
                return Independent;
            } else {
                // TODO: check loop bounds
                return LoopCarried;
            }
        } else {
            // weak
            auto [g, na, nb] =
                gcdx(getFirstLoopStride(x), getFirstLoopStride(y));
            // solve a1 * x + a0 = b1 * y + b0;
            if (delta.getCount(SourceType::Constant)) {
                // c = a0 - b0;
                // g, na, nb = gcdx(a1, b1);
                Polynomial::Multivariate<intptr_t> c = delta.begin()->first;
                // https://en.wikipedia.org/wiki/B%C3%A9zout%27s_identity
                // Bézout's identity
                // x(k) = -na * (c / g) + k * b1 / g;
                // y(k) =  nb * (c / g) + k * a1 / g;
                // we must therefore check if any solutions are within the loop
                // bounds i.e., is there any `k` that gives us an answer such
                // that x(k) in loopNestX && y(k) in loopNestY
            } else {
                // no constant term
                // solutions are
                // xk = k * b1 / g;
                // yk = k * a1 / g;
                // Question: do we have any solutions within the loop domain?
                // k = 0 => xk = 0, yk = 0
                // given we've set all loops to start at 0, this suggests
                // trivially that
                return LoopCarried;
            }
        }
    } else {
        return LoopIndependent;
    }
    return LoopCarried;
}

// the basic idea from Banerjee is just to ask whether there is a solution
// A(f(...))
// A(g(...))
// does this ever hold?
// f(...) == g(...)
// We assume linear, so these are linear diophantine
// a0 + a1 * i1 + a2 * i2 + ... = b0 + b1 * j1 + b2 * j2 + ...
// max_Rf f >= min_Rg g
// Rf is the range of inputs for f, so
// L_i_k <= i_k < U_i_k
//
// Lets hypothetically break things down, before backpedalling later
// h_k(i, j) = a_k*i - b_k*j
// \sum_{k=1}^K h_k(i_k, j_k) = b_0 - a_0
//
// p(x) = max(x, 0)
// m(x) = min(x, 0)
// x == p(x) + m(x)
// say 0 <= x <= s
// then
// m(t)*s <= t*x <= p(t)*s
// let y := (u - l) * x + l
// let s := 1
// then
// l <= y <= u
// x = (y - l) / (u - l)
// m(t) <= t*(y - l) / (y - l) <= p(t)
// (y - l)*m(t) <= t*y - t*l <= (y - l)*p(t)
// y*m(t) - l*m(t) + t*l <= t*y <= y*p(t) - l*p(t) + t*l
// y*m(t) - l*m(t) + (p(t) + m(t))*l <= t*y <= y*p(t) - l*p(t) + (p(t) +
// m(t)))*l y*m(t) + l*p(t) <= t*y <= y*p(t) + l*m(t) u*m(t) + l*p(t) <= t*y <=
// u*p(t) + l*m(t) imagine `y` is actually `i`, a loop induction variable and
// `t` is `a`, the slope/rate `f` changes w/ respect to `i`, that is `a * i`
// then, this is saying that `a * i` is bounded by...
// if a > 0: a*l <= a*i <= a*u
// if a < 0: a*u <= a*i <= a*l
//
// if the lower bound on sum of differences > 0, no intersection
// if the upper bound on sum of differences < 0, no intersection
// h_k_m(...) = u_k*m(a_k) + l_k*p(a_k) - (u_k*p(b_k) + l_k*m(b_k))
// h_k_p(...) = u_k*p(a_k) + l_k*m(a_k) - (u_k*m(b_k) + l_k*p(b_k))
// \sum_{k=1}^K h_k_m(...) <= 0 <= h_k_p(...)
// if the above is violated => no dependency
template <typename LX, typename LY>
DependenceType multipleInductionVariableTest(Function &fun, Stride &x,
                                             Stride &y, LX loopNestX,
                                             LY loopNestY) {
    return LoopCarried;
}

template <typename LX, typename LY>
bool checkIndependent(Function &fun, Term &tx, ArrayRef &arx, LX &loopNestX,
                      Term &ty, ArrayRef &ary, LY &loopNestY) {

    std::vector<std::pair<Stride, Stride>> stridePairs =
        pairStrides(fun, arx, ary);

    for (size_t i = 0; i < stridePairs.size(); ++i) {
        auto [sx, sy] = stridePairs[i];
        // SourceCount scx = sourceCount(sx);
        // SourceCount scy = sourceCount(sy);
        size_t numLoopInductVar =
            std::max(sx.getCount(SourceType::LoopInductionVariable),
                     sy.getCount(SourceType::LoopInductionVariable));
        // std::max(scx.loopInductVar, scy.loopInductVar);
        if (sx.isAffine() & sy.isAffine()) {
            switch (numLoopInductVar) {
            case 0: // ZIV
                zeroInductionVariableTest(fun, sx, sy);
                break;
            case 1: // SIV
                singleInductionVariableTest(fun, sx, sy, loopNestX, loopNestY);
                break;
            default: // MIV
                multipleInductionVariableTest(fun, sx, sy, loopNestX,
                                              loopNestY);
                break;
            }
        } else {
            // we check if they're equal?
            // but if they are equal, what does that tell us?
            // that on any particular iteration, the values are identical.
            // This is useful -- shows us that they're the same on a particular
            // iteration and thus should be noted. But it is not enough, as it
            // won't rule out loop carried dependencies.

            ;
        }
    }

    /*
    //std::bitset<64> xMatched;
    //std::bitset<64> yMatched;
    std::vector<Int> xMatched(arx.strides.size(), -1);
    std::vector<Int> yMatched(ary.strides.size(), -1);
    for (size_t i = 0; i < arx.inds.size(); ++i) {
        Source srci = std::get<1>(arx.inds[i]);
        size_t strdi = arx.indToStrideMap[i];

        for (size_t j = 0; j < ary.inds.size(); ++j) {
            Source srcj = std::get<1>(ary.inds[j]);
            if (srci == srcj) {
                size_t strdj = ary.indToStrideMap[j];
                // in strideCmp, we must add a combination of `strdi` and
                // `strdj`.
                // first, we check if the stride is already present
                Int xId = xMatched[i];
                Int yId = yMatched[j];
                if ((xId >= 0) | (yId >= 0)){
                    // we have already included this stride
                    if (xId == yId){
                        if (xId >= 0){ continue; } // don't need to do anything
                        // they're both `-1`, therefore neither have been added.
                        // now, we check if strides are equal
                        Stride &strideX = arx.axes[strdi];
                        Stride &strideY = ary.axes[strdj];

                    } else if (xId == -1) {
                        // `xId` not added yet, but `yId` already added. So we
    add missing `xId`. Stride &strideX = strideCmp[yId].first; strideX +=
    arx.axes[strdi];
                        // is `strideX` still valid as a separate stride?
                    } else if (yId == -1) {
                        // fuse with `xId`
                        Stride &strideY = strideCmp[xId].second;
                        strideY += ary.axes[strdj];
                        // is `strideY` still valid as a separate stride?
                    } else {
                        // they're pointing to two separate strides

                    }
                }

                Stride &si = arx.axes[strdi];
                Stride &sj = ary.axes[strdj];
                strideCmp.push_back(std::make_pair(si, sj));
            }
        }
    }
    */
    /*
    for (size_t i = 0; i < arx.strides.size(); ++i){
        Stride &stride_i = arx.axes[i];
        for (size_t j = 0; j < ary.strides.size(); ++j){
            if (yMatched[j]){ continue; } // TODO: confirm it doesn't also match
            Stride &stride_j = ary.axes[j];

        }
    }
    */
    return false;
}

template <typename PX, typename PY, typename LX, typename LY>
void partitionStrides(IndexDelta &differences, Function &fun, Term &tx,
                      ArrayRef &arx, PX &permx, LX &loopnestx, Term &ty,
                      ArrayRef &ary, PY &permy, LY &loopnesty) {
    // The approach is to just iterate over sources, checking all already added
    // sources to check
    // 1. if the stride is the same as an existing one
    // 2. if it does not match an existing one, but can overlap with an existing
    // one (means we must fuse them and find the greatest common divisor) 3.
    size_t numSourcesX = length(
        arx.programVariableCombinations); // VoVoV sources[ +[*[]] ] = Sources[i
                                          // * +[ *[] + *[M*N]], j * +[
    size_t numSourcesY = length(
        ary.programVariableCombinations); // ar(x/y).coef VoV constant
                                          // coefficients multiplying the +[*[]]
    // Stride is std::vector<std::pair<Vector<size_t,0>,T>>
    // differences.strides is:
    // std::vector<std::pair<Stride<Int>,std::vector<std::tuple<Stride<std::pair<Int,Int>>,size_t,SourceType>>>>
    // strides; // strides of `X` and `Y`
    for (size_t i = 0; i < std::min(numSourcesX, numSourcesY); ++i) {
        size_t idx = permx(i);
        size_t idy = permy(i);
        VoV<size_t> argsX = arx.programVariableCombinations(idx);
        Vector<Int, 0> coefsX = arx.coef(idx);

        VoV<size_t> argsY = ary.programVariableCombinations(idy);
        Vector<Int, 0> coefsY = ary.coef(idy);
        for (size_t j = 0; j < length(differences.strides); ++j) {
            if () {
            }
        }
    }
    // if numSourcesX is larger
    for (size_t i = numSourcesY; i < numSourcesX; ++i) {
    }
    // if numSourcesY is larger
    for (size_t i = numSourcesX; i < numSourcesY; ++i) {
    }
    if (numSourcesX > numSourcesY) {

    } else if (numSourcesX < numSourcesY) {
    }

    auto [maxIndx, maxLenx] = findMaxLength(arx.programVariableCombinations);
    auto [maxIndy, maxLeny] = findMaxLength(ary.programVariableCombinations);
    BitSet64 includedx;
    BitSet64 includedy;
    // the approach is to initialize it to the longest
    //
    if (maxLenx > maxLeny) {
        includedx[maxIndx] = true;
    } else if (maxLenx < maxLeny) {
        includedy[maxIndy] = true;
    } else { // they're equal
        includedx[maxIndx] = true;
    }

    // Stride is std::vector<std::pair<Vector<size_t,0>,T>>
    // differences.strides is:
    std::vector<std::pair<Stride<Int>,
                          std::vector<std::tuple<Stride<std::pair<Int, Int>>,
                                                 size_t, SourceType>>>>
        strides; // strides of `X` and `Y`

    differences.strides

        return;
}

template <typename PX, typename PY, typename LX, typename LY>
std::pair<IndexDelta, bool>
analyzeDependencies(Function &fun, Term &tx, size_t arxId, Term &ty,
                    size_t aryId, InvTree &it, PX permx, PY permy, LX loopnestx,
                    LY loopnesty) {

    IndexDelta differences; // default constructor, now we fill
    if ((arxId == aryId) & (permx == permy)) {
        // TODO: make sure this simple special case is handled.
    }

    ArrayRef arx = getArrayRef(fun, arxId);
    ArrayRef ary = getArrayRef(fun, aryId);

    partitionStrides(differences, fun, tx, arx, permx, loopnestx, ty, ary,
                     permy, loopnesty);

    Vector<size_t, 0> x = it(arxId);
    Vector<size_t, 0> y = it(aryId);
    for (size_t i = 0; i < length(x); ++i) {
        if (x(i) < y(i)) {
            // return 0;
        } else if (x(i) > y(i)) {
            // return 1;
        }
    }
}
// template <typename TX, typename TY>
// auto strideDifference(ArrayRefStrides strides, ArrayRef arx, TX permx,
// ArrayRef ary, TY permy){
//     std::vector<std::vector<std::vector<size_t>>> differences;
//     // iterate over all sources
//     // this is an optimized special case for the situation where `stridex ===
//     stridey`.
// };

template <typename TX, typename TY>
IndexDelta strideDifference(ArrayRef arx, TX permx, ArrayRef ary, TY permy) {
    IndexDelta differences;
    // iterate over all sources
    // do we really need sources to be in different objects from strides?
    // do we really want to match multiple different source objects to the same
    // strides object? if (arx.strideId == ary.strideId){ return
    // strideDifference(stridex, arx, permx, ary, permy); }
    BitSet64 matchedx; // zero initialized by default constructor
    for (size_t i = 0; i < length(ary.inds); ++i) {
        bool matchFound = false;
        auto [srcIdy, srcTypy] = arx.inds(i);
        for (size_t j = 0; j < length(arx.inds); ++j) {
            auto [srcIdx, srcTypx] = arx.inds(j);

            if ((srcIdx == srcIdy) & (srcTypx == srcTypy)) {
                std::vector<std::pair<Vector<size_t, 0>, Int>> diff =
                    difference(arx, permx(j), ary, permy(i));
                if (length(diff)) { // may be empty if all coefs cancel
                    differences.emplace_back(
                        std::make_tuple(diff, srcIdy, srcTypy));
                }
                matchedx[j] = true;
                matchFound = true;
                break; // we found our match
            }
        }
        if (!matchFound) { // then this source type is only used by `y`
            differences.emplace_back(
                std::make_tuple(mulCoefs(ary, permy(i), -1), srcIdy, srcTypy));
        }
    }
    for (size_t j = 0; j < length(arx.inds); ++j) {
        if (!matchedx[j]) { // then this source type is only used by `x`
            auto [srcIdx, srcTypx] = arx.inds(j);
            differences.emplace_back(
                std::make_tuple(mulCoefs(arx, permx(j), 1), srcIdx, srcTypx));
        }
    }
    differences.fillStrides();
    return differences;
};

BitSet64
inductionVariables(std::vector<std::tuple<Int, size_t, SourceType>> &x) {
    BitSet64 m;
    for (size_t i = 0; i < length(x); ++i) {
        m[i] = std::get<2>(x[i]) == LOOPINDUCTVAR;
    }
    return m;
}

// checks if `diff` can equal `0`, if it cannot, then the results are
// independent motivating example: loop that copies elements from below diagonal
// to above diagonal: for (size_t i = 0; i < N; ++i){
//   for (size_t j = 0; j < i; ++j){
//     A(i,j) = A(j,i);
//   }
// }
// there are no dependencies here.
// Another example:
// for (size_t i = 0; i < N, ++i){
//   for (size_t j = 0; j < N, ++j){
//     A(N,i) = A(j,N); // i and j never reach `N`
//   }
// }
// Any examples that consider strides?
//
// Inputs: fun, diff, x loop index, y loop index.
BitSet64 accessesIndependent(Function &fun, IndexDelta &diff, size_t lidx,
                             size_t lidy) {
    BitSet64 independent;
    if (!(diff.isStrided & diff.isLinear)) {
        return independent;
    } // give up
    // array is strided, and accesses are linear.
    for (size_t i = 0; i < length(diff.diffsByStride); ++i) {
        // check if we can get `0`, if we cannot, return `true`.
        // sources can be arbitrarilly long
        std::vector<std::tuple<Int, size_t, SourceType>> sources =
            diff.diffsByStride[i].second;
        BitSet64 m = inductionVariables(sources);
        switch (m.count()) {
        case 0: // ZIV

            break;
        case 1: // SIV
            // check if it can be zero.
            // then check to confirm that it is indeed a "valid" stride.
            break;
        case 2: // check if combination excludes 0

            break;
        default: // don't bother checking >= 3
            break;
        };
        // for (size_t j = 0; j < length(sources); ++j){

        // }
        // matchedx.reset();

        // std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType>
    }
    // we managed to get `0` on every index.
    return independent;
}
// TODO: implement
// getStride
// divrem(IndexDelta, stride);

// Check array id reference inds vs span
// for m in 1:M, n in 1:N
//   A[m,n] = A[m,n] / U[n,n]
//   for k in n+1:N
//     A[m,k] = A[m,k] - A[m,n]*U[n,k]
//   end
// end
//
// Lets consider all the loads and stores to `A` above.
// 1. A[m,n] = /(A[m,n], U[n,n])
// 2. tmp = *(A[m,n], U[n,k])
// 3. A[m,k] = -(A[m,k], tmp)
//
// We must consider loop bounds for ordering.
// Note that this example translates to
// for (m=0; m<M; ++m){ for (n=0; n<N; ++n){
//   A(m,n) = A(m,n) / U(n,n);
//   for (k=0; k<N-n-1; ++k){
//     kk = k + n + 1
//     A(m,kk) = A(m,kk) - A(m,n)*U(n,kk);
//   }
// }}
//
//
// for (m=0; m<M; ++m){ for (n=0; n<N; ++n){
//   for (k=0; k<n; ++k){
//     A(m,n) = A(m,n) - A(m,k)*U(k,n);
//   }
//   A(m,n) = A(m,n) / U(n,n);
// }}
//
// So for ordering, we must build up minimum and maximum vectors for comparison.
// Possible return values:
// -2: invalid
// -1: indepdent
//  0: x executes first
//  1: y executes first
template <typename PX, typename PY, typename LX, typename LY>
int8_t precedes(Function &fun, Term &tx, size_t xId, Term &ty, size_t yId,
                InvTree &it, PX permx, PY permy, LX loopnestx, LY loopnesty) {
    // iterate over loops
    Vector<size_t, 0> x = it(tx.id);
    Vector<size_t, 0> y = it(ty.id);
    ArrayRef arx = getArrayRef(fun, xId);
    ArrayRef ary = getArrayRef(fun, yId);

    IndexDelta diff = strideDifference(arx, permx, ary, permy);

    if (accessesIndependent(fun, diff, tx.loopNestId, ty.loopNestId)) {
        return -1;
    }

    for (size_t i = 0; i < length(x); ++i) {
        if (x(i) < y(i)) {
            return 0;
        } else if (x(i) > y(i)) {
            return 1;
        }
        // else x(i) == y(i)
        // we cannot reach here if `i == length(x) - 1`, as this last index
        // corresponds to order of statements w/in their inner most loop.
        // Two statements can have the same position. Thus, we do not need
        // any additional checks to break here.
        //
        // this loop occurs at the same time
        // TODO: finish...
        // basic plan is to look at the array refs, gathering all terms
        // containing ind then take difference between the two array refs that
        // gives offset. If diff contains any loop variables, must check to
        // confirm that value is < 0 or > 0, so that the dependency always
        // points in the same direction. We must also check stride vs offset to
        // confirm that the dependency is real. If (stride % offset) != 0, then
        // there is no dependency.
        //
        // We want to create a vector of parameter values, similar to what we
        // had in the `iscompatible` checks, giving multiples of different
        // loop constants.
        // We take the difference between X and Y, if there are any loop
        // variables in the difference, then we have to take the min and maximum
        // to confirm that one is always before the other or vice versa, i.e.
        // that the order of dependencies never change.
        // Then we use this to specify the order of the dependencies.
        //
        // Another thing to check for is whether there is a dependency, e.g.
        // if `offset` % `i`th loop stride != 0, then they are not actually
        // dependent.
        // else if the bounds are equal, they happen at the same time (at this
        // level) and we precede to the next iteration.
        // elseif xb > yb
        //   return false;
        // else(if xb < yb)
        //   return true;
        // end
        //
        // For example, if we have the loop
        // for (m=0; m<M; ++m){ for (n=0; n<N; ++n){
        //      A(m,n) /= U(n,n);
        //      for (k=n+1; k<N; ++k){
        //          A(m,k) -= A(m,n) * U(n,k);
        //      }
        // }}
        //
        // for (m=0; m<M; ++m){ for (n=0; n<N; ++n){
        //      A(m,n) = A(m,n) / U(n,n);
        //      for (k=0; k<N-n-1; ++k){
        //          A(m,k+n+1) = A(m,k+n+1) - A(m,n) * U(n,k+n+1);
        //      }
        // }}
        //
        // Let's compare `A(m,n) = A(m,n) / U(n,n)` write with
        // `A(m,k+n+1)` read at loop `n`.
        //
        // Checking `n` index of `A(m,n)`:
        // `[ [ M ] ] * n`
        // Checking `n` index of `A(m,k+n+1)`:
        // `[ [ M ] ] * n`
        // `[ [ M ] ] * k`
        // `[ [ M ] ] * 1`
        // TODO: use stride-based logic to exclude `m` from this comparison,
        // and to exclude `n` and `k+n+1` for comparison in `m` loop.
        // For `m` loop, everything else is constant/not changing as a function
        // of it...what does that look like?
        // Every other change has a stride of `M`, which is too large?
        //
        // Maybe check for everything multiplied by the strides that `m` appears
        // in, in this case, that is `[]` only? For now, let's take that
        // approach.
        //
        // (n + k + 1 ) * [ M ] - (n) * [ M ] == ( k + 1 ) * [ M ]
        // Here, the lower and upper bounds are ( 1 ) and
        // ( 1 + N - lower_bound(n) - 2 )
        // or ( 1 ) and ( N - 1 ). Both bounds exceed 0, thus
        // A(m, n + k + 1) is in the future.
        auto stride = getStride();
        auto [d, r] = divrem(diff, stride);
    }
    return 0;
}
template <typename PX, typename PY, typename LX>
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId,
              InvTree it, PX permx, PY permy, LX loopnestx) {
    auto [loopId, isTri] = getLoopId(ty);
    if (isTri) {
        return precedes(fun, tx, xId, ty, yId, it, permx, permy, loopnestx,
                        fun.triln[loopId]);
    } else {
        return precedes(fun, tx, xId, ty, yId, it, permx, permy, loopnestx,
                        fun.rectln[loopId]);
    }
}
template <typename PX, typename PY>
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId,
              InvTree it, PX permx, PY permy) {
    auto [loopId, isTri] = getLoopId(tx);
    if (isTri) {
        return precedes(fun, tx, xId, ty, yId, it, permx, permy,
                        fun.triln[loopId]);
    } else {
        return precedes(fun, tx, xId, ty, yId, it, permx, permy,
                        fun.rectln[loopId]);
    }
}
// definition with respect to a schedule
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId,
              Schedule &s) {
    return precedes(fun, tx, xId, ty, yId, InvTree(s.tree), s.perms(xId),
                    s.perms(yId));
}
// definition with respect to orginal order; original permutations are all
// `UnitRange`s, so we don't bother materializing
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId) {
    return precedes(fun, tx, xId, ty, yId, InvTree(fun.initialLoopTree),
                    UnitRange<size_t>(), UnitRange<size_t>());
}

void discoverMemDeps(Function fun, Tree<size_t>::Iterator I) {

    for (; I != Tree<size_t>(fun.initialLoopTree).end(); ++I) {
        auto [position, v, t] = *I;
        // `v` gives terms at this level, in order.
        // But, if this isn't the final level, not really the most useful
        // iteration scheme, is it?
        if (t.depth) {
            // descend
            discoverMemDeps(fun, t.begin());
        } else {
            // evaluate
            std::vector<std::vector<std::pair<size_t, size_t>>>
                &arrayReadsToTermMap = fun.arrayReadsToTermMap;
            std::vector<std::vector<std::pair<size_t, size_t>>>
                &arrayWritesToTermMap = fun.arrayWritesToTermMap;
            for (size_t i = 0; i < length(v); ++i) {
                size_t termId = v[i];
                Term &t = fun.terms[termId];
                for (size_t j = 0; j < length(t.srcs); ++j) {
                    auto [srcId, srcTyp] = t.srcs[j];
                    if (srcTyp == MEMORY) {
                        arrayReadsToTermMap[srcId].push_back(
                            std::make_pair(termId, j));
                        // std::vector<std::pair<size_t,size_t>> &loads =
                        // arrayReadsToTermMap[srcId];
                        // loads.push_back(std::make_pair(termId,j));
                        // stores into the location read by `Term t`.
                        std::vector<std::pair<size_t, size_t>> &stores =
                            arrayWritesToTermMap[srcId];
                        for (size_t k = 0; k < stores.size(); ++k) {
                            auto [wId, dstId] =
                                stores[k]; // wId is a termId, dstId is the id
                                           // amoung destinations;
                            Term &storeTerm = fun.terms[wId];
                            auto [arrayDstId, dstTyp] = storeTerm.dsts[dstId];
                            // if `dstTyp` is `MEMORY`, we transform it into
                            // `RTW` if `dstTyp` is `WTR` or `RTW`, we add
                            // another destination.
                            if (precedes(fun, t, srcId, storeTerm,
                                         arrayDstId)) {

                                // RTW (read then write)
                            } else {
                                // WTR (write then read)
                            }
                        }
                    }
                }
                for (size_t j = 0; j < length(t.dsts); ++j) {
                    auto [dstId, dstTyp] = t.dsts[j];
                    if (dstTyp == MEMORY) {
                    }
                }
            }
        }
    }
}
void discoverMemDeps(Function fun) {
    return discoverMemDeps(fun, Tree<size_t>(fun.initialLoopTree).begin());
}
