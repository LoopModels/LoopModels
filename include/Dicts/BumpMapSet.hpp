#pragma once
#include <Alloc/Arena.hpp>
#include <Dicts/BumpVector.hpp>
#include <IR/Hash.hpp>
#include <ankerl/unordered_dense.h>

namespace poly::dict {

template <typename K> using set = ankerl::unordered_dense::set<K>;
template <typename K, typename V>
using map = ankerl::unordered_dense::map<K, V>;

template <typename K, typename V>
struct amap // NOLINT(readability-identifier-naming)
  : ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>,
                                 math::BumpPtrVector<std::pair<K, V>>> {
  using Base =
    ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>,
                                 math::BumpPtrVector<std::pair<K, V>>>;
  amap(Arena<> *alloc) : Base{WArena<std::pair<K, V>>(alloc)} {}
};
template <typename K>
struct aset // NOLINT(readability-identifier-naming)
  : ankerl::unordered_dense::set<K, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>, math::BumpPtrVector<K>> {
  using Base =
    ankerl::unordered_dense::set<K, ankerl::unordered_dense::hash<K>,
                                 std::equal_to<K>, math::BumpPtrVector<K>>;
  aset(Arena<> *alloc) : Base{WArena<K>(alloc)} {}
};

static_assert(std::same_as<amap<int, int>::value_container_type,
                           math::BumpPtrVector<std::pair<int, int>>>);
static_assert(std::same_as<amap<int, int>::allocator_type,
                           alloc::WArena<std::pair<int, int>, 16384, true>>);

} // namespace poly::dict
