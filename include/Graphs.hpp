#pragma once

#include "./Math.hpp"
#include <llvm/ADT/SmallVector.h>
#include <tuple>
#include <type_traits>

namespace Graph {
template <typename G>
concept Graph = requires(G g, size_t i) {
    // { g.outNeighbors(i) } -> AbstractVector;
    { g.getVertices() } -> AbstractVector;
    {
        g.getVertices()(i)
        } -> std::same_as<typename std::remove_reference_t<G>::VertexType>;
    // { g.outNeighbors(i)(i) } -> std::convertible_to<unsigned>;
    { g.wasVisited(i) } -> std::same_as<bool>;
    { g.getVertices()(i).wasVisited() } -> std::same_as<bool>;
    {g.getVertices()(i).visit()};
    {g.getVertices()(i).unVisit()};
    { g.numVertices() } -> std::convertible_to<size_t>;
    {g.visit(i)};
    { g.numVertices } -> std::convertible_to<unsigned>;
};

[[maybe_unused]] static void clearVisited(Graph auto &g) {
    for (auto &&v : g.getVertices())
        v.unVisit();
}

[[maybe_unused]] static void
visit(Graph auto &g, llvm::SmallVectorImpl<unsigned> &sorted, unsigned v) {
    g.visit(v);
    for (auto j : g.outNeighbors(v))
        if (!g.wasVisited(j))
            visit(g, sorted, j);
    sorted.push_back(v);
}

template <Graph G>
[[maybe_unused]] static auto weaklyConnectedComponents(G &g) {
    llvm::SmallVector<llvm::SmallVector<unsigned>> components;
    g.clearVisited();
    for (unsigned j = 0; j < g.numVerticies(); ++j) {
        if (g.wasVisited(j))
            continue;
        components.emplace_back();
        llvm::SmallVector<unsigned> &sorted = components.back();
        visit(g, sorted, j);
        std::reverse(sorted.begin(), sorted.end());
    }
    return components;
}

[[maybe_unused]] static size_t
strongConnect(Graph auto &g,
              llvm::SmallVector<llvm::SmallVector<unsigned>> &components,
              llvm::SmallVector<unsigned> &stack,
              llvm::MutableArrayRef<std::tuple<unsigned, unsigned, bool>>
                  indexLowLinkOnStack,
              size_t index, size_t v) {
    indexLowLinkOnStack[v] = std::make_tuple(index, index, true);
    ++index;
    stack.push_back(v);
    for (auto w : g.outNeighbors(v)) {
        if (g.wasVisited(w)) {
            auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
            if (wOnStack) {
		unsigned &vll = std::get<1>(indexLowLinkOnStack[v]);
		vll = std::min(vll, wIndex);
            }
        } else { // not visited
            strongConnect(g, components, stack, indexLowLinkOnStack, index, w);
            unsigned &vll = std::get<1>(indexLowLinkOnStack[v]);
	    vll = std::min(vll, std::get<1>(indexLowLinkOnStack[w]));
        }
    }
    auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
    if (vIndex == vLowLink) {
        components.emplace_back();
        llvm::SmallVector<unsigned> &component = components.back();
        unsigned w;
        do {
            w = stack.back();
            stack.pop_back();
	    std::get<2>(indexLowLinkOnStack[w]) = false;
            component.push_back(w);
        } while (w != v);
    }
    return index;
}

[[maybe_unused]] static llvm::SmallVector<llvm::SmallVector<unsigned>>
stronglyConnectedComponents(Graph auto &g) {
    llvm::SmallVector<llvm::SmallVector<unsigned>> components;
    size_t nVertex = g.numVerticies();
    components.reserve(nVertex);
    llvm::SmallVector<std::tuple<unsigned, unsigned, bool>> indexLowLinkOnStack(
        nVertex);
    llvm::SmallVector<unsigned> stack;
    size_t index = 0;
    g.clearVisited();
    for (size_t v = 0; v < nVertex; ++v) {
        if (!g.wasVisited(v))
            index = strongConnect(g, components, stack, indexLowLinkOnStack,
                                  index, v);
    }
    return components;
}
} // namespace Graph

// template <typename G>
// concept Graph = requires(G g) {
//     {
//         g.getVertices()
//         } -> std::same_as<typename std::remove_reference<G>::nodetype>;
// };

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
