#pragma once

#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Symbolics.hpp"
#include "EmptyArrays.hpp"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

// prints in current permutation order.
// TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
// in which case, we have to remove `currentToOriginalPerm`,
// which menas either change printing, or move prints `<<` into
// the derived classes.
static std::ostream &printConstraints(std::ostream &os,
                                      PtrMatrix<const int64_t> A,
                                      size_t numSyms, bool inequality = true,
                                      size_t numAuxVar = 0) {
    const unsigned numConstraints = A.numRow();
    const unsigned numVar = A.numCol();
    for (size_t c = 0; c < numConstraints; ++c) {
        bool hasPrinted = false;
        for (size_t v = numSyms; v < numVar; ++v) {
            if (int64_t Acv = A(c, v)) {
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
                if (v >= numAuxVar) {
                    os << "v_" << v - numAuxVar;
                } else {
                    os << "d_" << v;
                }
                hasPrinted = true;
            }
        }
	if (!hasPrinted)
	    os << '0';
        if (inequality) {
            os << " <= ";
        } else {
            os << " == ";
        }
        os << A(c, 0);
        for (size_t v = 1; v < numSyms; ++v)
            os << " + " << A(c, v) << "*" << monomialTermStr(v - 1, 1);
        os << std::endl;
    }
    return os;
}
static std::ostream &printConstraints(std::ostream &os, EmptyMatrix<int64_t>,
                                      size_t, bool = true, size_t = 0) {
    return os;
}

// does not preserve the order of columns, instead it swaps the `i`th column
// to the last, and truncates.
/*
MULTIVERSION static void eraseConstraintImpl(PtrMatrix<int64_t> A,
                                             llvm::MutableArrayRef<int64_t> b,
                                             size_t i) {
    const auto [M, N] = A.size();
    const size_t lastRow = M - 1;
    if (lastRow != i) {
        VECTORIZE
        for (size_t n = 0; n < N; ++n)
            A(i, n) = A(lastRow, n);
        b[i] = b[lastRow];
    }
}
*/
MULTIVERSION static void eraseConstraintImpl(PtrMatrix<int64_t> A, size_t i) {
    const auto [M, N] = A.size();
    const size_t lastRow = M - 1;
    if (lastRow != i) {
        VECTORIZE
        for (size_t n = 0; n < N; ++n)
            A(i, n) = A(lastRow, n);
    }
}
/*
MULTIVERSION static void eraseConstraintImpl(PtrMatrix<int64_t> A,
                                             llvm::MutableArrayRef<MPoly> b,
                                             size_t i) {
    const auto [M, N] = A.size();
    const size_t lastRow = M - 1;
    if (lastRow != i) {
        VECTORIZE
        for (size_t n = 0; n < N; ++n) {
            A(i, n) = A(lastRow, n);
        }
        b[i] = b[lastRow];
    }
}
template <typename T>
static void eraseConstraint(IntMatrix &A, llvm::SmallVectorImpl<T> &b,
                            size_t i) {
    // #ifndef NDEBUG
    //     std::cout << "A0 (i = " << i << ") = \n" << A << std::endl;
    // #endif
    eraseConstraintImpl(A, llvm::MutableArrayRef<T>(b), i);
    // #ifndef NDEBUG
    //     std::cout << "A1=\n" << A << std::endl;
    // #endif
    const size_t lastRow = A.numRow() - 1;
    A.truncateRows(lastRow);
    // #ifndef NDEBUG
    //     std::cout << "A2=\n" << A << std::endl;
    // #endif
    b.truncate(lastRow);
}
*/
static void eraseConstraint(IntMatrix &A, size_t i) {
    // std::cout << "erase constraint i = " << i <<" of A =\n" <<A<< std::endl;
    eraseConstraintImpl(A, i);
    A.truncateRows(A.numRow() - 1);
}

static void eraseConstraint(IntMatrix &A, size_t _i, size_t _j) {
    assert(_i != _j);
    size_t i = std::min(_i, _j);
    size_t j = std::max(_i, _j);
    const auto [M, N] = A.size();
    const size_t lastRow = M - 1;
    const size_t penuRow = lastRow - 1;
    if (j == penuRow) {
        // then we only need to copy one column (i to lastCol)
        eraseConstraint(A, i);
    } else if (i != penuRow) {
        // if i == penuCol, then j == lastCol
        // and we thus don't need to copy
        if (lastRow != i) {
            for (size_t n = 0; n < N; ++n) {
                A(i, n) = A(penuRow, n);
                A(j, n) = A(lastRow, n);
            }
        }
    }
    A.truncateRows(penuRow);
}

