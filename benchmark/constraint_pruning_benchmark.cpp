#include "../include/AbstractEqualityPolyhedra.hpp"
#include "../include/NormalForm.hpp"
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

static void BM_NullSpace(benchmark::State &state) {

    IntMatrix B(4, 6);
    B(0, 0) = 1;
    B(0, 1) = 0;
    B(0, 2) = -3;
    B(0, 3) = 0;
    B(0, 4) = 2;
    B(0, 5) = -8;

    B(0, 0) = 0;
    B(0, 1) = 1;
    B(0, 2) = 5;
    B(0, 3) = 0;
    B(0, 4) = -1;
    B(0, 5) = 4;

    B(0, 0) = 0;
    B(0, 1) = 0;
    B(0, 2) = 0;
    B(0, 3) = 1;
    B(0, 4) = 7;
    B(0, 5) = -9;

    // fourth row is 0

    for (auto _ : state) {
        NormalForm::nullSpace(B);
    }
}
// Register the function as a benchmark
BENCHMARK(BM_NullSpace);

static void BM_NullSpace2000(benchmark::State &state) {
    const size_t N = 20;
    IntMatrix A(N, N);
    A(0, 0) = 2;
    for (size_t i = 1; i < N; ++i) {
        A(i - 1, i) = -1;
        A(i, i) = 2;
        A(i, i - 1) = -1;
    }
    for (size_t j = 0; j < N; j += 8) {
        // A(j,:)
        for (size_t i = 0; i < N; ++i) {
            A(j, i) = 0;
        }
        for (size_t i = 0; i < N; i += 7) {
            int64_t s = (i & 1) ? 1 : -1;
            for (size_t k = 0; k < N; ++k) {
                A(j, k) += s * A(i, k);
            }
        }
    }

    // fourth row is 0
    IntMatrix NS;
    for (auto _ : state) {
        NS = NormalForm::nullSpace(A);
    }
    // std::cout << "NS.size() = (" << NS.numRow() << ", " << NS.numCol() << ")"
    //           << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_NullSpace2000);

BENCHMARK_MAIN();
