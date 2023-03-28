#pragma once

#include "Containers/TinyVector.hpp"
#include "Math/Array.hpp"
#include "Math/LinearAlgebra.hpp"
#include "Math/Math.hpp"
#include "Math/Matrix.hpp"
#include "Math/StaticArrays.hpp"
#include <algorithm>
#include <array>
#include <benchmark/benchmark.h>
#include <llvm/ADT/TinyPtrVector.h>
#include <random>

template <class T, size_t N> class Dual {
  T val{};
  SVector<T, N> partials{};

public:
  constexpr Dual() = default;
  constexpr Dual(T v) : val(v), partials() {}
  constexpr Dual(T v, SVector<T, N> g) : val(v), partials(g) {}
  constexpr Dual(const std::initializer_list<T> &list) {
    invariant(list.size(), N + 1);
    auto b = list.begin();
    val = *b;
    std::copy(++b, list.end(), partials.data());
  }

  constexpr auto value() -> T & { return val; }
  constexpr auto gradient() -> SVector<T, N> & { return partials; }
  constexpr auto value() const -> const T & { return val; }
  constexpr auto gradient() const -> const SVector<T, N> & { return partials; }
  // constexpr auto operator[](size_t i) const -> T { return grad[i]; }
  // constexpr auto operator[](size_t i) -> T & { return grad[i]; }
  constexpr auto operator-() const -> Dual { return Dual(-val, -partials); }
  constexpr auto operator+(const Dual &other) const -> Dual {
    return Dual(val + other.val, partials + other.partials);
  }
  constexpr auto operator-(const Dual &other) const -> Dual {
    return Dual(val - other.val, partials - other.partials);
  }
  constexpr auto operator*(const Dual &other) const -> Dual {
    return Dual(val * other.val, val * other.partials + other.val * partials);
  }
  constexpr auto operator/(const Dual &other) const -> Dual {
    return Dual(val / other.val, (other.val * partials - val * other.partials) /
                                   (other.val * other.val));
  }
  constexpr auto operator+=(const Dual &other) -> Dual & {
    val += other.val;
    partials += other.partials;
    return *this;
  }
  constexpr auto operator-=(const Dual &other) -> Dual & {
    val -= other.val;
    partials -= other.partials;
    return *this;
  }
  constexpr auto operator*=(const Dual &other) -> Dual & {
    val *= other.val;
    partials = val * other.partials + other.val * partials;
    return *this;
  }
  constexpr auto operator/=(const Dual &other) -> Dual & {
    val /= other.val;
    partials =
      (other.val * partials - val * other.partials) / (other.val * other.val);
    return *this;
  }
  constexpr auto operator+(T other) const -> Dual {
    return Dual(val + other, partials);
  }
  constexpr auto operator-(T other) const -> Dual {
    return Dual(val - other, partials);
  }
  constexpr auto operator*(T other) const -> Dual {
    return Dual(val * other, other * partials);
  }
  constexpr auto operator/(T other) const -> Dual {
    return Dual(val / other, partials / other);
  }
  constexpr auto operator+=(T other) -> Dual & {
    val += other;
    return *this;
  }
  constexpr auto operator-=(T other) -> Dual & {
    val -= other;
    return *this;
  }
  constexpr auto operator*=(T other) -> Dual & {
    val *= other;
    partials *= other;
    return *this;
  }
  constexpr auto operator/=(T other) -> Dual & {
    val /= other;
    partials /= other;
    return *this;
  }
  constexpr auto operator==(const Dual &other) const -> bool {
    return val == other.val; // && grad == other.grad;
  }
  constexpr auto operator!=(const Dual &other) const -> bool {
    return val != other.val; // || grad != other.grad;
  }
  constexpr auto operator==(T other) const -> bool { return val == other; }
  constexpr auto operator!=(T other) const -> bool { return val != other; }
  constexpr auto operator<(T other) const -> bool { return val < other; }
  constexpr auto operator>(T other) const -> bool { return val > other; }
  constexpr auto operator<=(T other) const -> bool { return val <= other; }
  constexpr auto operator>=(T other) const -> bool { return val >= other; }
  constexpr auto operator<(const Dual &other) const -> bool {
    return val < other.val;
  }
  constexpr auto operator>(const Dual &other) const -> bool {
    return val > other.val;
  }
  constexpr auto operator<=(const Dual &other) const -> bool {
    return val <= other.val;
  }
  constexpr auto operator>=(const Dual &other) const -> bool {
    return val >= other.val;
  }
};

