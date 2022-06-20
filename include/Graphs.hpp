#pragma once

#include "./Math.hpp"
#include <tuple>

template <typename G>
void visit(llvm::SmallVector<int64_t> sorted, G &graph, size_t idx) {
    auto outs = outNeighbors(graph, idx);
    visited(graph, idx) = true;
    for (size_t j = 0; j < length(outs); ++j) {
        if (!visited(graph, j)) {
            visit(sorted, graph, outs(j));
        }
    }
    sorted.push_back(idx);
}

template <typename G> llvm::SmallVector<int64_t> topologicalSort(G &graph) {
    llvm::SmallVector<int64_t> sorted;
    clearVisited(graph);
    for (size_t j = 0; j < nv(graph); j++) {
        if (!visited(graph, j))
            visit(sorted, graph, j);
    }
    std::reverse(sorted.begin(), sorted.end());
    return sorted;
}

template <typename G>
llvm::SmallVector<llvm::SmallVector<int64_t>>
weaklyConnectedComponents(G &graph) {
    llvm::SmallVector<llvm::SmallVector<int64_t>> components;
    clearVisited(graph);
    for (size_t j = 0; j < nv(graph); ++j) {
        if (visited(graph, j))
            continue;
        llvm::SmallVector<int64_t> sorted;
        visit(sorted, graph, j);
        std::reverse(sorted.begin(), sorted.end());
        components.emplace_back(sorted);
    }
    return components;
}

// ref:
// https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm#The_algorithm_in_pseudocode
template <typename G>
void strongConnect(
    llvm::SmallVector<llvm::SmallVector<int64_t>> &components,
    llvm::SmallVector<size_t> &stack,
    llvm::SmallVector<std::tuple<size_t, size_t, bool>> &indexLowLinkOnStack,
    size_t &index, G &graph, size_t v) {
    indexLowLinkOnStack[v] = std::make_tuple(index, index, true);
    index += 1;
    stack.push_back(v);

    auto outN = outNeighbors(graph, v);
    for (size_t w = 0; w < length(outN); ++w) {
        if (visited(graph, w)) {
            auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
            if (wOnStack) {
                auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
                indexLowLinkOnStack[v] = std::make_tuple(
                    vIndex, std::min(vLowLink, wIndex), vOnStack);
            }
        } else { // not visited
            strongConnect(components, stack, indexLowLinkOnStack, index, graph,
                          w);
            auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
            indexLowLinkOnStack[v] = std::make_tuple(
                vIndex, std::min(vLowLink, std::get<1>(indexLowLinkOnStack[w])),
                vOnStack);
        }
    }
    auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
    if (vIndex == vLowLink) {
        size_t w;
        llvm::SmallVector<int64_t> component;
        do {
            w = stack[stack.size() - 1];
            stack.pop_back();
            auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
            indexLowLinkOnStack[w] = std::make_tuple(wIndex, wLowLink, false);
            component.push_back(w);
        } while (w != v);
        components.emplace_back(component);
    }
}

template <typename G>
llvm::SmallVector<llvm::SmallVector<int64_t>>
stronglyConnectedComponents(G &graph) {
    llvm::SmallVector<llvm::SmallVector<int64_t>> components;
    size_t nVertex = nv(graph);
    llvm::SmallVector<std::tuple<size_t, size_t, bool>> indexLowLinkOnStack(
        nVertex);
    llvm::SmallVector<size_t> stack;
    size_t index = 0;
    clearVisited(graph);
    for (size_t v = 0; v < nVertex; ++v) {
        if (!visited(graph, v))
            strongConnect(components, stack, indexLowLinkOnStack, index, graph,
                          v);
    }
    return components;
}

// Naive algorithm that looks like it may work to identify cycles:
// 0 -> 1 -> 3 -> 5
//  \            /
//   -> 2 -> 4 ->
// As we do dfs,
// first, we iterate down 0 -> 1, and build
// [0, 1, 3, 5] // all unique -> no cycle
// then, we iterate down 0 -> 2
// [0, 2, 4, 5] // all unique -> no cycle
// vs:
// 0 -> 1 -> 3 -> 0
// [0, 1, 3, 0] // not unique -> cycle
//
// However, it does not because dfs does not explore all possible paths, meaning
//   it is likely to miss the cyclic paths, e.g.:
// 0 -> 1 -> 3 -> 5
//  \    \<-/    /
//   -> 2 -> 4 ->
// [0, 1, 3, 5] // no cycle
// [0, 2, 4, 5] // no cycle
//
// Thus a better approach is to group a TermBundle by strongly connected
// components.
// We shall take the approach of:
//
// 1. Split graph into weakly connected components. For each wcc:
// 2. Prefuse these weakly connected components.
// 3. Group these into strongly connected components.
// 4. Iterate over schedules by strongly connected components.
