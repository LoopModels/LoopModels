#pragma once
#include "Math/BumpVector.hpp"
#include <ankerl/unordered_dense.h>

template <typename K, typename V>
using amap =
  ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                               std::equal_to<K>,
                               LinAlg::BumpPtrVector<std::pair<K, V>>>;
template <typename K>
using aset = ankerl::unordered_dense::set<K, ankerl::unordered_dense::hash<K>,
                                          std::equal_to<K>,
                                          LinAlg::BumpPtrVector<K>>;