constexpr auto extractDualValRecurse(const auto &x) { return x; }
template <class T, size_t N>
constexpr auto extractDualValRecurse(const Dual<T, N> &x) {
  return extractDualValRecurse(x.value());
  // return x.value();
}

auto x = Dual<double, 4>{1.0, {1.0, 2.0, 3.0, 4.0}};
static_assert(
  std::same_as<decltype(extractDualValRecurse(Dual<double, 4>{})), double>);

template <AbstractMatrix T> constexpr auto evalpoly(const T &C, const auto &p) {
  using U = eltype_t<T>;
  using S = SquareMatrix<U>;
  assert(C.numRow() == C.numCol());
  S A{SquareDims{C.numRow()}}, B{SquareDims{C.numRow()}};
  B << p[0] * C + I * p[1];
  for (size_t i = 2; i < p.size(); ++i) {
    std::swap(A, B);
    B << A * C + I * p[i];
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
  B << p[0] * C + I * p[1];
  for (size_t i = 2; i < N; ++i) {
    std::swap(A, B);
    B << A * C + I * p[i];
  }
}

template <AbstractMatrix T> constexpr auto opnorm1(const T &A) {
  using S = decltype(extractDualValRecurse(std::declval<eltype_t<T>>()));
  size_t n = size_t(A.numRow());
  Vector<S> v;
  v.resizeForOverwrite(n);
  for (size_t j = 0; j < n; ++j)
    v[j] = std::abs(extractDualValRecurse(A(0, j)));
  for (size_t i = 1; i < n; ++i)
    for (size_t j = 0; j < n; ++j)
      v[j] += std::abs(extractDualValRecurse(A(i, j)));
  return *std::max_element(v.begin(), v.end());
}

template <AbstractMatrix T> constexpr auto expm(const T &A) {
  using S = eltype_t<T>;
  unsigned n = unsigned(A.numRow());
  S nA = opnorm1(A);
  SquareMatrix<S> A2{A * A}, U{SquareDims{n}}, V{SquareDims{n}};
  unsigned int s = 0;
  if (nA <= 2.1) {
    TinyVector<S, 5> p0, p1;
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
      S t = S(1) / std::exp2(s);
      A2 *= (t * t);
    }
    SquareMatrix<S> A4{A2 * A2};
    SquareMatrix<S> A6{A2 * A4};

    V << A6 * (A6 + S(16380) * A4 + S(40840800) * A2) +
           (S(33522128640) * A6 + S(10559470521600) * A4 +
            S(1187353796428800) * A2) +
           S(32382376266240000) * I;
    U << A * V;
    V << A6 * (S(182) * A6 + S(960960) * A4 + S(1323241920) * A2) +
           (S(670442572800) * A6 + S(129060195264000) * A4 +
            S(7771770303897600) * A2) +
           S(64764752532480000) * I;
  }
  for (auto a = A2.begin(), v = V.begin(), u = U.begin(), e = A2.end(); a != e;
       ++a, ++v, ++u) {
    *a = *v - *u;
    *v += *u;
  }
  // return (V - U) \ (V + U);
  LU::fact(std::move(A2)).ldiv(MutPtrMatrix<S>(V));
  for (; s--;) {
    U = V * V;
    std::swap(U, V);
  }
  return V;
}

static void BM_expm(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 mt(0);
  SquareMatrix<double> A{SquareDims{dim}};
  for (auto &a : A) a = std::uniform_real_distribution<double>(-1, 1)(mt);
  // llvm::errs() << "dim = " << dim << "; A = " << A << "\nexpm(A) = " <<
  // expm(A)
  //              << "\n";
  for (auto b : state) expm(A);
}
BENCHMARK(BM_expm)->DenseRange(2, 10, 1);
static void BM_expm_dual4(benchmark::State &state) {
  unsigned dim = state.range(0);
  std::mt19937_64 mt(0);
  SquareMatrix<Dual<double, 4>> A{SquareDims{dim}};
  for (auto &a : A) a = std::uniform_real_distribution<double>(-1, 1)(mt);
  // llvm::errs() << "dim = " << dim << "; A = " << A << "\nexpm(A) = " <<
  // expm(A)
  //              << "\n";
  for (auto b : state) expm(A);
}
BENCHMARK(BM_expm_dual4)->DenseRange(2, 10, 1);
