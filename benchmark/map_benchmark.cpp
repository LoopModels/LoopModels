
#include "Alloc/Arena.hpp"
#include "Dicts/BumpMapSet.hpp"
#include "Dicts/BumpVector.hpp"
#include "Dicts/Linear.hpp"
#include "Dicts/Trie.hpp"
#include <ankerl/unordered_dense.h>
#include <benchmark/benchmark.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <random>
#include <unordered_map>

namespace poly::dict {

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

template <class D> struct TrieWrap {
  D d;
  poly::alloc::Arena<> *alloc;

  template <class K> auto operator[](const K &k) -> auto & {
    return d[alloc, k];
  };
  template <class K> void erase(const K &k) { d.erase(k); }
};

inline auto randvp(std::mt19937_64 &rng, uint64_t mask) {
  return reinterpret_cast<void *>((rng() & mask) | 8);
}

// static constexpr uint64_t numIter = 32;
static constexpr uint64_t numIter = 16;

template <typename D>
void InsertLookup2(std::mt19937_64 &rng, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < numIter; ++i) {
    void *p0 = randvp(rng, mask);
    void *p1 = randvp(rng, mask);
    map[p0] += i + map[p1];
  }
}

template <typename D>
void InsertErase(std::mt19937_64 &rng, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < numIter; ++i) {
    void *p0 = randvp(rng, mask);
    void *p1 = randvp(rng, mask);
    map[p0] = i;
    map.erase(p1);
  }
}
template <typename D>
void InsertLookup3(std::mt19937_64 &rng, D &map, uint64_t mask) {
  for (uint64_t i = 0; i < numIter; ++i) {
    void *p0 = randvp(rng, mask);
    void *p1 = randvp(rng, mask);
    void *p2 = randvp(rng, mask);
    map[p0] += map[p1] + map[p2];
  }
}

static void BM_llvmDenseMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng{};
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertErase)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertErase)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertErase(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::amap<void *, uint64_t> map{&alloc};
    InsertErase(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertErase)->DenseRange(2, 8, 1);
static void BM_TrieInsertErase(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::TrieMap<true, void *, uint64_t>> map{{}, &alloc};
    InsertErase(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_TrieInsertErase)->DenseRange(2, 8, 1);

static void BM_InlineTrie2InsertErase(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 2>> map{{}, &alloc};
    InsertErase(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie2InsertErase)->DenseRange(2, 8, 1);
static void BM_InlineTrie3InsertErase(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 3>> map{{}, &alloc};
    InsertErase(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie3InsertErase)->DenseRange(2, 8, 1);

static void BM_ankerlMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertErase)->DenseRange(2, 8, 1);
static void BM_ankerlMimallocMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::map<void *, uint64_t> map;
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMimallocMapInsertErase)->DenseRange(2, 8, 1);
static void BM_boostMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<void *, uint64_t> map;
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_boostMapInsertErase)->DenseRange(2, 8, 1);
static void BM_boostMimallocMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<
      void *, uint64_t, boost::hash<void *>, std::equal_to<void *>,
      poly::alloc::Mallocator<std::pair<void *const, uint64_t>>>
      map{};
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_boostMimallocMapInsertErase)->DenseRange(2, 8, 1);
static void BM_LinearMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Linear<void *, uint64_t> map{};
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_LinearMapInsertErase)->DenseRange(2, 8, 1);
static void BM_BinaryMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Binary<void *, uint64_t> map{};
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_BinaryMapInsertErase)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertErase(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertErase(rng, map, mask);
  }
}
BENCHMARK(BM_stdUnorderedMapInsertErase)->DenseRange(2, 8, 1);

