#pragma once

#include <Containers/TinyVector.hpp>
#include <Math/Array.hpp>
#include <Math/Dual.hpp>
#include <Math/LinearAlgebra.hpp>
#include <Math/Matrix.hpp>
#include <Math/StaticArrays.hpp>
#include <Utilities/Invariant.hpp>
#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <concepts>
#include <cstdint>
#include <random>

using poly::math::Dual, poly::math::Vector, poly::containers::TinyVector,
  poly::math::SquareMatrix, poly::math::AbstractMatrix, poly::math::SquareDims,
  poly::math::I, poly::utils::eltype_t, poly::utils::invariant;

// auto x = Dual<Dual<double, 4>, 2>{1.0};
// auto y = x * 3.4;

static_assert(std::convertible_to<int, Dual<double, 4>>);
static_assert(std::convertible_to<int, Dual<Dual<double, 4>, 2>>);

template <class T> struct URand {};

template <class T, ptrdiff_t N> struct URand<Dual<T, N>> {
  auto operator()(std::mt19937_64 &rng) -> Dual<T, N> {
    Dual<T, N> x{URand<T>{}(rng)};
    for (size_t i = 0; i < N; ++i) x.gradient()[i] = URand<T>{}(rng);
    return x;
  }
};
template <> struct URand<double> {
  auto operator()(std::mt19937_64 &rng) -> double {
    return std::uniform_real_distribution<double>(-2, 2)(rng);
  }
};

constexpr auto extractDualValRecurse(const auto &x) { return x; }
template <class T, size_t N>
constexpr auto extractDualValRecurse(const Dual<T, N> &x) {
  return extractDualValRecurse(x.value());
  // return x.value();
}

template <AbstractMatrix T> constexpr auto evalpoly(const T &C, const auto &p) {
  using U = eltype_t<T>;
  using S = SquareMatrix<U>;
  assert(C.numRow() == C.numCol());
  S A{SquareDims{C.numRow()}}, B{SquareDims{C.numRow()}};
  B << p[0] * C + I * p[1];
  for (size_t i = 2; i < p.size(); ++i) {
    std::swap(A, B);
    B << A * C + p[i] * I;
  }
  return B;
}
template <AbstractMatrix T>
constexpr void evalpoly(T &B, const T &C, const auto &p) {
  using U = eltype_t<T>;
  using S = SquareMatrix<U>;
  size_t N = p.size();
  invariant(N > 0);
  invariant(size_t(C.numRow()), size_t(C.numCol()));
  invariant(size_t(B.numRow()), size_t(B.numCol()));
  invariant(size_t(B.numRow()), size_t(C.numRow()));
  S A{SquareDims{B.numRow()}};
  B << p[0] * C + p[1] * I;
  for (size_t i = 2; i < N; ++i) {
    std::swap(A, B);
    B << A * C + p[i] * I;
  }
}

template <AbstractMatrix T> constexpr auto opnorm1(const T &A) {
  using S = decltype(extractDualValRecurse(std::declval<eltype_t<T>>()));
  size_t n = size_t(A.numRow());
  Vector<S> v;
  v.resizeForOverwrite(n);
  invariant(A.numRow() > 0);
  for (size_t j = 0; j < n; ++j)
    v[j] = std::abs(extractDualValRecurse(A[0, j]));
  for (size_t i = 1; i < n; ++i)
    for (size_t j = 0; j < n; ++j)
      v[j] += std::abs(extractDualValRecurse(A[i, j]));
  return *std::max_element(v.begin(), v.end());
}

