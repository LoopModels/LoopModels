#pragma once

#include <ir.hpp>
#include <math.hpp>

struct Schedule {
    Int *ptr;
    size_t nloops;
};

template <> size_t getNLoops(Schedule x) { return x.nloops; };

Permutation getpermutation(Schedule x) {
    return Permutation(x.ptr, getNLoops(x));
};
Vector<Int, 0> getbeta(Schedule x){
    return Vector<Int, 0>(x.ptr + 2 * getNLoops(x), 1 + 2 * getNLoops(x))};

size_t schedule_size(size_t nloops){return 4 * nloops + 1};
size_t schedule_size(Schedule x) { return schedule_size(getNLoops(x)); };

struct BaselineModelCost {};

void visit(std::vector<Int> sorted, Function fun, size_t idx) {
    if (fun.visited(idx)) return;
    auto outs = outneighbors(fun, idx);
    for (size_t j=0; j<length(outs); ++j)
        visit(sorted, fun, outs(j));
    sorted.push_back(idx);
}

void topologicalSort(Function fun, TermBundle tb){
    std::vector<TermBundle> terms;
    std::vector<Int> sorted;
    clear(fun);
    for (size_t j = 0; j < nv(fun); j++) {
        visit(sorted, fun, j);
    }

}


void schedule(Function fun, TermBundle tb) { return; }
