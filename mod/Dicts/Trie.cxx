#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <algorithm>
#include <array>
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <cstddef>
#include <cstdint>

#ifndef USE_MODULE
#include "Utilities/Valid.cxx"
#include "Utilities/Parameters.cxx"
#include "Containers/Pair.cxx"
#include "Utilities/Optional.cxx"
#include "Utilities/Invariant.cxx"
#include "Alloc/Arena.cxx"
#else
export module Trie;

import Arena;
import Invariant;
import Optional;
import Pair;
import Param;
import Valid;
#endif

#ifdef USE_MODULE
export namespace dict {
#else
namespace dict {
#endif
using utils::invariant, utils::inparam_t, containers::Pair;

template <class T> constexpr auto fastHash(const T &x) -> uint64_t {
  return boost::hash<T>{}(x);
}
template <class T> constexpr auto fastHash(T *x) -> uint64_t {
  return reinterpret_cast<uintptr_t>(x) >>
         std::countr_zero(alignof(std::max_align_t));
}

// Idea from from https://nullprogram.com/blog/2023/09/30/
template <class K, class V> struct TrieMapNode {
  K first;
  V second{};
  std::array<TrieMapNode<K, V> *, 4> children{};

  constexpr auto find(inparam_t<K> k) -> TrieMapNode * {
    return findChild(k).child;
  }

protected:
  struct Child {
    TrieMapNode *child;
    TrieMapNode *parent;
    uint64_t index; // child == parent->children[index];
  };
  constexpr auto isLeaf() -> bool {
    return first && !std::ranges::any_of(children);
  }
  constexpr auto getLeaf() -> Child {
    if (!first) return {nullptr, nullptr, 0};
    for (size_t i = 0; i < std::size(children); ++i)
      if (TrieMapNode *child = children[i])
        if (Child leaf = child->getLeaf(); leaf.child)
          return leaf.parent ? leaf : Child{leaf.child, this, i};
    return {this, nullptr, 0};
  }
  constexpr auto getSubLeaf() -> Child {
    Child c = getLeaf();
    return c.child != this ? c : Child{nullptr, nullptr, 0};
  }
  auto findChild(inparam_t<K> k) -> Child {
    if (k == first) return {this, nullptr, 0};
    TrieMapNode *p = this, *c = nullptr;
    for (uint64_t h = fastHash(k);; h >>= 2) {
      c = p->children[h & 3];
      if (!c || (c->first == k)) return {c, p, h & 3};
      p = c;
    }
  }
  // Returns the removed node
  auto eraseImpl(inparam_t<K> k) -> TrieMapNode * {
    Child child = findChild(k);
    if (!child.child) return nullptr;
    // we're erasing `child`
    Child l = child.child->getSubLeaf();
    if (l.child) {
      l.parent->children[l.index] = nullptr; // leaf is moved up
      std::swap(l.child->children, child.child->children);
    }
    child.parent->children[child.index] = l.child; // leaf replaces deleted
    child.child->second = {};
    return child.child;
  }
};

// If `EfficientErase = true`, it stores a list of erased nodes.
// Future allocations will allocate from this list if possible.
// Thus, whenever using a pattern that involves interleaving erase and
// insertions, it is worth setting `EfficientErase = true`. It is common enough
// not to do this, that the option for `false` also exists. Don't pay for what
// you don't use.
template <bool EfficientErase, class K, class V>
struct TrieMap : TrieMapNode<K, V> {
  using NodeT = TrieMapNode<K, V>;
  NodeT *list{nullptr};
  // TODO: implement using `list` to avoid allocs
  void erase(inparam_t<K> k) {
    if (NodeT *erased = this->eraseImpl(k))
      erased->children[0] = std::exchange(list, erased);
  }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, inparam_t<K> k) -> V & {
    typename NodeT::Child c = this->findChild(k);
    if (c.child) return c.child->second;
    invariant(c.parent != nullptr);
    invariant(c.index < 4);
    NodeT *&res = c.parent->children[c.index];
    invariant(res == nullptr);
    if (list) {
      res = list;
      list = std::exchange(list->children[0], nullptr);
      res->second = {};
    } else {
      res = alloc->create<NodeT>();
      invariant(res->second == V{});
    }
    res->first = k;
    return res->second;
  }
};

template <class K, class V> struct TrieMap<false, K, V> : TrieMapNode<K, V> {
  using NodeT = TrieMapNode<K, V>;
  void erase(inparam_t<K> k) { this->eraseImpl(k); }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, inparam_t<K> k) -> V & {
    typename NodeT::Child c = this->findChild(k);
    if (c.child) return c.child->second;
    invariant(c.parent != nullptr);
    invariant(c.index < 4);
    invariant(c.parent->children[c.index] == nullptr);
    NodeT *res = c.parent->children[c.index] = alloc->create<NodeT>();
    res->first = k;
    return res->second;
  }
};

static_assert(sizeof(TrieMap<false, int, int>) ==
              sizeof(TrieMapNode<int, int>));
