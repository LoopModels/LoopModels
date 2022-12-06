#pragma once

#include "./BitSets.hpp"
#include "./Math.hpp"
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <tuple>
#include <type_traits>

/// TODO: when we have better std::ranges support in compilers, use it?
namespace Graphs {
template <typename G>
concept AbstractGraph =
    AbstractRange<G> && requires(G g, const G cg, size_t i) {
                            { g.vertexIds() } -> AbstractRange;
                            // { *std::ranges::begin(g.vertexIds()) } ->
                            // std::convertible_to<unsigned>;
                            {
                                *g.vertexIds().begin()
                                } -> std::convertible_to<unsigned>;
                            { g.outNeighbors(i) } -> AbstractRange;
                            { cg.outNeighbors(i) } -> AbstractRange;
                            // { *std::ranges::begin(g.outNeighbors(i)) } ->
                            // std::convertible_to<unsigned>;
                            {
                                *g.outNeighbors(i).begin()
                                } -> std::convertible_to<unsigned>;
                            { g.inNeighbors(i) } -> AbstractRange;
                            { cg.inNeighbors(i) } -> AbstractRange;
                            // { *std::ranges::begin(g.inNeighbors(i)) } ->
                            // std::convertible_to<unsigned>;
                            {
                                *g.inNeighbors(i).begin()
                                } -> std::convertible_to<unsigned>;
                            { g.wasVisited(i) } -> std::same_as<bool>;
                            { g.begin()->wasVisited() } -> std::same_as<bool>;
                            { g.begin()->visit() };
                            { g.begin()->unVisit() };
                            { g.visit(i) };
                            {
                                g.getNumVertices()
                                } -> std::convertible_to<unsigned>;
                            { g.maxVertexId() } -> std::convertible_to<size_t>;
                        };

[[maybe_unused]] static void clearVisited(AbstractGraph auto &g) {
    for (auto &&v : g)
        v.unVisit();
}

[[maybe_unused]] static void weakVisit(AbstractGraph auto &g,
                                       llvm::SmallVectorImpl<unsigned> &sorted,
                                       unsigned v) {
    g.visit(v);
    for (auto j : g.outNeighbors(v))
        if (!g.wasVisited(j))
            weakVisit(g, sorted, j);
    sorted.push_back(v);
}

[[maybe_unused]] static auto weaklyConnectedComponents(AbstractGraph auto &g) {
    llvm::SmallVector<llvm::SmallVector<unsigned>> components;
    g.clearVisited();
    for (auto j : g.vertexIds()) {
        if (g.wasVisited(j))
            continue;
        components.emplace_back();
        llvm::SmallVector<unsigned> &sorted = components.back();
        weakVisit(g, sorted, j);
        std::reverse(sorted.begin(), sorted.end());
    }
    return components;
}

[[maybe_unused]] static size_t
strongConnect(AbstractGraph auto &g, llvm::SmallVector<BitSet<>> &components,
              llvm::SmallVector<unsigned> &stack,
              llvm::MutableArrayRef<std::tuple<unsigned, unsigned, bool>>
                  indexLowLinkOnStack,
              size_t index, size_t v) {
    indexLowLinkOnStack[v] = std::make_tuple(index, index, true);
    g.visit(v);
    ++index;
    stack.push_back(v);
    for (auto w : g.inNeighbors(v)) {
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
        BitSet<> &component = components.back();
        unsigned w;
        do {
            w = stack.back();
            stack.pop_back();
            std::get<2>(indexLowLinkOnStack[w]) = false;
            component.insert(w);
        } while (w != v);
    }
    return index;
}

[[maybe_unused]] static llvm::SmallVector<BitSet<>>
stronglyConnectedComponents(AbstractGraph auto &g) {
    llvm::SmallVector<BitSet<>> components;
    size_t maxId = g.maxVertexId();
    components.reserve(maxId);
    llvm::SmallVector<std::tuple<unsigned, unsigned, bool>> indexLowLinkOnStack(
        maxId);
    llvm::SmallVector<unsigned> stack;
    size_t index = 0;
    clearVisited(g);
    for (auto v : g.vertexIds()) {
        if (!g.wasVisited(v))
            index = strongConnect(g, components, stack, indexLowLinkOnStack,
                                  index, v);
    }
    return components;
}

llvm::raw_ostream &print(const AbstractGraph auto &g,
                         llvm::raw_ostream &os = llvm::errs()) {
    for (auto i : g.vertexIds()) {
        os << "Vertex " << i << ":";
        printRange(os << "\ninNeighbors: ", g.inNeighbors(i));
        printRange(os << "\noutNeighbors: ", g.outNeighbors(i)) << "\n";
    }
    return os;
}

} // namespace Graphs

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
