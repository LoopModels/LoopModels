#pragma once
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
// #include <llvm/ADT/APInt.h> // llvm::Optional
#include <llvm/ADT/SmallVector.h>
#include <numeric>
#include <utility>

namespace NormalForm {

inline std::tuple<int64_t, int64_t, int64_t, int64_t> gcdxScale(int64_t a,
                                                                int64_t b) {
    if (std::abs(a) == 1) {
        return std::make_tuple(a, 0, a, b);
    } else {
        auto [g, p, q] = gcdx(a, b);
        return std::make_tuple(p, q, a / g, b / g);
    }
}
MULTIVERSION void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                  SquareMatrix<int64_t> &K, size_t i, size_t M,
                                  size_t N) {
    // std::cout << "M = " << M << "; N = " << N << "; i = " << i << std::endl;
    // printMatrix(std::cout, A) << std::endl;
    for (size_t j = i + 1; j < M; ++j) {
        int64_t Aii = A(i, i);
        if (int64_t Aji = A(j, i)) {
            // std::cout << "A(" << i << ", " << i << ") = " << Aii << "; A(" <<
            // j
            // << ", " << i << ") = " << Aji << std::endl;
            const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aji);
            // std::cout << "r = " << r << "; p = " << p << "; q = " << q <<
            // std::endl;

            VECTORIZE
            for (size_t k = 0; k < std::min(M, N); ++k) {
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
            VECTORIZE
            for (size_t k = N; k < M; ++k) {
                int64_t Kki = K(i, k);
                int64_t Kkj = K(j, k);
                K(i, k) = p * Kki + q * Kkj;
                K(j, k) = Aiir * Kkj - Aijr * Kki;
            }
            VECTORIZE
            for (size_t k = M; k < N; ++k) {
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
MULTIVERSION void zeroSubDiagonal(PtrMatrix<int64_t> A,
                                  SquareMatrix<int64_t> &K, size_t k, size_t M,
                                  size_t N) {
    int64_t Akk = A(k, k);
    if (Akk == -1) {
        VECTORIZE
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(k, i) *= -1;
            K(k, i) *= -1;
        }
        VECTORIZE
        for (size_t i = N; i < M; ++i) {
            K(k, i) *= -1;
        }
        VECTORIZE
        for (size_t i = M; i < N; ++i) {
            A(k, i) *= -1;
        }
    } else {
        assert(Akk == 1);
    }
    for (size_t z = 0; z < k; ++z) {
        // eliminate `A(k,z)`
        if (int64_t Akz = A(z, k)) {
            // A(k, k) == 1, so A(k,z) -= Akz * 1;
            VECTORIZE
            for (size_t i = 0; i < std::min(M, N); ++i) {
                A(z, i) -= Akz * A(k, i);
                K(z, i) -= Akz * K(k, i);
            }
            VECTORIZE
            for (size_t i = N; i < M; ++i) {
                K(z, i) -= Akz * K(k, i);
            }
            VECTORIZE
            for (size_t i = M; i < N; ++i) {
                A(z, i) -= Akz * A(k, i);
            }
        }
    }
}

MULTIVERSION inline bool pivotRows(PtrMatrix<int64_t> A, PtrMatrix<int64_t> K,
                                   size_t i, size_t M, size_t piv) {
    size_t j = piv;
    while (A(piv, i) == 0) {
        if (++piv == M) {
            return true;
        }
    }
    if (j != piv) {
        // const size_t N = A.numCol();
        // assert(N == K.numCol());
        // VECTORIZE
        // for (size_t n = 0; n < N; ++n) {
        //     std::swap(A(i, n), A(piv, n));
        //     std::swap(K(i, n), K(piv, n));
        // }
        swapRows(A, j, piv);
        swapRows(K, j, piv);
    }
    return false;
}
inline bool pivotRows(PtrMatrix<int64_t> A, SquareMatrix<int64_t> &K, size_t i,
                      size_t M) {
    return pivotRows(A, K, i, M, i);
}
template <typename T>
inline bool pivotRows(PtrMatrix<int64_t> A, llvm::SmallVectorImpl<T> &K,
                      size_t i, size_t M, size_t piv) {
    size_t j = piv;
    while (A(piv, i) == 0) {
        if (++piv == M) {
            return true;
        }
    }
    if (j != piv) {
        swapRows(A, j, piv);
        swapRows(K, j, piv);
    }
    return false;
}
inline bool pivotRows(PtrMatrix<int64_t> A, llvm::SmallVectorImpl<int64_t> &K,
                      size_t i, size_t N) {
    return pivotRows(A, K, i, N, i);
}
inline bool pivotRows(PtrMatrix<int64_t> A, size_t i, size_t M, size_t piv) {
    size_t j = piv;
    while (A(piv, i) == 0) {
        if (++piv == M) {
            return true;
        }
    }
    if (j != piv) {
        swapRows(A, j, piv);
    }
    return false;
}
inline bool pivotRows(PtrMatrix<int64_t> A, size_t i, size_t N) {
    return pivotRows(A, i, N, i);
}

MULTIVERSION void dropCol(PtrMatrix<int64_t> A, size_t i, size_t M, size_t N) {
    // if any rows are left, we shift them up to replace it
    if (i < N) {
        // std::cout << "A.numRow() = " << A.numRow()
        //           << "; A.numCol() = " << A.numCol() << "; M = " << M
        //           << "; N = " << N << std::endl;
        for (size_t m = 0; m < M; ++m) {
            VECTORIZE
            for (size_t n = i; n < N; ++n) {
                A(m, n) = A(m, n + 1);
            }
        }
    }
}

MULTIVERSION std::pair<SquareMatrix<int64_t>, llvm::SmallVector<unsigned>>
orthogonalizeBang(PtrMatrix<int64_t> A) {
    // we try to orthogonalize with respect to as many rows of `A` as we can
    // prioritizing earlier rows.
    auto [M, N] = A.size();
    // std::cout << "orthogonalizeBang; M = " << M << "; N = " << N <<
    // std::endl;
    SquareMatrix<int64_t> K = SquareMatrix<int64_t>::identity(M);
    llvm::SmallVector<unsigned> included;
    included.reserve(std::min(M, N));
    unsigned j = 0;
    for (size_t i = 0; i < std::min(M, N);) {
        // std::cout << "i = " << i << "; N = " << N << std::endl;
        // zero ith row
        if (pivotRows(A, K, i, M)) {
            // cannot pivot, this is a linear combination of previous
            // therefore, we drop the row
            dropCol(A, i, M, --N);
            ++j;
            continue;
        }
        zeroSupDiagonal(A, K, i, M, N);
        int64_t Aii = A(i, i);
        if (std::abs(Aii) != 1) {
            // including this row renders the matrix not unimodular!
            // therefore, we drop the row.
            dropCol(A, i, M, --N);
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
std::pair<SquareMatrix<int64_t>, llvm::SmallVector<unsigned>>
orthogonalize(IntMatrix A) {
    return orthogonalizeBang(A);
}

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A, size_t r,
                                         size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, r);
        if (int64_t Aij = A(j, r)) {
            const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Aki = A(c, k);
                int64_t Akj = A(j, k);
                A(c, k) = p * Aki + q * Akj;
                A(j, k) = Aiir * Akj - Aijr * Aki;
            }
        }
    }
}
MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                         llvm::SmallVectorImpl<int64_t> &b,
                                         size_t r, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, r);
        if (int64_t Aij = A(j, r)) {
            const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Aki = A(c, k);
                int64_t Akj = A(j, k);
                A(c, k) = p * Aki + q * Akj;
                A(j, k) = Aiir * Akj - Aijr * Aki;
            }
            int64_t bi = b[c];
            int64_t bj = b[j];
            b[c] = p * bi + q * bj;
            b[j] = Aiir * bj - Aijr * bi;
        }
    }
}
MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                         PtrMatrix<int64_t> B, size_t r,
                                         size_t c) {
    auto [M, N] = A.size();
    const size_t K = B.numCol();
    assert(M == B.numRow());
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, r);
        if (int64_t Aij = A(j, r)) {
            const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Ack = A(c, k);
                int64_t Ajk = A(j, k);
                A(c, k) = p * Ack + q * Ajk;
                A(j, k) = Aiir * Ajk - Aijr * Ack;
            }
            VECTORIZE
            for (size_t k = 0; k < K; ++k) {
                int64_t Bck = B(c, k);
                int64_t Bjk = B(j, k);
                B(c, k) = p * Bck + q * Bjk;
                B(j, k) = Aiir * Bjk - Aijr * Bck;
            }
        }
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<int64_t> A, size_t r,
                                           size_t c) {
    const size_t N = A.numCol();
    int64_t Akk = A(c, r);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < N; ++i) {
            A(c, i) *= -1;
        }
    }
    for (size_t z = 0; z < c; ++z) {
        // try to eliminate `A(k,z)`
        // if Akk == 1, then this zeros out Akz
        if (int64_t Akz = A(z, r)) {
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
            int64_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
            VECTORIZE
            for (size_t i = 0; i < N; ++i) {
                A(z, i) -= Akz * A(c, i);
            }
        }
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<int64_t> A,
                                           llvm::SmallVectorImpl<int64_t> &b,
                                           size_t r, size_t c) {
    const size_t N = A.numCol();
    int64_t Akk = A(c, r);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < N; ++i) {
            A(c, i) *= -1;
        }
        negate(b[c]);
    }
    for (size_t z = 0; z < c; ++z) {
        // try to eliminate `A(k,z)`
        // if Akk == 1, then this zeros out Akz
        if (int64_t Akz = A(z, r)) {
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
            int64_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
            VECTORIZE
            for (size_t i = 0; i < N; ++i) {
                A(z, i) -= Akz * A(c, i);
            }
            Polynomial::fnmadd(b[z], b[c], Akz);
        }
    }
}

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                         llvm::SmallVectorImpl<MPoly> &b,
                                         size_t rr, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, rr);
        if (int64_t Aij = A(j, rr)) {
            const auto [p, q, Aiir, Aijr] = gcdxScale(Aii, Aij);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Aki = A(c, k);
                int64_t Akj = A(j, k);
                A(c, k) = p * Aki + q * Akj;
                A(j, k) = Aiir * Akj - Aijr * Aki;
            }
            MPoly bi = std::move(b[c]);
            MPoly bj = std::move(b[j]);
            b[c] = p * bi + q * bj;
            b[j] = Aiir * std::move(bj) - Aijr * std::move(bi);
        }
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<int64_t> A,
                                           llvm::SmallVectorImpl<MPoly> &b,
                                           size_t r, size_t c) {
    const size_t N = A.numCol();
    int64_t Akk = A(c, r);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < N; ++i) {
            A(c, i) *= -1;
        }
        negate(b[c]);
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
                if (AkzOld < 0) {
                    Akz -= (AkzOld != (Akz * Akk));
                }
            }
            VECTORIZE
            for (size_t i = 0; i < N; ++i) {
                A(z, i) -= Akz * A(c, i);
            }
            Polynomial::fnmadd(b[z], b[c], Akz);
        }
    }
}
MULTIVERSION inline void reduceSubDiagonal(PtrMatrix<int64_t> A,
                                           PtrMatrix<int64_t> B, size_t r,
                                           size_t c) {
    const size_t N = A.numCol();
    const size_t K = B.numCol();
    int64_t Akk = A(c, r);
    if (Akk < 0) {
        Akk = -Akk;
        VECTORIZE
        for (size_t i = 0; i < N; ++i) {
            A(c, i) *= -1;
        }
        VECTORIZE
        for (size_t i = 0; i < K; ++i) {
            B(c, i) *= -1;
        }
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
                if (AkzOld < 0) {
                    Akz -= (AkzOld != (Akz * Akk));
                }
            }
            VECTORIZE
            for (size_t i = 0; i < N; ++i) {
                A(z, i) -= Akz * A(c, i);
            }
            VECTORIZE
            for (size_t i = 0; i < K; ++i) {
                B(z, i) -= Akz * B(c, i);
            }
        }
    }
}

