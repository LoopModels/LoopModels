#pragma once

#include "./Math.hpp"
#include <llvm/ADT/SmallVector.h>
#include <tuple>
#include <type_traits>

// graph uses vertex type V rather than indices
// because our Dependence contains pointers rather than indices
// and we need to go MemoryAccess* -> Edge (Dependnece) -> MemoryAccess*
template <typename G, typename V> struct BaseGraph {
    // API
    auto &outNeighbors(size_t idx) {
        return static_cast<G *>(this)->outNeighbors(idx);
    }
    auto &outNeighbors(size_t idx) const {
        return static_cast<const G *>(this)->outNeighbors(idx);
    }
    auto &getVertices() { return static_cast<G *>(this)->getVertices(); }
    auto &getVertices() const {
        return static_cast<const G *>(this)->getVertices();
    }
    size_t numVerticies() const {
        return static_cast<const G *>(this)->getVertices().size();
    }
    bool isVisited(size_t j) const {
        return static_cast<const G *>(this)->getVertices()[j].isVisited();
    }
    // V &getNode(size_t idx) { return static_cast<G *>(this)->getNode(idx); }
    // V &getNode(size_t idx) const {
    //     return static_cast<const G *>(this)->getNode(idx);
    // }
    void clearVisited() {
        for (auto &&v : getVertices())
            v.unVisit();
    }

    void visit(llvm::SmallVectorImpl<V *> &sorted, V *v) {
        auto &outs = outNeighbors(v);
        v->visit();
        for (size_t j = 0; j < outs.size(); ++j)
            if (!isVisited(j))
                visit(sorted, outs[j]); // no, we really need idx
        sorted.push_back(v);
    }

    llvm::SmallVector<llvm::SmallVector<V *>> weaklyConnectedComponents() {
        llvm::SmallVector<llvm::SmallVector<V *>> components;
        clearVisited();
        for (size_t j = 0; j < numVerticies(); ++j) {
            if (isVisited(j))
                continue;
            components.emplace_back();
            llvm::SmallVector<V *> &sorted = components.back();
            visit(sorted, j);
            std::reverse(sorted.begin(), sorted.end());
        }
        return components;
    }

    size_t
    strongConnect(llvm::SmallVector<llvm::SmallVector<unsigned>> &components,
                  llvm::SmallVector<unsigned> &stack,
                  llvm::SmallVector<std::tuple<unsigned, unsigned, bool>>
                      &indexLowLinkOnStack,
                  size_t index, size_t v) {
        indexLowLinkOnStack[v] = std::make_tuple(index, index, true);
        ++index;
        stack.push_back(v);
        auto &outN = outNeighbors(v);
        for (size_t w = 0; w < outN.size(); ++w) {
            if (isVisited(w)) {
                auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
                if (wOnStack) {
                    auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
                    indexLowLinkOnStack[v] = std::make_tuple(
                        vIndex, std::min(vLowLink, wIndex), vOnStack);
                }
            } else { // not visited
                strongConnect(components, stack, indexLowLinkOnStack, index, w);
                auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
                indexLowLinkOnStack[v] = std::make_tuple(
                    vIndex,
                    std::min(vLowLink, std::get<1>(indexLowLinkOnStack[w])),
                    vOnStack);
            }
        }
        auto [vIndex, vLowLink, vOnStack] = indexLowLinkOnStack[v];
        if (vIndex == vLowLink) {
            components.emplace_back(llvm::SmallVector<unsigned>());
            llvm::SmallVector<unsigned> &component = components.back();
            unsigned w;
            do {
                w = stack.back();
                stack.pop_back();
                auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
                indexLowLinkOnStack[w] =
                    std::make_tuple(wIndex, wLowLink, false);
                component.push_back(w);
            } while (w != v);
        }
        return index;
    }

    llvm::SmallVector<llvm::SmallVector<unsigned>>
    stronglyConnectedComponents() {
        llvm::SmallVector<llvm::SmallVector<unsigned>> components;
        size_t nVertex = numVerticies();
        llvm::SmallVector<std::tuple<unsigned, unsigned, bool>>
            indexLowLinkOnStack(nVertex);
        llvm::SmallVector<unsigned> stack;
        size_t index = 0;
        clearVisited();
        for (size_t v = 0; v < nVertex; ++v) {
            if (!isVisited(v))
                index = strongConnect(components, stack, indexLowLinkOnStack,
                                      index, v);
        }
        return components;
    }
};

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
