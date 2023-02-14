#pragma once
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Constructors.hpp"
#include "Math/EmptyArrays.hpp"
#include "Math/GreatestCommonDivisor.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/VectorGreatestCommonDivisor.hpp"
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/SmallVector.h>
#include <map>
#include <memory>
#include <numeric>
#include <sys/types.h>
#include <utility>

namespace NormalForm {

constexpr auto gcdxScale(int64_t a, int64_t b)
  -> std::tuple<int64_t, int64_t, int64_t, int64_t> {
  if (constexpr_abs(a) == 1) return std::make_tuple(a, 0, a, b);
  auto [g, p, q] = gcdx(a, b);
  return std::make_tuple(p, q, a / g, b / g);
}
// zero out below diagonal
constexpr void zeroSupDiagonal(MutPtrMatrix<int64_t> A,
                               MutSquarePtrMatrix<int64_t> K, size_t i, Row M,
                               Col N) {
  size_t minMN = std::min(size_t(M), size_t(N));
  for (size_t j = i + 1; j < M; ++j) {
    int64_t Aii = A(i, i);
    if (int64_t Aji = A(j, i)) {
      const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aji);
      for (size_t k = 0; k < minMN; ++k) {
        int64_t Aki = A(i, k);
        int64_t Akj = A(j, k);
        int64_t Kki = K(i, k);
        int64_t Kkj = K(j, k);
        // when k == i, then
        // p * Aii + q * Akj == r, so we set A(i,i) = r
        A(i, k) = p * Aki + q * Akj;
        // Aii/r * Akj - Aij/r * Aki = 0
        A(j, k) = Aiir * Akj - Aijr * Aki;
        // Mirror for K
        K(i, k) = p * Kki + q * Kkj;
        K(j, k) = Aiir * Kkj - Aijr * Kki;
      }

      for (auto k = size_t(N); k < M; ++k) {
        int64_t Kki = K(i, k);
        int64_t Kkj = K(j, k);
        K(i, k) = p * Kki + q * Kkj;
        K(j, k) = Aiir * Kkj - Aijr * Kki;
      }

      for (auto k = size_t(M); k < N; ++k) {
        int64_t Aki = A(i, k);
        int64_t Akj = A(j, k);
        A(i, k) = p * Aki + q * Akj;
        A(j, k) = Aiir * Akj - Aijr * Aki;
      }
    }
  }
}
// This method is only called by orthogonalize, hence we can assume
// (Akk == 1) || (Akk == -1)
constexpr void zeroSubDiagonal(MutPtrMatrix<int64_t> A,
                               MutSquarePtrMatrix<int64_t> K, size_t k, Row M,
                               Col N) {
  int64_t Akk = A(k, k);
  if (Akk == -1) {
    for (size_t m = 0; m < N; ++m) A(k, m) *= -1;
    for (size_t m = 0; m < M; ++m) K(k, m) *= -1;
  } else {
    assert(Akk == 1);
  }
  size_t minMN = std::min(size_t(M), size_t(N));
  for (size_t z = 0; z < k; ++z) {
    // eliminate `A(k,z)`
    if (int64_t Akz = A(z, k)) {
      // A(k, k) == 1, so A(k,z) -= Akz * 1;
      // A(z,_) -= Akz * A(k,_);
      // K(z,_) -= Akz * K(k,_);

      for (size_t i = 0; i < minMN; ++i) {
        A(z, i) -= Akz * A(k, i);
        K(z, i) -= Akz * K(k, i);
      }

      for (auto i = size_t(N); i < M; ++i) K(z, i) -= Akz * K(k, i);

      for (auto i = size_t(M); i < N; ++i) A(z, i) -= Akz * A(k, i);
    }
  }
}

constexpr auto pivotRows(std::array<MutPtrMatrix<int64_t>, 2> AK, Col i, Row M,
                         Row piv) -> bool {
  Row j = piv;
  while (AK[0](piv, i) == 0)
    if (++piv == M) return true;
  if (j != piv) {
    swap(AK[0], j, piv);
    swap(AK[1], j, piv);
  }
  return false;
}
constexpr auto pivotRows(MutPtrMatrix<int64_t> A, MutSquarePtrMatrix<int64_t> K,
                         size_t i, Row M) -> bool {
  MutPtrMatrix<int64_t> B = K;
  return pivotRows({A, B}, Col{i}, M, Row{i});
}
constexpr auto pivotRows(MutPtrMatrix<int64_t> A, Col i, Row M, Row piv)
  -> bool {
  Row j = piv;
  while (A(piv, i) == 0)
    if (++piv == size_t(M)) return true;
  if (j != piv) swap(A, j, piv);
  return false;
}
constexpr auto pivotRows(MutPtrMatrix<int64_t> A, size_t i, Row N) -> bool {
  return pivotRows(A, Col{i}, N, Row{i});
}

