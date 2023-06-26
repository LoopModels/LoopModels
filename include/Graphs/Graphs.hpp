#pragma once
#include <Math/Array.hpp>
#include <Utilities/Allocators.hpp>
namespace poly::graph {

// Currently, only implements top sort, and Tarjan's strongly connected
// components which returns the components in topological order, because we
// iterate over successors and push-first to components. These graphs assume IR
// nodes have some means of representing cycles e.g., a linked list class List{
//   List *next;
//   List *prev;
//   List *nextComponent
//   List *prevComponent
//   public;
//   // API methods
// };
// we could represent components
// A -> [B, C] -> [D, E, F] -> G -> [H, I]
// via this list, Let
// W -> (X, Y) mean W->next == X && W->nextComponent = Y
// and `_` means `nullptr`.
// `prev`s could be found by reversing the `next`s.
//
// A -> (B, _)
// B -> (D, C)
// C -> (D, B)
// D -> (G, E)
// E -> (G, F)
// F -> (G, D)
// G -> (H, _)
// H -> (_, I)
// I -> (_, H)
//
//
template <typename G>
concept AbstractPtrGraph = requires(G g, const G cg, size_t i) {
  { *g.outNeighbors(i).begin() } -> std::same_as<typename G::VertexType *>;
  { *g.inNeighbors(i).begin() } -> std::same_as<typename G::VertexType *>;
  { g.inNeighbors(i).begin()->index() } -> std::assignable_from<unsigned>;
  { g.inNeighbors(i).begin()->lowLink() } -> std::assignable_from<unsigned>;
  { g.inNeighbors(i).begin()->onStack() } -> std::same_as<bool>;
  { g.inNeighbors(i).begin()->addToStack() };
  { g.inNeighbors(i).begin()->removeFromStack() };
};

template <class N> struct State {
  N *components{nullptr};
  N *stack{nullptr};
  unsigned index{0};
};

// TODO: address code duplication by abstracting between AbstractIndexGraph and
// AbstractPtrGraph
template <typename N>
inline auto strongConnect(const AbstractPtrGraph auto &g, State<N> state, N *v)
  -> std::tuple<N *, N *, unsigned> {
  v->index() = v->lowLink() = state.index++;
  v->setOnStack();
  v->visit();
  state.stack = v->setNext(state.stack);
  for (auto *w : g.outNeighbors(v))
    if (!w->wasVisited()) {
      state = strongConnect(g, state, w);
      v->lowLink() = std::min(v->lowLink(), w->lowLink());
    } else if (w->onStack()) v->lowLink() = std::min(v->lowLink(), w->index());
  if (v->index() == v->lowLink()) {
    N *component{nullptr}, *s;
    do {
      s = std::exchange(state.stack, state.stack->getNext());
      s->removeFromStack();
      component = s->setNext(component);
    } while (s != v);
    state.components = v->setNext(component);
  }
  return state;
}

template <AbstractPtrGraph G>
inline auto stronglyConnectedComponents(const G &g) -> typename G::node_type * {
  using N = typename G::node_type;
  State<N *> state{};
  for (auto *v : g.vertexIds())
    if (!v->wasVisited()) state = strongConnect(g, state, v);
  return state.components;
}
template <AbstractPtrGraph G, class N>
inline auto topVisit(const G &g, N *list, N *v) -> N * {
  v->visit();
  for (auto *w : g.outNeighbors(v))
    if (!w->wasVisited()) list = topVisit(g, list, w);
  return v->setNext(list);
}

template <AbstractPtrGraph G>
inline auto topSort(const G &g) -> typename G::node_type * {
  using N = typename G::node_type;
  N *list{nullptr};
  for (auto *v : g.vertexIds())
    if (!v->wasVisited()) list = topVisit(g, list, v);
  return list;
}

} // namespace poly::graph
