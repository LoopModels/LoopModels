#include <cassert>
#include <cstddef>
#include<vector>

// A set of `size_t` elements.
// Initially constructed
struct SmallSet{
    std::vector<size_t> data;
    std::vector<size_t> included;
    
    size_t operator[](size_t i) { return data[i]; } // allow `getindex` but not `setindex`

    SmallSet(size_t N) : data(std::vector<size_t>()), included(std::vector<size_t>(0, N)){}
};

size_t length(SmallSet &s){ return s.data.size(); }

size_t contains(SmallSet &s, size_t x){
    #ifndef DONOTBOUNDSCHECK
    if (x > s.included.size()) return false;
    #endif
    return s.included[x];
};

bool push(SmallSet &s, size_t x){
    #ifndef DONOTBOUNDSCHECK
    assert(x <= s.included.size());
    #endif
    size_t donotpush = contains(s, x);
    if (donotpush != 0){
	s.data.push_back(x);
	s.included[x] = s.data.size(); // ordered after push so that 0 indicates empty, and we branch on jz/jnz
    }
    return donotpush;
}

// Note: has linear complexity, removing elements from the backing vector is expensive.
size_t remove(SmallSet &s, size_t x){
    #ifndef DONOTBOUNDSCHECK
    assert(x <= s.included.size());
    #endif
    size_t contain = contains(s, x);
    if (contain){
	s.data.erase(s.data.begin() + (s.included[x] - 1));
	s.included[x] = 0;
    }
    return contain;
}


