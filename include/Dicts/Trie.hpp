#pragma once
#include "Containers/Pair.hpp"
#include <Alloc/Arena.hpp>
#include <Utilities/Invariant.hpp>
#include <Utilities/Optional.hpp>
#include <Utilities/Valid.hpp>
#include <ankerl/unordered_dense.h>
#include <cstdint>

namespace poly::dict {
using utils::invariant, containers::Pair;

template <class T> constexpr auto fastHash(const T &x) -> uint64_t {
  return ankerl::unordered_dense::hash<T>{}(x);
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

  constexpr auto find(const K &k) -> TrieMapNode * {
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
  auto findChild(const K &k) -> Child {
    if (k == first) return {this, nullptr, 0};
    TrieMapNode *p = this, *c = nullptr;
    for (uint64_t h = fastHash(k);; h >>= 2) {
      c = p->children[h & 3];
      if (!c || (c->first == k)) return {c, p, h & 3};
      p = c;
    }
  }
  // Returns the removed node
  auto eraseImpl(const K &k) -> TrieMapNode * {
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
  void erase(const K &k) {
    if (NodeT *erased = this->eraseImpl(k))
      erased->children[0] = std::exchange(list, erased);
  }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, const K &k) -> V & {
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
  void erase(const K &k) { this->eraseImpl(k); }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, const K &k) -> V & {
    typename NodeT::Child c = findChild(k);
    if (c.child) return c.child->second;
    invariant(c.parent != nullptr);
    invariant(c.index < 4);
    invariant(c.parent->children[c.index] == nullptr);
    TrieMapNode res = c.parent->children[c.index] = alloc->create<NodeT>();
    res->first = k;
    return res->second;
  }
};

static_assert(sizeof(TrieMap<false, int, int>) ==
              sizeof(TrieMapNode<int, int>));
static_assert(sizeof(TrieMap<true, int, int>) ==
              sizeof(TrieMapNode<int, int>) + sizeof(TrieMapNode<int, int> *));

// Optional can be specialized for types to add dead-values without requiring
// extra space. E.g., `sizeof(utils::Optional<T*>) == sizeof(T*)`, as `nullptr`
// indicates empty.
template <class K, class V> struct InlineTrie {
  InlineTrie<K, V> *children[4];
  utils::Optional<K> keys[4];
  V values[4];

  // Returns an optional pointer to the value.
  constexpr auto find(const K &k) -> utils::Optional<V &> {
    auto [node, index] = findChild<false>(this, k);
    return node ? utils::Optional<V &>{node->values[index]} : std::nullopt;
  }

  auto operator[](utils::Valid<alloc::Arena<>> alloc, const K &k) -> V & {
    Child c = findChild<true>(this, k);
    if (c.subIndex) {
      c.node = c.node->children[*c.subIndex] =
        alloc->create<InlineTrie<K, V>>();
      c.node->keys[c.index] = k;
    }
    return c.node->values[c.index];
  }

  void erase(const K &k) {
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
    for (int j = 0; j < 4; ++j)
      if (!children[i]->isLeaf(j)) return false;
    return true;
  }
  // A leaf is a key without any child keys.
  // A leaf may have children without keys.
  auto findLeaf() -> Pair<InlineTrie *, ptrdiff_t> {
    InlineTrie *leaf = this;
    bool descend[4]{false, false, false, false};
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
  struct Child {
    InlineTrie *node;
    size_t index;
    utils::Optional<size_t> subIndex;
  };

  template <bool Insert>
  static constexpr auto findChild(InlineTrie *node, const K &k) {
    for (uint64_t h = fastHash(k);;) {
      uint64_t ind = h & 3;
      bool noKey = !node->keys[ind];
      if constexpr (Insert) {
        if (noKey) node->keys[ind] = k;
        if (noKey || (*node->keys[ind] == k)) return Child{node, ind, {}};
      } else {
        if (noKey) return Pair<InlineTrie *, uint64_t>{nullptr, ind};
        if (*node->keys[ind] == k)
          return Pair<InlineTrie *, uint64_t>{node, ind};
      }
      h >>= 2;
      if (!node->children[ind]) {
        if constexpr (Insert) return Child{node, h & 3, ind};
        else return Pair<InlineTrie *, uint64_t>{nullptr, ind};
      }
      node = node->children[ind];
    }
  };
};

// static_assert(sizeof(std::array<TrieMapNode<int,int>*,0 >)==1);

} // namespace poly::dict