static_assert(sizeof(TrieMap<true, int, int>) ==
              sizeof(TrieMapNode<int, int>) + sizeof(TrieMapNode<int, int> *));

template <typename InlineTrie> struct Child {
  InlineTrie *node;
  size_t index;
  utils::Optional<size_t> subIndex;
};
template <bool Insert, typename InlineTrie>
constexpr auto findChild(InlineTrie *node,
                         inparam_t<typename InlineTrie::KeyTyp> &k)
  -> std::conditional_t<Insert, Child<InlineTrie>,
                        Pair<InlineTrie *, uint64_t>> {
  for (uint64_t h = fastHash(k);;) {
    uint64_t ind = h & (InlineTrie::Nodes - 1);
    bool noKey = !node->keys[ind];
    if constexpr (Insert) {
      if (noKey) node->keys[ind] = k;
      if (noKey || (*node->keys[ind] == k)) return Child{node, ind, {}};
    } else {
      if (noKey) return Pair<InlineTrie *, uint64_t>{nullptr, ind};
      if (*node->keys[ind] == k) return Pair<InlineTrie *, uint64_t>{node, ind};
    }
    h >>= InlineTrie::Log2Nodes;
    if (!node->children[ind]) {
      if constexpr (Insert)
        return Child{node, h & (InlineTrie::Nodes - 1), ind};
      else return Pair<InlineTrie *, uint64_t>{nullptr, ind};
    }
    node = node->children[ind];
  }
}
// template <typename InlineTrie>
// static constexpr auto findChildConst(const InlineTrie *node, inparam_t<K> k)
// {
//   for (uint64_t h = fastHash(k);;) {
//     uint64_t ind = h & (InlineTrie::Nodes - 1);
//     bool noKey = !node->keys[ind];
//     if (noKey) return Pair<const InlineTrie *, uint64_t>{nullptr, ind};
//     if (*node->keys[ind] == k)
//       return Pair<const InlineTrie *, uint64_t>{node, ind};
//     h >>= InlineTrie::Log2Nodes;
//     if (!node->children[ind])
//       return Pair<const InlineTrie *, uint64_t>{nullptr, ind};
//     node = node->children[ind];
//   }
// }
// Optional can be specialized for types to add dead-values without requiring
// extra space. E.g., `sizeof(utils::Optional<T*>) == sizeof(T*)`, as `nullptr`
// indicates empty.
// Note: default initializes all fields, so one can assume they are constructed.
template <class K, class V = void, int L2N = 3> struct InlineTrie {
  static constexpr auto Log2Nodes = L2N;
  static constexpr auto Nodes = 1 << Log2Nodes;
  using KeyTyp = K;
  InlineTrie<K, V, Log2Nodes> *children[Nodes]{};
  utils::Optional<K> keys[Nodes]{};
  V values[Nodes]{};

  // Returns an optional pointer to the value.
  constexpr auto find(inparam_t<K> k) -> utils::Optional<V &> {
    auto [node, index] = findChild<false>(this, k);
    return node ? utils::Optional<V &>{node->values[index]} : std::nullopt;
  }
  constexpr auto find(inparam_t<K> k) const -> utils::Optional<const V &> {
    auto [node, index] = findChild<false>(this, k);
    return node ? utils::Optional<const V &>{node->values[index]}
                : std::nullopt;
  }
  constexpr auto contains(inparam_t<K> k) const -> bool {
    auto [node, index] = findChild<false>(this, k);
    return node;
  }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, inparam_t<K> k) -> V & {
    Child<InlineTrie> c = findChild<true>(this, k);
    if (c.subIndex) {
      c.node = c.node->children[*c.subIndex] =
        alloc->create<InlineTrie<K, V, Log2Nodes>>();
      c.node->keys[c.index] = k;
    }
    return c.node->values[c.index];
  }
  /// returns a `Pair<V*,bool>`
  /// the `bool` is `true` if a value was inserted, `false` otherwise
  auto insert(utils::Valid<alloc::Arena<>> alloc,
              K k) -> containers::Pair<V *, bool> {
    Child<InlineTrie> c = findChild<true>(this, k);
    bool mustInsert = c.subIndex.hasValue();
    if (mustInsert) {
      c.node = c.node->children[*c.subIndex] =
        alloc->create<InlineTrie<K, V, Log2Nodes>>();
      c.node->keys[c.index] = k;
    }
    return {&(c.node->values[c.index]), mustInsert};
  }
  // calls `f(key, value) for each key, value`
  void foreachkv(const auto &f) {
    for (int i = 0; i < Nodes; ++i)
      if (utils::Optional<K> o = keys[i]) f(*o, values[i]);
    for (int i = 0; i < Nodes; ++i)
      if (children[i]) children[i]->foreachkv(f);
  }
  void merge(utils::Valid<alloc::Arena<>> alloc, InlineTrie *other) {
    other->foreachkv([=, this](K k, V v) { (*this)[alloc, k] = v; });
  }
  // NOTE: this leaks!!
  void clear() {
    for (int i = 0; i < Nodes; ++i) {
      children[i] = nullptr;
      keys[i] = {};
      values[i] = {};
    }
  }

  void erase(inparam_t<K> k) {
    auto [child, index] = findChild<false>(this, k);
    if (!child) return; // was not found
    // We now find a leaf key/value pair, and move them here.
    if (InlineTrie *descendent = child->children[index]) {
      auto [lc, li] = descendent->findLeaf();
      if (lc) {
        child->keys[index] = std::move(lc->keys[li]);
        child->values[index] = std::move(lc->values[li]);
        child = lc;
        index = li;
      }
    }
    child->keys[index] = {}; // set to null
    child->values[index] = {};
  }

