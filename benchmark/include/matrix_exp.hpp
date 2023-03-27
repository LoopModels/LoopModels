#pragma once

#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Math/Matrix.hpp"
#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <random>

template <AbstractMatrix T, size_t N>
constexpr auto evalpoly(const T &C, const std::array<double, N> &p) {
  using U = typename T::value_type;
  using S = SquareMatrix<U>;
  static_assert(N > 0);
  assert(C.numRow() == C.numCol());
  S A{C.numRow()}, B{SquareDims{C.numRow()}};
  A << I * p[0];
  for (size_t i = 1; i < p.size(); ++i) {
    B << A * C + LinAlg::UniformScaling(p[i]);
    std::swap(A, B);
  }
  return A;
}

template <AbstractMatrix T> constexpr auto expm(const T &A) {
  using U = typename T::value_type;
  size_t n = size_t(A.numRow());
  Vector<U> v{n};
  v << A(0, _);
  for (size_t j = 0; j < n; ++j) v[j] = std::abs(A(0, j));
  for (size_t i = 1; i < n; ++i)
    for (size_t j = 0; j < n; ++j) v[j] = std::abs(A(i, j));
  U nA = *std::max_element(v.begin(), v.end());
  if (nA <= 2.1) {
    SquareMatrix<U> A2{A * A};
    if (nA > 0.95) {
      std::array p0{1.0, 3960.0, 2162160.0, 302702400.0, 8821612800.0};
      SquareMatrix<U> V{evalpoly(A2, p0)};
      SquareMatrix<U> U{A * V};
      std::array p1{90.0, 110880.0, 3.027024e7, 2.0756736e9, 1.76432256e10};
      V << evalpoly(A2, p1);
    } else if (nA > 0.25) {
      std::array p0{1.0, 1512.0, 277200.0, 8.64864e6};
      SquareMatrix<U> V{evalpoly(A2, p0)};
      SquareMatrix<U> U{A * V};
      std::array p1{56.0, 25200.0, 1.99584e6, 1.729728e7};
      V << evalpoly(A2, p1);
    } else if (nA > 0.015) {
      std::array p0{1.0, 420.0, 15120.0};
      SquareMatrix<U> V{evalpoly(A2, p0)};
      SquareMatrix<U> U{A * V};
      std::array p1{30.0, 3360.0, 30240.0};
      V << evalpoly(A2, p1);
    } else {
      std::array p0{1.0, 60.0};
      SquareMatrix<U> V{evalpoly(A2, p0)};
      SquareMatrix<U> U{A * V};
      std::array p1{12.0, 120.0};
      V << evalpoly(A2, p1);
    }
    // return (V - U) \ (V + U);
  } else {
  }
}

static void BM_expm(benchmark::State &state) {
  size_t dim = state.range(0);
  std::mt19937_64 mt(0);
  SquareMatrix<double> A{dim};
  for (auto &a : A) a = std::uniform_real_distribution<double>(-1, 1)(mt);
  for (auto b : state) expm(A);
}
BENCHMARK(BM_expm)->DenseRange(2, 8, 1);
