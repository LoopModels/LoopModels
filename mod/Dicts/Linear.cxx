#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

#ifndef USE_MODULE
#include "Containers/Pair.cxx"
#include "Math/Array.cxx"
#include "Math/Indexing.cxx"
#include "Math/SOA.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Parameters.cxx"
#else
export module LinearDict;
import Array;
import Indexing;
import Invariant;
import Pair;
import Param;
import SOA;
#endif

#ifdef USE_MODULE
export namespace dict {
#else
namespace dict {
#endif
using math::last, utils::inparam_t;

template <typename K, typename V> class Linear {
  using Data = math::ManagedSOA<containers::Pair<K, V>>;
  using Ref = typename Data::reference_type;
  Data data_{};

public:
  constexpr auto keys() -> math::MutPtrVector<K> {
    return data_.template get<0>();
  }
  constexpr auto keys() const -> math::PtrVector<K> {
    return data_.template get<0>();
  }
  constexpr auto values() -> math::MutPtrVector<V> {
    return data_.template get<1>();
  }
  constexpr auto values() const -> math::PtrVector<V> {
    return data_.template get<1>();
  }
  constexpr auto find(inparam_t<K> key) -> std::optional<Ref> {
    auto ks = keys();
    auto ki = std::ranges::find_if(ks, [&](const auto &k) { return key == k; });
    if (ki == ks.end()) return {};
    return data_[std::distance(ks.begin(), ki)];
  }
  // TODO: implement `eraseUnordered`
  constexpr auto erase(inparam_t<K> key) -> bool {
    auto ks = keys();
    auto ki = std::ranges::find_if(ks, [&](const auto &k) { return key == k; });
    if (ki == ks.end()) return false;
    data_.erase(std::distance(ks.begin(), ki));
    return true;
  }
  constexpr auto operator[](inparam_t<K> key) -> V & {
    if (auto f = find(key)) return f->template get<1>();
    data_.resize(data_.size() + 1); // unsafe
    std::construct_at(&(keys().back()), key);
    std::construct_at(&(values().back()));
    return values().back();
  }
  constexpr void decRemoveIfNot(inparam_t<K> key) {
    auto ks = keys();
    auto ki = std::ranges::find_if(ks, [&](const auto &k) { return key == k; });
    utils::invariant(ki != ks.end());
    ptrdiff_t i = std::distance(ks.begin(), ki);
    if (!--values()[i]) data_.erase(i);
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
    return data_.size();
  }
  [[nodiscard]] constexpr auto getData() { return data_; }
  constexpr auto clear() { return data_.clear(); }
  // constexpr auto begin() { return data_.begin(); }
  // constexpr auto end() { return data_.end(); }
  // constexpr auto begin() const { return data_.begin(); }
  // constexpr auto end() const { return data_.end(); }
};

template <std::totally_ordered K, typename V> class Binary {
  using Data = math::ManagedSOA<containers::Pair<K, V>>;
  using Ref = typename Data::reference_type;
  Data data_{};

  static constexpr bool trivial =
    std::is_trivially_default_constructible_v<K> &&
    std::is_trivially_default_constructible_v<V> &&
    std::is_trivially_destructible_v<K> && std::is_trivially_destructible_v<V>;

public:
  constexpr auto keys() -> math::MutPtrVector<K> {
    return data_.template get<0>();
  }
  constexpr auto keys() const -> math::PtrVector<K> {
    return data_.template get<0>();
  }
  constexpr auto values() -> math::MutPtrVector<V> {
    return data_.template get<1>();
  }
  constexpr auto values() const -> math::PtrVector<V> {
    return data_.template get<1>();
  }
  constexpr auto find(inparam_t<K> key) -> std::optional<Ref> {
    auto ks = keys();
    auto ki = std::ranges::lower_bound(ks, key);
    if ((ki == ks.end()) || (*ki != key)) return {};
    return data_[std::distance(ks.begin(), ki)];
  }
  constexpr auto erase(inparam_t<K> key) -> bool {
    auto ks = keys();
    auto ki = std::ranges::lower_bound(ks, key);
    if ((ki == ks.end()) || (*ki != key)) return false;
    data_.erase(std::distance(ks.begin(), ki));
    return true;
  }
  constexpr auto operator[](inparam_t<K> key) -> V & {
    auto ks = keys();
    auto ki = std::ranges::lower_bound(ks, key);
    ptrdiff_t pos = std::distance(ks.begin(), ki);
    if ((pos != ks.size()) && (*ki == key)) return values()[pos];
    data_.resize(data_.size() + 1); // unsafe
    ks = keys();                    // reset, in case data moved
    auto vs = values();
    if constexpr (trivial) {
      for (ptrdiff_t i = ks.size(); --i > pos;) {
        ks[i] = ks[i - 1];
        vs[i] = vs[i - 1];
      }
      vs[pos] = V{};
    } else {
      if (ki == ks.end()) {
        std::construct_at(&(ks[last]), key);
        std::construct_at(&(vs[last]));
        return vs[last];
      }
      std::construct_at(&(ks[last]), std::move(ks[last - 1]));
      std::construct_at(&(vs[last]), std::move(vs[last - 1]));
      for (ptrdiff_t i = ks.size() - 1; --i > pos;) {
        ks[i] = std::move(ks[i - 1]);
        vs[i] = std::move(vs[i - 1]);
      }
    }
    ks[pos] = key;
    return vs[pos];
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
    return data_.size();
  }
  constexpr void clear() { return data_.clear(); }
  // constexpr auto begin() { return data_.begin(); }
  // constexpr auto end() { return data_.end(); }
  // constexpr auto begin() const { return data_.begin(); }
  // constexpr auto end() const { return data_.end(); }
};
} // namespace dict
