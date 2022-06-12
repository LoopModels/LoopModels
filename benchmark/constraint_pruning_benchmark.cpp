#include "../include/AbstractEqualityPolyhedra.hpp"
#include "../include/Polyhedra.hpp"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>

static void BM_Constraint_Elim(benchmark::State &state) {

    Matrix<int64_t, 0, 0, 0> A(17, 12), Ac(17, 12);
    llvm::SmallVector<int64_t, 8> b(12), bc(12);
    Matrix<int64_t, 0, 0, 0> E(17, 7), Ec(17, 7);
    llvm::SmallVector<int64_t, 8> q(7), qc(7);
    for (size_t i = 0; i < 12; ++i) {
        A(i + 5, i) = -1;
    }
    E(4, 0) = -1;
    E(5, 0) = -1;
    E(8, 0) = 1;
    E(9, 0) = -2;
    E(12, 0) = -2;
    E(15, 0) = 1;
    E(16, 0) = -1;
    q[0] = 0;

    E(0, 1) = 1;
    E(9, 1) = -1;
    E(10, 1) = 1;
    E(13, 1) = 1;
    E(14, 1) = -1;
    q[1] = 0;

    E(1, 2) = 1;
    E(11, 2) = 1;
    E(15, 2) = 1;
    E(16, 2) = -1;
    q[2] = 0;

    E(2, 3) = -1;
    E(13, 3) = -1;
    E(14, 3) = 1;
    q[3] = 0;

    E(3, 4) = -1;
    E(12, 4) = -1;
    E(15, 4) = -1;
    E(16, 4) = 1;
    q[4] = 0;

    E(6, 5) = -1;
    E(9, 5) = 1;
    q[5] = 0;

    E(7, 6) = -1;
    E(12, 6) = 1;
    q[6] = 0;

    IntegerEqPolyhedra ipoly(A, b, E, q);

    for (auto _ : state) {
        Ac = A;
        bc = b;
        Ec = E;
        qc = q;
        removeExtraVariables(Ac, bc, Ec, qc, 8);
    }
}
// Register the function as a benchmark
BENCHMARK(BM_Constraint_Elim);

BENCHMARK_MAIN();
