#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>

static void BM_GCD_Big_Sparse(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});
    Polynomial::Monomial y = Polynomial::Monomial(Polynomial::ID{1});
    Polynomial::Monomial z = Polynomial::Monomial(Polynomial::ID{2});
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> xp1z = x * z + z;
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<int64_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> c2 = 2 * (2 - z);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    Polynomial::Multivariate<int64_t,Polynomial::Monomial> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q = p * (p + 1) * (p + 2) * (p + 3);
    for (auto _ : state)
	for (size_t i = 0; i < 4; ++i)
	    gcd(p+i, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Big_Sparse);

static void BM_GCD_Small_Sparse(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});
    Polynomial::Monomial y = Polynomial::Monomial(Polynomial::ID{1});

    Polynomial::Term<int64_t, Polynomial::Monomial> p = 2 * (x * y);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q = (2 * x) * y + x;

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Small_Sparse);

static void BM_GCD_Simp_Sparse(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});
    Polynomial::Monomial y = Polynomial::Monomial(Polynomial::ID{1});

    Polynomial::Multivariate<int64_t,Polynomial::Monomial> p = (x ^ 2) - (y ^ 2);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q = x + y;

    for (auto _ : state)
        Polynomial::divExact(p, gcd(p, q));
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Simp_Sparse);

static void BM_GCD_EqualMonomial_Sparse(benchmark::State &state) {

    Polynomial::Monomial x = Polynomial::Monomial(Polynomial::ID{0});

    Polynomial::Multivariate<int64_t,Polynomial::Monomial> p =
        Polynomial::Term<int64_t, Polynomial::Monomial>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q =
        Polynomial::Term<int64_t, Polynomial::Monomial>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualMonomial_Sparse);
static void BM_GCD_EqualConstants1_Sparse(benchmark::State &state) {

    Polynomial::Monomial x;

    Polynomial::Multivariate<int64_t,Polynomial::Monomial> p =
        Polynomial::Term<int64_t, Polynomial::Monomial>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q =
        Polynomial::Term<int64_t, Polynomial::Monomial>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants1_Sparse);
static void BM_GCD_EqualConstants2_Sparse(benchmark::State &state) {

    Polynomial::Monomial x;

    Polynomial::Multivariate<int64_t,Polynomial::Monomial> p =
        Polynomial::Term<int64_t, Polynomial::Monomial>(2, x);
    Polynomial::Multivariate<int64_t,Polynomial::Monomial> q =
        Polynomial::Term<int64_t, Polynomial::Monomial>(2, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants2_Sparse);


static void BM_GCD_Big_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x = Polynomial::PackedMonomial<31,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<31,7> y = Polynomial::PackedMonomial<31,7>(Polynomial::ID{1});
    Polynomial::PackedMonomial<31,7> z = Polynomial::PackedMonomial<31,7>(Polynomial::ID{2});
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> xp1z = x * z + z;
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<int64_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> c2 = 2 * (2 - z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q = p * (p + 1) * (p + 2) * (p + 3);
    for (auto _ : state)
	for (size_t i = 0; i < 4; ++i)
	    gcd(p+i, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Big_Packed31);

static void BM_GCD_Small_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x = Polynomial::PackedMonomial<31,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<31,7> y = Polynomial::PackedMonomial<31,7>(Polynomial::ID{1});

    Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>> p = 2 * (x * y);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q = (2 * x) * y + x;

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Small_Packed31);

static void BM_GCD_Simp_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x = Polynomial::PackedMonomial<31,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<31,7> y = Polynomial::PackedMonomial<31,7>(Polynomial::ID{1});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> p = (x ^ 2) - (y ^ 2);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q = x + y;

    for (auto _ : state)
        Polynomial::divExact(p, gcd(p, q));
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Simp_Packed31);