constexpr void dropCol(MutPtrMatrix<int64_t> A, size_t i, Row M, Col N) {
  // if any rows are left, we shift them up to replace it
  if (N <= i) return;
  for (size_t m = 0; m < M; ++m)

    for (size_t n = i; n < N; ++n) A(m, n) = A(m, n + 1);
}

constexpr auto orthogonalizeBang(MutPtrMatrix<int64_t> A)
  -> std::pair<SquareMatrix<int64_t>, Vector<unsigned>> {
  // we try to orthogonalize with respect to as many rows of `A` as we can
  // prioritizing earlier rows.
  auto [M, N] = A.size();
  SquareMatrix<int64_t> K{identity(std::allocator<int64_t>{}, unsigned(M))};
  Vector<unsigned> included;
  included.reserve(std::min(size_t(M), size_t(N)));
  for (size_t i = 0, j = 0; i < std::min(size_t(M), size_t(N)); ++j) {
    // zero ith row
    if (pivotRows(A, K, i, M)) {
      // cannot pivot, this is a linear combination of previous
      // therefore, we drop the row
      dropCol(A, i, M, --N);
    } else {
      zeroSupDiagonal(A, K, i, M, N);
      int64_t Aii = A(i, i);
      if (constexpr_abs(Aii) != 1) {
        // including this row renders the matrix not unimodular!
        // therefore, we drop the row.
        dropCol(A, i, M, --N);
      } else {
        // we zero the sub diagonal
        zeroSubDiagonal(A, K, i++, M, N);
        included.push_back(j);
      }
    }
  }
  return std::make_pair(std::move(K), std::move(included));
}
constexpr auto orthogonalize(IntMatrix A)
  -> std::pair<SquareMatrix<int64_t>, Vector<unsigned>> {
  return orthogonalizeBang(A);
}

