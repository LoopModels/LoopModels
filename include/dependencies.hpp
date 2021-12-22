#pragma once

#include "./graphs.hpp"
#include "./ir.hpp"
#include "./math.hpp"
#include "affine.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <bitset>
#include <algorithm>

struct ShorterCoef {
    bool operator()(std::pair<Vector<size_t,0>,std::vector<std::tuple<Int,size_t,SourceType>>> &x, std::pair<Vector<size_t,0>,std::vector<std::tuple<Int,size_t,SourceType>>> &y){
	return length(x.first) < length(y.first);
    }
};

struct IndexDelta{
    // { src {                  aff terms of src { ids mulled }, coef }, srcId,  type  }
    std::vector<std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType>> diffsBySource;
    std::vector<std::pair<Vector<size_t,0>,std::vector<std::tuple<Int,size_t,SourceType>>>> diffsByStride; // sorted by length
    bool isStrided;
    bool isLinear;
    void checkStrided(){ // O(N^2) check, but `N` should generally be small.
	// Assumes that `diffsByStride` is sorted by length of the `Vector<size_t,0>`s.
	// Assumes that the contents of the `Vector<size_t,0>`s are sorted.
	isStrided = true;
	for (size_t i = 1; i < length(diffsByStride); ++i){
	    Vector<size_t,0> stride0 = diffsByStride[i-1].first;
	    Vector<size_t,0> stride1 = diffsByStride[i].first;
	    size_t j = 0;
	    for (size_t k = 0; k < length(stride0); ++k){
		if (j == length(stride1)){
		    isStrided = false; return;
		}
		// stride1 must be a super set of stride0, so we keep incrementing `j` while trying to find a match.
		while (stride0[k] != stride1[j]) {
		    ++j;
		    if (j == length(stride1)){ // if we reach the end, that means there was an element in `stride0` absent in `stride1`.
			isStrided = false; return;
		    }
		}
		++j;
	    }
	}
    }
    void checkLinear(){
	isLinear = true;
	for (size_t i = 0; i < length(diffsBySource); ++i){
	    SourceType srcTyp = std::get<2>(diffsBySource[i]);
	    if (!((srcTyp == CONSTANT) | (srcTyp == LOOPINDUCTVAR))){
		isLinear = false; return;
	    }
	}
    }
    void fillStrides(){
	for (size_t i = 0; i < length(diffsBySource); ++i){
	    auto [coefpairs, srcId, srcTyp] = diffsBySource[i];
	    for (size_t j = 0; j < length(coefpairs); ++j){
		auto [stride, mul] = coefpairs[j];
		std::tuple<Int,size_t,SourceType> newSource = std::make_tuple(mul, srcId, srcTyp);
		bool found = false;
		for (size_t k = 0; k < length(diffsByStride); ++k){
		    auto [stridek, terms] = diffsByStride[k];
		    if (stridek == stride){
			terms.emplace_back(newSource);
			found = true; break;
		    }
		}
		if (!found){
		    std::vector<std::tuple<Int,size_t,SourceType>> newSources { newSource };
		    diffsByStride.emplace_back(std::make_pair(stride, newSources));
		}
	    }
	}
	std::sort(diffsByStride.begin(), diffsByStride.end(), ShorterCoef());
	checkStrided();
	checkLinear();
    }
    void emplace_back(std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType> &&x){ diffsBySource.emplace_back(x); return; }
    // std::tuple<std::vector<std::pair<Vector<size_t,0>,Int>>,size_t,SourceType>& operator[](size_t i){ return diffs[i]; }
};
// size_t length(IndexDelta &d){ return d.diffs.size(); }

std::vector<std::pair<Vector<size_t,0>,Int>> difference(ArrayRefStrides x, size_t idx, ArrayRefStrides y, size_t idy){
    VoV<size_t> pvcx = x.programVariableCombinations(idx);
    Vector<Int,0> coefx = x.coef(idx);
    VoV<size_t> pvcy = y.programVariableCombinations(idy);
    Vector<Int,0> coefy = y.coef(idy);
    std::bitset<64> matchedx; // zero initialized by default constructor
    std::vector<std::pair<Vector<size_t,0>,Int>> diffs;
    for (size_t i = 0; i < length(pvcy); ++i){
	bool matchFound = false;
	Int coefi = coefy(i);
	Vector<size_t,0> argy = pvcy(i);
	for (size_t j = 0; j < length(pvcx); ++j){
	    Vector<size_t,0> argx = pvcx(j);
	    if (argx == argy){
		Int deltaCoef = coefx(j) - coefi;
		if (deltaCoef){// only need to push if not 0
		    diffs.emplace_back(std::make_pair(argy, deltaCoef));
		}
		matchedx[j] = true;
		matchFound = true;
		break;
	    }
	}
	if (!matchFound){
	    diffs.emplace_back(std::make_pair(argy, -coefi));
	}
    }
    for (size_t j = 0; j < length(pvcx); ++j){
	if (!matchedx[j]) { 
	    diffs.emplace_back(std::make_pair(pvcx(j), coefx(j)));
	}
    }
    return diffs;
}

