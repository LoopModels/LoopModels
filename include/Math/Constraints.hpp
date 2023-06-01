
#pragma once

#include "Containers/BitSets.hpp"
#include "Math/Array.hpp"
#include "Math/AxisTypes.hpp"
#include "Math/Comparisons.hpp"
#include "Math/EmptyArrays.hpp"
#include "Math/GreatestCommonDivisor.hpp"
#include "Math/Indexing.hpp"
#include "Math/Math.hpp"
#include "Math/NormalForm.hpp"
#include <cstddef>
#include <cstdint>

template <OStream OS>
inline auto printConstraint(OS &os, PtrVector<int64_t> a, size_t numSyms,
                            bool inequality) {
  size_t numVar = a.size();
  bool hasPrinted = false, allVarNonNegative = allGEZero(a[_(numSyms, numVar)]);
  int64_t sign = allVarNonNegative ? 1 : -1;
  for (size_t v = numSyms; v < numVar; ++v) {
    if (int64_t Acv = sign * a[v]) {
      if (hasPrinted) {
        if (Acv > 0) {
          os << " + ";
        } else {
          os << " - ";
          Acv *= -1;
        }
      }
      if (Acv != 1) {
        if (Acv == -1) os << "-";
        else os << Acv;
      }
      os << "v_" << v - numSyms;
      hasPrinted = true;
    }
  }
  if (!hasPrinted) os << '0';
  if (inequality) os << (allVarNonNegative ? " >= " : " <= ");
  else os << " == ";
  os << a[0];
}
/// prints in current permutation order.
/// TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
/// in which case, we have to remove `currentToOriginalPerm`,
/// which menas either change printing, or move prints `<<` into
/// the derived classes.
template <OStream OS>
inline auto printConstraints(OS &os, DensePtrMatrix<int64_t> A,
                             bool inequality = true) -> OS & {
  const Row numConstraints = A.numRow();
  for (Row c = 0; c < numConstraints; ++c) {
    printConstraint(os, A(c, _), 1, inequality);
    os << "\n";
  }
  return os;
}

constexpr void eraseConstraintImpl(MutDensePtrMatrix<int64_t> A, Row i) {
  const Row lastRow = A.numRow() - 1;
  invariant(i <= lastRow);
  if (lastRow != i) A(i, _) << A(lastRow, _);
}
constexpr void eraseConstraint(MutDensePtrMatrix<int64_t> &A, Row i) {
  eraseConstraintImpl(A, i);
  A.truncate(A.numRow() - 1);
}
constexpr void eraseConstraintImpl(MutDensePtrMatrix<int64_t> A, size_t _i,
                                   size_t _j) {
  assert(_i != _j);
  Row i = std::min(_i, _j);
  Row j = std::max(_i, _j);
  const auto [M, N] = A.size();
  const Row lastRow = M - 1;
  const Row penuRow = lastRow - 1;
  if (j == penuRow) {
    // then we only need to copy one column (i to lastCol)
    eraseConstraintImpl(A, i);
  } else if ((i != penuRow) && (i != lastRow)) {
    // if i == penuCol, then j == lastCol
    // and we thus don't need to copy
    for (size_t n = 0; n < N; ++n) {
      A(i, n) = A(penuRow, n);
      A(j, n) = A(lastRow, n);
    }
  }
}
constexpr void eraseConstraint(MutDensePtrMatrix<int64_t> &A, size_t i,
                               size_t j) {
  eraseConstraintImpl(A, i, j);
  A.truncate(A.numRow() - 2);
}

constexpr auto substituteEqualityImpl(MutDensePtrMatrix<int64_t> E,
                                      const size_t i) -> Row {
  const auto [numConstraints, numVar] = E.size();
  Col minNonZero = numVar + 1;
  Row rowMinNonZero = numConstraints;
  for (Row j = 0; j < numConstraints; ++j)
    if (E(j, i)) {
      size_t nonZero = 0;

      for (Col v = 0; v < numVar; ++v) nonZero += (E(j, v) != 0);
      if (nonZero < minNonZero) {
        minNonZero = nonZero;
        rowMinNonZero = j;
      }
    }
  if (rowMinNonZero == numConstraints) return rowMinNonZero;
  auto Es = E(rowMinNonZero, _);
  int64_t Eis = Es[i];
  // we now subsitute the equality expression with the minimum number
  // of terms.
  if (constexpr_abs(Eis) == 1) {
    for (Row j = 0; j < numConstraints; ++j) {
      if (j == rowMinNonZero) continue;
      if (int64_t Eij = E(j, i)) E(j, _) << Eis * E(j, _) - Eij * Es;
    }
  } else {
    for (Row j = 0; j < numConstraints; ++j) {
      if (j == rowMinNonZero) continue;
      if (int64_t Eij = E(j, i)) {
        int64_t g = gcd(Eij, Eis);
        E(j, _) << (Eis / g) * E(j, _) - (Eij / g) * Es;
      }
    }
  }
  return rowMinNonZero;
}
constexpr auto substituteEquality(DenseMatrix<int64_t> &E, const size_t i)
  -> bool {
  Row minNonZero = substituteEqualityImpl(E, i);
  if (minNonZero == E.numRow()) return true;
  eraseConstraint(E, minNonZero);
  return false;
}

