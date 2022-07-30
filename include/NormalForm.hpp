#pragma once
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include "EmptyArrays.hpp"
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
    if (std::abs(a) == 1)
        return std::make_tuple(a, 0, a, b);
    auto [g, p, q] = gcdx(a, b);
    return std::make_tuple(p, q, a / g, b / g);
}
// zero out below diagonal
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
    while (A(piv, i) == 0)
        if (++piv == M)
            return true;
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
inline bool pivotRows(PtrMatrix<int64_t> A, size_t i, size_t M, size_t piv) {
    size_t j = piv;
    while (A(piv, i) == 0)
        if (++piv == M)
            return true;
    if (j != piv)
        swapRows(A, j, piv);
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
            for (size_t n = i; n < N; ++n)
                A(m, n) = A(m, n + 1);
        }
    }
}

MULTIVERSION std::pair<SquareMatrix<int64_t>, llvm::SmallVector<unsigned>>
orthogonalizeBang(PtrMatrix<int64_t> A) {
    // we try to orthogonalize with respect to as many rows of `A` as we can
    // prioritizing earlier rows.
    auto [M, N] = A.size();
    SquareMatrix<int64_t> K = SquareMatrix<int64_t>::identity(M);
    llvm::SmallVector<unsigned> included;
    included.reserve(std::min(M, N));
    for (unsigned i = 0, j = 0; i < std::min(M, N); ++j) {
        // std::cout << "i = " << i << "; N = " << N << std::endl;
        // zero ith row
        if (pivotRows(A, K, i, M)) {
            // cannot pivot, this is a linear combination of previous
            // therefore, we drop the row
            dropCol(A, i, M, --N);
        } else {
            zeroSupDiagonal(A, K, i, M, N);
            int64_t Aii = A(i, i);
            if (std::abs(Aii) != 1) {
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
        for (size_t i = 0; i < N; ++i)
            A(c, i) *= -1;
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
            if (AkzOld < 0)
                Akz -= (AkzOld != (Akz * Akk));
            VECTORIZE
            for (size_t i = 0; i < N; ++i)
                A(z, i) -= Akz * A(c, i);
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
        for (size_t i = 0; i < N; ++i)
            A(c, i) *= -1;
        VECTORIZE
        for (size_t i = 0; i < K; ++i)
            B(c, i) *= -1;
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
                if (AkzOld < 0)
                    Akz -= (AkzOld != (Akz * Akk));
            }
            VECTORIZE
            for (size_t i = 0; i < N; ++i)
                A(z, i) -= Akz * A(c, i);
            VECTORIZE
            for (size_t i = 0; i < K; ++i)
                B(z, i) -= Akz * B(c, i);
        }
    }
}

void reduceColumn(PtrMatrix<int64_t> A, size_t c, size_t r) {
    zeroSupDiagonal(A, c, r);
    reduceSubDiagonal(A, c, r);
}
MULTIVERSION size_t simplifySystemImpl(PtrMatrix<int64_t> A,
                                       size_t colInit = 0) {
    auto [M, N] = A.size();
    for (size_t r = 0, c = colInit; c < N && r < M; ++c)
        if (!pivotRows(A, c, M, r))
            reduceColumn(A, c, r++);
    size_t Mnew = M;
    while (allZero(A.getRow(Mnew - 1)))
        --Mnew;
    return Mnew;
}
constexpr static void simplifySystem(EmptyMatrix<int64_t>, size_t = 0) {}
static void simplifySystem(IntMatrix &E, size_t colInit = 0) {
    size_t Mnew = simplifySystemImpl(E, colInit);
    E.truncateRows(Mnew);
}
void reduceColumn(PtrMatrix<int64_t> A, PtrMatrix<int64_t> B, size_t c,
                  size_t r) {
    zeroSupDiagonal(A, B, c, r);
    reduceSubDiagonal(A, B, c, r);
}
MULTIVERSION static void simplifySystemImpl(PtrMatrix<int64_t> A,
                                            PtrMatrix<int64_t> B) {
    auto [M, N] = A.size();
    for (size_t r = 0, c = 0; c < N && r < M; ++c)
        if (!pivotRows(A, B, c, M, r))
            reduceColumn(A, B, c, r++);
}
MULTIVERSION static void simplifySystem(IntMatrix &A, IntMatrix &B) {
    simplifySystemImpl(A, B);
    size_t Mnew = A.numRow();
    bool need_trunc = false;
    while (allZero(A.getRow(Mnew - 1))) {
        --Mnew;
        need_trunc = true;
    }
    if (need_trunc) {
        A.truncateRows(Mnew);
        B.truncateRows(Mnew);
    }
    return;
}
std::pair<IntMatrix, SquareMatrix<int64_t>>
hermite(IntMatrix A) {
    auto [M, N] = A.size();
    SquareMatrix<int64_t> U{SquareMatrix<int64_t>::identity(M)};
    simplifySystemImpl(A, U);
    return std::make_pair(std::move(A), std::move(U));
}

