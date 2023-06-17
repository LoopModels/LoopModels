#pragma once

#include "Dicts/BumpMapSet.hpp"
#include <Math/Array.hpp>

namespace poly::dict {

template <class K, class V> class OrderedMap {
  amap<K, size_t> map;
  // math::BumpPtrVector<std::pair<K, V>> vector;
  math::ResizeableView<std::pair<K, V>, unsigned> vector;

public:
  constexpr OrderedMap(BumpAlloc<> &alloc) : map(alloc), vector() {}
  OrderedMap(const OrderedMap &) = default;
  OrderedMap(OrderedMap &&) noexcept = default;
  constexpr auto operator=(const OrderedMap &) -> OrderedMap & = default;
  constexpr auto operator=(OrderedMap &&) noexcept -> OrderedMap & = default;
  constexpr auto find(const K &key) const {
    auto f = map.find(key);
    if (f == map.end()) return vector.end();
    return vector.begin() + f->second;
  }
  constexpr auto find(const K &key) {
    auto f = map.find(key);
    if (f == map.end()) return vector.end();
    return vector.begin() + f->second;
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
    auto f = map.find(key);
    if (f == map.end()) {
      auto i = vector.size();
      map[key] = i;
      grow(i);
      vector.emplace_back(key, V());
      return vector[i].second;
    }
    return vector[f->second].second;
  }
  constexpr auto size() const { return vector.size(); }
  constexpr auto empty() const { return vector.empty(); }
  constexpr auto back() -> auto & { return vector.back(); }
  constexpr auto back() const -> auto & { return vector.back(); }
  constexpr auto front() -> auto & { return vector.front(); }
  constexpr auto front() const -> auto & { return vector.front(); }
  constexpr void insert(const K &key, const V &value) {
    auto f = map.find(key);
    if (f == map.end()) {
      auto i = vector.size();
      map[key] = i;
      grow(i);
      vector.emplace_back(key, value);
    } else {
      vector[f->second].second = value;
    }
  }
  constexpr void grow(unsigned i) {
    if (i == vector.getCapacity())
      vector.reserve(*(map.get_allocator().get_allocator()),
                     std::max<unsigned>(8, 2 * i));
  }
  constexpr void insert(std::pair<K, V> &&value) {
    insert(std::move(value.first), std::move(value.second));
  }
  constexpr void clear() {
    map.clear();
    vector.clear();
  }
  auto count(const K &key) const -> size_t { return map.count(key); }
  auto contains(const K &key) const -> bool { return map.contains(key); }
};
} // namespace poly::dict
