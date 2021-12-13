#pragma once

#include "./ir.hpp"
#include "./math.hpp"
#include "./graphs.hpp"
#include <algorithm>
#include <vector>


// struct BaselineModelCost {};

// template <typename A>
// cycles marks write leading to cycle, and outnum of the write.

// void fillfastmemcostsum(Function fun) { return; }

//
// x = foo(...)
// y = bar(...)
// z = x + y
// Maybe we do want x and z together, and y separate
//
// T* mem = alloca(2048);
// d, r = divrem(length(I), 2048);
// for di in 1:d
// ...
// for i in I
//   mem[i] = y = bar(...)
// end
// for i in I
//   x = foo(...)
//   z = x + mem[i]
// end
// end
//

// fusion(level) -> order(level) -> cost(level) ->
// fusion(level+1) -> ...
//

// An idea:
// Create vector, initialized to length(inNeighbors(tb)) per TermBundle
//   Each time a TermBundle is scheduled, decrement all descendents's values
//   Once a value is 0, this means it can be scheduled/queued.
// But then, what about cycles?
// Given an empty queue, search for SCCs?
// Or, should the graph be of strongly connected components?
//
// Latter sounds easier to work with.
//
// Once an SCC can be scheduled, we can then worry about TermBundles within.


template <typename I>
void scheduleBundleFusion(Function fun, I tidx_begin, I tidx_end, size_t level,
                          bool updateCost) {
    for (auto it = tidx_begin; it != tidx_end; ++it) {
        auto [c0, isvalid] =
            scheduleBundleOrder(fun, tidx_begin, it + 1, level);
        if (!isvalid)
            break;
        double c1;
        if (it == tidx_begin) {
            c1 = scheduleBundleFusion(fun, it + 1, tidx_end, level, false);
        } else {
            c1 = getbestcost(fun, it + 1, tidx_end, level);
        }
        if (((c0 + c1) < best_cost)) {

            if (updateCost) {
                updateBestCost();
            }
        }
    }
}
// Evaluates [`tidx_begin`, `tidx_end`) as a bundle together, fused at level
// `level`
template <typename I>
void schedule_bundle_order(Function fun, I tidx_begin, I tidx_end,
                           size_t level) {}

void schedule_bundle_level(Function fun, TermBundle tb, size_t level) {}

// Greedily prefuse elements in tb.
TermBundleGraph &prefuse(Function fun, std::vector<Int> wcc) {
    std::vector<TermBundle> tbs;
}

// at level `l`...
// 1. iterate over fusion combinations

void scheduleBundleFusion(Function fun, std::vector<TermBundle> tbs,
                          size_t i = 0) {
    TermBundle tb = tbs[i];
    std::vector<size_t> dsts = outNeighbors(tb);
    for (size_t j = 0; j < dsts.size(); ++j) {
    }
}

size_t getSegmentSize(TermBundleGraph tbg, size_t segment, size_t level = 0) {
    if (level) {
        return countScheduled(tbg.tempSchedule, segment, level - 1);
    } else {
        return tbg.tbs.size();
    }
}

void scheduleBundleLevel(Function fun, TermBundleGraph tbg, size_t position = 0,
                         size_t level = 0) {
    TermBundle tb = tbg.tbs[position];
}
void scheduleBundleLevel(Function fun, TermBundleGraph tbg,
                         std::vector<std::pair<Int, Int>> cycles,
                         size_t position = 0, size_t level = 0) {
    TermBundle tb = tbg.tbs[position];
}

// prefuse terms
void scheduleBundle(Function fun, std::vector<Int> wcc) {
    TermBundleGraph tbg = prefuse(fun, wcc);
    scheduleBundleLevel(fun, tbg, 0, 0);
    return;
}
void scheduleBundle(Function fun, std::vector<Int> wcc,
                    std::vector<std::pair<Int, Int>> cycles) {
    TermBundleGraph tbg = prefuse(fun, wcc);
    scheduleBundleLevel(fun, tbg, cycles, 0, 0);
    return;
}

void schedule(Function fun) {
    std::vector<std::vector<Int>> components = weaklyConnectedComponents(fun);
    TermBundleGraph tbg(fun);
    for (size_t j = 0; j < length(tb); ++j) {
        scheduleBundle(fun, tb[j]);
    }
    return;
}
