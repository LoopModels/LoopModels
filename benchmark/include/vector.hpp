#pragma once
#include "Utilities/StackMeMaybe.hpp"
#include <absl/container/inlined_vector.h>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <llvm/ADT/SmallVector.h>
#include <vector>

void fillVector(auto &v, size_t len) {
  v.resize(0);
  for (size_t i = 0; i < len; ++i) v.push_back(i);
  benchmark::DoNotOptimize(v);
}

void BM_InlinedVectorFill(benchmark::State &state) {
  absl::InlinedVector<size_t, 8> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_InlinedVectorFill)->Range(1, 1 << 8);

void BM_SmallVector8Fill(benchmark::State &state) {
  llvm::SmallVector<size_t, 8> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_SmallVector8Fill)->Range(1, 1 << 8);

void BM_SmallVectorFill(benchmark::State &state) {
  llvm::SmallVector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_SmallVectorFill)->Range(1, 1 << 8);

void BM_StdVectorFill(benchmark::State &state) {
  std::vector<size_t> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_StdVectorFill)->Range(1, 1 << 8);

void BM_BufferFill(benchmark::State &state) {
  Buffer<size_t, 8, unsigned, std::allocator<size_t>> v;
  size_t len = state.range(0);
  for (auto b : state) fillVector(v, len);
}
BENCHMARK(BM_BufferFill)->Range(1, 1 << 8);

void BM_InlinedVectorAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    absl::InlinedVector<size_t, 8> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_InlinedVectorAllocFill)->Range(1, 1 << 8);

void BM_SmallVector8AllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t, 8> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_SmallVector8AllocFill)->Range(1, 1 << 8);

void BM_SmallVectorAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    llvm::SmallVector<size_t> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_SmallVectorAllocFill)->Range(1, 1 << 8);

void BM_StdVectorAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    std::vector<size_t> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_StdVectorAllocFill)->Range(1, 1 << 8);

void BM_BufferAllocFill(benchmark::State &state) {
  size_t len = state.range(0);
  for (auto b : state) {
    Buffer<size_t, 8, unsigned, std::allocator<size_t>> v;
    fillVector(v, len);
  }
}
BENCHMARK(BM_BufferAllocFill)->Range(1, 1 << 8);
