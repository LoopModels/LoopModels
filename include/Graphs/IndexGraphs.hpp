#pragma once

#include "Containers/BitSets.hpp"
#include <concepts>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <type_traits>

/// TODO: when we have better std::ranges support in compilers, use it?
namespace poly::graphs {
template <typename R>
concept AbstractRange = requires(R r) {
  { r.begin() };
  { r.end() };
};
inline auto printRange(llvm::raw_ostream &os, AbstractRange auto &r)
  -> llvm::raw_ostream & {
  os << "[ ";
  bool needComma = false;
  for (auto x : r) {
    if (needComma) os << ", ";
    os << x;
    needComma = true;
  }
  os << " ]";
  return os;
}

// A graph where neighbors are pointers to other vertices
template <typename G>
concept AbstractGraphCore = requires(G &g, const G &cg, size_t i) {
  { g.inNeighbors(i) } -> AbstractRange;
  { cg.inNeighbors(i) } -> AbstractRange;
  // { g.outNeighbors(i) } -> AbstractRange;
  // { cg.outNeighbors(i) } -> AbstractRange;
  { g.getNumVertices() } -> std::convertible_to<unsigned>;
  { g.wasVisited(i) } -> std::same_as<bool>;
  { g.visit(i) };
  { g.unVisit(i) };
};

// graphs as in LoopBlocks, where we use BitSets to subset portions
template <typename G>
concept AbstractIndexGraph =
  AbstractGraphCore<G> && requires(G g, const G cg, size_t i) {
    { g.vertexIds() } -> AbstractRange;
    // { *std::ranges::begin(g.vertexIds()) } ->
    // std::convertible_to<unsigned>;
    { *g.vertexIds().begin() } -> std::convertible_to<unsigned>;
    // { *std::ranges::begin(g.outNeighbors(i)) } ->
    // std::convertible_to<unsigned>;
    // { *g.outNeighbors(i).begin() } -> std::convertible_to<unsigned>;
    // { *std::ranges::begin(g.inNeighbors(i)) } ->
    // std::convertible_to<unsigned>;
    { *g.inNeighbors(i).begin() } -> std::convertible_to<unsigned>;
    { g.maxVertexId() } -> std::convertible_to<size_t>;
  };

template <typename G>
concept AbstractGraphClearVisited = AbstractGraphCore<G> && requires(G g) {
  { g.clearVisited() };
};

inline void clearVisited(AbstractIndexGraph auto &g) {
  for (auto &&v : g) v.unVisit();
}
inline void clearVisited(AbstractGraphCore auto &g) { g.clearVisited(); }

inline void weakVisit(AbstractIndexGraph auto &g,
                      llvm::SmallVectorImpl<unsigned> &sorted, unsigned v) {
  g.visit(v);
  for (auto j : g.inNeighbors(v))
    if (!g.wasVisited(j)) weakVisit(g, sorted, j);
  sorted.push_back(v);
}

inline auto topologicalSort(AbstractIndexGraph auto &g) {
  llvm::SmallVector<unsigned> sorted;
  sorted.reserve(g.getNumVertices());
  clearVisited(g);
  for (auto j : g.vertexIds()) {
    if (g.wasVisited(j)) continue;
    weakVisit(g, sorted, j);
  }
  return sorted;
}

struct SCC {
  [[no_unique_address]] unsigned index;
  [[no_unique_address]] unsigned lowLink;
  [[no_unique_address]] bool onStack;
  [[no_unique_address]] bool visited{false};
};

template <typename B>
inline auto strongConnect(AbstractIndexGraph auto &g,
                          llvm::SmallVectorImpl<B> &components,
                          llvm::SmallVector<unsigned> &stack,
                          llvm::MutableArrayRef<SCC> iLLOS, unsigned index,
                          size_t v) -> unsigned {
  iLLOS[v] = {index, index, true, true};
  ++index;
  stack.push_back(v);
  for (auto w : g.inNeighbors(v)) {
    if (iLLOS[w].visited) {
      if (iLLOS[w].onStack)
        iLLOS[v].lowLink = std::min(iLLOS[v].lowLink, iLLOS[w].index);
    } else { // not visited
      index = strongConnect<B>(g, components, stack, iLLOS, index, w);
      iLLOS[v].lowLink = std::min(iLLOS[v].lowLink, iLLOS[w].lowLink);
    }
  }
  if (iLLOS[v].index == iLLOS[v].lowLink) {
    B &component = components.emplace_back();
    unsigned w;
    do {
      w = stack.pop_back_val();
      iLLOS[w].onStack = false;
      component.insert(w);
    } while (w != v);
  }
  return index;
}

template <typename B>
inline void stronglyConnectedComponents(llvm::SmallVectorImpl<B> &cmpts,
                                        AbstractIndexGraph auto &g) {
  size_t maxId = g.maxVertexId();
  cmpts.reserve(maxId);
  // TODO: this vector may be sparse, so this is wasteful
  llvm::SmallVector<SCC> indexLowLinkOnStack{maxId};
#ifndef NDEBUG
  for (auto scc : indexLowLinkOnStack) assert(!scc.visited);
#endif
  llvm::SmallVector<unsigned> stack;
  unsigned index = 0;
  for (auto v : g.vertexIds())
    if (!indexLowLinkOnStack[v].visited)
      index = strongConnect(g, cmpts, stack, indexLowLinkOnStack, index, v);
}
inline auto stronglyConnectedComponents(AbstractIndexGraph auto &g)
  -> llvm::SmallVector<containers::BitSet<>> {
  llvm::SmallVector<containers::BitSet<>> components;
  stronglyConnectedComponents(components, g);
  return components;
}

inline auto print(const AbstractIndexGraph auto &g,
                  llvm::raw_ostream &os = llvm::errs()) -> llvm::raw_ostream & {
  for (auto i : g.vertexIds()) {
    os << "Vertex " << i << ":";
    printRange(os << "\ninNeighbors: ", g.inNeighbors(i));
    printRange(os << "\noutNeighbors: ", g.outNeighbors(i)) << "\n";
  }
  return os;
}

} // namespace poly::graphs

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
