#pragma once
#include "./NormalForm.hpp"
#include "./Rational.hpp"
#include "./VectorGreatestCommonDivisor.hpp"
#include "Math/Math.hpp"
#include <cstdint>

[[nodiscard]] constexpr auto orthogonalize(DenseMatrix<int64_t> A)
  -> DenseMatrix<int64_t> {
  if ((A.numCol() < 2) || (A.numRow() == 0)) return A;
  normalizeByGCD(A(0, _));
  if (A.numRow() == 1) return A;
  Vector<Rational, 8> buff;
  buff.resizeForOverwrite(size_t(A.numCol()));
  for (size_t i = 1; i < A.numRow(); ++i) {
    for (size_t j = 0; j < A.numCol(); ++j) buff[j] = A(i, j);
    for (size_t j = 0; j < i; ++j) {
      int64_t n = 0;
      int64_t d = 0;
      for (size_t k = 0; k < A.numCol(); ++k) {
        n += A(i, k) * A(j, k);
        d += A(j, k) * A(j, k);
      }
      for (size_t k = 0; k < A.numCol(); ++k)
        buff[k] -= Rational::createPositiveDenominator(A(j, k) * n, d);
    }
    int64_t lm = 1;
    for (size_t k = 0; k < A.numCol(); ++k) lm = lcm(lm, buff[k].denominator);
    for (size_t k = 0; k < A.numCol(); ++k)
      A(i, k) = buff[k].numerator * (lm / buff[k].denominator);
  }
  return A;
}

[[nodiscard]] constexpr auto orthogonalNullSpace(DenseMatrix<int64_t> A)
  -> DenseMatrix<int64_t> {
  return orthogonalize(NormalForm::nullSpace(std::move(A)));
}