constexpr void zeroSupDiagonal(MutPtrMatrix<int64_t> A, Col r, Row c) {
  auto [M, N] = A.size();
  for (Row j = c + 1; j < M; ++j) {
    int64_t Aii = A(c, r);
    if (int64_t Aij = A(j, r)) {
      const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
      for (Col k = 0; k < N; ++k) {
        int64_t Aki = A(c, k);
        int64_t Akj = A(j, k);
        A(c, k) = p * Aki + q * Akj;
        A(j, k) = Aiir * Akj - Aijr * Aki;
      }
    }
  }
}
constexpr void
zeroSupDiagonal(std::pair<MutPtrMatrix<int64_t>, MutPtrMatrix<int64_t>> AB,
                Col r, Row c) {
  auto [A, B] = AB;
  auto [M, N] = A.size();
  const Col K = B.numCol();
  assert(M == B.numRow());
  for (Row j = c + 1; j < M; ++j) {
    int64_t Aii = A(c, r);
    if (int64_t Aij = A(j, r)) {
      const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
      for (Col k = 0; k < N; ++k) {
        int64_t Ack = A(c, k);
        int64_t Ajk = A(j, k);
        A(c, k) = p * Ack + q * Ajk;
        A(j, k) = Aiir * Ajk - Aijr * Ack;
      }
      for (Col k = 0; k < K; ++k) {
        int64_t Bck = B(c, k);
        int64_t Bjk = B(j, k);
        B(c, k) = p * Bck + q * Bjk;
        B(j, k) = Aiir * Bjk - Aijr * Bck;
      }
    }
  }
}
constexpr void reduceSubDiagonal(MutPtrMatrix<int64_t> A, Col r, Row c) {
  int64_t Akk = A(c, r);
  if (Akk < 0) {
    Akk = -Akk;
    A(c, _) *= -1;
  }
  for (size_t z = 0; z < c; ++z) {
    // try to eliminate `A(k,z)`
    // if Akk == 1, then this zeros out Akz
    if (int64_t Azr = A(z, r)) {
      // we want positive but smaller subdiagonals
      // e.g., `Akz = 5, Akk = 2`, then in the loop below when `i=k`, we
      // set A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
      //        =   5 - 2*2 = 1
      // or if `Akz = -5, Akk = 2`, then in the loop below we get
      // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
      //        =  -5 - (-2 - 1)*2 = = 6 - 5 = 1
      // if `Akk = 1`, then
      // A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
      //        = A(k,z) - A(k,z) = 0
      // or if `Akz = -7, Akk = 39`, then in the loop below we get
      // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
      //        =  -7 - ((-7/39) - 1)*39 = = 6 - 5 = 1
      int64_t AzrOld = Azr;
      Azr /= Akk;
      if (AzrOld < 0) Azr -= (AzrOld != (Azr * Akk));
      A(z, _) -= Azr * A(c, _);
    }
  }
}
constexpr void reduceSubDiagonalStack(MutPtrMatrix<int64_t> A,
                                      MutPtrMatrix<int64_t> B, size_t r,
                                      size_t c) {
  int64_t Akk = A(c, r);
  if (Akk < 0) {
    Akk = -Akk;
    A(c, _) *= -1;
  }
  for (size_t z = 0; z < c; ++z) {
    if (int64_t Akz = A(z, r)) {
      int64_t AkzOld = Akz;
      Akz /= Akk;
      if (AkzOld < 0) Akz -= (AkzOld != (Akz * Akk));
      A(z, _) -= Akz * A(c, _);
    }
  }
  for (size_t z = 0; z < B.numRow(); ++z) {
    if (int64_t Bzr = B(z, r)) {
      int64_t BzrOld = Bzr;
      Bzr /= Akk;
      if (BzrOld < 0) Bzr -= (BzrOld != (Bzr * Akk));
      B(z, _) -= Bzr * A(c, _);
    }
  }
}
constexpr void
reduceSubDiagonal(std::pair<MutPtrMatrix<int64_t>, MutPtrMatrix<int64_t>> AB,
                  Col r, Row c) {
  auto [A, B] = AB;
  int64_t Akk = A(c, r);
  if (Akk < 0) {
    Akk = -Akk;
    A(c, _) *= -1;
    B(c, _) *= -1;
  }
  for (size_t z = 0; z < c; ++z) {
    // try to eliminate `A(k,z)`
    if (int64_t Akz = A(z, r)) {
      // if Akk == 1, then this zeros out Akz
      if (Akk != 1) {
        // we want positive but smaller subdiagonals
        // e.g., `Akz = 5, Akk = 2`, then in the loop below when `i=k`,
        // we set A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
        //        =   5 - 2*2 = 1
        // or if `Akz = -5, Akk = 2`, then in the loop below we get
        // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
        //        =  -5 - (-2 - 1)*2 = = 6 - 5 = 1
        // if `Akk = 1`, then
        // A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
        //        = A(k,z) - A(k,z) = 0
        // or if `Akz = -7, Akk = 39`, then in the loop below we get
        // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
        //        =  -7 - ((-7/39) - 1)*39 = = 6 - 5 = 1
        int64_t AkzOld = Akz;
        Akz /= Akk;
        if (AkzOld < 0) Akz -= (AkzOld != (Akz * Akk));
      }
      A(z, _) -= Akz * A(c, _);
      B(z, _) -= Akz * B(c, _);
    }
  }
}

constexpr void reduceColumn(MutPtrMatrix<int64_t> A, Col c, Row r) {
  zeroSupDiagonal(A, c, r);
  reduceSubDiagonal(A, c, r);
}
// treats A as stacked on top of B
constexpr void reduceColumnStack(MutPtrMatrix<int64_t> A,
                                 MutPtrMatrix<int64_t> B, size_t c, size_t r) {
  zeroSupDiagonal(B, c, r);
  reduceSubDiagonalStack(B, A, c, r);
}

