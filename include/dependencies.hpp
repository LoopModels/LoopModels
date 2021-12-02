#pragma once

#include "./graphs.hpp"
#include "./math.hpp"
#include "./ir.hpp"
#include <cstddef>
#include <vector>

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
			std::vector<std::pair<size_t,size_t>> &loads = arrayReadsToTermMap[srcId];
			std::vector<std::pair<size_t,size_t>> &stores = arrayReadsToTermMap[srcId];
			
			loads.push_back(termId);
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


