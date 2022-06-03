#pragma once

#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./Symbolics.hpp"

// prints in current permutation order.
// TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
// in which case, we have to remove `currentToOriginalPerm`,
// which menas either change printing, or move prints `<<` into
// the derived classes.
template <typename T>
static std::ostream &
printConstraints(std::ostream &os, PtrMatrix<const intptr_t> A,
                 llvm::ArrayRef<T> b, bool inequality = true,
                 size_t numAuxVar = 0) {
    const auto [numVar, numConstraints] = A.size();
    for (size_t c = 0; c < numConstraints; ++c) {
        bool hasPrinted = false;
        for (size_t v = 0; v < numVar; ++v) {
            if (intptr_t Avc = A(v, c)) {
                if (hasPrinted) {
                    if (Avc > 0) {
                        os << " + ";
                    } else {
                        os << " - ";
                        Avc *= -1;
                    }
                }
                if (Avc != 1) {
                    if (Avc == -1) {
                        os << "-";
                    } else {
                        os << Avc;
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
        if (inequality) {
            os << " <= ";
        } else {
            os << " == ";
        }
        os << b[c] << std::endl;
    }
    return os;
}
template <typename T>
static std::ostream &
printConstraints(std::ostream &os, PtrMatrix<const intptr_t> A,
                 const llvm::SmallVectorImpl<T> &b, bool inequality = true,
                 size_t numAuxVar = 0) {
    return printConstraints(os, A, llvm::ArrayRef<T>(b), inequality, numAuxVar);
}



// does not preserve the order of columns, instead it swaps the `i`th column
// to the last, and truncates.
MULTIVERSION static void eraseConstraintImpl(PtrMatrix<intptr_t> A,
                                             llvm::MutableArrayRef<intptr_t> b,
                                             size_t i) {
    const auto [M, N] = A.size();
    const size_t lastCol = N - 1;
    if (lastCol != i) {
        // VECTORIZE
        for (size_t m = 0; m < M; ++m) {
            A(m, i) = A(m, lastCol);
        }
        b[i] = b[lastCol];
    }
}
MULTIVERSION static void eraseConstraintImpl(PtrMatrix<intptr_t> A,
                                             llvm::MutableArrayRef<MPoly> b,
                                             size_t i) {
    const auto [M, N] = A.size();
    const size_t lastCol = N - 1;
    if (lastCol != i) {
        // VECTORIZE
        for (size_t m = 0; m < M; ++m) {
            A(m, i) = A(m, lastCol);
        }
        b[i] = b[lastCol];
    }
}
template <typename T>
static void eraseConstraint(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                            size_t i) {
    eraseConstraintImpl(A, llvm::MutableArrayRef<T>(b), i);
    const size_t lastCol = A.numCol() - 1;
    A.truncateColumns(lastCol);
    b.truncate(lastCol);
}

template <typename T>
static void eraseConstraint(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                            size_t _i, size_t _j) {
    assert(_i != _j);
    size_t i = std::min(_i, _j);
    size_t j = std::max(_i, _j);
    const auto [M, N] = A.size();
    const size_t lastCol = N - 1;
    const size_t penuCol = lastCol - 1;
    if (j == penuCol) {
        // then we only need to copy one column (i to lastCol)
        eraseConstraint(A, b, i);
    } else if (i != penuCol) {
        // if i == penuCol, then j == lastCol
        // and we thus don't need to copy
        if (lastCol != i) {
            for (size_t m = 0; m < M; ++m) {
                A(m, i) = A(m, penuCol);
                A(m, j) = A(m, lastCol);
            }
            b[i] = b[penuCol];
            b[j] = b[lastCol];
        }
    }
    A.truncateColumns(penuCol);
    b.truncate(penuCol);
}

MULTIVERSION static size_t
substituteEqualityImpl(PtrMatrix<intptr_t> E, llvm::SmallVectorImpl<MPoly> &q,
                       const size_t i) {
    const auto [numVar, numColE] = E.size();
    size_t minNonZero = numVar + 1;
    size_t colMinNonZero = numColE;
    for (size_t j = 0; j < numColE; ++j) {
        if (E(i, j)) {
            // if (std::abs(E(i,j)) == 1){
            size_t nonZero = 0;
            VECTORIZE
            for (size_t v = 0; v < numVar; ++v) {
                nonZero += (E(v, j) != 0);
            }
            if (nonZero < minNonZero) {
                minNonZero = nonZero;
                colMinNonZero = j;
            }
        }
    }
    if (colMinNonZero == numColE) {
        return colMinNonZero;
    }
    auto Es = E.getCol(colMinNonZero);
    intptr_t Eis = Es[i];
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eis * E(v, j) - Eij * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eis, q[colMinNonZero], Eij);
            }
        }
    } else {
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                intptr_t g = std::gcd(Eij, Eis);
                intptr_t Ag = Eij / g;
                intptr_t Eg = Eis / g;
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eg * E(v, j) - Ag * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eg, q[colMinNonZero], Ag);
            }
        }
    }
    return colMinNonZero;
}
MULTIVERSION static size_t
substituteEqualityImpl(PtrMatrix<intptr_t> E,
                       llvm::SmallVectorImpl<intptr_t> &q, const size_t i) {
    const auto [numVar, numColE] = E.size();
    size_t minNonZero = numVar + 1;
    size_t colMinNonZero = numColE;
    for (size_t j = 0; j < numColE; ++j) {
        if (E(i, j)) {
            // if (std::abs(E(i,j)) == 1){
            size_t nonZero = 0;
            VECTORIZE
            for (size_t v = 0; v < numVar; ++v) {
                nonZero += (E(v, j) != 0);
            }
            if (nonZero < minNonZero) {
                minNonZero = nonZero;
                colMinNonZero = j;
            }
        }
    }
    if (colMinNonZero == numColE) {
        return colMinNonZero;
    }
    auto Es = E.getCol(colMinNonZero);
    intptr_t Eis = Es[i];
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eis * E(v, j) - Eij * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eis, q[colMinNonZero], Eij);
            }
        }
    } else {
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                intptr_t g = std::gcd(Eij, Eis);
                intptr_t Ag = Eij / g;
                intptr_t Eg = Eis / g;
                VECTORIZE
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eg * E(v, j) - Ag * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eg, q[colMinNonZero], Ag);
            }
        }
    }
    return colMinNonZero;
}
template <typename T>
static bool substituteEquality(IntMatrix auto &E, llvm::SmallVectorImpl<T> &q,
                               const size_t i) {
    size_t colMinNonZero = substituteEqualityImpl(E, q, i);
    if (colMinNonZero != E.numCol()) {
        eraseConstraint(E, q, colMinNonZero);
        return false;
    }
    return true;
}

