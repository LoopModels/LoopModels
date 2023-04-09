#pragma once
#include "Math/Array.hpp"
#include "Math/Vector.hpp"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <llvm/ADT/SmallVector.h>
#include <random>
#include <vector>

double randVecFillSum(std::mt19937 &gen, double p) {
  std::uniform_real_distribution<> dis(0, 1);
  size_t L = (dis(gen) < p) ? 10 : 10000;
  Vector<double> v(L);
  std::iota(v.begin(), v.end(), 1); // 1, 2, ..., L
  return v.sum();
}
double randStdVecFillSum(std::mt19937 &gen, double p) {
  std::uniform_real_distribution<> dis(0, 1);
  size_t L = (dis(gen) < p) ? 10 : 10000;
  std::vector<double> v(L);
  std::iota(v.begin(), v.end(), 1); // 1, 2, ..., L
  return std::reduce(v.begin(), v.end());
}

void fillVector(auto &v, size_t len) {
  v.clear();
  assert(v.size() == 0);
  for (size_t i = 0; i < len; ++i) v.push_back(i);
  benchmark::DoNotOptimize(v);
}

void BM_SmallVector8Fill(benchmark::State &state) {
  llvm::SmallVector<size_t, 8> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_SmallVector8Fill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVectorFill(benchmark::State &state) {
  llvm::SmallVector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_SmallVectorFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorFill(benchmark::State &state) {
  std::vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_StdVectorFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_BufferFill(benchmark::State &state) {
  Vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_BufferFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVector8AllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t, 8> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_SmallVector8AllocFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_SmallVectorAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_SmallVectorAllocFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_StdVectorAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    std::vector<size_t> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_StdVectorAllocFill)->RangeMultiplier(2)->Range(1, 1 << 8);
void BM_BufferAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    Vector<size_t> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_BufferAllocFill)->RangeMultiplier(2)->Range(1, 1 << 8);

void BM_VectorRandSum(benchmark::State &state) {
  double p = state.range(0) / 100.0;
  std::random_device rd;
  std::mt19937 gen(rd());
  for (auto b : state)
    for (int i = 0; i < 1000; ++i) randVecFillSum(gen, p);
}
BENCHMARK(BM_VectorRandSum)->DenseRange(95, 100, 1);
void BM_VectorRandSumStd(benchmark::State &state) {
  double p = state.range(0) / 100.0;
  std::random_device rd;
  std::mt19937 gen(rd());
  for (auto b : state)
    for (int i = 0; i < 1000; ++i) randStdVecFillSum(gen, p);
}
BENCHMARK(BM_VectorRandSumStd)->DenseRange(95, 100, 1);