static void BM_llvmDenseMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertLookup(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::amap<void *, uint64_t> map{&alloc};
    InsertLookup2(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_TrieInsertLookup(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::TrieMap<false, void *, uint64_t>> map{{}, &alloc};
    InsertLookup2(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_TrieInsertLookup)->DenseRange(2, 8, 1);
static void BM_InlineTrie2InsertLookup(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 2>> map{{}, &alloc};
    InsertLookup2(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie2InsertLookup)->DenseRange(2, 8, 1);
static void BM_InlineTrie3InsertLookup(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 3>> map{{}, &alloc};
    InsertLookup2(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie3InsertLookup)->DenseRange(2, 8, 1);
static void BM_ankerlMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_ankerlMimallocMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::map<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMimallocMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_boostMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_boostMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_boostMimallocMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<
      void *, uint64_t, boost::hash<void *>, std::equal_to<void *>,
      poly::alloc::Mallocator<std::pair<void *const, uint64_t>>>
      map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_boostMimallocMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_LinearMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Linear<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_LinearMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_BinaryMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Binary<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_BinaryMapInsertLookup)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertLookup(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertLookup2(rng, map, mask);
  }
}
BENCHMARK(BM_stdUnorderedMapInsertLookup)->DenseRange(2, 8, 1);

static void BM_llvmDenseMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    llvm::DenseMap<void *, uint64_t> map{};
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_llvmDenseMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_llvmSmallDenseMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    llvm::SmallDenseMap<void *, uint64_t> map{};
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_llvmSmallDenseMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_BumpMapInsertLookup3(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::amap<void *, uint64_t> map{&alloc};
    InsertLookup3(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_TrieInsertLookup3(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::TrieMap<false, void *, uint64_t>> map{{}, &alloc};
    InsertLookup3(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_TrieInsertLookup3)->DenseRange(2, 8, 1);
static void BM_InlineTrie2InsertLookup3(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 2>> map{{}, &alloc};
    InsertLookup3(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie2InsertLookup3)->DenseRange(2, 8, 1);
static void BM_InlineTrie3InsertLookup3(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    TrieWrap<poly::dict::InlineTrie<void *, uint64_t, 3>> map{{}, &alloc};
    InsertLookup3(rng, map, mask);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie3InsertLookup3)->DenseRange(2, 8, 1);
static void BM_ankerlMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    ankerl::unordered_dense::map<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_ankerlMimallocMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::map<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_ankerlMimallocMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_boostMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_boostMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_boostMimallocMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    boost::unordered_flat_map<
      void *, uint64_t, boost::hash<void *>, std::equal_to<void *>,
      poly::alloc::Mallocator<std::pair<void *const, uint64_t>>>
      map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_boostMimallocMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_LinearMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Linear<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_LinearMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_BinaryMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    poly::dict::Binary<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
  }
}
BENCHMARK(BM_BinaryMapInsertLookup3)->DenseRange(2, 8, 1);
static void BM_stdUnorderedMapInsertLookup3(benchmark::State &state) {
  uint64_t mask = ((1ULL << state.range(0)) - 1) << 4ULL;
  std::mt19937_64 rng;
  for (auto b : state) {
    std::unordered_map<void *, uint64_t> map;
    InsertLookup3(rng, map, mask);
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
  poly::alloc::OwningArena<> alloc;
  for (auto b : state) {
    poly::dict::amap<void *, uint64_t> map{&alloc};
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
    alloc.reset();
  }
}
BENCHMARK(BM_BumpMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_TrieMapSeq(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  for (auto b : state) {
    poly::dict::TrieMap<false, void *, uint64_t> map{};
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[&alloc, reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[&alloc, reinterpret_cast<void *>(8 * i)]);
    alloc.reset();
  }
}
BENCHMARK(BM_TrieMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_InlineTrie2Seq(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  for (auto b : state) {
    poly::dict::InlineTrie<void *, uint64_t, 2> map{};
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[&alloc, reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[&alloc, reinterpret_cast<void *>(8 * i)]);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie2Seq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

static void BM_InlineTrie3Seq(benchmark::State &state) {
  poly::alloc::OwningArena<> alloc;
  for (auto b : state) {
    poly::dict::InlineTrie<void *, uint64_t, 3> map{};
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[&alloc, reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[&alloc, reinterpret_cast<void *>(8 * i)]);
    alloc.reset();
  }
}
BENCHMARK(BM_InlineTrie3Seq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);

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
static void BM_AnkerlMimallocMapSeq(benchmark::State &state) {
  for (auto b : state) {
    poly::dict::map<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_AnkerlMimallocMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
static void BM_boostMapSeq(benchmark::State &state) {
  for (auto b : state) {
    boost::unordered_flat_map<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_boostMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
static void BM_boostMimallocMapSeq(benchmark::State &state) {
  for (auto b : state) {
    boost::unordered_flat_map<
      void *, uint64_t, boost::hash<void *>, std::equal_to<void *>,
      poly::alloc::Mallocator<std::pair<void *const, uint64_t>>>
      map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_boostMimallocMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
static void BM_LinearMapSeq(benchmark::State &state) {
  for (auto b : state) {
    poly::dict::Linear<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_LinearMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
static void BM_BinaryMapSeq(benchmark::State &state) {
  for (auto b : state) {
    poly::dict::Binary<void *, uint64_t> map;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      map[reinterpret_cast<void *>(8 * i)] = i;
    for (uint64_t i = 1; i <= uint64_t(state.range(0)); ++i)
      benchmark::DoNotOptimize(map[reinterpret_cast<void *>(8 * i)]);
  }
}
BENCHMARK(BM_BinaryMapSeq)->RangeMultiplier(2)->Range(1 << 2, 1 << 10);
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
