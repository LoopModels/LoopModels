#pragma once
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h> // llvm::Optional
#include <llvm/ADT/SmallVector.h>
#include <numeric>
#include <utility>

namespace NormalForm {

void reduceSubDiagonal(IntMatrix auto &A, IntMatrix auto &K, size_t k, size_t M,
                       size_t N) {
    intptr_t Akk = A(k, k);
    if (Akk < 0) {
        Akk = -Akk;
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(i, k) *= -1;
            K(i, k) *= -1;
        }
        for (size_t i = M; i < N; ++i) {
            K(i, k) *= -1;
        }
        for (size_t i = N; i < M; ++i) {
            A(i, k) *= -1;
        }
    }
    for (size_t z = 0; z < k; ++z) {
        // try to eliminate `A(k,z)`
        intptr_t Akz = A(k, z);
        // if Akk == 1, then this zeros out Akz
        if (Akz) {
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
            intptr_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
        } else {
            continue;
        }
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(i, z) -= Akz * A(i, k);
            K(i, z) -= Akz * K(i, k);
        }
        for (size_t i = M; i < N; ++i) {
            K(i, z) -= Akz * K(i, k);
        }
        for (size_t i = N; i < M; ++i) {
            A(i, z) -= Akz * A(i, k);
        }
    }
}
void zeroSubDiagonal(IntMatrix auto &A, IntMatrix auto &K, size_t k, size_t M,
                     size_t N) {
    intptr_t Akk = A(k, k);
    if (Akk == -1) {
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(i, k) *= -1;
            K(i, k) *= -1;
        }
        for (size_t i = M; i < N; ++i) {
            K(i, k) *= -1;
        }
        for (size_t i = N; i < M; ++i) {
            A(i, k) *= -1;
        }
    } else {
        assert(Akk == 1);
    }
    for (size_t z = 0; z < k; ++z) {
        // eliminate `A(k,z)`
        intptr_t Akz = A(k, z);
        if (Akz == 0) {
            continue;
        }
        // A(k, k) == 1, so A(k,z) -= Akz * 1;
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(i, z) -= Akz * A(i, k);
            K(i, z) -= Akz * K(i, k);
        }
        for (size_t i = M; i < N; ++i) {
            K(i, z) -= Akz * K(i, k);
        }
        for (size_t i = N; i < M; ++i) {
            A(i, z) -= Akz * A(i, k);
        }
    }
}
// extend HNF form from (0:i-1,0:i-1) block to the (0:i,0:i) block
bool extendHNF(IntMatrix auto &A, IntMatrix auto &K, size_t i, size_t M,
               size_t N) {
    size_t piv = N;
    while (true) {
        zeroSupDiagonal(A, K, i, M, N);
        for (size_t j = 1; j < std::min(i, M); ++j) {
            reduceSubDiagonal(A, K, j, M, N);
        }
        if (i >= M) {
            return false;
        } else if (A(i, i)) {
            // we've succesfully brought the block into HNF
            reduceSubDiagonal(A, K, i, M, N);
            return false;
        }
        // We have not succesfully brought it into HNF form,
        // therefore we'll try a different column.
        //
        // Is there maybe a better strategy than blindly trying
        // columns until we find one that works? Also, searching
        // from N-1...i+1 means if we must pivot for more than one
        // `i`, we'll start going over discarded ones a second time.
        // These may be more likely than average to fail again.
        // Perhaps we could check their `A(i,i)` value to be a bit
        // smarter about it; given that many of the rows would've
        // already been zerod by the previous try, it might be less
        // likely the `A(i,i)` will get an update than a random row.
        if (--piv == i) {
            // rank deficient!!!
            return true;
        }
        swapCols(A, i, piv);
        swapCols(K, i, piv);
    }
}

template <IntMatrix T>
llvm::Optional<std::pair<T, SquareMatrix<intptr_t>>> hermite2(T A) {
    auto [M, N] = A.size();
    SquareMatrix<intptr_t> K = SquareMatrix<intptr_t>::identity(N);
    for (size_t i = 1; i < N; ++i) {
        if (extendHNF(A, K, i, M, N)) {
            return {}; // failed
        }
    }
    return std::make_pair(std::move(A), std::move(K));
}