/// numNonZeroRows(PtrMatrix<int64_t> A) -> Row
/// Assumes some number of the trailing rows have been
/// zeroed out.  Returns the number of rows that are remaining.
constexpr auto numNonZeroRows(PtrMatrix<int64_t> A) -> Row {
  Row Mnew = A.numRow();
  while (Mnew && allZero(A(Mnew - 1, _))) --Mnew;
  return Mnew;
}
// NormalForm version assumes zero rows are sorted to end due to pivoting
constexpr void removeZeroRows(IntMatrix &A) { A.truncate(numNonZeroRows(A)); }
[[nodiscard]] constexpr auto removeZeroRows(MutPtrMatrix<int64_t> A)
  -> MutPtrMatrix<int64_t> {
  return A.truncate(numNonZeroRows(A));
}

constexpr auto simplifySystemImpl(MutPtrMatrix<int64_t> A, size_t colInit = 0)
  -> Row {
  auto [M, N] = A.size();
  for (size_t r = 0, c = colInit; c < N && r < M; ++c)
    if (!pivotRows(A, Col{c}, M, Row{r})) reduceColumn(A, Col{c}, Row{r++});
  return numNonZeroRows(A);
}
constexpr void simplifySystem(EmptyMatrix<int64_t>, size_t = 0) {}
constexpr void simplifySystem(IntMatrix &E, size_t colInit = 0) {
  E.truncate(simplifySystemImpl(E, colInit));
}
constexpr auto rank(IntMatrix E) -> size_t {
  return size_t(simplifySystemImpl(E, 0));
}
constexpr void
reduceColumn(std::pair<MutPtrMatrix<int64_t>, MutPtrMatrix<int64_t>> AB, Col c,
             Row r) {
  zeroSupDiagonal(AB, c, r);
  reduceSubDiagonal(AB, c, r);
}
constexpr void
simplifySystemImpl(std::pair<MutPtrMatrix<int64_t>, MutPtrMatrix<int64_t>> AB) {
  auto [M, N] = AB.first.size();
  for (size_t r = 0, c = 0; c < N && r < M; ++c)
    if (!pivotRows(AB, Col{c}, M, Row{r})) reduceColumn(AB, Col{c}, Row{r++});
}
constexpr void simplifySystem(IntMatrix &A, IntMatrix &B) {
  simplifySystemImpl(solvePair(A, B));
  Row Mnew = numNonZeroRows(A);
  if (Mnew < A.numRow()) {
    A.truncate(Mnew);
    B.truncate(Mnew);
  }
  return;
}
[[nodiscard]] constexpr auto hermite(IntMatrix A)
  -> std::pair<IntMatrix, SquareMatrix<int64_t>> {
  SquareMatrix<int64_t> U{SquareMatrix<int64_t>::identity(size_t(A.numRow()))};
  simplifySystemImpl(solvePair(A, U));
  return std::make_pair(std::move(A), std::move(U));
}

/// zero A(i,k) with A(j,k)
constexpr auto zeroWithRowOperation(MutPtrMatrix<int64_t> A, Row i, Row j,
                                    Col k, int64_t f) -> int64_t {
  if (int64_t Aik = A(i, k)) {
    int64_t Ajk = A(j, k);
    int64_t g = gcd(Aik, Ajk);
    Aik /= g;
    Ajk /= g;
    int64_t ret = f * Ajk;
    g = ret;
    for (size_t l = 0; l < A.numCol(); ++l) {
      int64_t Ail = Ajk * A(i, l) - Aik * A(j, l);
      A(i, l) = Ail;
      g = gcd(Ail, g);
    }
    if (g > 1) {
      for (size_t l = 0; l < A.numCol(); ++l)
        if (int64_t Ail = A(i, l)) A(i, l) = Ail / g;
      ret /= g;
    }
    return ret;
  }
  return f;
}
constexpr void zeroWithRowOperation(MutPtrMatrix<int64_t> A, Row i, Row j,
                                    Col k, Range<size_t, size_t> skip) {
  if (int64_t Aik = A(i, k)) {
    int64_t Ajk = A(j, k);
    int64_t g = gcd(Aik, Ajk);
    Aik /= g;
    Ajk /= g;
    g = 0;
    for (size_t l = 0; l < skip.b; ++l) {
      int64_t Ail = Ajk * A(i, l) - Aik * A(j, l);
      A(i, l) = Ail;
      g = gcd(Ail, g);
    }
    for (size_t l = skip.e; l < A.numCol(); ++l) {
      int64_t Ail = Ajk * A(i, l) - Aik * A(j, l);
      A(i, l) = Ail;
      g = gcd(Ail, g);
    }
    if (g > 1) {
      for (size_t l = 0; l < skip.b; ++l)
        if (int64_t Ail = A(i, l)) A(i, l) = Ail / g;
      for (size_t l = skip.e; l < A.numCol(); ++l)
        if (int64_t Ail = A(i, l)) A(i, l) = Ail / g;
    }
  }
}