std::vector<std::pair<Vector<size_t,0>,Int>> mulCoefs(ArrayRefStrides x, size_t idx, Int factor){
    VoV<size_t> pvcx = x.programVariableCombinations(idx);
    Vector<Int,0> coefx = x.coef(idx);
    std::vector<std::pair<Vector<size_t,0>,Int>> diffs;
    for (size_t j = 0; j < length(pvcx); ++j){
	diffs.emplace_back(std::make_pair(pvcx(j), factor * coefx(j)));
    }
    return diffs;
}



// template <typename TX, typename TY>
// auto strideDifference(ArrayRefStrides strides, ArrayRef arx, TX permx, ArrayRef ary, TY permy){ 
//     std::vector<std::vector<std::vector<size_t>>> differences;
//     // iterate over all sources
//     // this is an optimized special case for the situation where `stridex === stridey`.
// };

template <typename TX, typename TY>
IndexDelta strideDifference(ArrayRefStrides stridex, ArrayRef arx, TX permx, ArrayRefStrides stridey, ArrayRef ary, TY permy){ 
    IndexDelta differences;
    // iterate over all sources
    // do we really need sources to be in different objects from strides?
    // do we really want to match multiple different source objects to the same strides object?
    // if (arx.strideId == ary.strideId){ return strideDifference(stridex, arx, permx, ary, permy); }
    std::bitset<64> matchedx; // zero initialized by default constructor
    for (size_t i = 0; i < length(ary.inds); ++i){
	bool matchFound = false;
	auto [srcIdy, srcTypy] = arx.inds(i);
	for (size_t j = 0; j < length(arx.inds); ++j){
	    auto [srcIdx, srcTypx] = arx.inds(j);
	    
	    if ((srcIdx == srcIdy) & (srcTypx == srcTypy)){
		std::vector<std::pair<Vector<size_t,0>,Int>> diff = difference(stridex, permx(j), stridey, permy(i));
		if (length(diff)){ // may be empty if all coefs cancel
		    differences.emplace_back(std::make_tuple(diff, srcIdy, srcTypy));
		}
		matchedx[j] = true; matchFound = true; break; // we found our match
	    }
	}
	if (!matchFound){ // then this source type is only used by `y`
	    differences.emplace_back(std::make_tuple(mulCoefs(stridey, permy(i), -1), srcIdy, srcTypy));
	}
    }
    for (size_t j = 0; j < length(arx.inds); ++j){
	if (!matchedx[j]){ // then this source type is only used by `x`
	    auto [srcIdx, srcTypx] = arx.inds(j);
	    differences.emplace_back(std::make_tuple(mulCoefs(stridex, permx(j), 1), srcIdx, srcTypx));
	}
    }
    differences.fillStrides();
    return differences;
};

#if __WORDSIZE == 64
inline uint64_t to_uint64_t(std::bitset<64> &x){ return x.to_ulong(); }
#else
inline uint64_t to_uint64_t(std::bitset<64> &x){ return x.to_ullong(); }
#endif

std::bitset<64> inductionVariables(std::vector<std::tuple<Int,size_t,SourceType>> &x){
    std::bitset<64> m;
    for (size_t i = 0; i < length(x); ++i){
	m[i] = std::get<2>(x[i]) == LOOPINDUCTVAR;
    }
    return m;
}

// checks if `diff` can equal `0`, if it cannot, then the results are independent
// motivating example: loop that copies elements from below diagonal to above diagonal:
// for (size_t i = 0; i < N; ++i){
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
std::bitset<64> accessesIndependent(Function &fun, IndexDelta &diff, size_t lidx, size_t lidy){
    // std::bitset<64> matchedx; // zero initialized
    std::bitset<64> independent;
    if (!(diff.isStrided & diff.isLinear)){ return independent; }// give up
    // array is strided, and accesses are linear.
    for (size_t i = 0; i < length(diff.diffsByStride); ++i){
	// check if we can get `0`, if we cannot, return `true`.
	// sources can be arbitrarilly long
	std::vector<std::tuple<Int,size_t,SourceType>> sources = diff.diffsByStride[i].second;
	std::bitset<64> m = inductionVariables(sources);
	switch (m.count()){
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
    Vector<size_t, 0> x = it(xId);
    Vector<size_t, 0> y = it(yId);
    auto [arx, arsx] = getArrayRef(fun, xId);
    auto [ary, arsy] = getArrayRef(fun, yId);

    IndexDelta diff = strideDifference(arsx, arx, permx, arsy, ary, permy);

    if (accessesIndependent(fun, diff, tx.loopNestId, ty.loopNestId)){ return -1; }
    
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
