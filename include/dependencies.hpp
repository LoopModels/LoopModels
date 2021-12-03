#pragma once

#include "./graphs.hpp"
#include "./math.hpp"
#include "./ir.hpp"
#include <cstddef>
#include <utility>
#include <vector>

bool precedes(Function fun, size_t xId, size_t yId){
    
    return true;
}

void discoverMemDeps(Function fun, Tree<size_t>::Iterator I){
    
    for (; I != fun.initialLoopTree.end(); ++I){
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