// use row `r` to zero the remaining rows of column `c`
constexpr void
zeroColumn(std::pair<MutPtrMatrix<int64_t>, MutPtrMatrix<int64_t>> AB, Col c,
           Row r) {
  auto [A, B] = AB;
  const Col N = A.numCol();
  const Col K = B.numCol();
  const Row M = A.numRow();
  assert(M == B.numRow());
  for (size_t j = 0; j < r; ++j) {
    int64_t Arc = A(r, c);
    if (int64_t Ajc = A(j, c)) {
      int64_t g = gcd(Arc, Ajc);
      Arc /= g;
      Ajc /= g;

      for (size_t k = 0; k < N; ++k) A(j, k) = Arc * A(j, k) - Ajc * A(r, k);

      for (size_t k = 0; k < K; ++k) B(j, k) = Arc * B(j, k) - Ajc * B(r, k);
    }
  }
  // greater rows in previous columns have been zeroed out
  // therefore it is safe to use them for row operations with this row
  for (Row j = r + 1; j < M; ++j) {
    int64_t Arc = A(r, c);
    if (int64_t Ajc = A(j, c)) {
      const auto [p, q, Arcr, Ajcr] = gcdxScale(Arc, Ajc);

      for (size_t k = 0; k < N; ++k) {
        int64_t Ark = A(r, k);
        int64_t Ajk = A(j, k);
        A(r, k) = q * Ajk + p * Ark;
        A(j, k) = Arcr * Ajk - Ajcr * Ark;
      }

      for (size_t k = 0; k < K; ++k) {
        int64_t Brk = B(r, k);
        int64_t Bjk = B(j, k);
        B(r, k) = q * Bjk + p * Brk;
        B(j, k) = Arcr * Bjk - Ajcr * Brk;
      }
    }
  }
}
// use row `r` to zero the remaining rows of column `c`
constexpr void zeroColumn(MutPtrMatrix<int64_t> A, Col c, Row r) {
  const Col N = A.numCol();
  const Row M = A.numRow();
  for (size_t j = 0; j < r; ++j) {
    int64_t Arc = A(r, c);
    if (int64_t Ajc = A(j, c)) {
      int64_t g = gcd(Arc, Ajc);
      Arc /= g;
      Ajc /= g;

      for (size_t k = 0; k < N; ++k) A(j, k) = Arc * A(j, k) - Ajc * A(r, k);
    }
  }
  // greater rows in previous columns have been zeroed out
  // therefore it is safe to use them for row operations with this row
  for (Row j = r + 1; j < M; ++j) {
    int64_t Arc = A(r, c);
    if (int64_t Ajc = A(j, c)) {
      const auto [p, q, Arcr, Ajcr] = gcdxScale(Arc, Ajc);

      for (size_t k = 0; k < N; ++k) {
        int64_t Ark = A(r, k);
        int64_t Ajk = A(j, k);
        A(r, k) = q * Ajk + p * Ark;
        A(j, k) = Arcr * Ajk - Ajcr * Ark;
      }
    }
  }
}

constexpr auto pivotRowsBareiss(MutPtrMatrix<int64_t> A, size_t i, Row M,
                                Row piv) -> Optional<size_t> {
  Row j = piv;
  while (A(piv, i) == 0)
    if (++piv == M) return {};
  if (j != piv) swap(A, j, piv);
  return size_t(piv);
}
constexpr void bareiss(IntMatrix &A, Vector<size_t> &pivots) {
  const auto [M, N] = A.size();
  pivots.reserve(min(M, N));
  int64_t prev = 1;
  for (size_t r = 0, c = 0; c < N && r < M; ++c) {
    if (auto piv = pivotRowsBareiss(A, c, M, r)) {
      pivots.push_back(*piv);
      for (size_t k = r + 1; k < M; ++k) {
        for (size_t j = c + 1; j < N; ++j) {
          auto Akj_u = A(r, c) * A(k, j) - A(k, c) * A(r, j);
          auto Akj = Akj_u / prev;
          assert(Akj_u % prev == 0);
          A(k, j) = Akj;
        }
        A(k, r) = 0;
      }
      prev = A(r++, c);
    }
  }
}

