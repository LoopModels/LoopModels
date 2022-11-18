#pragma once

#include "./EmptyArrays.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Support/raw_ostream.h>
#include <sys/types.h>

// prints in current permutation order.
// TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
// in which case, we have to remove `currentToOriginalPerm`,
// which menas either change printing, or move prints `<<` into
// the derived classes.
[[maybe_unused]] static llvm::raw_ostream &
printConstraints(llvm::raw_ostream &os, PtrMatrix<int64_t> A,
                 llvm::ArrayRef<const llvm::SCEV *> syms,
                 bool inequality = true) {
    const unsigned numConstraints = A.numRow();
    const unsigned numVar = A.numCol();
    const unsigned numSyms = syms.size() + 1;
    for (size_t c = 0; c < numConstraints; ++c) {
        bool hasPrinted = false;
        bool allVarNonNegative = allGEZero(A(c, _(numSyms, numVar)));
        int64_t sign = allVarNonNegative ? 1 : -1;
        for (size_t v = numSyms; v < numVar; ++v) {
            if (int64_t Acv = sign * A(c, v)) {
                if (hasPrinted) {
                    if (Acv > 0) {
                        os << " + ";
                    } else {
                        os << " - ";
                        Acv *= -1;
                    }
                }
                if (Acv != 1) {
                    if (Acv == -1) {
                        os << "-";
                    } else {
                        os << Acv;
                    }
                }
                os << "v_" << v - numSyms;
                hasPrinted = true;
            }
        }
        if (!hasPrinted)
            os << '0';
        if (inequality) {
            os << (allVarNonNegative ? " >= " : " <= ");
        } else {
            os << " == ";
        }
        os << A(c, 0);
        for (size_t v = 1; v < numSyms; ++v) {
            if (int64_t Acv = A(c, v)) {
                os << (Acv > 0 ? " + " : " - ");
                Acv = std::abs(Acv);
                if (Acv != 1)
                    os << Acv << "*";
                os << *syms[v - 1];
            }
        }
        os << "\n";
    }
    return os;
}
[[maybe_unused]] static llvm::raw_ostream &
printConstraints(llvm::raw_ostream &os, EmptyMatrix<int64_t>,
                 llvm::ArrayRef<const llvm::SCEV *>, bool = true, size_t = 0) {
    return os;
}

MULTIVERSION [[maybe_unused]] static void
eraseConstraintImpl(MutPtrMatrix<int64_t> A, size_t i) {
    const size_t lastRow = A.numRow() - 1;
    assert(i <= lastRow);
    if (lastRow != i)
        A(i, _) = A(lastRow, _);
}
[[maybe_unused]] static void eraseConstraint(IntMatrix &A, size_t i) {
    eraseConstraintImpl(A, i);
    A.truncateRows(A.numRow() - 1);
}
[[maybe_unused]] static void eraseConstraint(IntMatrix &A, size_t _i,
                                             size_t _j) {
    assert(_i != _j);
    size_t i = std::min(_i, _j);
    size_t j = std::max(_i, _j);
    const auto [M, N] = A.size();
    const size_t lastRow = M - 1;
    const size_t penuRow = lastRow - 1;
    if (j == penuRow) {
        // then we only need to copy one column (i to lastCol)
        eraseConstraint(A, i);
    } else if ((i != penuRow) && (i != lastRow)) {
        // if i == penuCol, then j == lastCol
        // and we thus don't need to copy
        for (size_t n = 0; n < N; ++n) {
            A(i, n) = A(penuRow, n);
            A(j, n) = A(lastRow, n);
        }
    }
    A.truncateRows(penuRow);
}

