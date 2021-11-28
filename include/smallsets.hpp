#include <cstddef>
#include<vector>

template <typename V, typename T>
bool contains(V data, T x){
    bool c = false;
    for (size_t i = 0; i < length(data); ++i){
	c |= (data[i] == x);
    }
    return c;
}

template <typename T>
struct SmallSet{
    std::vector<T> data;
    void push_back(T x){
	if (!contains(data, x))
	    data.push_back(x);
    }
    void emplace_back(T x){
	if (!contains(data, x))
	    data.emplace_back(x);
    }
    size_t size(){ return data.size(); }
    T operator[](size_t i) { return data[i]; } // allow `getindex` but not `setindex`
};


