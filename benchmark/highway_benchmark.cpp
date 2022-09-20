// #include "../include/NormalForm.hpp"
#include "../include/Math.hpp"
// #include "../include/Polyhedra.hpp"
// #include "Orthogonalize.hpp"
#include "llvm/ADT/SmallVector.h"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>

static void BM_LoopHighway(benchmark::State &state) {

    const size_t n = 1600;
    Vector<int64_t> x(n);
    Vector<int64_t> y(n);
    int64_t a = 2;
    int64_t b = 3;
    // fourth row is 0
    // std::cout << "B=\n" << B << "\nnullSpace(B) =\n" <<
    // NormalForm::nullSpace(B) << std::endl;
    for (auto _ : state) {
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = a * x[i] - b * y[i];
    }
    // std::cout << "x[0] = " << x(0) << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_LoopHighway);

static void BM_LoopHighway2(benchmark::State &state) {

    const size_t n = 1600;
    Vector<int64_t> x(n);
    Vector<int64_t> y(n);
    // Vector<int64_t> x(63);
    // Vector<int64_t> y(63);
    int64_t a = 2;
    int64_t b = 3;
    // int a = 2;
    // int b = 3;
    // fourth row is 0
    // std::cout << "B=\n" << B << "\nnullSpace(B) =\n" <<
    // NormalForm::nullSpace(B) << std::endl;
    // x = a * y;
    // x = y * a;
    for (auto _ : state) {
        x = a * x - b * y;
    }
    // for (auto _ : state) {
    //     x = x * a - y * b;
    // }
    // std::cout << "x[0] = " << x(0) << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_LoopHighway2);
BENCHMARK_MAIN();