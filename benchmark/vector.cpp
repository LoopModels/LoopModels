#include "Math/Array.hpp"
#include "Math/Indexing.hpp"
#include "Math/Iterators.hpp"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <cassert>
#include <cstddef>
#include <llvm/ADT/SmallVector.h>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

using poly::math::Vector;

auto randVecFillSum(std::mt19937 &gen, double p) -> double {
  std::uniform_real_distribution<> dis(0, 1);
  ptrdiff_t L = (dis(gen) < p) ? 10z : 10000z;
  Vector<double> v(poly::math::length(L));
  std::iota(v.begin(), v.end(), 1); // 1, 2, ..., L
  return v.sum();
}
auto randStdVecFillSum(std::mt19937 &gen, double p) -> double {
  std::uniform_real_distribution<> dis(0, 1);
  ptrdiff_t L = (dis(gen) < p) ? 10z : 10000z;
  std::vector<double> v(L);
  std::iota(v.begin(), v.end(), 1); // 1, 2, ..., L
  return std::reduce(v.begin(), v.end());
}

void pushVector(auto &v, size_t len) {
  v.clear();
  assert(v.empty());
  for (size_t i = 0; i < len; ++i) v.push_back(i);
  benchmark::DoNotOptimize(v);
}
auto pushVectorValue(auto v, size_t len) {
  v.clear();
  assert(v.empty());
  for (size_t i = 0; i < len; ++i) v.push_back(i);
  benchmark::DoNotOptimize(v);
  return std::move(v);
}
void pushVectorReserve(auto &v, size_t len) {
  v.clear();
  assert(v.empty());
  v.reserve(len);
  for (size_t i = 0; i < len; ++i) v.push_back(i);
  benchmark::DoNotOptimize(v);
}
void fillVectorReserve(auto &v, size_t len) {
  v.clear();
  assert(v.empty());
  v.resize(len);
  for (size_t i = 0; i < len; ++i) v[i] = i;
  benchmark::DoNotOptimize(v);
}
template <typename T, ptrdiff_t L>
void fillVectorReserve(Vector<T, L> &v, size_t len) {
  v.clear();
  assert(v.empty());
  v.resize(len);
  v << poly::math::_(0, len);
  // for (size_t i = 0; i < len; ++i) v[i] = i;
  benchmark::DoNotOptimize(v);
}

void BM_SmallVector8Push(benchmark::State &state) {
  llvm::SmallVector<size_t, 8> v;
  size_t len = state.range(0);
  for (auto b : state) pushVector(v, len);
}
BENCHMARK(BM_SmallVector8Push)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVectorPush(benchmark::State &state) {
  llvm::SmallVector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVector(v, len);
}
BENCHMARK(BM_SmallVectorPush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorPush(benchmark::State &state) {
  std::vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVector(v, len);
}
BENCHMARK(BM_StdVectorPush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_MathVectorPush(benchmark::State &state) {
  Vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVector(v, len);
}
BENCHMARK(BM_MathVectorPush)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_SmallVector8PushMove(benchmark::State &state) {
  llvm::SmallVector<size_t, 8> v;
  size_t len = state.range(0);
  for (auto b : state) v = pushVectorValue(std::move(v), len);
}
BENCHMARK(BM_SmallVector8PushMove)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVectorPushMove(benchmark::State &state) {
  llvm::SmallVector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) v = pushVectorValue(std::move(v), len);
}
BENCHMARK(BM_SmallVectorPushMove)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorPushMove(benchmark::State &state) {
  std::vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) v = pushVectorValue(std::move(v), len);
}
BENCHMARK(BM_StdVectorPushMove)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_MathVectorPushMove(benchmark::State &state) {
  Vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) v = pushVectorValue(std::move(v), len);
}
BENCHMARK(BM_MathVectorPushMove)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_SmallVectorReservePush(benchmark::State &state) {
  llvm::SmallVector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVectorReserve(v, len);
}
BENCHMARK(BM_SmallVectorReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorReservePush(benchmark::State &state) {
  std::vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVectorReserve(v, len);
}
BENCHMARK(BM_StdVectorReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_MathVectorReservePush(benchmark::State &state) {
  Vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) pushVectorReserve(v, len);
}
BENCHMARK(BM_MathVectorReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_SmallVector8AllocPush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t, 8> v;
    pushVector(v, len);
  }
}
BENCHMARK(BM_SmallVector8AllocPush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVectorAllocPush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t> v;
    pushVector(v, len);
  }
}
BENCHMARK(BM_SmallVectorAllocPush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorAllocPush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    std::vector<size_t> v;
    pushVector(v, len);
  }
}
BENCHMARK(BM_StdVectorAllocPush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_MathVectorAllocPush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    Vector<size_t> v;
    pushVector(v, len);
  }
}
BENCHMARK(BM_MathVectorAllocPush)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_SmallVector8AllocReservePush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t, 8> v;
    pushVectorReserve(v, len);
  }
}
BENCHMARK(BM_SmallVector8AllocReservePush)
  ->RangeMultiplier(2)
  ->Range(1, 1 << 8);