constexpr auto
substituteEqualityPairImpl(std::array<MutDensePtrMatrix<int64_t>, 2> AE,
                           const size_t i) -> Row {
  auto [A, E] = AE;
  const auto [numConstraints, numVar] = E.size();
  Col minNonZero = numVar + 1;
  Row rowMinNonZero = numConstraints;
  for (Row j = 0; j < numConstraints; ++j) {
    if (E(j, i)) {
      size_t nonZero = 0;
      for (Col v = 0; v < numVar; ++v) nonZero += (E(j, v) != 0);
      if (nonZero < minNonZero) {
        minNonZero = nonZero;
        rowMinNonZero = j;
      }
    }
  }
  if (rowMinNonZero == numConstraints) return rowMinNonZero;
  auto Es = E(rowMinNonZero, _);
  int64_t Eis = Es[i];
  int64_t s = 2 * (Eis > 0) - 1;
  // we now subsitute the equality expression with the minimum number
  // of terms.
  if (constexpr_abs(Eis) == 1) {
    for (Row j = 0; j < A.numRow(); ++j)
      if (int64_t Aij = A(j, i))
        A(j, _) << (s * Eis) * A(j, _) - (s * Aij) * Es;
    for (Row j = 0; j < numConstraints; ++j) {
      if (j == rowMinNonZero) continue;
      if (int64_t Eij = E(j, i)) E(j, _) << Eis * E(j, _) - Eij * Es;
    }
  } else {
    for (Row j = 0; j < A.numRow(); ++j) {
      if (int64_t Aij = A(j, i)) {
        int64_t g = gcd(Aij, Eis);
        assert(g > 0);
        // `A` contains inequalities; flipping signs is illegal
        A(j, _) << ((s * Eis) / g) * A(j, _) - ((s * Aij) / g) * Es;
      }
    }
    for (Row j = 0; j < numConstraints; ++j) {
      if (j == rowMinNonZero) continue;
      if (int64_t Eij = E(j, i)) {
        int64_t g = gcd(Eij, Eis);
        E(j, _) << (Eis / g) * E(j, _) - (Eij / g) * Es;
      }
    }
  }
  return rowMinNonZero;
}
constexpr auto substituteEquality(MutDensePtrMatrix<int64_t> &,
                                  EmptyMatrix<int64_t>, size_t) -> bool {
  return false;
}

constexpr auto substituteEquality(MutDensePtrMatrix<int64_t> &A,
                                  MutDensePtrMatrix<int64_t> &E, const size_t i)
  -> bool {

  Row minNonZero = substituteEqualityPairImpl({A, E}, i);
  if (minNonZero == E.numRow()) return true;
  eraseConstraint(E, minNonZero);
  return false;
}

// C = [ I A
//       0 B ]
constexpr void slackEqualityConstraints(MutPtrMatrix<int64_t> C,
                                        PtrMatrix<int64_t> A,
                                        PtrMatrix<int64_t> B) {
  const Col numVar = A.numCol();
  invariant(numVar, B.numCol());
  const Row numSlack = A.numRow(), numStrict = B.numRow();
  invariant(C.numRow(), numSlack + numStrict);
  size_t slackAndVar = size_t(numSlack) + size_t(numVar);
  invariant(size_t(C.numCol()), slackAndVar);
  // [I A]
  for (size_t s = 0; s < numSlack; ++s) {
    C(s, _(begin, numSlack)) << 0;
    C(s, s) = 1;
    C(s, _(numSlack, slackAndVar)) << A(s, _(begin, numVar));
  }
  // [0 B]
  for (Row s = 0; s < numStrict; ++s) {
    C(s + numSlack, _(begin, numSlack)) << 0;
    C(s + numSlack, _(numSlack, slackAndVar)) << B(s, _(begin, numVar));
  }
}
constexpr void slackEqualityConstraints(MutPtrMatrix<int64_t> C,
                                        PtrMatrix<int64_t> A) {
  const Col numVar = A.numCol();
  const Row numSlack = A.numRow();
  size_t slackAndVar = size_t(numSlack) + size_t(numVar);
  invariant(size_t(C.numCol()), slackAndVar);
  // [I A]
  for (size_t s = 0; s < numSlack; ++s) {
    C(s, _(begin, numSlack)) << 0;
    C(s, s) = 1;
    C(s, _(numSlack, slackAndVar)) << A(s, _(begin, numVar));
  }
}
// counts how many negative and positive elements there are in row `i`.
// A row corresponds to a particular variable in `A'x <= b`.
constexpr auto countNonZeroSign(DensePtrMatrix<int64_t> A, size_t i)
  -> std::array<size_t, 2> {
  size_t numNeg = 0;
  size_t numPos = 0;
  Row numRow = A.numRow();
  for (Row j = 0; j < numRow; ++j) {
    int64_t Aij = A(j, i);
    numNeg += (Aij < 0);
    numPos += (Aij > 0);
  }
  return {numNeg, numPos};
}