[[nodiscard]] constexpr auto bareiss(IntMatrix &A) -> Vector<size_t> {
  Vector<size_t> pivots;
  bareiss(A, pivots);
  return pivots;
}

/// void solveSystem(IntMatrix &A, IntMatrix &B)
/// Say we wanted to solve \f$\textbf{AX} = \textbf{B}\f$.
/// `solveSystem` left-multiplies both sides by
/// a matrix \f$\textbf{W}\f$ that diagonalizes \f$\textbf{A}\f$.
/// Once \f$\textbf{A}\f$ has been diagonalized, the solution is trivial.
/// Both inputs are overwritten with the product of the left multiplications.
constexpr void solveSystem(MutPtrMatrix<int64_t> A, MutPtrMatrix<int64_t> B) {
  const auto [M, N] = A.size();
  auto AB = solvePair(A, B);
  for (auto [r, c] = CarInd{0, 0}; c < N && r < M; ++c)
    if (!pivotRows(AB, c, M, r)) zeroColumn(AB, c, r++);
}
// diagonalizes A(1:K,1:K)
constexpr void solveSystem(MutPtrMatrix<int64_t> A, size_t K) {
  const auto [M, N] = A.size();
  for (auto [r, c] = CarInd{0, 0}; c < K && r < M; ++c)
    if (!pivotRows(A, c, M, r)) zeroColumn(A, c, r++);
}

// returns `true` if the solve failed, `false` otherwise
// diagonals contain denominators.
// Assumes the last column is the vector to solve for.
constexpr void solveSystem(MutPtrMatrix<int64_t> A) {
  solveSystem(A, size_t(A.numCol()) - 1);
}
/// inv(A) -> (D, B)
/// Given a matrix \f$\textbf{A}\f$, returns two matrices \f$\textbf{D}\f$ and
/// \f$\textbf{B}\f$ so that \f$\textbf{D}^{-1}\textbf{B} = \textbf{A}^{-1}\f$,
/// and \f$\textbf{D}\f$ is diagonal.
/// NOTE: This function assumes non-singular
[[nodiscard]] constexpr auto inv(SquareMatrix<int64_t> A)
  -> std::pair<IntMatrix, SquareMatrix<int64_t>> {
  auto B = SquareMatrix<int64_t>::identity(A.numCol());
  solveSystem(A, B);
  return std::make_pair(A, B);
}
/// inv(A) -> (B, s)
/// Given a matrix \f$\textbf{A}\f$, returns a matrix \f$\textbf{B}\f$ and a
/// scalar \f$s\f$ such that \f$\frac{1}{s}\textbf{B} = \textbf{A}^{-1}\f$.
/// NOTE: This function assumes non-singular
/// D0 * B^{-1} = Binv0
/// (s/s) * D0 * B^{-1} = Binv0
/// s * B^{-1} = (s/D0) * Binv0
[[nodiscard]] constexpr auto scaledInv(SquareMatrix<int64_t, 4> A)
  -> std::pair<SquareMatrix<int64_t, 4>, int64_t> {
  auto B = SquareMatrix<int64_t, 4>::identity(A.numCol());
  solveSystem(A, B);
  auto [s, nonUnity] = lcmNonUnity(A.diag());
  if (nonUnity)
    for (size_t i = 0; i < A.numRow(); ++i) B(i, _) *= s / A(i, i);
  return std::make_pair(B, s);
}

constexpr void nullSpace11(IntMatrix &B, IntMatrix &A) {
  const Row M = A.numRow();
  B.resizeForOverwrite(LinearAlgebra::SquareDims{M});
  B << 0;
  B.diag() << 1;
  solveSystem(A, B);
  Row R = numNonZeroRows(A);
  // slice B[_(R,end), :]
  if (!R) return;
  // we keep last D columns
  Row D = M - R;
  size_t o = size_t(R * M);
  // we keep `D` columns
  std::copy_n(B.data() + o, size_t(D * M), B.data() + o);
  B.truncate(D);
}
[[nodiscard]] constexpr auto nullSpace(IntMatrix A) -> IntMatrix {
  IntMatrix B;
  nullSpace11(B, A);
  return B;
}

} // namespace NormalForm
