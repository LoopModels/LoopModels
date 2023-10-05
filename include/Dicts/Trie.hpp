#pragma once
#include <Utilities/Allocators.hpp>
#include <Utilities/Valid.hpp>
#include <ankerl/unordered_dense.h>

namespace poly::dict {
// Based on implementation from https://nullprogram.com/blog/2023/09/30/
// Blog included statement: All information on this blog, unless otherwise
// noted, is hereby released into the public domain, with no rights reserved.
template <class K, class V> struct TrieMap {
  K first;
  V second;
  TrieMap<K, V> *children[4]{};

  constexpr auto find(const K &k) -> TrieMap<K, V> * {
    if (k == first) return this;
    TrieMap<K, V> *m = this;
    uint64_t h = ankerl::unordered_dense::hash<K>{}(k);
    for (;; h <<= 2) {
      m = m->children[h >> 62];
      if (!m) return nullptr;
      if (k == m->first) return m;
    }
  }
  auto operator[](utils::NotNull<utils::Arena<>> alloc, const K &k) -> V & {
    if (k == first) return second;
    TrieMap<K, V> *m = this, *c = nullptr;
    uint64_t h = ankerl::unordered_dense::hash<K>{}(k);
    for (;; h <<= 2) {
      c = m->children[h >> 62];
      if (!c) break;
      if (k == c->first) return c->second;
      m = c;
    }
    m->children[h >> 62] = alloc->create<TrieMap<K, V>>();
    c = m->children[h >> 62];
    c->first = k;
    return c->second;
  }
};
} // namespace poly::dict
