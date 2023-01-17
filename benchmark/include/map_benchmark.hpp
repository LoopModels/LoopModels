#pragma once

#include "Containers/BumpMap.hpp"
#include "Utilities/Allocators.hpp"
#include <absl/container/flat_hash_map.h>
#include <ankerl/unordered_dense.h>
#include <benchmark/benchmark.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <llvm/ADT/DenseMap.h>
// #include <absl/container/flat_hash_set.h>

// #include "Math/BumpVector.hpp"

static void BM_llvmDenseMap(benchmark::State &state) {
  for (auto b : state) {
    llvm::DenseMap<uint64_t, uint64_t> map;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i) map[i] = i;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[i]);
  }
}
// Register the function as a benchmark
BENCHMARK(BM_llvmDenseMap)->RangeMultiplier(2)->Range(1 << 4, 1 << 10);

static void BM_llvmSmallDenseMap(benchmark::State &state) {
  for (auto b : state) {
    llvm::SmallDenseMap<uint64_t, uint64_t> map;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i) map[i] = i;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[i]);
  }
}
// Register the function as a benchmark
BENCHMARK(BM_llvmSmallDenseMap)->RangeMultiplier(2)->Range(1 << 4, 1 << 10);

static void BM_BumpMap(benchmark::State &state) {
  BumpAlloc<> alloc;
  for (auto b : state) {
    BumpMap<uint64_t, uint64_t> map(alloc);
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i) map[i] = i;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[i]);
    alloc.reset();
  }
}
// Register the function as a benchmark
BENCHMARK(BM_BumpMap)->RangeMultiplier(2)->Range(1 << 4, 1 << 10);

// template <typename K, typename V>
// using amap =
//   absl::flat_hash_map<K, V, absl::container_internal::hash_default_hash<K>,
//                       absl::container_internal::hash_default_eq<K>,
//                       WBumpAlloc<std::pair<const K, V>>>;
// template <typename K, typename V>
// using aset =
//   absl::flat_hash_set<K, absl::container_internal::hash_default_hash<K>,
//                       absl::container_internal::hash_default_eq<K>,
//                       WBumpAlloc<K>>;

static void BM_AbslMap(benchmark::State &state) {
  for (auto b : state) {
    absl::flat_hash_map<uint64_t, uint64_t> map;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i) map[i] = i;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[i]);
  }
}

// Register the function as a benchmark
BENCHMARK(BM_AbslMap)->RangeMultiplier(2)->Range(1 << 4, 1 << 10);
// template <typename K, typename V>
// using amap =
//   ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
//                                std::equal_to<K>, BumpVector<std::pair<K,
//                                V>>>;
// template <typename K, typename V>
// using aset = ankerl::unordered_dense::set<K,
// ankerl::unordered_dense::hash<K>,
//                                           std::equal_to<K>, BumpVector<K>>;

static void BM_AnkerlMap(benchmark::State &state) {
  for (auto b : state) {
    ankerl::unordered_dense::map<uint64_t, uint64_t> map;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i) map[i] = i;
    for (uint64_t i = 0; i < uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[i]);
  }
}
// Register the function as a benchmark
BENCHMARK(BM_AnkerlMap)->RangeMultiplier(2)->Range(1 << 4, 1 << 10);