MULTIVERSION [[maybe_unused]] static size_t
substituteEqualityImpl(IntMatrix &E, const size_t i) {
    const auto [numConstraints, numVar] = E.size();
    size_t minNonZero = numVar + 1;
    size_t rowMinNonZero = numConstraints;
    for (size_t j = 0; j < numConstraints; ++j)
        if (E(j, i)) {
            size_t nonZero = 0;
            VECTORIZE
            for (size_t v = 0; v < numVar; ++v)
                nonZero += (E(j, v) != 0);
            if (nonZero < minNonZero) {
                minNonZero = nonZero;
                rowMinNonZero = j;
            }
        }
    if (rowMinNonZero == numConstraints)
        return rowMinNonZero;
    auto Es = E(rowMinNonZero, _);
    int64_t Eis = Es[i];
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i))
                E(j, _) = Eis * E(j, _) - Eij * Es;
        }
    } else {
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i)) {
                int64_t g = gcd(Eij, Eis);
                E(j, _) = (Eis / g) * E(j, _) - (Eij / g) * Es;
            }
        }
    }
    return rowMinNonZero;
}
[[maybe_unused]] static bool substituteEquality(IntMatrix &E, const size_t i) {
    size_t rowMinNonZero = substituteEqualityImpl(E, i);
    if (rowMinNonZero == E.numRow())
        return true;
    eraseConstraint(E, rowMinNonZero);
    return false;
}

inline size_t substituteEqualityImpl(IntMatrix &A, IntMatrix &E,
                                     const size_t i) {
    const auto [numConstraints, numVar] = E.size();
    size_t minNonZero = numVar + 1;
    size_t rowMinNonZero = numConstraints;
    for (size_t j = 0; j < numConstraints; ++j) {
        if (E(j, i)) {
            size_t nonZero = 0;
            for (size_t v = 0; v < numVar; ++v)
                nonZero += (E(j, v) != 0);
            if (nonZero < minNonZero) {
                minNonZero = nonZero;
                rowMinNonZero = j;
            }
        }
    }
    if (rowMinNonZero == numConstraints)
        return rowMinNonZero;
    auto Es = E(rowMinNonZero, _);
    int64_t Eis = Es[i];
    int64_t s = 2 * (Eis > 0) - 1;
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < A.numRow(); ++j)
            if (int64_t Aij = A(j, i))
                A(j, _) = (s * Eis) * A(j, _) - (s * Aij) * Es;
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i))
                E(j, _) = Eis * E(j, _) - Eij * Es;
        }
    } else {
        for (size_t j = 0; j < A.numRow(); ++j) {
            if (int64_t Aij = A(j, i)) {
                int64_t g = gcd(Aij, Eis);
                assert(g > 0);
                // `A` contains inequalities; flipping signs is illegal
                A(j, _) = ((s * Eis) / g) * A(j, _) - ((s * Aij) / g) * Es;
            }
        }
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i)) {
                int64_t g = gcd(Eij, Eis);
                E(j, _) = (Eis / g) * E(j, _) - (Eij / g) * Es;
            }
        }
    }
    return rowMinNonZero;
}
constexpr bool substituteEquality(IntMatrix &, EmptyMatrix<int64_t>, size_t) {
    return false;
}

MULTIVERSION [[maybe_unused]] static bool
substituteEquality(IntMatrix &A, IntMatrix &E, const size_t i) {

    size_t rowMinNonZero = substituteEqualityImpl(A, E, i);
    if (rowMinNonZero == E.numRow())
        return true;
    eraseConstraint(E, rowMinNonZero);
    return false;
}

