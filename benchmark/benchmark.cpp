#include "../include/math.hpp"
#include "../include/symbolics.hpp"
#include <benchmark/benchmark.h>

static void BM_GCD_Big(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::MonomialID(0);
    Polynomial::Monomial y = Polynomial::MonomialID(1);
    Polynomial::Monomial z = Polynomial::MonomialID(2);
    Polynomial::Multivariate<intptr_t> xp1z = x * z + z;
    Polynomial::Multivariate<intptr_t> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<intptr_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<intptr_t> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<intptr_t> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<intptr_t> c2 = 2 * (2 - z);
    Polynomial::Multivariate<intptr_t> c3 = 20 * (x * (z ^ 2));

    intptr_t e0 = 0;
    intptr_t e1 = 5;
    intptr_t e2 = 7;
    intptr_t e3 = 10;

    Polynomial::Multivariate<intptr_t> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    Polynomial::Multivariate<intptr_t> q = p * (p + 1) * (p + 2) * (p + 3);
    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Big);

static void BM_GCD_Small(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::MonomialID(0);
    Polynomial::Monomial y = Polynomial::MonomialID(1);

    Polynomial::Term<intptr_t, Polynomial::Monomial> p = 2 * (x * y);
    Polynomial::Multivariate<intptr_t> q = (2 * x) * y + x;

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Small);

static void BM_GCD_Simp(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::MonomialID(0);
    Polynomial::Monomial y = Polynomial::MonomialID(1);

    Polynomial::Multivariate<intptr_t> p = (x^2) - (y^2);
    Polynomial::Multivariate<intptr_t> q = x + y;

    for (auto _ : state)
        Polynomial::divExact(p, gcd(p, q));
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Simp);

/*
// Define another benchmark
static void BM_StringCopy(benchmark::State& state) {
  std::string x = "hello";
  for (auto _ : state)
    std::string copy(x);
}
BENCHMARK(BM_StringCopy);
*/
BENCHMARK_MAIN();
