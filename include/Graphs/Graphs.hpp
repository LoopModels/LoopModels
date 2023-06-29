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
concept AbstractPtrGraph = requires(G *g, typename G::VertexType *v) {
  {
    *(g.getVertices().begin())
  } -> std::template same_as<typename G::VertexType *>;
  { g.getVertices() } -> std::ranges::forward_range;
  {
    *(g.outNeighbors(v).begin())
  } -> std::template same_as<typename G::VertexType *>;
  { g.outNeighbors(v) } -> std::ranges::forward_range;
  { v->index() } -> std::assignable_from<unsigned>;
  { v->lowLink() } -> std::assignable_from<unsigned>;
  { v->onStack() } -> std::same_as<bool>;
  { v->addToStack() };
  { v->removeFromStack() };
  { v->visited() } -> std::same_as<bool>;
  { v->visit() };
  { v->unVisit() };
  { v->setNext(v) } -> std::template same_as<typename G::VertexType *>;
  { v->getNext() } -> std::template same_as<typename G::VertexType *>;
  { v->setNextComponent(v) } -> std::template same_as<typename G::VertexType *>;
  { v->getNextComponent() } -> std::template same_as<typename G::VertexType *>;
};

template <class N> struct State {
  N *components{nullptr};
  N *stack{nullptr};
  unsigned index{0};
};

// TODO: address code duplication by abstracting between AbstractIndexGraph and
// AbstractPtrGraph
template <typename N>
inline auto strongConnect(State<N> state, N *v) -> State<N> {
  v->index() = v->lowLink() = state.index++;
  v->addToStack();
  v->visit();

  state.stack = v->setNext(state.stack);
  for (auto *w : outNeighbors(v))
    if (!w->wasVisited()) {
      state = strongConnect(state, w);
      v->lowLink() = std::min(v->lowLink(), w->lowLink());
    } else if (w->onStack()) v->lowLink() = std::min(v->lowLink(), w->index());
  if (v->index() == v->lowLink()) {
    N *component{nullptr}, *s;
    do {
      s = std::exchange(state.stack, state.stack->getNext());
      s->removeFromStack();
      component = s->setNext(component);
    } while (s != v);
    state.components = component->setNextComponent(state.components);
  }
  return state;
}
/// Returns a list of lists; each SCC will be connected via `getNext()`
/// while the SCCs will be connected by `component` pointers.
/// These next component pointers are only stored in the list-heads.
/// This allows for immediately checking if there is only a single SCC
/// (by comparing with `nullptr`)
template <AbstractPtrGraph G>
inline auto stronglyConnectedComponents(G *g) -> typename G::VertexType * {
  using N = typename G::VertexType;
  State<N *> state{};
  for (auto *v : g->getVertices())
    if (!v->wasVisited()) state = strongConnect(state, v);
  return state.components;
}

template <class N> inline auto topVisit(N *list, N *v) -> N * {
  v->visit();
  for (auto *w : v->outNeighbors())
    if (!w->wasVisited()) list = topVisit(list, w);
  return v->setNext(list);
}

template <AbstractPtrGraph G>
inline auto topSort(const G &g) -> typename G::VertexType * {
  using N = typename G::VertexType;
  N *list{nullptr};
  for (auto *v : g->getVertices())
    if (!v->wasVisited()) list = topVisit(list, v);
  return list;
}

} // namespace poly::graph