// C = [ I A
//       0 B ]
[[maybe_unused]] static void slackEqualityConstraints(MutPtrMatrix<int64_t> C,
                                                      PtrMatrix<int64_t> A,
                                                      PtrMatrix<int64_t> B) {
    const size_t numVar = A.numCol();
    assert(numVar == B.numCol());
    const size_t numSlack = A.numRow();
    const size_t numStrict = B.numRow();
    assert(C.numRow() == numSlack + numStrict);
    assert(C.numCol() == numSlack + numVar);
    // [I A]
    for (size_t s = 0; s < numSlack; ++s) {
        C(s, _(begin, numSlack)) = 0;
        C(s, s) = 1;
        C(s, _(numSlack, numSlack + numVar)) = A(s, _(begin, numVar));
    }
    // [0 B]
    for (size_t s = 0; s < numStrict; ++s) {
        C(s + numSlack, _(begin, numSlack)) = 0;
        C(s + numSlack, _(numSlack, numSlack + numVar)) =
            B(s, _(begin, numVar));
    }
}
// counts how many negative and positive elements there are in row `i`.
// A row corresponds to a particular variable in `A'x <= b`.
[[maybe_unused]] static std::pair<size_t, size_t>
countNonZeroSign(PtrMatrix<int64_t> A, size_t i) {
    size_t numNeg = 0;
    size_t numPos = 0;
    size_t numRow = A.numRow();
    for (size_t j = 0; j < numRow; ++j) {
        int64_t Aij = A(j, i);
        numNeg += (Aij < 0);
        numPos += (Aij > 0);
    }
    return std::make_pair(numNeg, numPos);
}

[[maybe_unused]] static void fourierMotzkin(IntMatrix &A, size_t v) {
    assert(v < A.numCol());
    const auto [numNeg, numPos] = countNonZeroSign(A, v);
    const size_t numRowsOld = A.numRow();
    const size_t numRowsNew =
        numRowsOld - numNeg - numPos + numNeg * numPos + 1;
    // we need one extra, as on the last overwrite, we still need to
    // read from two constraints we're deleting; we can't write into
    // both of them. Thus, we use a little extra memory here,
    // and then truncate.
    if ((numNeg == 0) | (numPos == 0)) {
        if ((numNeg == 0) & (numPos == 0))
            return;
        for (size_t i = numRowsOld; i != 0;)
            if (A(--i, v))
                eraseConstraint(A, i);
        return;
    }
    A.resizeRows(numRowsNew);
    // plan is to replace
    for (size_t i = 0, numRows = numRowsOld, posCount = numPos; posCount; ++i) {
        int64_t Aiv = A(i, v);
        if (Aiv <= 0)
            continue;
        --posCount;
        for (size_t negCount = numNeg, j = 0; negCount; ++j) {
            int64_t Ajv = A(j, v);
            if (Ajv >= 0)
                continue;
            // for the last `negCount`, we overwrite `A(i, k)`
            // last posCount does not get overwritten
            --negCount;
            size_t c = posCount ? (negCount ? numRows++ : i) : j;
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
                if (posCount) {
                    if (negCount) {
                        --numRows;
                    } else {
                        --i;
                    }
                } else {
                    --j;
                }
            }
        }
        if (posCount == 0) // last posCount not overwritten, so we erase
            eraseConstraint(A, i);
    }
    // assert(numRows == (numRowsNew+1));
}
// non-negative Fourier-Motzkin
[[maybe_unused]] static void fourierMotzkinNonNegative(IntMatrix &A, size_t v) {
    assert(v < A.numCol());
    const auto [numNeg, numPos] = countNonZeroSign(A, v);
    const size_t numPosP1 = numPos + 1;
    const size_t numRowsOld = A.numRow();
    const size_t numRowsNew =
        numRowsOld - numNeg - numPosP1 + numNeg * numPosP1 + 1;
    // we need one extra, as on the last overwrite, we still need to
    // read from two constraints we're deleting; we can't write into
    // both of them. Thus, we use a little extra memory here,
    // and then truncate.
    if ((numNeg == 0) | (numPosP1 == 0)) {
        if ((numNeg == 0) & (numPosP1 == 0))
            return;
        for (size_t i = numRowsOld; i != 0;)
            if (A(--i, v))
                eraseConstraint(A, i);
        return;
    }
    A.resizeRows(numRowsNew);
    // plan is to replace
    size_t numRows = numRowsOld;
    for (size_t i = 0, posCount = numPos; posCount; ++i) {
        int64_t Aiv = A(i, v);
        if (Aiv <= 0)
            continue;
        --posCount;
        for (size_t negCount = numNeg, j = 0; negCount; ++j) {
            int64_t Ajv = A(j, v);
            if (Ajv >= 0)
                continue;
            // for the last `negCount`, we overwrite `A(i, k)`
            // note that `A(i,k)` is the positive element
            size_t c = --negCount ? numRows++ : i;
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
                if (negCount) {
                    --numRows;
                } else {
                    --i;
                }
            }
        }
    }
    for (size_t negCount = numNeg, j = 0; negCount; ++j) {
        int64_t Ajv = A(j, v);
        if (Ajv >= 0)
            continue;
        // we can always overwrite the old negCount here
        --negCount;
        bool allZero = true;
        for (size_t k = 0; k < A.numCol(); ++k) {
            int64_t Ajk = A(j, k) - Ajv * (k == v);
            A(j, k) = Ajk;
            allZero &= (Ajk == 0);
        }
        if (allZero)
            eraseConstraint(A, j--);
    }
}
// [[maybe_unused]] static constexpr bool substituteEquality(IntMatrix &,
// EmptyMatrix<int64_t>, size_t){
//     return true;
// }
[[maybe_unused]] static void eliminateVariable(IntMatrix &A,
                                               EmptyMatrix<int64_t>, size_t v) {
    fourierMotzkin(A, v);
}
[[maybe_unused]] static void eliminateVariable(IntMatrix &A, IntMatrix &E,
                                               size_t v) {
    if (substituteEquality(A, E, v))
        fourierMotzkin(A, v);
}
[[maybe_unused]] static void removeZeroRows(IntMatrix &A) {
    for (size_t i = A.numRow(); i;)
        if (allZero(A(--i, _)))
            eraseConstraint(A, i);
}

