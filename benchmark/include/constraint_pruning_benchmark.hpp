#pragma once
#include <Math/NormalForm.hpp>
#include <Math/Orthogonalize.hpp>
#include <Utilities/MatrixStringParse.hpp>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

using poly::math::Vector,poly::math::IntMatrix, poly::math::Row, poly::math::Col,
  poly::math::DenseDims, poly::math::_, poly::utils::operator""_mat;
static void BM_NullSpace(benchmark::State &state) {

  IntMatrix<> B(poly::math::DenseDims{Row{6}, Col{3}});
  B[0, 0] = 1;
  B[1, 0] = 0;
  B[2, 0] = -3;
  B[3, 0] = 0;
  B[4, 0] = 2;
  B[5, 0] = -8;

  B[0, 1] = 0;
  B[1, 1] = 1;
  B[2, 1] = 5;
  B[3, 1] = 0;
  B[4, 1] = -1;
  B[5, 1] = 4;

  B[0, 2] = 0;
  B[1, 2] = 0;
  B[2, 2] = 0;
  B[3, 2] = 1;
  B[4, 2] = 7;
  B[5, 2] = -9;

  // fourth row is 0
  // std::cout << "B=\n" << B << "\nnullSpace(B) =\n" <<
  // NormalForm::nullSpace(B) << std::endl;
  poly::math::IntMatrix<> A;
  for (auto b : state) A = poly::math::NormalForm::nullSpace(B);
}
// Register the function as a benchmark
BENCHMARK(BM_NullSpace);

static void BM_NullSpace2000(benchmark::State &state) {
  const size_t N = 20;
  IntMatrix<> A(DenseDims{Row{N}, Col{N}});
  A << 0;
  A[0, 0] = 2;
  for (size_t i = 1; i < N; ++i) {
    A[i - 1, i] = -1;
    A[i, i] = 2;
    A[i, i - 1] = -1;
  }
  for (size_t j = 0; j < N; j += 8) {
    A[j, _] << 0;
    for (size_t i = 0; i < N; i += 7) A[j, _] += ((i & 1) ? 1 : -1) * A[i, _];
  }

  // fourth row is 0
  IntMatrix<> NS;
  for (auto b : state) NS = poly::math::NormalForm::nullSpace(A);
  // std::cout << "NS.size() = (" << NS.numRow() << ", " << NS.numCol() << ")"
  //           << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_NullSpace2000);

static void BM_Orthogonalize(benchmark::State &state) {
  IntMatrix<> A =
    "[-2 2 0 1 1 1 2; 3 -3 2 3 2 3 2; -3 0 2 3 -2 0 1; 2 1 0 -1 3 -1 1; 1 -3 -3 -2 2 -2 2; 0 0 1 2 -3 -2 -2; 0 -3 -2 -1 1 0 1]"_mat;
  IntMatrix<> B;
  for (auto b : state) B = orthogonalize(A);
}
BENCHMARK(BM_Orthogonalize);

static void BM_Bareiss2000(benchmark::State &state) {
  const size_t N = 20;
  IntMatrix<> A(DenseDims{Row{N}, Col{N}});
  A << 0;
  A[0, 0] = 2;
  for (size_t i = 1; i < N; ++i) {
    A[i - 1, i] = -1;
    A[i, i] = 2;
    A[i, i - 1] = -1;
  }
  for (size_t j = 0; j < N; j += 8) {
    A[j,_] << 0;
    for (size_t i = 0; i < N; i += 7) {
      int64_t s = (i & 1) ? 1 : -1;
      A[j,_] += s * A[i,_];
    }
  }
  // std::cout << A << std::endl;

  // fourth row is 0
  Vector<ptrdiff_t> pivots(N);
  IntMatrix<> B;
  for (auto b : state) {
    B = A;
    poly::math::NormalForm::bareiss(B, pivots);
  }
  // std::cout << "NS.size() = (" << NS.numRow() << ", " << NS.numCol() << ")"
  //           << std::endl;
}
// Register the function as a benchmark
BENCHMARK(BM_Bareiss2000);