MULTIVERSION static size_t substituteEqualityImpl(IntMatrix &E,
                                                  const size_t i) {
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
    auto Es = E.getRow(rowMinNonZero);
    int64_t Eis = Es[i];
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i)) {
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v)
                    E(j, v) = Eis * E(j, v) - Eij * Es[v];
            }
        }
    } else {
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i)) {
                int64_t g = gcd(Eij, Eis);
                int64_t Ag = Eij / g;
                int64_t Eg = Eis / g;
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v)
                    E(j, v) = Eg * E(j, v) - Ag * Es[v];
            }
        }
    }
    return rowMinNonZero;
}
static bool substituteEquality(IntMatrix &E, const size_t i) {
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
    auto Es = E.getRow(rowMinNonZero);
    int64_t Eis = Es[i];
    int64_t s = 2 * (Eis > 0) - 1;
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < A.numRow(); ++j)
            if (int64_t Aij = A(j, i)) {
                // `A` contains inequalities; flipping signs is illegal
                int64_t Ag = (s * Aij);
                int64_t Eg = (s * Eis);
                for (size_t v = 0; v < numVar; ++v)
                    A(j, v) = Eg * A(j, v) - Ag * Es[v];
                // TODO: check if should drop
            }
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i))
                for (size_t v = 0; v < numVar; ++v)
                    E(j, v) = Eis * E(j, v) - Eij * Es[v];
        }
    } else {
        for (size_t j = 0; j < A.numRow(); ++j) {
            if (int64_t Aij = A(j, i)) {
                int64_t g = gcd(Aij, Eis);
                assert(g > 0);
                // `A` contains inequalities; flipping signs is illegal
                int64_t Ag = (s * Aij) / g;
                int64_t Eg = (s * Eis) / g;
                for (size_t v = 0; v < numVar; ++v)
                    A(j, v) = Eg * A(j, v) - Ag * Es[v];
                // TODO: check if should drop
            }
        }
        for (size_t j = 0; j < numConstraints; ++j) {
            if (j == rowMinNonZero)
                continue;
            if (int64_t Eij = E(j, i)) {
                int64_t g = gcd(Eij, Eis);
                int64_t Ag = Eij / g;
                int64_t Eg = Eis / g;
                for (size_t v = 0; v < numVar; ++v)
                    E(j, v) = Eg * E(j, v) - Ag * Es[v];
            }
        }
    }
    return rowMinNonZero;
}
constexpr bool substituteEquality(IntMatrix &, EmptyMatrix<int64_t>, size_t) {
    return false;
}

MULTIVERSION static bool substituteEquality(IntMatrix &A, IntMatrix &E,
                                            const size_t i) {

    size_t rowMinNonZero = substituteEqualityImpl(A, E, i);
    if (rowMinNonZero == E.numRow())
        return true;
    eraseConstraint(E, rowMinNonZero);
    return false;
}