/// x == 0 -> 0, x < 0 -> 1, x > 0 -> 2
inline constexpr auto orderedCmp(auto x) -> size_t {
  return (x < 0) | (2 * (x > 0));
}
/// returns three bitsets, indicating indices that are 0, negative, and positive
template <class T, LinAlg::VectorDimension S>
constexpr auto indsZeroNegPos(LinAlg::Array<T, S> a)
  -> std::array<BitSet64, 3> {
  std::array<BitSet64, 3> ret;
  for (size_t j = 0; j < a.size(); ++j) ret[orderedCmp(a[j])].insert(j);
  return ret;
}
// 4*4 + 16 = 32
static_assert(sizeof(std::array<Vector<unsigned, 4>, 2>) == 64);

template <bool NonNegative>
constexpr auto fourierMotzkinCore(MutDensePtrMatrix<int64_t> B,
                                  DensePtrMatrix<int64_t> A, size_t v,
                                  const std::array<BitSet64, 3> &znp) -> Row {
  const auto &[zero, neg, pos] = znp;
  // we have the additional v >= 0
  if constexpr (NonNegative)
    invariant(B.numRow(),
              A.numRow() - pos.size() + size_t(neg.size()) * pos.size());
  else
    invariant(B.numRow(), A.numRow() - pos.size() - neg.size() +
                            size_t(neg.size()) * pos.size());
  invariant(B.numCol() + 1, A.numCol());
  Row r = 0;
  // x - v >= 0 -> x >= v
  // x + v >= 0 -> v >= -x
  for (auto i : neg) {
    // we  have implicit v >= 0, matching x >= v
    if constexpr (NonNegative) {
      B(r, _(0, v)) << A(i, _(0, v));
      B(r, _(v, end)) << A(i, _(v + 1, end));
      r += anyNEZero(B(r, _(0, end)));
    }
    int64_t Aiv = A(i, v);
    invariant(Aiv < 0);
    for (auto j : pos) {
      int64_t Ajv = A(j, v);
      invariant(Ajv > 0);
      auto [ai, aj] = divgcd(Aiv, Ajv);
      B(r, _(0, v)) << aj * A(i, _(0, v)) - ai * A(j, _(0, v));
      B(r, _(v, end)) << aj * A(i, _(v + 1, end)) - ai * A(j, _(v + 1, end));
      r += anyNEZero(B(r, _(0, end)));
    }
  }
  for (auto i : zero) {
    B(r, _(0, v)) << A(i, _(0, v));
    B(r, _(v, end)) << A(i, _(v + 1, end));
    r += anyNEZero(B(r, _(0, end)));
  }
  return r;
}

template <bool NonNegative>
constexpr auto fourierMotzkin(LinAlg::Alloc<int64_t> auto &alloc,
                              DensePtrMatrix<int64_t> A, size_t v)
  -> MutDensePtrMatrix<int64_t> {

  auto znp = indsZeroNegPos(A(_, v));
  auto &[zero, neg, pos] = znp;
  Row r = A.numRow() - pos.size() + size_t(neg.size()) * pos.size();
  if constexpr (!NonNegative) r -= neg.size();
  auto B = matrix(alloc, r, A.numCol() - 1);
  B.truncate(fourierMotzkinCore<NonNegative>(B, A, v, znp));
  return B;
}

