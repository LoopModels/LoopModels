#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>

#ifndef USE_MODULE
#include "Math/ManagedArray.cxx"
#include "Containers/BitSets.cxx"
#else
export module IndexGraph;
import BitSet;
import ManagedArray;
#endif

#ifdef USE_MODULE
export namespace graph {
#else
namespace graph {
#endif
template <typename R>
concept AbstractRange = requires(R r) {
  { r.begin() };
  { r.end() };
};
inline auto printRange(std::ostream &os,
                       AbstractRange auto &r) -> std::ostream & {
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
concept AbstractGraphCore = requires(G &g, const G &cg, ptrdiff_t i) {
  { g.inNeighbors(i) } -> AbstractRange;
  { cg.inNeighbors(i) } -> AbstractRange;
  { cg.getNumVertices() } -> std::convertible_to<unsigned>;
};

// graphs as in LoopBlocks, where we use BitSets to subset portions
template <typename G>
concept AbstractIndexGraph =
  AbstractGraphCore<G> && requires(G g, const G cg, ptrdiff_t i) {
    { g.vertexIds() } -> AbstractRange;
    { *g.vertexIds().begin() } -> std::convertible_to<unsigned>;
    { *g.inNeighbors(i).begin() } -> std::convertible_to<unsigned>;
    { g.maxVertexId() } -> std::convertible_to<size_t>;
  };

inline void weakVisit(AbstractIndexGraph auto &g,
                      math::Vector<unsigned> &sorted,
                      containers::BitSet<> &visited, unsigned v) {
  visited.insert(v);
  for (auto j : g.inNeighbors(v))
    if (!visited[j]) weakVisit(g, sorted, visited, j);
  sorted.push_back(v);
}

inline auto topologicalSort(AbstractIndexGraph auto &g) {
  math::Vector<unsigned> sorted;
  sorted.reserve(g.getNumVertices());
  containers::BitSet visited{};
  for (auto j : g.vertexIds()) {
    if (visited[j]) continue;
    weakVisit(g, sorted, visited, j);
  }
  return sorted;
}

struct SCC {
  uint32_t index_ : 31;
  uint32_t on_stack_ : 1;
  uint32_t low_link_ : 31;
  uint32_t visited_ : 1;
};

template <typename C>
inline auto strongConnect(AbstractIndexGraph auto &g, C &components,
                          math::Vector<unsigned> &stack,
                          math::MutPtrVector<SCC> iLLOS, unsigned index,
                          size_t v) -> unsigned {
  iLLOS[v] = {index, true, index, true};
  ++index;
  stack.push_back(v);
  for (auto w : g.inNeighbors(v)) {
    if (iLLOS[w].visited_) {
      if (iLLOS[w].on_stack_)
        iLLOS[v].low_link_ = std::min(iLLOS[v].low_link_, iLLOS[w].index_);
    } else { // not visited
      index = strongConnect<C>(g, components, stack, iLLOS, index, w);
      iLLOS[v].low_link_ = std::min(iLLOS[v].low_link_, iLLOS[w].low_link_);
    }
  }
  if (iLLOS[v].index_ == iLLOS[v].low_link_) {
    utils::eltype_t<C> &component = components.emplace_back();
    unsigned w;
    do {
      w = stack.pop_back_val();
      iLLOS[w].on_stack_ = false;
      component.insert(w);
    } while (w != v);
  }
  return index;
}

inline void stronglyConnectedComponents(auto &cmpts,
                                        AbstractIndexGraph auto &g) {
  ptrdiff_t nv = g.getNumVertices();
  cmpts.reserve(nv);
  // TODO: this vector may be sparse, so this is wasteful
  math::Vector<SCC> index_low_link_on_stack{math::length(nv), {0, 0, 0, 0}};
  math::Vector<unsigned> stack;
  unsigned index = 0;
  for (auto v : g.vertexIds())
    if (!index_low_link_on_stack[v].visited_)
      index = strongConnect(g, cmpts, stack, index_low_link_on_stack, index, v);
}
inline auto stronglyConnectedComponents(AbstractIndexGraph auto &g)
  -> math::Vector<containers::BitSet<>> {
  math::Vector<containers::BitSet<>> components;
  stronglyConnectedComponents(components, g);
  return components;
}

inline auto print(const AbstractIndexGraph auto &g,
                  std::ostream &os = std::cout) -> std::ostream & {
  for (auto i : g.vertexIds()) {
    os << "Vertex " << i << ":";
    printRange(os << "\ninNeighbors: ", g.inNeighbors(i));
    printRange(os << "\noutNeighbors: ", g.outNeighbors(i)) << "\n";
  }
  return os;
}

} // namespace graph

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