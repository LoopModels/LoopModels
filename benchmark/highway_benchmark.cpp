// #include "../include/NormalForm.hpp"
// #include "../include/Polyhedra.hpp"
// #include "Orthogonalize.hpp"
#include "llvm/ADT/SmallVector.h"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>


#undef HWY_TARGET_INCLUDE
// #define HWY_TARGET_INCLUDE "/home/junpeng/Desktop/projects/LoopModels/benchmark/highway_benchmark.cpp"
#define HWY_TARGET_INCLUDE "benchmark/highway_benchmark.cpp"
// #define HWY_TARGET_INCLUDE "./highway_benchmark.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
#include "../include/Math.hpp"

HWY_BEFORE_NAMESPACE();
namespace HWY_NAMESPACE {

static void BM_LoopHighway(benchmark::State &state) {

    // const size_t n = 1600;
    Vector<int64_t> x(1263);
    Vector<int64_t> y(1263);
    int64_t a = 2;
    int64_t b = 3;
    for (size_t i = 0; i < x.size(); ++i)
        y(2) = 2;
    // fourth row is 0
    // std::cout << "B=\n" << B << "\nnullSpace(B) =\n" <<
    // NormalForm::nullSpace(B) << std::endl;
    for (auto _ : state) {
        for (size_t i = 0; i < x.size(); ++i)
            // x[i] = a * x[i] - 4;//- b * y[i];
            x(i) *= (a * y(i) - 4);
    }
    // std::cout << "x[0] = " << x(0) << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_LoopHighway);

static void BM_LoopHighway2(benchmark::State &state) {

    // const size_t n = 1600;
    Vector<int64_t> x(1263);
    Vector<int64_t> y(1263);
    // y = 6;
    // std::cout << y(2) << std::endl;
    for (size_t i = 0; i < x.size(); ++i)
        y(2) = 2;
    // std::cout << y(2) << std::endl;
    // for (size_t i = 0; i < n; ++i) {
    //     x(i) = 1;
    //     y(i) = 2;
    // }
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
        // x = a * x - b * y
        x *= (a * y - 4);
    }
    // std::cout << x(2) << std::endl;
    // for (auto _ : state) {
    //     x = x * a - y * b;
    // }
    // std::cout << "x[0] = " << x(0) << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_LoopHighway2);
}
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
BENCHMARK_MAIN();
#endif