constexpr void fourierMotzkinCore(DenseMatrix<int64_t> &A, size_t v,
                                  std::array<size_t, 2> negPos) {
  auto [numNeg, numPos] = negPos;
  // we need one extra, as on the last overwrite, we still need to
  // read from two constraints we're deleting; we can't write into
  // both of them. Thus, we use a little extra memory here,
  // and then truncate.
  const Row numRowsOld = A.numRow();
  const Row numRowsNew = numRowsOld - numNeg - numPos + numNeg * numPos + 1;
  A.resize(numRowsNew);
  // plan is to replace
  for (size_t i = 0, numRows = size_t(numRowsOld), posCount = numPos; posCount;
       ++i) {
    int64_t Aiv = A(i, v);
    if (Aiv <= 0) continue;
    --posCount;
    for (size_t negCount = numNeg, j = 0; negCount; ++j) {
      int64_t Ajv = A(j, v);
      if (Ajv >= 0) continue;
      // for the last `negCount`, we overwrite `A(i, k)`
      // last posCount does not get overwritten
      --negCount;
      size_t c = posCount ? (negCount ? numRows++ : size_t(i)) : j;
      int64_t Ai = Aiv, Aj = Ajv;
      int64_t g = gcd(Aiv, Ajv);
      if (g != 1) {
        Ai /= g;
        Aj /= g;
      }
      bool allZero = true;
      for (size_t k = 0; k < A.numCol(); ++k) {
        int64_t Ack = Ai * A(j, k) - Aj * A(i, k);
        A(c, k) = Ack;
        allZero &= (Ack == 0);
      }
      if (allZero) {
        eraseConstraint(A, c);
        if (posCount)
          if (negCount) --numRows;
          else --i;
        else --j;
      }
    }
    if (posCount == 0) // last posCount not overwritten, so we erase
      eraseConstraint(A, i);
  }
  // assert(numRows == (numRowsNew+1));
}
constexpr void fourierMotzkin(DenseMatrix<int64_t> &A, size_t v) {
  invariant(v < A.numCol());
  const auto [numNeg, numPos] = countNonZeroSign(A, v);
  const Row numRowsOld = A.numRow();
  if ((numNeg == 0) | (numPos == 0)) {
    if ((numNeg == 0) & (numPos == 0)) return;
    for (Row i = numRowsOld; i != 0;)
      if (A(--i, v)) eraseConstraint(A, i);
    return;
  }
  fourierMotzkinCore(A, v, {numNeg, numPos});
} // non-negative Fourier-Motzki

/// Checks all rows, dropping those that are 0.
constexpr void removeZeroRows(MutDensePtrMatrix<int64_t> &A) {
  for (Row i = A.numRow(); i;)
    if (allZero(A(--i, _))) eraseConstraint(A, i);
}

/// checks whether `r` is a copy of any preceding rows
/// NOTE: does not compare to any following rows
constexpr auto uniqueConstraint(DensePtrMatrix<int64_t> A, Row r) -> bool {
  for (Row i = r; i != 0;)
    if (A(--i, _) == A(r, _)) return false;
  return !allZero(A(r, _));
}

/// A is an inequality matrix, A*x >= 0
/// B is an equality matrix, E*x == 0
/// Use the equality matrix B to remove redundant constraints
[[nodiscard]] constexpr auto removeRedundantRows(MutDensePtrMatrix<int64_t> A,
                                                 MutDensePtrMatrix<int64_t> B)
  -> std::array<Row, 2> {
  auto [M, N] = B.size();
  for (size_t r = 0, c = 0; c++ < N && r < M;)
    if (!NormalForm::pivotRows(B, c == N ? 0 : c, M, r))
      NormalForm::reduceColumnStack(A, B, c == N ? 0 : c, r++);
  // scan duplicate rows in `A`
  for (Row r = A.numRow(); r != 0;)
    if (!uniqueConstraint(A, --r)) eraseConstraint(A, r);
  return {NormalForm::numNonZeroRows(A), NormalForm::numNonZeroRows(B)};
}

constexpr void dropEmptyConstraints(MutDensePtrMatrix<int64_t> &A) {
  for (Row c = A.numRow(); c != 0;)
    if (allZero(A(--c, _))) eraseConstraint(A, c);
}

constexpr auto uniqueConstraint(DensePtrMatrix<int64_t> A, size_t C) -> bool {
  for (size_t c = 0; c < C; ++c) {
    bool allEqual = true;
    for (size_t r = 0; r < A.numCol(); ++r) allEqual &= (A(c, r) == A(C, r));
    if (allEqual) return false;
  }
  return true;
}

constexpr auto countSigns(DensePtrMatrix<int64_t> A, size_t i)
  -> std::array<size_t, 2> {
  size_t numNeg = 0;
  size_t numPos = 0;
  for (size_t j = 0; j < A.numRow(); ++j) {
    int64_t Aij = A(j, i);
    numNeg += (Aij < 0);
    numPos += (Aij > 0);
  }
  return {numNeg, numPos};
}

constexpr void deleteBounds(MutDensePtrMatrix<int64_t> &A, size_t i) {
  for (Row j = A.numRow(); j != 0;)
    if (A(--j, i)) eraseConstraint(A, j);
}
