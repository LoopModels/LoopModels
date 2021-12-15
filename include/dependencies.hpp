#pragma once

#include "./graphs.hpp"
#include "./math.hpp"
#include "./ir.hpp"
#include <cstddef>
#include <utility>
#include <vector>

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
template <typename PX, typename PY, typename LX, typename LY>
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId, InvTree it, PX permx, PY permy, LX loopnestx, LY loopnesty){
    // iterate over loops
    Vector<size_t,0> x = it(xId);
    Vector<size_t,0> y = it(yId);
    auto [arx, arsx] = getArrayRef(fun, xId);
    auto [ary, arsy] = getArrayRef(fun, yId);
    for (size_t i = 0; i < length(x); ++i){
	if(x(i) < y(i)){
	    return true;
	} else if (x(i) > y(i)) {
	    return false;
	}
	// else x(i) == y(i)
	// this loop occurs at the same time
	// TODO: finish...
	// basic plan is to look at the array refs, gathering all terms containing ind
	// then take difference between the two array refs
	// that gives offset.
	// If diff contains any loop variables, must check to confirm that value is < 0 or > 0, so
	// that the dependency always points in the same direction.
	// We must also check stride vs offset to confirm that the dependency is real.
	// If (stride % offset) != 0, then there is no dependency.
    }
    return true;
}
template <typename PX, typename PY, typename LX>
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId, InvTree it, PX permx, PY permy, LX loopnestx){
    auto [loopId,isTri] = getLoopId(ty);
    if (isTri){
	return precedes(fun, tx, xId, ty, yId, it, permx, permy, loopnestx, fun.triln[loopId]);
    } else {
	return precedes(fun, tx, xId, ty, yId, it, permx, permy, loopnestx, fun.rectln[loopId]);
    }    
}
template <typename PX, typename PY>
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId, InvTree it, PX permx, PY permy){
    auto [loopId,isTri] = getLoopId(tx);
    if (isTri){
	return precedes(fun, tx, xId, ty, yId, it, permx, permy, fun.triln[loopId]);
    } else {
	return precedes(fun, tx, xId, ty, yId, it, permx, permy, fun.rectln[loopId]);	
    }
}
// definition with respect to a schedule
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId, Schedule &s){
    return precedes(fun, tx, xId, ty, yId, InvTree(s.tree), s.perms(xId), s.perms(yId));
}
// definition with respect to orginal order; original permutations are all `UnitRange`s, so we don't bother materializing
bool precedes(Function fun, Term &tx, size_t xId, Term &ty, size_t yId){
    return precedes(fun, tx, xId, ty, yId, InvTree(fun.initialLoopTree), UnitRange<size_t>(), UnitRange<size_t>());
}

void discoverMemDeps(Function fun, Tree<size_t>::Iterator I){
    
    for (; I != Tree<size_t>(fun.initialLoopTree).end(); ++I){
	auto [position, v, t] = *I;
	// `v` gives terms at this level, in order.
	// But, if this isn't the final level, not really the most useful iteration scheme, is it?
	if (t.depth){
	    // descend
	    discoverMemDeps(fun, t.begin());
	} else {
	    // evaluate
	    std::vector<std::vector<std::pair<size_t,size_t>>> &arrayReadsToTermMap = fun.arrayReadsToTermMap;
	    std::vector<std::vector<std::pair<size_t,size_t>>> &arrayWritesToTermMap = fun.arrayWritesToTermMap;
	    for (size_t i = 0; i < length(v); ++i){
		size_t termId = v[i];
		Term &t = fun.terms[termId];
		for (size_t j = 0; j < length(t.srcs); ++j){
		    auto [srcId, srcTyp] = t.srcs[j];
		    if (srcTyp == MEMORY){
			arrayReadsToTermMap[srcId].push_back(std::make_pair(termId, j));
			// std::vector<std::pair<size_t,size_t>> &loads = arrayReadsToTermMap[srcId];
			// loads.push_back(std::make_pair(termId,j));
			// stores into the location read by `Term t`.
			std::vector<std::pair<size_t,size_t>> &stores = arrayWritesToTermMap[srcId];
			for (size_t k = 0; k < stores.size(); ++k){
			    auto [wId, dstId] = stores[k]; // wId is a termId, dstId is the id amoung destinations;
			    Term &storeTerm = fun.terms[wId];
			    auto [arrayDstId, dstTyp] = storeTerm.dsts[dstId];
			    // if `dstTyp` is `MEMORY`, we transform it into `RTW`
			    // if `dstTyp` is `WTR` or `RTW`, we add another destination.
			    if (precedes(fun, t, srcId, storeTerm, arrayDstId)){
				
				// RTW (read then write)
			    } else {
				// WTR (write then read)
			    }
			}
		    }
		}
		for (size_t j = 0; j < length(t.dsts); ++j){
		    auto [dstId, dstTyp] = t.dsts[j];
		    if (dstTyp == MEMORY){
			
		    }
		}
	    }
	}
    }
}
void discoverMemDeps(Function fun){
    return discoverMemDeps(fun, fun.initialLoopTree.begin());
}