static void BM_GCD_EqualMonomial_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x = Polynomial::PackedMonomial<31,7>(Polynomial::ID{0});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualMonomial_Packed31);
static void BM_GCD_EqualConstants1_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants1_Packed31);
static void BM_GCD_EqualConstants2_Packed31(benchmark::State &state) {

    Polynomial::PackedMonomial<31,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(2, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<31,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<31,7>>(2, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants2_Packed31);


static void BM_GCD_Big_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x = Polynomial::PackedMonomial<15,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<15,7> y = Polynomial::PackedMonomial<15,7>(Polynomial::ID{1});
    Polynomial::PackedMonomial<15,7> z = Polynomial::PackedMonomial<15,7>(Polynomial::ID{2});
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> xp1z = x * z + z;
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<int64_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> c2 = 2 * (2 - z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q = p * (p + 1) * (p + 2) * (p + 3);
    for (auto _ : state)
	for (size_t i = 0; i < 4; ++i)
	    gcd(p+i, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Big_Packed15);

static void BM_GCD_Small_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x = Polynomial::PackedMonomial<15,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<15,7> y = Polynomial::PackedMonomial<15,7>(Polynomial::ID{1});

    Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>> p = 2 * (x * y);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q = (2 * x) * y + x;

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Small_Packed15);

static void BM_GCD_Simp_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x = Polynomial::PackedMonomial<15,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<15,7> y = Polynomial::PackedMonomial<15,7>(Polynomial::ID{1});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> p = (x ^ 2) - (y ^ 2);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q = x + y;

    for (auto _ : state)
        Polynomial::divExact(p, gcd(p, q));
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Simp_Packed15);

static void BM_GCD_EqualMonomial_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x = Polynomial::PackedMonomial<15,7>(Polynomial::ID{0});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualMonomial_Packed15);
static void BM_GCD_EqualConstants1_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants1_Packed15);
static void BM_GCD_EqualConstants2_Packed15(benchmark::State &state) {

    Polynomial::PackedMonomial<15,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(2, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<15,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<15,7>>(2, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants2_Packed15);


static void BM_GCD_Big_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x = Polynomial::PackedMonomial<7,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<7,7> y = Polynomial::PackedMonomial<7,7>(Polynomial::ID{1});
    Polynomial::PackedMonomial<7,7> z = Polynomial::PackedMonomial<7,7>(Polynomial::ID{2});
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> xp1z = x * z + z;
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> c0v2 = 10 * xp1z;

    // Polynomial::Multivariate<int64_t> c0 = 10*(x*z + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> c0 = 10 * (x * z + x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> c1 = 2 * ((x ^ 2) + z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> c2 = 2 * (2 - z);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> c3 = 20 * (x * (z ^ 2));

    int64_t e0 = 0;
    int64_t e1 = 5;
    int64_t e2 = 7;
    int64_t e3 = 10;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> p =
        c0 * (y ^ e0) + c1 * (y ^ e1) + c2 * (y ^ e2) + c3 * (y ^ e3);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q = p * (p + 1) * (p + 2) * (p + 3);
    for (auto _ : state)
	for (size_t i = 0; i < 4; ++i)
	    gcd(p+i, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Big_Packed7);

static void BM_GCD_Small_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x = Polynomial::PackedMonomial<7,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<7,7> y = Polynomial::PackedMonomial<7,7>(Polynomial::ID{1});

    Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>> p = 2 * (x * y);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q = (2 * x) * y + x;

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Small_Packed7);

static void BM_GCD_Simp_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x = Polynomial::PackedMonomial<7,7>(Polynomial::ID{0});
    Polynomial::PackedMonomial<7,7> y = Polynomial::PackedMonomial<7,7>(Polynomial::ID{1});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> p = (x ^ 2) - (y ^ 2);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q = x + y;

    for (auto _ : state)
        Polynomial::divExact(p, gcd(p, q));
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_Simp_Packed7);

static void BM_GCD_EqualMonomial_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x = Polynomial::PackedMonomial<7,7>(Polynomial::ID{0});

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualMonomial_Packed7);
static void BM_GCD_EqualConstants1_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(1, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(1, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants1_Packed7);
static void BM_GCD_EqualConstants2_Packed7(benchmark::State &state) {

    Polynomial::PackedMonomial<7,7> x;

    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> p =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(2, x);
    Polynomial::Multivariate<int64_t,Polynomial::PackedMonomial<7,7>> q =
        Polynomial::Term<int64_t, Polynomial::PackedMonomial<7,7>>(2, x);

    for (auto _ : state)
        gcd(p, q);
}
// Register the function as a benchmark
BENCHMARK(BM_GCD_EqualConstants2_Packed7);

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