template <AbstractMatrix T> constexpr auto expm(const T &A) {
  using S = eltype_t<T>;
  unsigned n = unsigned(A.numRow());
  auto nA = opnorm1(A);
  SquareMatrix<S> A2{A * A}, U{SquareDims{n}}, V{SquareDims{n}};
  unsigned int s = 0;
  if (nA <= 2.1) {
    TinyVector<double, 5> p0, p1;
    if (nA > 0.95) {
      p0 = {1.0, 3960.0, 2162160.0, 302702400.0, 8821612800.0};
      p1 = {90.0, 110880.0, 3.027024e7, 2.0756736e9, 1.76432256e10};
    } else if (nA > 0.25) {
      p0 = {1.0, 1512.0, 277200.0, 8.64864e6};
      p1 = {56.0, 25200.0, 1.99584e6, 1.729728e7};
    } else if (nA > 0.015) {
      p0 = {1.0, 420.0, 15120.0};
      p1 = {30.0, 3360.0, 30240.0};
    } else {
      p0 = {1.0, 60.0};
      p1 = {12.0, 120.0};
    }
    evalpoly(V, A2, p0);
    U << A * V;
    evalpoly(V, A2, p1);
  } else {
    s = std::max(int(std::ceil(std::log2(nA / 5.4))), 0);
    if (s > 0) {
      double t = 1.0 / std::exp2(s);
      A2 *= (t * t);
    }
    SquareMatrix<S> A4{A2 * A2};
    SquareMatrix<S> A6{A2 * A4};

    V << A6 * (A6 + 16380 * A4 + 40840800 * A2) +
           (33522128640 * A6 + 10559470521600 * A4 + 1187353796428800 * A2) +
           32382376266240000 * I;
    U << A * V;
    V << A6 * (182 * A6 + 960960 * A4 + 1323241920 * A2) +
           (670442572800 * A6 + 129060195264000 * A4 + 7771770303897600 * A2) +

           64764752532480000 * I;
  }
  for (auto a = A2.begin(), v = V.begin(), u = U.begin(), e = A2.end(); a != e;
       ++a, ++v, ++u) {
    *a = *v - *u;
    *v += *u;
  }
  // return (V - U) \ (V + U);
  poly::math::LU::fact(std::move(A2)).ldiv(poly::math::MutPtrMatrix<S>(V));
  for (; s--;) {
    U = V * V;
    std::swap(U, V);
  }
  return V;
}

auto expwork(const auto &A) {
  auto B = expm(A);
  decltype(B) C{A.dim()};
  for (size_t i = 0; i < 8; ++i) {
    C << A * exp2(-double(i));
    B += expm(C);
  }
  return B;
}
void expbench(const auto &A) {
  auto B{expwork(A)};
  for (auto &b : B) benchmark::DoNotOptimize(b);
}

static void BM_expm(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 rng0;
  SquareMatrix<double> A{SquareDims{dim}};
  for (auto &a : A) a = URand<double>{}(rng0);
  for (auto b : state) expbench(A);
}
BENCHMARK(BM_expm)->DenseRange(2, 10, 1);
static void BM_expm_dual4(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 rng0;
  using D = Dual<double, 4>;
  SquareMatrix<D> A{SquareDims{dim}};
  for (auto &a : A) a = URand<D>{}(rng0);
  for (auto b : state) expbench(A);
}
BENCHMARK(BM_expm_dual4)->DenseRange(2, 10, 1);

static void BM_expm_dual4x2(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 rng0;
  using D = Dual<Dual<double, 4>, 2>;
  SquareMatrix<D> A{SquareDims{dim}};
  for (auto &a : A) a = URand<D>{}(rng0);
  for (auto b : state) expbench(A);
}
BENCHMARK(BM_expm_dual4x2)->DenseRange(2, 10, 1);

using D4D2 = Dual<Dual<double, 4>, 2>;
using SMDD = SquareMatrix<D4D2>;
#ifdef __INTEL_LLVM_COMPILER
using SMDD0 = poly::math::ManagedArray<D4D2, SquareDims, 0>;
#else
using SMDD0 = poly::math::ManagedArray<D4D2, SquareDims>;
#endif
#pragma omp declare reduction(+ : SMDD0 : omp_out += omp_in)                   \
  initializer(omp_priv = SMDD0{omp_orig.dim(), D4D2{}})

static void BM_expm_dual4x2_threads(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 rng0;
  using D = Dual<Dual<double, 4>, 2>;
  SquareMatrix<D> A{SquareDims{dim}};
  for (auto &a : A) a = URand<D>{}(rng0);
  for (auto bch : state) {
    SMDD0 B{SquareDims{dim}};
    B.fill(D{0});
#pragma omp parallel for reduction(+ : B)
    for (int i = 0; i < 1000; ++i) B += expwork(A);
    for (auto &b : B) benchmark::DoNotOptimize(b);
  }
}
BENCHMARK(BM_expm_dual4x2_threads)->DenseRange(2, 10, 1);
