#pragma once

#include "./graphs.hpp"
#include "./ir.hpp"
#include "./math.hpp"
#include "affine.hpp"
#include <algorithm>
#include <bitset>
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
    // std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType>&
    // operator[](size_t i){ return diffs[i]; }
};
// size_t length(IndexDelta &d){ return d.diffs.size(); }

std::vector<std::pair<Vector<size_t, 0>, Int>>
difference(ArrayRef x, size_t idx, ArrayRef y, size_t idy) {
    VoV<size_t> pvcx = x.programVariableCombinations(idx);
    Vector<Int, 0> coefx = x.coef(idx);
    VoV<size_t> pvcy = y.programVariableCombinations(idy);
    Vector<Int, 0> coefy = y.coef(idy);
    std::bitset<64> matchedx; // zero initialized by default constructor
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
                matchedx[j] = true;
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
template <typename T>
std::vector<T> intersectSortedVectors(Vector<T, 0> x, Vector<T, 0> y) {
    std::vector<T> z;
    size_t i = 0;
    size_t j = 0;
    while (i < length(x)) {
        T a = x(i);
        size_t ca = 1;
        ++i;
        while (i < length(x)) {
            if (x(i) == a) {
                ++ca;
                ++i;
            } else {
                break;
            }
        }
        // ca is the count of `a` in `x`
        size_t cb = 0;
        while (j < length(y)) {
            T b = y(j);
            if (b <= a) {
                if (b == a) {
                    ++cb;
                }
                // if (a == b){ z.push_back(a); }
                ++j;
            } else {
                break;
            }
        }
        size_t c = std::min(ca, cb);
        for (size_t k = 0; k < c; ++k) {
            z.push_back(a);
        }
    }
    return z;
}

template <typename T>
std::vector<T> intersectSortedVectors(std::vector<T> &z, std::vector<T> &x,
                                      Vector<T, 0> y) {
    size_t i = 0;
    size_t j = 0;
    while (i < length(x)) {
        T a = x[i];
        size_t ca = 1;
        ++i;
        while (i < length(x)) {
            if (x[i] == a) {
                ++ca;
                ++i;
            } else {
                break;
            }
        }
        // ca is the count of `a` in `x`
        size_t cb = 0;
        while (j < length(y)) {
            T b = y(j);
            if (b <= a) {
                if (b == a) {
                    ++cb;
                }
                // if (a == b){ z.push_back(a); }
                ++j;
            } else {
                break;
            }
        }
        size_t c = std::min(ca, cb);
        for (size_t k = 0; k < c; ++k) {
            z.push_back(a);
        }
    }
    return z;
}

template <typename T>
std::pair<std::vector<T>, std::vector<T>>
excludeIntersectSortedVectors(Vector<T, 0> x, Vector<T, 0> y) {
    std::vector<T> zx;
    std::vector<T> zy;
    size_t i = 0;
    size_t j = 0;
    while (i < length(x)) {
        T a = x(i);
        size_t ca = 1;
        ++i;
        while (i < length(x)) {
            if (x(i) == a) {
                ++ca;
                ++i;
            } else {
                break;
            }
        }
        // ca is the count of `a` in `x`
        size_t cb = 0;
        while (j < length(y)) {
            T b = y(j);
            if (b < a) {
                zy.push_back(b); // b not in `x`
                ++j;
            } else if (b == a) {
                ++j;
                ++cb;
            } else {
                break;
            }
        }
        size_t c = std::min(ca, cb);
        for (size_t k = c; k < ca; ++k) {
            zx.push_back(a);
        }
        for (size_t k = c; k < cb; ++k) {
            zy.push_back(a);
        }
    }
    return std::make_pair(zx, zy);
}

std::pair<std::vector<size_t>, Int>
symbolicGCD(std::vector<std::pair<Vector<size_t, 0>, Int>> a,
            std::vector<std::pair<Vector<size_t, 0>, Int>> b) {
    size_t na = length(a);
    size_t nb = length(b);
    if ((na == 1) & (nb == 1)) {
        auto [da, ca] = a[0];
        auto [db, cb] = b[0];
        return std::make_pair(intersectSortedVectors(da, db), std::gcd(ca, cb));
    } else if ((na == 0) | (nb == 0)) {
        return std::make_pair(std::vector<size_t>(), 1);
    } else {
        auto [da, ca] = a[0];
        auto [db, cb] = b[0];
        std::vector<size_t> dg = intersectSortedVectors(da, db);
        std::vector<size_t> temp;
        Int cg = std::gcd(ca, cb);
        for (size_t i = 1; i < length(a); ++i) {
            auto [da, ca] = a[i];
            intersectSortedVectors(temp, dg, da);
            std::swap(dg, temp);
            temp.clear();
            cg = std::gcd(cg, ca);
        }
        return std::make_pair(dg, cg);
    }
}
std::pair<std::vector<std::pair<std::vector<size_t>, Int>>,
          std::vector<std::pair<std::vector<size_t>, Int>>>
divByGCD(std::vector<std::pair<Vector<size_t, 0>, Int>> a,
         std::vector<std::pair<Vector<size_t, 0>, Int>> b) {
    std::vector<std::pair<std::vector<size_t>, Int>> ga;
    std::vector<std::pair<std::vector<size_t>, Int>> gb;
    size_t na = length(a);
    size_t nb = length(b);
    if ((na == 1) & (nb == 1)) {
        auto [da, ca] = a[0];
        auto [db, cb] = b[0];
        auto [dadg, dbdg] = excludeIntersectSortedVectors(da, db);
        Int g = std::gcd(ca, cb);
        Int cadg = ca / g;
        Int cbdg = cb / g;
        ga.emplace_back(std::make_pair(dadg, cadg));
        gb.emplace_back(std::make_pair(dbdg, cbdg));
    } else {
    }
    return std::make_pair(ga, gb);
}

Symbol::Affine upperBound(std::pair<size_t,SourceType> inds, RektM loopvars){
    auto [srcId, srcTyp] = inds;
    // std::numeric_limits<Int>::max(): not making assumptions on returned value.
    if (srcTyp == LOOPINDUCTVAR){
	return loopToAffineUpperBound(getCol(loopvars, srcId));
    } else {
	return Symbol::Affine(Symbol(std::numeric_limits<Int>::max()));
    }
}
// x y
// T T T
// T F F
// F T F
// F F T
// only returns true if guaranteed

bool maybeLess(Function &fun, Symbol::Affine &diff){
    if (isZero(diff)){ return false; }
    bool positive = diff.gcd.coef > 0;
    // here we check if all terms summed in the `diff` are positive.
    for (auto it = diff.terms.begin(); it != diff.terms.end(); ++it){
	Symbol &rit = *it;
	bool itpos = positive == (rit.coef > 0);
	for (auto s = rit.prodIDs.begin(); s != rit.prodIDs.end(); ++s){
	    Sign sign = fun.signMap[*s];
	    if (sign == UNKNOWNSIGN){ return true; }
	    itpos = (itpos == ((sign == POSITIVE) | (sign == NONNEGATIVE)));
	}
	if (!itpos){ return true; }
    }
    return false;
}
bool maybeLess(Function &fun, Symbol::Affine &x, Symbol::Affine &y){
    Symbol::Affine diff = x - y;
    return maybeLess(fun, diff);
}
bool maybeLess(Function &fun, Symbol::Affine &&x, Symbol::Affine &y){
    x -= y;
    return maybeLess(fun, x);
}
bool maybeLess(Function &fun, Stride &x, Symbol::Affine &y){
    for (auto it = x.stride.begin(); it != x.stride.end(); ++it){
	if (maybeLess(fun, std::get<0>(*it) * x.gcd, y)){ return true; }
    }
    return false;
}
void partitionStrides(Function &fun, ArrayRef ar, RektM loopnest) {
    // std::vector<std::pair<Vector<size_t, 0>, T>> strides;
    size_t Ninds = length(ar.inds);
    std::vector<Stride> &strides = ar.strides;
    strides.reserve(Ninds);
    std::vector<Symbol::Affine> upperBounds;
    upperBounds.reserve(Ninds);
    // std::vector<Symbol::Affine> strideSums;
    // strideSums.reserve(Ninds);
    std::vector<std::bitset<64>> recheck;
    recheck.reserve(Ninds);
    for (size_t i = 0; i < Ninds; ++i) {
	Symbol::Affine &affine = ar.inds[i];
	Symbol::Affine ubi = upperBound(ar.indTyps[i], loopnest);
        // auto [srcId, srcTyp] = ar.indTyps[i];
	Symbol::Affine &a = ar.inds[i];
	bool overlaps = false;
	std::bitset<64> upperBoundGreaterOrEqualToStride;
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
	    if (ar.indTyps[i].second != LOOPINDUCTVAR){
		if (maybeLess(fun, b, ubi)){ // if !, then `b` definitely >= than `ubi`
		    // we require the stride to be larger than the sum of the upper bounds
		    if (maybeLess(fun, a, upperBounds[j])) {
			b.add_term(a, ar.indTyps[i]);
			upperBounds[j] += ubi;
			// now we need to revalidate, possibly collapsing strides.
			std::vector<size_t> eraseInds; // record strides we're removing
			for (size_t k = 0; k < length(strides); ++k){
			    if (k == j){ continue; }
			    if (recheck[k][j]){
				// means that stride of the `k`th was previously >= than `j`th upper bound.
				// We must now check if that is still true
				if (maybeLess(fun, strides[k], upperBounds[j])){
				    // so it may now be less, that means we must combine `k` with `j`, and then remove `k`.
				    b += strides[k];
				    upperBounds[j] += upperBounds[k];
				    eraseInds.push_back(k);
				}
			    }
			}
			for (size_t k = 0; k < length(eraseInds); ++k){
			    size_t del = eraseInds[length(eraseInds) - 1 - k];
			    strides.erase(strides.begin() + del);
			    upperBounds.erase(upperBounds.begin() + del);
			    recheck.erase(recheck.begin() + del);
			}
			overlaps = true;
			break;
		    } else {
			// need to recheck because `i`th stride is definitely larger than upperBound[j].
			// recheck[j][j] = true;
			upperBoundGreaterOrEqualToStride[j] = true;
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
            strides.emplace_back(affine, ar.indTyps[i]);
	    upperBounds.emplace_back(std::move(ubi));
	    recheck.push_back(std::move(upperBoundGreaterOrEqualToStride));
	    // recheck.push_back(std::bitset<64>());
        }
    }
    // now that strides should be settled, we'll fill the `indToStridemap`
    for (size_t i = 0; i < Ninds; ++i){
	std::bitset<32> m;
	auto [srcId, srcTyp] = ar.indTyps[i];
	for (size_t j = 0; j < strides.size(); ++j){
	    Stride &s = strides[j];
	    for (auto it = s.stride.begin(); it != s.stride.end(); ++it){
		if ((srcId == std::get<1>(*it)) & (srcTyp == std::get<2>(*it))){
		    m[j] = true;
		    break;
		}
	    }
	}
	ar.indToStrideMap.push_back(std::move(m));
    }
    return;
}
template <typename L> void partitionStrides(ArrayRef ar, L loopnest) {
    partitionStrides(ar, getUpperbound(loopnest));
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
    std::bitset<64> includedx;
    std::bitset<64> includedy;
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
    std::bitset<64> matchedx; // zero initialized by default constructor
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

#if __WORDSIZE == 64
inline uint64_t to_uint64_t(std::bitset<64> &x) { return x.to_ulong(); }
#else
inline uint64_t to_uint64_t(std::bitset<64> &x) { return x.to_ullong(); }
#endif

std::bitset<64>
inductionVariables(std::vector<std::tuple<Int, size_t, SourceType>> &x) {
    std::bitset<64> m;
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
std::bitset<64> accessesIndependent(Function &fun, IndexDelta &diff,
                                    size_t lidx, size_t lidy) {
    // std::bitset<64> matchedx; // zero initialized
    std::bitset<64> independent;
    if (!(diff.isStrided & diff.isLinear)) {
        return independent;
    } // give up
    // array is strided, and accesses are linear.
    for (size_t i = 0; i < length(diff.diffsByStride); ++i) {
        // check if we can get `0`, if we cannot, return `true`.
        // sources can be arbitrarilly long
        std::vector<std::tuple<Int, size_t, SourceType>> sources =
            diff.diffsByStride[i].second;
        std::bitset<64> m = inductionVariables(sources);
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
