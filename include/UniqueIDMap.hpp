#pragma once


#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseMapInfo.h>

template<typename T>
struct UniqueIDMap {
    llvm::DenseMap<T,unsigned> map;
    unsigned operator[](const T& x){
	auto c = map.find(x);
	if (c != map.end())
	    return c->second;
	unsigned count = map.size();
	map[x] = count;
	return count;
    }
};


