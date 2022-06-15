#include "../include/AbstractEqualityPolyhedra.hpp"
#include "../include/Polyhedra.hpp"
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>

static void BM_Constraint_Elim(benchmark::State &state) {

    IntMatrix A(12, 17), Ac(12, 17);
    llvm::SmallVector<int64_t, 8> b(12), bc(12);
    IntMatrix E(7, 17), Ec(7, 17);
    llvm::SmallVector<int64_t, 8> q(7), qc(7);
    for (size_t i = 0; i < 12; ++i) {
        A(i, i + 5) = -1;
    }
    E(0, 4) = -1;
    E(0, 5) = -1;
    E(0, 8) = 1;
    E(0, 9) = -2;
    E(0, 12) = -2;
    E(0, 15) = 1;
    E(0, 16) = -1;
    q[0] = 0;

    E(1, 0) = 1;
    E(1, 9) = -1;
    E(1, 10) = 1;
    E(1, 13) = 1;
    E(1, 14) = -1;
    q[1] = 0;

    E(2, 1) = 1;
    E(2, 11) = 1;
    E(2, 15) = 1;
    E(2, 16) = -1;
    q[2] = 0;

    E(3, 2) = -1;
    E(3, 13) = -1;
    E(3, 14) = 1;
    q[3] = 0;

    E(4, 3) = -1;
    E(4, 12) = -1;
    E(4, 15) = -1;
    E(4, 16) = 1;
    q[4] = 0;

    E(5, 6) = -1;
    E(5, 9) = 1;
    q[5] = 0;

    E(6, 7) = -1;
    E(6, 12) = 1;
    q[6] = 0;

    // IntegerEqPolyhedra ipoly(A, b, E, q);

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