// C = [ I A
//       0 B ]
void slackEqualityConstraints(PtrMatrix<int64_t> C, PtrMatrix<const int64_t> A,
                              PtrMatrix<const int64_t> B) {
    const size_t numVar = A.numCol();
    assert(numVar == B.numCol());
    const size_t numSlack = A.numRow();
    const size_t numStrict = B.numRow();
    assert(C.numRow() == numSlack + numStrict);
    assert(C.numCol() == numSlack + numVar);
    // [I A]
    for (size_t s = 0; s < numSlack; ++s) {
        for (size_t i = 0; i < numSlack; ++i)
            C(s, i) = 0;
        C(s, s) = 1;
        for (size_t i = 0; i < numVar; ++i)
            C(s, i + numSlack) = A(s, i);
    }
    // [0 B]
    for (size_t s = 0; s < numStrict; ++s) {
        for (size_t i = 0; i < numSlack; ++i)
            C(s + numSlack, i) = 0;
        for (size_t i = 0; i < numVar; ++i)
            C(s + numSlack, i + numSlack) = B(s, i);
    }
}
// counts how many negative and positive elements there are in row `i`.
// A row corresponds to a particular variable in `A'x <= b`.
static std::pair<size_t, size_t> countNonZeroSign(PtrMatrix<const int64_t> A,
                                                  size_t i) {
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

static void fourierMotzkin(IntMatrix &A, size_t v) {
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
            // std::cout << "c = " << c << "; posCount = " << posCount
            //           << "; negCount = " << negCount << "; i = " << i
            //           << "; j = " << j << std::endl;
            int64_t g = gcd(Aiv, Ajv);
            int64_t Ai = Aiv / g, Aj = Ajv / g;
	    bool allZero = true;
            for (size_t k = 0; k < A.numCol(); ++k){
		int64_t Ack = Ai * A(j, k) - Aj * A(i, k);
                A(c, k) = Ack;
		allZero &= (Ack == 0);
	    }
	    if (allZero){
		eraseConstraint(A,c);
		if (posCount){
		    if (negCount){
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
// static constexpr bool substituteEquality(IntMatrix &, EmptyMatrix<int64_t>,
// size_t){
//     return true;
// }
static void eliminateVariable(IntMatrix &A, EmptyMatrix<int64_t>, size_t v) {
    fourierMotzkin(A, v);
}
static void eliminateVariable(IntMatrix &A, IntMatrix &E, size_t v) {
    if (substituteEquality(A, E, v))
        fourierMotzkin(A, v);
}

/*
IntMatrix slackEqualityConstraints(PtrMatrix<const int64_t> A,
                                   PtrMatrix<const int64_t> E) {

    const auto [M, N] = A.size();
    const size_t K = E.numRow();
    assert(E.numCol() == N);
    // We add M augment variables (a_m), one per inequality constraint
    // a_n = b_n - (A*x)_n, so a_n >= 0
    // C's first N columns contain constraints from A, last K from E
    // so we have C*x = [b; q]
    // C = [ I A
    //       0 E ]
    IntMatrix C{M + K, M + N};
    slackEqualityConstraints(C, A, E);
    return C;
}
std::pair<IntMatrix, llvm::SmallVector<int64_t>>
slackEqualityConstraints(PtrMatrix<const int64_t> A, llvm::ArrayRef<int64_t> b,
                         PtrMatrix<const int64_t> E,
                         llvm::ArrayRef<int64_t> q) {

    const auto [M, N] = A.size();
    const size_t K = E.numRow();
    assert(E.numCol() == N);
    assert(b.size() == M);
    assert(q.size() == K);
    // We add M augment variables (a_m), one per inequality constraint
    // a_n = b_n - (A*x)_n, so a_n >= 0
    // C's first N columns contain constraints from A, last K from E
    // so we have C*x = [b; q]
    // C = [ I A
    //       0 E ]
    IntMatrix C{M + K, M + N};
    llvm::SmallVector<int64_t> d(M + K);
    for (size_t m = 0; m < M; ++m) {
        C(m, m) = 1;
        for (size_t n = 0; n < N; ++n) {
            C(m, M + n) = A(m, n);
        }
        d[m] = b[m];
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t n = 0; n < N; ++n) {
            C(M + k, M + n) = E(k, n);
        }
        d[M + k] = q[k];
    }
    return std::make_pair(C, d);
}

// (A*x <= b) && (E*x == q)
template <typename T>
void removeExtraVariables(IntMatrix &A, llvm::SmallVectorImpl<T> &b,
                          IntMatrix &E, llvm::SmallVectorImpl<T> &q,
                          const size_t numNewVar) {
    // N variables
    // M inequality constraints
    // K equality constraints
    const auto [M, N] = A.size();
    const size_t K = E.numRow();
    assert(E.numCol() == N);
    assert(b.size() == M);
    assert(q.size() == K);
    // We add M augment variables (a_m), one per inequality constraint
    // a_n = b_n - (A*x)_n, so a_n >= 0
    // C's first N columns contain constraints from A, last K from E
    // so we have C*x = [b; q]
    // C = [ I A
    //       0 E ]
    auto [C, d] = slackEqualityConstraints(A, b, E, q);
    // IntMatrix C{slackEqualityConstraints(A, b, E, q)};
    for (size_t o = M + N; o > numNewVar + M;) {
        substituteEquality(C, d, --o);
        if (C.numRow() > 1)
            NormalForm::simplifySystem(C, d);
    }
    A.resizeForOverwrite(M, numNewVar);
    b.resize_for_overwrite(M);
    size_t nC = 0, nA = 0, i = 0;
    // TODO: document behavior and actually verify correctness...
    // what if we have a_3 - a_7 + .... = ....
    // if another constraint reveals `a_3 = 33`, then this isn't unbounded
    // and we are not allowed to just drop the constraint...
    // However, if we had that, the a_3 would zero-out, so normal form
    // eliminates this possiblity.
    // Or, what if 2*a_3 - 3*a_7 == 1?
    // Then, we have a line, and a_3 - a_7 could still be anything.
    while ((i < M) && (nC < C.numRow()) && (nA < M)) {
        if (C(nC, i++)) {
            // if we have multiple positives, that still represents a positive
            // constraint, as augments are >=. if we have + and -, then the
            // relationship becomes unknown and thus dropped.
            bool otherNegative = false;
            for (size_t j = i; j < M; ++j)
                otherNegative |= (C(nC, j) < 0);
            if (otherNegative) {
                ++nC;
                continue;
            }
            bool duplicate = false;
            for (size_t k = 0; k < nA; ++k) {
                bool allMatch = true;
                for (size_t m = 0; m < numNewVar; ++m)
                    allMatch &= (A(k, m) == C(nC, M + m));
                if (allMatch) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                ++nC;
                continue;
            }
            for (size_t m = 0; m < numNewVar; ++m)
                A(nA, m) = C(nC, M + m);
            b[nA] = d[nC];
            ++nA;
            ++nC;
        }
    }
    A.truncateRows(nA);
    b.truncate(nA);
    E.resizeForOverwrite(C.numRow() - nC, numNewVar);
    q.resize_for_overwrite(C.numRow() - nC);
    for (size_t i = 0; i < E.numRow(); ++i) {
        for (size_t m = 0; m < numNewVar; ++m)
            E(i, m) = C(nC + i, M + m);
        q[i] = d[nC + i];
    }
    // pruneBounds(A, b, E, q);
}

static void divByGCDDropZeros(IntMatrix &A, llvm::SmallVectorImpl<int64_t> &b) {
    for (size_t c = b.size(); c != 0;) {
        int64_t bc = b[--c];
        int64_t g = std::abs(bc);
        if (g == 1)
            continue;
        for (size_t v = 0; v < A.numCol(); ++v) {
            if (int64_t Acv = A(c, v)) {
                int64_t absAcv = std::abs(Acv);
                if (Acv == 1) {
                    g = 1;
                    break;
                }
                g = gcd(g, absAcv);
            }
        }
        if (g) {
            if (g == 1)
                continue;
            if (bc)
                b[c] = bc / g;
            for (size_t v = 0; v < A.numCol(); ++v)
                if (int64_t Acv = A(c, v))
                    A(c, v) = Acv / g;
        } else {
            eraseConstraint(A, b, c);
        }
    }
}
static void divByGCDDropZeros(IntMatrix &A, llvm::SmallVectorImpl<MPoly> &b) {
    for (size_t c = b.size(); c != 0;) {
        MPoly &bc = b[--c];
        int64_t g = 0;
        for (auto &t : bc)
            g = gcd(t.coefficient, g);
        if (g == 1)
            continue;
        for (size_t v = 0; v < A.numCol(); ++v) {
            if (int64_t Acv = A(c, v)) {
                int64_t absAcv = std::abs(Acv);
                if (Acv == 1) {
                    g = 1;
                    break;
                }
                g = gcd(g, absAcv);
            }
        }
        if (g) {
            if (g == 1)
                continue;
            if (!isZero(bc))
                for (auto &&t : bc)
                    t.coefficient /= g;
            for (size_t v = 0; v < A.numCol(); ++v)
                if (int64_t Acv = A(c, v))
                    A(c, v) = Acv / g;
        } else {
            eraseConstraint(A, b, c);
        }
    }
}
*/
static void dropEmptyConstraints(IntMatrix &A) {
    for (size_t c = A.numRow(); c != 0;)
        if (allZero(A.getRow(--c)))
            eraseConstraint(A, c);
}
