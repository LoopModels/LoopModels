#pragma once

#include "Containers/BumpMapSet.hpp"
#include "Math/BumpVector.hpp"
#include "Utilities/Allocators.hpp"

template <class K, class V> class MapVector {
  amap<K, size_t> map;
  LinAlg::BumpPtrVector<std::pair<K, V>> vector;

public:
  MapVector(BumpAlloc<> &alloc) : map(alloc), vector(alloc) {}
  auto find(const K &key) const {
    auto f = map.find(key);
    if (f == map.end()) return vector.end();
    return vector.begin() + f->second;
  }
  auto find(const K &key) {
    auto f = map.find(key);
    if (f == map.end()) return vector.end();
    return vector.begin() + f->second;
  }
  auto begin() const { return vector.begin(); }
  auto end() const { return vector.end(); }
  auto begin() { return vector.begin(); }
  auto end() { return vector.end(); }
  auto rbegin() const { return vector.rbegin(); }
  auto rend() const { return vector.rend(); }
  auto rbegin() { return vector.rbegin(); }
  auto rend() { return vector.rend(); }
  auto &operator[](const K &key) {
    auto f = map.find(key);
    if (f == map.end()) {
      auto i = vector.size();
      map[key] = i;
      vector.emplace_back(key, V());
      return vector[i].second;
    }
    return vector[f->second].second;
  }
  auto size() const { return vector.size(); }
  auto empty() const { return vector.empty(); }
  auto &back() { return vector.back(); }
  auto &back() const { return vector.back(); }
  auto &front() { return vector.front(); }
  auto &front() const { return vector.front(); }
};
