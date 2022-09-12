#pragma once

#include "./Math.hpp"
#include <llvm/ADT/SmallVector.h>
#include <tuple>

//
template <typename G> struct BaseGraph {
    llvm::SmallVector<bool, 256> visited;

    // API
    auto &outNeighbors(size_t idx) {
        return static_cast<G *>(this)->outNeighbors(idx);
    }
    auto &outNeighbors(size_t idx) const {
        return static_cast<const G *>(this)->outNeighbors(idx);
    }
    size_t nv() const { return static_cast<const G *>(this)->nv(); }

    void clearVisited() {
        for (auto &&b : visited)
            b = false;
    }

    void visit(llvm::SmallVectorImpl<unsigned> &sorted, size_t idx) {
        auto &outs = outNeighbors(idx);
        visited[idx] = true;
        for (size_t j = 0; j < outs.size(); ++j)
            if (!visited[j])
                visit(sorted, outs[j]);
        sorted.push_back(idx);
    }

    llvm::SmallVector<llvm::SmallVector<unsigned>> weaklyConnectedComponents() {
        llvm::SmallVector<llvm::SmallVector<unsigned>> components(nv());
        clearVisited();
        for (size_t j = 0; j < nv(); ++j) {
            if (visited[j])
                continue;
            llvm::SmallVector<unsigned> &sorted = components[j];
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
        auto outN = outNeighbors(v);
        for (size_t w = 0; w < outN.size(); ++w) {
            if (visited[w]) {
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
            unsigned w;
            llvm::SmallVector<unsigned> component;
            do {
                w = stack.back();
                stack.pop_back();
                auto [wIndex, wLowLink, wOnStack] = indexLowLinkOnStack[w];
                indexLowLinkOnStack[w] =
                    std::make_tuple(wIndex, wLowLink, false);
                component.push_back(w);
            } while (w != v);
            components.emplace_back(component);
        }
        return index;
    }

    llvm::SmallVector<llvm::SmallVector<unsigned>>
    stronglyConnectedComponents() {
        llvm::SmallVector<llvm::SmallVector<unsigned>> components;
        size_t nVertex = nv();
        llvm::SmallVector<std::tuple<unsigned, unsigned, bool>>
            indexLowLinkOnStack(nVertex);
        llvm::SmallVector<unsigned> stack;
        size_t index = 0;
        clearVisited();
        for (size_t v = 0; v < nVertex; ++v) {
            if (!visited[v])
                index = strongConnect(components, stack, indexLowLinkOnStack,
                                      index, v);
        }
        return components;
    }
};

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
