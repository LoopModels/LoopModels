#pragma once
#include <Alloc/Arena.hpp>
#include <Utilities/Valid.hpp>
#include <ankerl/unordered_dense.h>
#include <cstdint>

namespace poly::dict {
// Idea from from https://nullprogram.com/blog/2023/09/30/
template <class K, class V> struct TrieMapNode {
  K first;
  V second;
  std::array<TrieMapNode<K, V> *, 4> children{};

  constexpr auto find(const K &k) -> TrieMapNode * {
    return findChild(k).child;
  }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, const K &k) -> V & {
    Child c = findChild(k);
    if (c.child) return c.second;
    invariant(c.parent != nullptr);
    invariant(c.index < 4);
    c.parent[c.index] = alloc->create<TrieMapNode>();
    c.parent[c.index]->first = k;
    return c.parent[c.index]->second;
  }

protected:
  struct Child {
    TrieMapNode *child;
    TrieMapNode *parent;
    uint64_t index; // child == parent->children[index];
  };
  constexpr auto firstChild() -> Child {
    for (int i = 0; i < 4; ++i)
      if (TrieMapNode *c = children[i]) return Child{c, this, i};
    return Child{nullptr, this, 0};
  }
  constexpr auto isLeaf() -> bool { return !std::ranges::any_of(children); }
  constexpr auto numChildren() -> int {
    int count = 0;
    for (auto c : children) count += (c != nullptr);
    return count;
  }
  auto findChild(const K &k) -> Child {
    if (k == first) return {this, nullptr, 0};
    TrieMapNode *p = this, *c = nullptr;
    uint64_t h = ankerl::unordered_dense::hash<K>{}(k);
    for (;; h <<= 2) {
      c = p->children[h >> 62];
      if (!c || (c->first == k)) return {c, p, h >> 62};
      p = c;
    }
  }
  // Returns the removed node
  auto eraseImpl(const K &k) -> TrieMapNode * {
    Child child = findChild(k);
    if (!child.child) return nullptr;
    // we're erasing `child`
    Child l = child.child->firstChild();
    if (l.child) {
      for (;;) {
        Child n = l.child->firstChild();
        if (!n.child) break;
        l = n;
      }
      l.parent.children[l.index] = nullptr;    // leaf is moved up
      l.child.children = child.child.children; // leaf takes child's children
    }
    child.parent[child.index] = l.child; // leaf replaces deleted

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
    NodeT* erased = this->eraseImpl(k);
    erased->children[0] = list;
    list = erased;
  }
  auto operator[](utils::Valid<alloc::Arena<>> alloc, const K &k) -> V & {
    typename NodeT::Child c = findChild(k);
    if (c.child) return c.second;
    invariant(c.parent != nullptr);
    invariant(c.index < 4);
    if (list) {
      c.parent[c.index] = list;
      list = list->children[0];
    } else {
      c.parent[c.index] = alloc->create<NodeT>();
    }
    c.parent[c.index]->first = k;
    return c.parent[c.index]->second;
  }
};

template <class K, class V> struct TrieMap<false, K, V> : TrieMapNode<K, V> {
  using BaseT = TrieMapNode<K, V>;
  void erase(const K &k) { this->eraseImpl(k); }
};

static_assert(sizeof(TrieMap<false, int, int>) ==
              sizeof(TrieMapNode<int, int>));
static_assert(sizeof(TrieMap<true, int, int>) ==
              sizeof(TrieMapNode<int, int>) + sizeof(TrieMapNode<int, int> *));
// static_assert(sizeof(std::array<TrieMapNode<int,int>*,0 >)==1);

} // namespace poly::dict