void zeroSupDiagonal(IntMatrix auto &A, IntMatrix auto &K, size_t i, size_t M,
                     size_t N) {
    for (size_t j = i + 1; j < N; ++j) {
        intptr_t Aii = A(i, i);
        intptr_t Aij = A(i, j);
        auto [r, p, q] = gcdx(Aii, Aij);
        intptr_t Aiir = Aii / r;
        intptr_t Aijr = Aij / r;
        for (size_t k = 0; k < std::min(M, N); ++k) {
            intptr_t Aki = A(k, i);
            intptr_t Akj = A(k, j);
            intptr_t Kki = K(k, i);
            intptr_t Kkj = K(k, j);
            // when k == i, then
            // p * Aii + q * Akj == r, so we set A(i,i) = r
            A(k, i) = p * Aki + q * Akj;
            // Aii/r * Akj - Aij/r * Aki = 0
            A(k, j) = Aiir * Akj - Aijr * Aki;
            // Mirror for K
            K(k, i) = p * Kki + q * Kkj;
            K(k, j) = Aiir * Kkj - Aijr * Kki;
        }
        for (size_t k = M; k < N; ++k) {
            intptr_t Kki = K(k, i);
            intptr_t Kkj = K(k, j);
            K(k, i) = p * Kki + q * Kkj;
            K(k, j) = Aiir * Kkj - Aijr * Kki;
        }
        for (size_t k = N; k < M; ++k) {
            intptr_t Aki = A(k, i);
            intptr_t Akj = A(k, j);
            A(k, i) = p * Aki + q * Akj;
            A(k, j) = Aiir * Akj - Aijr * Aki;
        }
    }
}

inline bool pivotCols(IntMatrix auto &A, auto &K, size_t i, size_t N,
                      size_t piv) {
    size_t j = piv;
    while (A(i, piv) == 0) {
        if (++piv == N) {
            return true;
        }
    }
    if (j != piv) {
        swapCols(A, j, piv);
        swapCols(K, j, piv);
    }
    return false;
}
inline bool pivotCols(IntMatrix auto &A, auto &K, size_t i, size_t N) {
    return pivotCols(A, K, i, N, i);
}

// extend HNF form from (0:i-1,0:N-1) block to the (0:i,0:N-1) block
bool extendHNFRow(IntMatrix auto &A, IntMatrix auto &K, size_t i, size_t M,
                  size_t N) {
    if (pivotCols(A, K, i, N)) {
        return true;
    }
    zeroSupDiagonal(A, K, i, M, N);
    reduceSubDiagonal(A, K, i, M, N);
    return false;
}

template <IntMatrix T>
llvm::Optional<std::pair<T, SquareMatrix<intptr_t>>> hermite(T A) {
    auto [M, N] = A.size();
    SquareMatrix<intptr_t> K = SquareMatrix<intptr_t>::identity(N);
    for (size_t i = 0; i < M; ++i) {
        if (extendHNFRow(A, K, i, M, N)) {
            return {}; // failed
        }
    }
    return std::make_pair(std::move(A), std::move(K));
}

void dropRow(IntMatrix auto &A, size_t i, size_t M, size_t N) {
    // if any rows are left, we shift them up to replace it
    if (i < M) {
        for (size_t n = 0; n < N; ++n) {
            for (size_t m = i; m < M; ++m) {
                A(m, n) = A(m + 1, n);
            }
        }
    }
}