private:
  auto isLeaf(int i) -> bool {
    if (!keys[i]) return false;
    if (!children[i]) return true;
    for (int j = 0; j < Nodes; ++j)
      if (!children[i]->isLeaf(j)) return false;
    return true;
  }
  // A leaf is a key without any child keys.
  // A leaf may have children without keys.
  auto findLeaf() -> Pair<InlineTrie *, ptrdiff_t> {
    InlineTrie *leaf = this;
    bool descend[Nodes]{};
    for (int j = 0; j < Nodes; ++j) descend[j] = false;
    for (ptrdiff_t i = 0; i < std::ssize(children); ++i) {
      if (!leaf->keys[i]) continue;             // need key to be leaf
      if (!leaf->children[i]) return {leaf, i}; // no children, no child keys
      descend[i] = true;
    }
    for (ptrdiff_t i = 0; i < std::ssize(children); ++i) {
      if (!descend[i]) continue;
      auto ret = leaf->children[i]->findLeaf();
      return ret.first ? ret : Pair<InlineTrie *, ptrdiff_t>{this, i};
    };
    return {nullptr, 0};
  }
};

template <class K, int L2N> struct InlineTrie<K, void, L2N> {
  static constexpr auto Log2Nodes = L2N;
  static constexpr auto Nodes = 1 << Log2Nodes;
  using KeyTyp = K;
  InlineTrie<K, void, Log2Nodes> *children[Nodes]{};
  utils::Optional<K> keys[Nodes]{};

  // Returns `true` if an insertion actually took place
  // `false` if the key was already present
  constexpr auto insert(utils::Valid<alloc::Arena<>> alloc,
                        inparam_t<K> k) -> bool {
    Child<InlineTrie> c = findChild<true>(this, k);
    if (!c.subIndex) return false;
    c.node = c.node->children[*c.subIndex] =
      alloc->create<InlineTrie<K, void, Log2Nodes>>();
    c.node->keys[c.index] = k;
    return true;
  }

  auto operator[](inparam_t<K> k) const -> bool {
    auto [node, index] = findChild<false>(this, k);
    return node;
  }
  auto contains(inparam_t<K> k) const -> bool { return (*this)[k]; }

  void erase(inparam_t<K> k) {
    auto [child, index] = findChild<false>(this, k);
    if (!child) return; // was not found
    // We now find a leaf key, and move them here.
    if (InlineTrie *descendent = child->children[index]) {
      auto [lc, li] = descendent->findLeaf();
      if (lc) {
        child->keys[index] = std::move(lc->keys[li]);
        child = lc;
        index = li;
      }
    }
    child->keys[index] = {}; // set to null
  }
  // calls `f(key, value) for each key, value`
  void foreachk(const auto &f) {
    for (int i = 0; i < Nodes; ++i)
      if (utils::Optional<K> o = keys[i]) f(*o);
    for (int i = 0; i < Nodes; ++i)
      if (children[i]) children[i]->foreachk(f);
  }
  void merge(utils::Valid<alloc::Arena<>> alloc, InlineTrie *other) {
    other->foreachk([=, this](K k) { this->insert(alloc, k); });
  }

private:
  auto isLeaf(int i) -> bool {
    if (!keys[i]) return false;
    if (!children[i]) return true;
    for (int j = 0; j < Nodes; ++j)
      if (!children[i]->isLeaf(j)) return false;
    return true;
  }
  // A leaf is a key without any child keys.
  // A leaf may have children without keys.
  auto findLeaf() -> Pair<InlineTrie *, ptrdiff_t> {
    InlineTrie *leaf = this;
    bool descend[Nodes]{};
    for (int j = 0; j < Nodes; ++j) descend[j] = false;
    for (ptrdiff_t i = 0; i < std::ssize(children); ++i) {
      if (!leaf->keys[i]) continue;             // need key to be leaf
      if (!leaf->children[i]) return {leaf, i}; // no children, no child keys
      descend[i] = true;
    }
    for (ptrdiff_t i = 0; i < std::ssize(children); ++i) {
      if (!descend[i]) continue;
      auto ret = leaf->children[i]->findLeaf();
      return ret.first ? ret : Pair<InlineTrie *, ptrdiff_t>{this, i};
    };
    return {nullptr, 0};
  }
};

// static_assert(sizeof(std::array<TrieMapNode<int,int>*,0 >)==1);

} // namespace dict