MULTIVERSION
size_t simplifyEqualityConstraintsImpl(PtrMatrix<int64_t> E,
                                       llvm::SmallVectorImpl<MPoly> &q) {
    auto [M, N] = E.size();
    if (M == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= M)
            break;

        if (pivotRows(E, q, m, M, m - dec)) {
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
    size_t Mnew = M;
    while (allZero(E.getRow(Mnew - 1))) {
        --Mnew;
    }
    return Mnew;
}
MULTIVERSION size_t simplifyEqualityConstraintsImpl(
    PtrMatrix<int64_t> E, llvm::SmallVectorImpl<int64_t> &q) {
    auto [M, N] = E.size();
    if (M == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= M)
            break;

        if (pivotRows(E, q, m, M, m - dec)) {
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
    size_t Mnew = M;
    while (allZero(E.getRow(Mnew - 1))) {
        --Mnew;
    }
    return Mnew;
}

template <typename T>
static void simplifyEqualityConstraints(IntMatrix &E,
                                        llvm::SmallVectorImpl<T> &q) {

    size_t Mnew = simplifyEqualityConstraintsImpl(E, q);
    E.truncateRows(Mnew);
    q.resize(Mnew);
}
MULTIVERSION static void simplifyEqualityConstraintsImpl(PtrMatrix<int64_t> A,
                                                         PtrMatrix<int64_t> B) {
    auto [M, N] = A.size();
    if (M == 0)
        return;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= M)
            break;

        if (pivotRows(A, B, m, M, m - dec)) {
            // row is entirely zero
            ++dec;
            continue;
        }
        // E(m, m-dec) now contains non-zero
        // zero row `m` of every column to the right of `m - dec`
        zeroSupDiagonal(A, B, m, m - dec);
        // now we reduce the sub diagonal
        reduceSubDiagonal(A, B, m, m - dec);
    }
}
MULTIVERSION static void simplifyEqualityConstraints(IntMatrix &A,
                                                     IntMatrix &B) {
    simplifyEqualityConstraintsImpl(A, B);
    size_t Mnew = A.numRow();
    while (allZero(A.getRow(Mnew - 1))) {
        --Mnew;
    }
    A.truncateRows(Mnew);
    B.truncateRows(Mnew);
    return;
}
llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>>
hermite(IntMatrix A) {
    auto [M, N] = A.size();
    SquareMatrix<int64_t> U = SquareMatrix<int64_t>::identity(M);
    simplifyEqualityConstraintsImpl(A, U);
    return std::make_pair(std::move(A), std::move(U));
}