// zero A(i,k) with A(j,k)
inline int64_t zeroWithRowOperation(PtrMatrix<int64_t> A, size_t i, size_t j,
                                    size_t k, size_t f) {
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
        std::cout << "g = " << g << std::endl;
        if (g > 1) {
            for (size_t l = 0; l < A.numCol(); ++l)
                if (int64_t Ail = A(i, l))
                    A(i, l) = Ail / g;
            ret /= g;
        }
        return ret;
    }
    return f;
}

// use row `r` to zero the remaining rows of column `c`
MULTIVERSION static void zeroColumn(IntMatrix &A, IntMatrix &B, size_t c,
                                    size_t r) {
    const size_t N = A.numCol();
    const size_t K = B.numCol();
    const size_t M = A.numRow();
    assert(M == B.numRow());
    for (size_t j = 0; j < r; ++j) {
        int64_t Arc = A(r, c);
        if (int64_t Ajc = A(j, c)) {
            int64_t g = gcd(Arc, Ajc);
            Arc /= g;
            Ajc /= g;
            VECTORIZE
            for (size_t k = 0; k < N; ++k)
                A(j, k) = Arc * A(j, k) - Ajc * A(r, k);
            VECTORIZE
            for (size_t k = 0; k < K; ++k)
                B(j, k) = Arc * B(j, k) - Ajc * B(r, k);
        }
    }
    // greater rows in previous columns have been zeroed out
    // therefore it is safe to use them for row operations with this row
    for (size_t j = r + 1; j < M; ++j) {
        int64_t Arc = A(r, c);
        if (int64_t Ajc = A(j, c)) {
            const auto [p, q, Arcr, Ajcr] = gcdxScale(Arc, Ajc);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Ark = A(r, k);
                int64_t Ajk = A(j, k);
                A(r, k) = q * Ajk + p * Ark;
                A(j, k) = Arcr * Ajk - Ajcr * Ark;
            }
            VECTORIZE
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
MULTIVERSION static void zeroColumn(IntMatrix &A, size_t c, size_t r) {
    const size_t N = A.numCol();
    const size_t M = A.numRow();
    for (size_t j = 0; j < r; ++j) {
        int64_t Arc = A(r, c);
        if (int64_t Ajc = A(j, c)) {
            int64_t g = gcd(Arc, Ajc);
            Arc /= g;
            Ajc /= g;
            VECTORIZE
            for (size_t k = 0; k < N; ++k)
                A(j, k) = Arc * A(j, k) - Ajc * A(r, k);
        }
    }
    // greater rows in previous columns have been zeroed out
    // therefore it is safe to use them for row operations with this row
    for (size_t j = r + 1; j < M; ++j) {
        int64_t Arc = A(r, c);
        if (int64_t Ajc = A(j, c)) {
            const auto [p, q, Arcr, Ajcr] = gcdxScale(Arc, Ajc);
            VECTORIZE
            for (size_t k = 0; k < N; ++k) {
                int64_t Ark = A(r, k);
                int64_t Ajk = A(j, k);
                A(r, k) = q * Ajk + p * Ark;
                A(j, k) = Arcr * Ajk - Ajcr * Ark;
            }
        }
    }
}

MULTIVERSION int pivotRows2(PtrMatrix<int64_t> A, size_t i, size_t M,
                            size_t piv) {
    size_t j = piv;
    while (A(piv, i) == 0)
        if (++piv == M)
            return -1;
    if (j != piv)
        swapRows(A, j, piv);
    return piv;
}
MULTIVERSION void bareiss(IntMatrix &A, llvm::SmallVectorImpl<size_t> &pivots) {
    const auto [M, N] = A.size();
    int64_t prev = 1;
    for (size_t r = 0, c = 0; c < N && r < M; ++c) {
        auto piv = pivotRows2(A, c, M, r);
        if (piv >= 0) {
            pivots.push_back(piv);
            for (size_t k = r + 1; k < M; ++k) {
                for (size_t j = c + 1; j < N; ++j) {
                    auto Akj_u = A(r, c) * A(k, j) - A(k, c) * A(r, j);
                    auto Akj = Akj_u / prev;
                    auto rr = Akj_u % prev;
                    assert(rr == 0);
                    A(k, j) = Akj;
                }
                A(k, r) = 0;
            }
            prev = A(r, c);
            ++r;
        }
    }
}

MULTIVERSION auto bareiss(IntMatrix &A) {
    llvm::SmallVector<size_t, 16> pivots;
    bareiss(A, pivots);
    return pivots;
}

// assume last col
// MULTIVERSION void solveSystem(IntMatrix &A, size_t K) {
//     const auto [M, N] = A.size();
//     if (M == 0)
//         return;
//     size_t n = 0;
//     for (size_t dec = 0; n < K; ++n) {
//         if (n - dec >= M)
//             break;
//         if (pivotRows(A, n, M, n - dec)) {
//             ++dec;
//         } else {
//             zeroColumn(A, n, n - dec);
//         }
//     }
//     for (size_t c = 0, dec = 0; c < n; ++c) {
//         size_t r = c - dec;
//         switch (int64_t Arc = A(r, c)) {
//         case 0:
//             ++dec;
//         case 1:
//             break;
//         default:
//             A(r, c) = 1;
//             for (size_t l = n; l < N; ++l)
//                 A(r, l) /= Arc;
//         }
//     }
// }
MULTIVERSION void solveSystem(IntMatrix &A, IntMatrix &B) {
    const auto [M, N] = A.size();
    for (size_t r = 0, c = 0; c < N && r < M; ++c)
        if (!pivotRows(A, B, c, M, r))
            zeroColumn(A, B, c, r++);
}
// diagonalizes A(1:K,1:K)
MULTIVERSION void solveSystem(IntMatrix &A, size_t K) {
    const auto [M, N] = A.size();
    for (size_t r = 0, c = 0; c < K && r < M; ++c)
        if (!pivotRows(A, c, M, r))
            zeroColumn(A, c, r++);
}

// returns `true` if the solve failed, `false` otherwise
// diagonals contain denominators.
// Assumes the last column is the vector to solve for.
MULTIVERSION void solveSystem(IntMatrix &A) { solveSystem(A, A.numCol() - 1); }
MULTIVERSION IntMatrix removeRedundantRows(IntMatrix A) {
    const auto [M, N] = A.size();
    for (size_t r = 0, c = 0; c < M && r < M; ++c)
        if (!pivotRows(A, c, M, r)) {
            zeroSupDiagonal(A, c, r++);
            reduceSubDiagonal(A, c, r++);
        }
    size_t R = M;
    while ((R > 0) && allZero(A.getRow(R - 1))) {
        --R;
    }
    A.truncateRows(R);
    return A;
}

MULTIVERSION IntMatrix nullSpace(IntMatrix A) {
    const size_t M = A.numRow();
    IntMatrix B(IntMatrix::identity(M));
    solveSystem(A, B);
    size_t R = M;
    while ((R > 0) && allZero(A.getRow(R - 1)))
        --R;
    // slice B[R:end, :]
    // if R == 0, no need to truncate or copy
    if (R) {
        // we keep last D columns
        size_t D = M - R;
        size_t o = R * M;
        // we keep `D` columns
        VECTORIZE
        for (size_t d = 0; d < D * M; ++d)
            B[d] = B[d + o];
        B.truncateRows(D);
    }
    return B;
}

} // namespace NormalForm
