#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <cstddef>

#ifndef USE_MODULE
#include "Dicts/Trie.cxx"
#include "Containers/Pair.cxx"
#include "Math/Array.cxx"
#include "Alloc/Arena.cxx"
#else
export module MapVector;

import Arena;
import Array;
import Pair;
import Trie;
#endif

#ifdef USE_MODULE
export namespace dict {
#else
namespace dict {
#endif

template <class K, class V> class OrderedMap {
  InlineTrie<K, ptrdiff_t> map{};
  // math::BumpPtrVector<containers::Pair<K, V>> vector;
  math::ResizeableView<containers::Pair<K, V>, math::Length<>> vector{};
  alloc::Arena<> *alloc_;

public:
  constexpr OrderedMap(alloc::Arena<> *alloc) : alloc_{alloc} {}
  OrderedMap(const OrderedMap &) = default;
  OrderedMap(OrderedMap &&) noexcept = default;
  constexpr auto operator=(const OrderedMap &) -> OrderedMap & = default;
  constexpr auto operator=(OrderedMap &&) noexcept -> OrderedMap & = default;
  /*
  constexpr auto find(const K &key) const {
    auto f = map.find(key);
    if (!f) return vector.end();
    return vector.begin() + *f;
  }
  */
  constexpr auto find(const K &key) {
    auto f = map.find(key);
    if (!f) return vector.end();
    return vector.begin() + *f;
  }
  constexpr auto begin() const { return vector.begin(); }
  constexpr auto end() const { return vector.end(); }
  constexpr auto begin() { return vector.begin(); }
  constexpr auto end() { return vector.end(); }
  constexpr auto rbegin() const { return vector.rbegin(); }
  constexpr auto rend() const { return vector.rend(); }
  constexpr auto rbegin() { return vector.rbegin(); }
  constexpr auto rend() { return vector.rend(); }
  constexpr auto operator[](const K &key) -> V & {
    auto [idx, inserted] = map.insert(alloc_, key);
    if (inserted) {
      auto i = vector.size();
      *idx = i;
      grow(i);
      return vector.emplace_back_within_capacity(key, V()).second;
    }
    return vector[*idx].second;
  }
  constexpr auto size() const { return vector.size(); }
  constexpr auto empty() const { return vector.empty(); }
  constexpr auto back() -> auto & { return vector.back(); }
  constexpr auto back() const -> auto & { return vector.back(); }
  constexpr auto front() -> auto & { return vector.front(); }
  constexpr auto front() const -> auto & { return vector.front(); }
  constexpr void insert(const K &key, const V &value) { (*this)[key] = value; }
  constexpr void grow(int i) {
    if (i == vector.getCapacity())
      vector.reserve(alloc_, std::max<unsigned>(8, 2 * i));
  }
  constexpr void insert(containers::Pair<K, V> &&value) {
    insert(std::move(value.first), std::move(value.second));
  }
  constexpr void clear() {
    map.clear();
    vector.clear();
  }
  auto count(const K &key) const -> size_t { return map.contains(key); }
  auto contains(const K &key) const -> bool { return map.contains(key); }
};

} // namespace dict