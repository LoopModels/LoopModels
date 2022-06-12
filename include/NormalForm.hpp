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

void reduceSubDiagonal(PtrMatrix<int64_t> A, SquareMatrix<int64_t> &K, size_t k,
                       size_t M, size_t N) {
    int64_t Akk = A(k, k);
    if (Akk < 0) {
        Akk = -Akk;
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(k, i) *= -1;
            K(k, i) *= -1;
        }
        for (size_t i = N; i < M; ++i) {
            K(k, i) *= -1;
        }
        for (size_t i = M; i < N; ++i) {
            A(k, i) *= -1;
        }
    }
    for (size_t z = 0; z < k; ++z) {
        // try to eliminate `A(k,z)`
        int64_t Akz = A(z, k);
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
            int64_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
        } else {
            continue;
        }
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(z, i) -= Akz * A(k, i);
            K(z, i) -= Akz * K(k, i);
        }
        for (size_t i = N; i < M; ++i) {
            K(z, i) -= Akz * K(k, i);
        }
        for (size_t i = M; i < N; ++i) {
            A(z, i) -= Akz * A(k, i);
        }
    }
}
void zeroSupDiagonal(PtrMatrix<int64_t> A, SquareMatrix<int64_t> &K, size_t i,
                     size_t M, size_t N) {
    // std::cout << "M = " << M << "; N = " << N << "; i = " << i << std::endl;
    // printMatrix(std::cout, A) << std::endl;
    for (size_t j = i + 1; j < M; ++j) {
        int64_t Aii = A(i, i);
        int64_t Aji = A(j, i);
        // std::cout << "A(" << i << ", " << i << ") = " << Aii << "; A(" << j
                  // << ", " << i << ") = " << Aji << std::endl;
        auto [r, p, q] = gcdx(Aii, Aji);
        // std::cout << "r = " << r << "; p = " << p << "; q = " << q << std::endl;
        int64_t Aiir = Aii / r;
        int64_t Aijr = Aji / r;
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
        for (size_t k = N; k < M; ++k) {
            int64_t Kki = K(i, k);
            int64_t Kkj = K(j, k);
            K(i, k) = p * Kki + q * Kkj;
            K(j, k) = Aiir * Kkj - Aijr * Kki;
        }
        for (size_t k = M; k < N; ++k) {
            int64_t Aki = A(i, k);
            int64_t Akj = A(j, k);
            A(i, k) = p * Aki + q * Akj;
            A(j, k) = Aiir * Akj - Aijr * Aki;
        }
    }
}
void zeroSubDiagonal(PtrMatrix<int64_t> A, SquareMatrix<int64_t> &K, size_t k,
                     size_t M, size_t N) {
    int64_t Akk = A(k, k);
    if (Akk == -1) {
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(k, i) *= -1;
            K(k, i) *= -1;
        }
        for (size_t i = N; i < M; ++i) {
            K(k, i) *= -1;
        }
        for (size_t i = M; i < N; ++i) {
            A(k, i) *= -1;
        }
    } else {
        assert(Akk == 1);
    }
    for (size_t z = 0; z < k; ++z) {
        // eliminate `A(k,z)`
        int64_t Akz = A(z, k);
        if (Akz == 0) {
            continue;
        }
        // A(k, k) == 1, so A(k,z) -= Akz * 1;
        for (size_t i = 0; i < std::min(M, N); ++i) {
            A(z, i) -= Akz * A(k, i);
            K(z, i) -= Akz * K(k, i);
        }
        for (size_t i = N; i < M; ++i) {
            K(z, i) -= Akz * K(k, i);
        }
        for (size_t i = M; i < N; ++i) {
            A(z, i) -= Akz * A(k, i);
        }
    }
}

