#pragma once

#include "Containers/BumpMapSet.hpp"
#include "Math/BumpVector.hpp"
#include "Utilities/Allocators.hpp"
#include <absl/container/flat_hash_map.h>
#include <ankerl/unordered_dense.h>
#include <benchmark/benchmark.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <llvm/ADT/DenseMap.h>
#include <random>
#include <unordered_map>
// #include <absl/container/flat_hash_set.h>

template <typename K, typename V>
using amap =
  ankerl::unordered_dense::map<K, V, ankerl::unordered_dense::hash<K>,
                               std::equal_to<K>,
                               LinAlg::BumpPtrVector<std::pair<K, V>>>;

template <typename D>
void InsertLookup2(std::mt19937_64 &mt, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < 256; ++i) {
    map[reinterpret_cast<void *>(mt() & mask)] +=
      i + map[reinterpret_cast<void *>(mt() & mask)];
  }
}

template <typename D>
void InsertErase(std::mt19937_64 &mt, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < 256; ++i) {
    map[reinterpret_cast<void *>(mt() & mask)] = i;
    map.erase(reinterpret_cast<void *>(mt() & mask));
  }
}
template <typename D>
void InsertLookup3(std::mt19937_64 &mt, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < 256; ++i) {
    map[reinterpret_cast<void *>(mt() & mask)] +=
      map[reinterpret_cast<void *>(mt() & mask)] +
      map[reinterpret_cast<void *>(mt() & mask)];
  }
}

static void BM_llvmDenseMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertErase(mt, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertErase)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertErase(mt, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertErase)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertErase(benchmark::State &state) {
  BumpAlloc<> alloc;
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    amap<void *, uint64_t> map(alloc);
    InsertErase(mt, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertErase)->DenseRange(2, 8, 1);
static void BM_AbslMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    absl::flat_hash_map<void *, uint64_t> map;
    InsertErase(mt, map, mask);
  }
}
BENCHMARK(BM_AbslMapInsertErase)->DenseRange(2, 8, 1);
static void BM_ankerlMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertErase(mt, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertErase)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertErase(mt, map, mask);
  }
}
BENCHMARK(BM_stdUnorderedMapInsertErase)->DenseRange(2, 8, 1);

static void BM_llvmDenseMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertLookup2(mt, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertLookup2(mt, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertLookup(benchmark::State &state) {
  BumpAlloc<> alloc;
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    amap<void *, uint64_t> map(alloc);
    InsertLookup2(mt, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_AbslMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    absl::flat_hash_map<void *, uint64_t> map;
    InsertLookup2(mt, map, mask);
  }
}
BENCHMARK(BM_AbslMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_ankerlMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertLookup2(mt, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertLookup2(mt, map, mask);
  }
}
BENCHMARK(BM_stdUnorderedMapInsertLookup)->DenseRange(2, 8, 1);

static void BM_llvmDenseMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertLookup3(mt, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertLookup3(mt, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertLookup3(benchmark::State &state) {
  BumpAlloc<> alloc;
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    amap<void *, uint64_t> map(alloc);
    InsertLookup3(mt, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_AbslMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    absl::flat_hash_map<void *, uint64_t> map;
    InsertLookup3(mt, map, mask);
  }
}
BENCHMARK(BM_AbslMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_ankerlMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertLookup3(mt, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ull << state.range(0)) - 1) << 3ull;
  std::mt19937_64 mt;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertLookup3(mt, map, mask);
  }
}
BENCHMARK(BM_stdUnorderedMapInsertLookup3)->DenseRange(2, 8, 1);

static void BM_llvmDenseMapSeq(benchmark::State &state) {
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};

    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}

BENCHMARK(BM_llvmDenseMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_llvmSmallDenseMapSeq(benchmark::State &state) {
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_llvmSmallDenseMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_BumpMapSeq(benchmark::State &state) {
  BumpAlloc<> alloc;
  for (auto b : state) {
    amap<void *, uint64_t> map(alloc);
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

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

static void BM_AbslMapSeq(benchmark::State &state) {
  for (auto b : state) {
    absl::flat_hash_map<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}

BENCHMARK(BM_AbslMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_AnkerlMapSeq(benchmark::State &state) {
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_AnkerlMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_stdUnorderedMapSeq(benchmark::State &state) {
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_stdUnorderedMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