template <typename T>
static size_t
substituteEqualityImpl(PtrMatrix<intptr_t> A, llvm::SmallVectorImpl<T> &b,
                       PtrMatrix<intptr_t> E, llvm::SmallVectorImpl<T> &q,
                       const size_t i) {
    const auto [numVar, numColE] = E.size();
    size_t minNonZero = numVar + 1;
    size_t colMinNonZero = numColE;
    for (size_t j = 0; j < numColE; ++j) {
        if (E(i, j)) {
            // if (std::abs(E(i,j)) == 1){
            size_t nonZero = 0;
            for (size_t v = 0; v < numVar; ++v) {
                nonZero += (E(v, j) != 0);
            }
            if (nonZero < minNonZero) {
                minNonZero = nonZero;
                colMinNonZero = j;
            }
        }
    }
    if (colMinNonZero == numColE) {
        return colMinNonZero;
    }
    auto Es = E.getCol(colMinNonZero);
    intptr_t Eis = Es[i];
    intptr_t s = 2 * (Eis > 0) - 1;
    // we now subsitute the equality expression with the minimum number
    // of terms.
    if (std::abs(Eis) == 1) {
        for (size_t j = 0; j < A.numCol(); ++j) {
            if (intptr_t Aij = A(i, j)) {
                // `A` contains inequalities; flipping signs is illegal
                intptr_t Ag = (s * Aij);
                intptr_t Eg = (s * Eis);
                for (size_t v = 0; v < numVar; ++v) {
                    A(v, j) = Eg * A(v, j) - Ag * Es[v];
                }
                Polynomial::fnmadd(b[j] *= Eg, q[colMinNonZero], Ag);
                // TODO: check if should drop
            }
        }
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eis * E(v, j) - Eij * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eis, q[colMinNonZero], Eij);
            }
        }
    } else {
        for (size_t j = 0; j < A.numCol(); ++j) {
            if (intptr_t Aij = A(i, j)) {
                intptr_t g = std::gcd(Aij, Eis);
                assert(g > 0);
                // `A` contains inequalities; flipping signs is illegal
                intptr_t Ag = (s * Aij) / g;
                intptr_t Eg = (s * Eis) / g;
                for (size_t v = 0; v < numVar; ++v) {
                    A(v, j) = Eg * A(v, j) - Ag * Es[v];
                }
                Polynomial::fnmadd(b[j] *= Eg, q[colMinNonZero], Ag);
                // TODO: check if should drop
            }
        }
        for (size_t j = 0; j < numColE; ++j) {
            if (j == colMinNonZero)
                continue;
            if (intptr_t Eij = E(i, j)) {
                intptr_t g = std::gcd(Eij, Eis);
                intptr_t Ag = Eij / g;
                intptr_t Eg = Eis / g;
                for (size_t v = 0; v < numVar; ++v) {
                    E(v, j) = Eg * E(v, j) - Ag * Es[v];
                }
                Polynomial::fnmadd(q[j] *= Eg, q[colMinNonZero], Ag);
            }
        }
    }
    return colMinNonZero;
}
template <typename T>
static bool substituteEquality(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                               IntMatrix auto &E, llvm::SmallVectorImpl<T> &q,
                               const size_t i) {

    size_t colMinNonZero = substituteEqualityImpl(A, b, E, q, i);
    if (colMinNonZero != E.numCol()) {
        eraseConstraint(E, q, colMinNonZero);
        return false;
    }
    return true;
}
// (A'*x <= b) && (E'*x == q)
template <typename T>
void removeExtraVariables(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                          IntMatrix auto &E, llvm::SmallVectorImpl<T> &q,
                          const size_t numNewVar) {
    // M variables
    // N inequality constraints
    // K equality constraints
    const auto [M, N] = A.size();
    const size_t K = E.numCol();
    assert(b.size() == N);
    assert(q.size() == K);
    // We add N augment variables (a_n), one per inequality constraint
    // a_n = b_n - (A'*x)_n, so a_n >= 0
    // C's first N columns contain constraints from A, last K from E
    // so we have C*x = [b; q]
    // C = [ I 0
    //       A E ]
    Matrix<intptr_t, 0, 0, 0> C(M + N, N + K);
    llvm::SmallVector<T> d(N + K);
    for (size_t n = 0; n < N; ++n) {
        C(n, n) = 1;
        for (size_t m = 0; m < M; ++m) {
            C(N + m, n) = A(m, n);
        }
        d[n] = b[n];
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t m = 0; m < M; ++m) {
            C(N + m, N + k) = E(m, k);
        }
        d[N + k] = q[k];
    }
    for (size_t o = M + N; o > numNewVar + N;) {
        --o;
        substituteEquality(C, d, o);
        if (C.numCol() > 1) {
            NormalForm::simplifyEqualityConstraints(C, d);
        }
    }
    A.resizeForOverwrite(numNewVar, N);
    b.resize_for_overwrite(N);
    size_t nC = 0, nA = 0, i = 0;
    // TODO: document behavior and actually verify correctness...
    // what if we have a_3 - a_7 + .... = ....
    // if another constraint reveals `a_3 = 33`, then this isn't unbounded
    // and we are not allowed to just drop the constraint...
    // However, if we had that, the a_3 would zero-out, so normal form
    // eliminates this possiblity.
    // Or, what if 2*a_3 - 3*a_7 == 1?
    // Then, we have a line, and a_3 - a_7 could still be anything.
    while ((i < N) && (nC < C.numCol()) && (nA < N)) {
        if (C(i++, nC)) {
            // if we have multiple positives, that still represents a positive
            // constraint, as augments are >=. if we have + and -, then the
            // relationship becomes unknown and thus dropped.
            bool otherNegative = false;
            for (size_t j = i; j < N; ++j) {
                otherNegative |= (C(j, nC) < 0);
            }
            if (otherNegative) {
                ++nC;
                continue;
            }
            bool duplicate = false;
            for (size_t k = 0; k < nA; ++k) {
                bool allMatch = true;
                for (size_t m = 0; m < numNewVar; ++m) {
                    allMatch &= (A(m, k) == C(N + m, nC));
                }
                if (allMatch) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                ++nC;
                continue;
            }
            for (size_t m = 0; m < numNewVar; ++m) {
                A(m, nA) = C(N + m, nC);
            }
            b[nA] = d[nC];
            ++nA;
            ++nC;
        }
    }
    A.truncateColumns(nA);
    b.truncate(nA);
    E.resizeForOverwrite(numNewVar, C.numCol() - nC);
    q.resize_for_overwrite(C.numCol() - nC);
    for (size_t i = 0; i < E.numCol(); ++i) {
        for (size_t m = 0; m < numNewVar; ++m) {
            E(m, i) = C(N + m, nC + i);
        }
        q[i] = d[nC + i];
    }
    // pruneBounds(A, b, E, q);
}

