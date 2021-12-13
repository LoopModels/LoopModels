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
//
template <typename PX, typename PY>
bool precedes(Function fun, size_t xId, size_t yId, InvTree it, PX permx, PY permy){
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
	// this loop occurs at the same time, 
	// TODO:
	
    }
    return true;
}
// definition with respect to a schedule
bool precedes(Function fun, size_t xId, size_t yId, Schedule s){
    return precedes(fun, xId, yId, InvTree(s.tree), s.perms(xId), s.perms(yId));
}
// definition with respect to orginal order; original permutations are all `UnitRange`s, so we don't bother materializing
bool precedes(Function fun, size_t xId, size_t yId){
    return precedes(fun, xId, yId, InvTree(fun.initialLoopTree), UnitRange<size_t>(), UnitRange<size_t>());
}

void discoverMemDeps(Function fun, Tree<size_t>::Iterator I){
    
    for (; I != Tree<size_t>(fun.initialLoopTree).end(); ++I){
	auto [v, t] = *I;
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
			std::vector<std::pair<size_t,size_t>> &stores = arrayWritesToTermMap[srcId];
			for (size_t k = 0; k < stores.size(); ++k){
			    auto [wId, dstId] = stores[k];
			    auto [arrayDstId, dstTyp] = fun.terms[wId].dsts[dstId];
			    // if `dstTyp` is `MEMORY`, we transform it into `RTW`
			    // if `dstTyp` is `WTR` or `RTW`, we add another.
			    if (precedes(fun, srcId, arrayDstId)){
				
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