// A is an inequality matrix, A*x >= 0
// B is an equality matrix, E*x == 0
// Use the equality matrix B to remove redundant constraints both matrices
//
[[maybe_unused]] static void removeRedundantRows(IntMatrix &A, IntMatrix &B) {
    auto [M, N] = B.size();
    for (size_t r = 0, c = 0; c < N && r < M; ++c)
        if (!NormalForm::pivotRows(B, c, M, r))
            NormalForm::reduceColumnStack(A, B, c, r++);
    removeZeroRows(A);
    NormalForm::removeZeroRows(B);
}

[[maybe_unused]] static void dropEmptyConstraints(IntMatrix &A) {
    for (size_t c = A.numRow(); c != 0;)
        if (allZero(A(--c, _)))
            eraseConstraint(A, c);
}

[[maybe_unused]] static bool uniqueConstraint(PtrMatrix<int64_t> A, size_t C) {
    for (size_t c = 0; c < C; ++c) {
        bool allEqual = true;
        for (size_t r = 0; r < A.numCol(); ++r)
            allEqual &= (A(c, r) == A(C, r));
        if (allEqual)
            return false;
    }
    return true;
}

[[maybe_unused]] static std::pair<size_t, size_t>
countSigns(PtrMatrix<int64_t> A, size_t i) {
    size_t numNeg = 0;
    size_t numPos = 0;
    for (size_t j = 0; j < A.numRow(); ++j) {
        int64_t Aij = A(j, i);
        numNeg += (Aij < 0);
        numPos += (Aij > 0);
    }
    return std::make_pair(numNeg, numPos);
}

[[maybe_unused]] inline static bool equalsNegative(llvm::ArrayRef<int64_t> x,
                                                   llvm::ArrayRef<int64_t> y) {
    assert(x.size() == y.size());
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] + y[i])
            return false;
    return true;
}

[[maybe_unused]] static void deleteBounds(IntMatrix &A, size_t i) {
    for (size_t j = A.numRow(); j != 0;)
        if (A(--j, i))
            eraseConstraint(A, j);
}