void BM_SmallVectorAllocReservePush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t> v;
    pushVectorReserve(v, len);
  }
}
BENCHMARK(BM_SmallVectorAllocReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorAllocReservePush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    std::vector<size_t> v;
    pushVectorReserve(v, len);
  }
}
BENCHMARK(BM_StdVectorAllocReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_MathVectorAllocReservePush(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    Vector<size_t> v;
    pushVectorReserve(v, len);
  }
}
BENCHMARK(BM_MathVectorAllocReservePush)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_MathVectorAllocReservePushRandSizes(benchmark::State &state) {
  Vector<size_t> sizes{};
  for (size_t i = 0; i < 512;) sizes.push_back(++i);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(sizes.begin(), sizes.end(), gen);
  for (auto b : state) {
    for (size_t len : sizes) {
      Vector<size_t> v;
      pushVectorReserve(v, len);
    }
  }
}
BENCHMARK(BM_MathVectorAllocReservePushRandSizes);

void BM_MathVectorAllocReserveFillRandSizes(benchmark::State &state) {
  Vector<size_t> sizes{};
  for (size_t i = 0; i < 512;) sizes.push_back(++i);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(sizes.begin(), sizes.end(), gen);
  for (auto b : state) {
    for (size_t len : sizes) {
      Vector<size_t> v;
      fillVectorReserve(v, len);
    }
  }
}
BENCHMARK(BM_MathVectorAllocReserveFillRandSizes);

void fillRandMath(const Vector<size_t> &sizes) {
  Vector<Vector<size_t>> vofvs;
  for (size_t len : sizes) {
    Vector<size_t> v;
    fillVectorReserve(v, len);
    if ((len % 32) == 0) vofvs.push_back(std::move(v));
  }
}
void fillRandMath0(const Vector<size_t> &sizes) {
  Vector<Vector<size_t, 0>> vofvs;
  for (size_t len : sizes) {
    Vector<size_t, 0> v;
    fillVectorReserve(v, len);
    if ((len % 32) == 0) vofvs.push_back(std::move(v));
  }
}
void fillRandStd(const Vector<size_t> &sizes) {
  std::vector<std::vector<size_t>> vofvs;
  for (size_t len : sizes) {
    std::vector<size_t> v;
    fillVectorReserve(v, len);
    if ((len % 32) == 0) vofvs.push_back(std::move(v));
  }
}
void BM_MathVectorAllocReserveFillRandSizesRandLife(benchmark::State &state) {
  Vector<size_t> sizes{};
  for (size_t i = 0; i < 512;) sizes.push_back(++i);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(sizes.begin(), sizes.end(), gen);
  for (auto b : state) fillRandMath(sizes);
}
BENCHMARK(BM_MathVectorAllocReserveFillRandSizesRandLife);
void BM_Math0VectorAllocReserveFillRandSizesRandLife(benchmark::State &state) {
  Vector<size_t> sizes{};
  for (size_t i = 0; i < 512;) sizes.push_back(++i);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(sizes.begin(), sizes.end(), gen);
  for (auto b : state) fillRandMath0(sizes);
}
BENCHMARK(BM_Math0VectorAllocReserveFillRandSizesRandLife);

void BM_StdVectorAllocReserveFillRandSizesRandLife(benchmark::State &state) {
  Vector<size_t> sizes{};
  for (size_t i = 0; i < 512;) sizes.push_back(++i);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(sizes.begin(), sizes.end(), gen);
  for (auto b : state) fillRandStd(sizes);
}
BENCHMARK(BM_StdVectorAllocReserveFillRandSizesRandLife);

void BM_VectorRandSum(benchmark::State &state) {
  double p = double(state.range(0)) / 100.0;
  std::random_device rd;
  std::mt19937 gen(rd());
  for (auto b : state)
    for (int i = 0; i < 1000; ++i) randVecFillSum(gen, p);
}
BENCHMARK(BM_VectorRandSum)->DenseRange(95, 100, 1);

void BM_VectorRandSumStd(benchmark::State &state) {
  double p = double(state.range(0)) / 100.0;
  std::random_device rd;
  std::mt19937 gen(rd());
  for (auto b : state)
    for (int i = 0; i < 1000; ++i) randStdVecFillSum(gen, p);
}
BENCHMARK(BM_VectorRandSumStd)->DenseRange(95, 100, 1);