MULTIVERSION static void zeroSubDiagonal(IntMatrix &A, IntMatrix &B, size_t rr,
                                         size_t c) {
    const size_t N = A.numCol();
    const size_t K = B.numCol();
    for (size_t j = 0; j < c; ++j) {
        int64_t Aic = A(c, rr);
        if (int64_t Aij = A(j, rr)) {
            int64_t g = gcd(Aic, Aij);
            int64_t Aicr = Aic / g;
            int64_t Aijr = Aij / g;
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Ack = A(c, k) * Aijr;
                int64_t Ajk = A(j, k) * Aicr;
                A(j, k) = Ajk - Ack;
            }
            VECTORIZE
            for (size_t k = 0; k < K; ++k) {
                int64_t Bck = B(c, k) * Aijr;
                int64_t Bjk = B(j, k) * Aicr;
                B(j, k) = Bjk - Bck;
            }
        }
    }
}
MULTIVERSION void simplifySystem(IntMatrix &A, IntMatrix &B) {
    const auto [M, N] = A.size();
    if (M == 0)
        return;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= M) {
            break;
        }
        if (pivotRows(A, B, m, M, m - dec)) {
            ++dec;
            continue;
        }
        zeroSupDiagonal(A, B, m, m - dec);
        zeroSubDiagonal(A, B, m, m - dec);
    }
}
MULTIVERSION IntMatrix removeRedundantRows(IntMatrix A) {
    const auto [M, N] = A.size();
    if (M == 0)
        return A;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= M) {
            break;
        }
        if (pivotRows(A, m, M, m - dec)) {
            ++dec;
            continue;
        }
        zeroSupDiagonal(A, m, m - dec);
        reduceSubDiagonal(A, m, m - dec);
    }
    size_t R = M;
    while ((R > 0) && allZero(A.getRow(R - 1))) {
        R -= 1;
    }
    A.truncateRows(R);
    return A;
}

MULTIVERSION IntMatrix nullSpace(IntMatrix A) {
    const size_t M = A.numRow();
    IntMatrix B(IntMatrix::identity(M));
    simplifySystem(A, B);
    size_t R = M;
    while ((R > 0) && allZero(A.getRow(R - 1))) {
        R -= 1;
    }
    // slice B[R:end, :]
    // if R == 0, no need to truncate or copy
    if (R) {
        // we keep last D columns
        size_t D = M - R;
        size_t o = R * M;
        // we keep `D` columns
        VECTORIZE
        for (size_t d = 0; d < D * M; ++d) {
            B[d] = B[d + o];
        }
        B.truncateRows(D);
    }
    return B;
}

} // namespace NormalForm