inline bool pivotRows(PtrMatrix<int64_t> A, SquareMatrix<int64_t> &K, size_t i,
                      size_t M, size_t piv) {
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

// extend HNF form from (0:i-1,0:N-1) block to the (0:i,0:N-1) block
bool extendHNFRow(IntMatrix &A, SquareMatrix<int64_t> &K, size_t i, size_t M,
                  size_t N) {
    if (pivotRows(A, K, i, M)) {
        return true;
    }
    assert(A(i, i) != 0 && "Diagonal of A is 0.");
    zeroSupDiagonal(A, K, i, M, N);
    reduceSubDiagonal(A, K, i, M, N);
    return false;
}

llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>>
hermite(IntMatrix A) {
    auto [M, N] = A.size();
    SquareMatrix<int64_t> K = SquareMatrix<int64_t>::identity(M);
    for (size_t i = 0; i < N; ++i) {
        if (extendHNFRow(A, K, i, M, N)) {
            return {}; // failed
        }
    }
    return std::make_pair(std::move(A), std::move(K));
}

void dropCol(PtrMatrix<int64_t> A, size_t i, size_t M, size_t N) {
    // if any rows are left, we shift them up to replace it
    if (i < N) {
        // std::cout << "A.numRow() = " << A.numRow()
        //           << "; A.numCol() = " << A.numCol() << "; M = " << M
        //           << "; N = " << N << std::endl;
        for (size_t m = 0; m < M; ++m) {
            for (size_t n = i; n < N; ++n) {
                A(m, n) = A(m, n + 1);
            }
        }
    }
}

std::pair<SquareMatrix<int64_t>, llvm::SmallVector<unsigned>>
orthogonalizeBang(PtrMatrix<int64_t> A) {
    // we try to orthogonalize with respect to as many rows of `A` as we can
    // prioritizing earlier rows.
    auto [M, N] = A.size();
    // std::cout << "orthogonalizeBang; M = " << M << "; N = " << N << std::endl;
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

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                         llvm::SmallVectorImpl<int64_t> &b,
                                         size_t r, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, r);
        int64_t Aij = A(j, r);
        auto [r, p, q] = gcdx(Aii, Aij);
        int64_t Aiir = Aii / r;
        int64_t Aijr = Aij / r;
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
        int64_t Akz = A(z, r);
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
            int64_t AkzOld = Akz;
            Akz /= Akk;
            if (AkzOld < 0) {
                Akz -= (AkzOld != (Akz * Akk));
            }
        } else {
            continue;
        }
        VECTORIZE
        for (size_t i = 0; i < N; ++i) {
            A(z, i) -= Akz * A(c, i);
        }
        Polynomial::fnmadd(b[z], b[c], Akz);
    }
}

MULTIVERSION inline void zeroSupDiagonal(PtrMatrix<int64_t> A,
                                         llvm::SmallVectorImpl<MPoly> &b,
                                         size_t rr, size_t c) {
    auto [M, N] = A.size();
    for (size_t j = c + 1; j < M; ++j) {
        int64_t Aii = A(c, rr);
        if (int64_t Aij = A(j, rr)) {
            if (std::abs(Aii) == 1) {
                VECTORIZE
                for (size_t k = 0; k < N; ++k) {
                    A(j, k) = Aii * A(j, k) - Aij * A(c, k);
                }
                Polynomial::fnmadd(b[j] *= Aii, b[c], Aij);
            } else {
                auto [r, p, q] = gcdx(Aii, Aij);
                int64_t Aiir = Aii / r;
                int64_t Aijr = Aij / r;
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
        int64_t Akz = A(z, r);
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

MULTIVERSION
size_t simplifyEqualityConstraintsImpl(PtrMatrix<int64_t> E,
                                       llvm::SmallVectorImpl<MPoly> &q) {
    auto [M, N] = E.size();
    if (N == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= N)
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
    size_t Nnew = N;
    while (allZero(E.getCol(Nnew - 1))) {
        --Nnew;
    }
    return Nnew;
}
MULTIVERSION size_t simplifyEqualityConstraintsImpl(
    PtrMatrix<int64_t> E, llvm::SmallVectorImpl<int64_t> &q) {
    auto [M, N] = E.size();
    if (N == 0)
        return 0;
    size_t dec = 0;
    for (size_t m = 0; m < N; ++m) {
        if (m - dec >= N)
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
    while (allZero(E.getCol(Mnew - 1))) {
        --Mnew;
    }
    return Mnew;
}

template <typename T, size_t L>
void simplifyEqualityConstraints(Matrix<int64_t, 0, 0, L> &E,
                                 llvm::SmallVectorImpl<T> &q) {

    size_t Mnew = simplifyEqualityConstraintsImpl(PtrMatrix<int64_t>(E), q);
    E.truncateRows(Mnew);
    q.resize(Mnew);
}

} // namespace NormalForm
