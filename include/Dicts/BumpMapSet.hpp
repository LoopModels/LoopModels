#pragma once
#include "Math/BumpVector.hpp"
#include "Utilities/Allocators.hpp"
#include <ankerl/unordered_dense.h>

template <typename K> using set = ankerl::unordered_dense::set<K>;
template <typename K, typename V>
using map = ankerl::unordered_dense::map<K, V>;

template <typename K, typename V>
struct amap
  : ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>,
                                 LinAlg::BumpPtrVector<std::pair<K, V>>> {
  using Base =
    ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>,
                                 LinAlg::BumpPtrVector<std::pair<K, V>>>;
  amap(BumpAlloc<> &alloc) : Base{WBumpAlloc<std::pair<K, V>>(alloc)} {}
};
template <typename K>
struct aset
  : ankerl::unordered_dense::set<K, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>, LinAlg::BumpPtrVector<K>> {
  using Base =
    ankerl::unordered_dense::set<K, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>, LinAlg::BumpPtrVector<K>>;
  aset(BumpAlloc<> &alloc) : Base{WBumpAlloc<K>(alloc)} {}
};