std::pair<SquareMatrix<intptr_t>, llvm::SmallVector<unsigned>>
orthogonalize(PtrMatrix<intptr_t> A) {
    // we try to orthogonalize with respect to as many rows of `A` as we can
    // prioritizing earlier rows.
    auto [M, N] = A.size();
    SquareMatrix<intptr_t> K = SquareMatrix<intptr_t>::identity(N);
    llvm::SmallVector<unsigned> included;
    included.reserve(std::min(M, N));
    unsigned j = 0;
    for (size_t i = 0; i < std::min(M, N);) {
        // zero ith row
        if (pivotCols(A, K, i, N)) {
            // cannot pivot, this is a linear combination of previous
            // therefore, we drop the row
            dropRow(A, i, --M, N);
            ++j;
            continue;
        }
        zeroSupDiagonal(A, K, i, M, N);
        intptr_t Aii = A(i, i);
        if (std::abs(Aii) != 1) {
            // including this row renders the matrix not unimodular!
            // therefore, we drop the row.
            dropRow(A, i, --M, N);
            ++j;
            continue;
        } else {
            // we zero the sub diagonal
            zeroSubDiagonal(A, K, i, M, N);
        }
        included.push_back(j);
        ++j;
        ++i;
    }
    return std::make_pair(std::move(K), std::move(included));
}

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<intptr_t> A,
                                         llvm::SmallVectorImpl<intptr_t> &b,
                                         size_t r, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < N; ++j) {
        intptr_t Aii = A(r, c);
        intptr_t Aij = A(r, j);
        auto [r, p, q] = gcdx(Aii, Aij);
        intptr_t Aiir = Aii / r;
        intptr_t Aijr = Aij / r;
        VECTORIZE
        for (size_t k = 0; k < M; ++k) {
            intptr_t Aki = A(k, c);
            intptr_t Akj = A(k, j);
            A(k, c) = p * Aki + q * Akj;
            A(k, j) = Aiir * Akj - Aijr * Aki;
        }
        intptr_t bi = b[c];
        intptr_t bj = b[j];
        b[c] = p * bi + q * bj;
        b[j] = Aiir * bj - Aijr * bi;
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<intptr_t> A,
                                           llvm::SmallVectorImpl<intptr_t> &b,
                                           size_t r, size_t c) {
    const size_t M = A.numRow();
    intptr_t Akk = A(r, c);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < M; ++i) {
            A(i, c) *= -1;
        }
        negate(b[c]);
    }
    for (size_t z = 0; z < c; ++z) {
        // try to eliminate `A(k,z)`
        intptr_t Akz = A(r, z);
        // if Akk == 1, then this zeros out Akz
        if (Akz) {
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
            intptr_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
        } else {
            continue;
        }
        VECTORIZE
        for (size_t i = 0; i < M; ++i) {
            A(i, z) -= Akz * A(i, c);
        }
        Polynomial::fnmadd(b[z], b[c], Akz);
    }
}

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<intptr_t> A,
                                         llvm::SmallVectorImpl<MPoly> &b,
                                         size_t rr, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < N; ++j) {
        intptr_t Aii = A(rr, c);
        if (intptr_t Aij = A(rr, j)) {
            if (std::abs(Aii) == 1) {
                VECTORIZE
                for (size_t k = 0; k < M; ++k) {
                    A(k, j) = Aii * A(k, j) - Aij * A(k, c);
                }
                Polynomial::fnmadd(b[j] *= Aii, b[c], Aij);
            } else {
                auto [r, p, q] = gcdx(Aii, Aij);
                intptr_t Aiir = Aii / r;
                intptr_t Aijr = Aij / r;
                VECTORIZE
                for (size_t k = 0; k < M; ++k) {
                    intptr_t Aki = A(k, c);
                    intptr_t Akj = A(k, j);
                    A(k, c) = p * Aki + q * Akj;
                    A(k, j) = Aiir * Akj - Aijr * Aki;
                }
                MPoly bi = std::move(b[c]);
                MPoly bj = std::move(b[j]);
                b[c] = p * bi + q * bj;
                b[j] = Aiir * std::move(bj) - Aijr * std::move(bi);
            }
        }
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<intptr_t> A,
                                           llvm::SmallVectorImpl<MPoly> &b,
                                           size_t r, size_t c) {
    const size_t M = A.numRow();
    intptr_t Akk = A(r, c);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < M; ++i) {
            A(i, c) *= -1;
        }
        negate(b[c]);
    }
    for (size_t z = 0; z < c; ++z) {
        // try to eliminate `A(k,z)`
        intptr_t Akz = A(r, z);
        // if Akk == 1, then this zeros out Akz
        if (Akz == 0) {
            continue;
        } else if (Akk != 1) {
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
            intptr_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
        }
        VECTORIZE
        for (size_t i = 0; i < M; ++i) {
            A(i, z) -= Akz * A(i, c);
        }
        Polynomial::fnmadd(b[z], b[c], Akz);
    }
}

MULTIVERSION
size_t simplifyEqualityConstraintsImpl(PtrMatrix<intptr_t> E,
                                       llvm::SmallVectorImpl<MPoly> &q) {
    auto [M, N] = E.size();
    if (N == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < M; ++m) {
        if (m - dec >= N)
            break;

        if (pivotCols(E, q, m, N, m - dec)) {
            // row is entirely zero
            ++dec;
            continue;
        }
        // E(m, m-dec) now contains non-zero
        // zero row `m` of every column to the right of `m - dec`
        zeroSupDiagonal(E, q, m, m - dec);
        // now we reduce the sub diagonal
        reduceSubDiagonal(E, q, m, m - dec);
    }
    size_t Nnew = N;
    while (allZero(E.getCol(Nnew - 1))) {
        --Nnew;
    }
    return Nnew;
}
MULTIVERSION size_t simplifyEqualityConstraintsImpl(
    PtrMatrix<intptr_t> E, llvm::SmallVectorImpl<intptr_t> &q) {
    auto [M, N] = E.size();
    if (N == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < M; ++m) {
        if (m - dec >= N)
            break;

        if (pivotCols(E, q, m, N, m - dec)) {
            // row is entirely zero
            ++dec;
            continue;
        }
        // E(m, m-dec) now contains non-zero
        // zero row `m` of every column to the right of `m - dec`
        zeroSupDiagonal(E, q, m, m - dec);
        // now we reduce the sub diagonal
        reduceSubDiagonal(E, q, m, m - dec);
    }
    size_t Nnew = N;
    while (allZero(E.getCol(Nnew - 1))) {
        --Nnew;
    }
    return Nnew;
}

template <typename T, size_t L>
void simplifyEqualityConstraints(Matrix<intptr_t, 0, 0, L> &E,
                                 llvm::SmallVectorImpl<T> &q) {

    size_t Nnew = simplifyEqualityConstraintsImpl(PtrMatrix<intptr_t>(E), q);
    E.truncateColumns(Nnew);
    q.resize(Nnew);
}

} // namespace NormalForm
