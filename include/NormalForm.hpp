#pragma once
#include "./Math.hpp"
#include "IntermediateRepresentation.hpp"
#include "llvm/ADT/APInt.h" // llvm::Optional
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace NormalForm {
// template <IntMatrix AM>
// intptr_t findMinAbsCol(AM &A, size_t i){
//	intptr_t best = std::numeric_limits<intptr_t>::max();
//	intptr_t bestInd = -1;
//	for (size_t j=i; j < A.size(1); ++j){
//         if (intptr_t Aij = A(i, j)) {
//		Aij = std::abs(Aij);
//             if (Aij == 1) {
//                 return j;
//             } else if (Aij < best) {
//                 best = Aij;
//                 bestInd = j;
//             }
//         }
//     }
//	return bestInd;
// }

// // get C(i,i) to divide C(i,j), j = i+1...N
// // not all C(i,j), j = i+1...N equal 0
// // col ops go from rows i...M
// // returns `true` if it failed
// template <IntMatrix AM> bool rowReduce(AM &C, size_t i){
//	intptr_t sk = findMinAbsCol(C, i);
//	if (sk < 0){
//	    return true;
//	}
//	size_t k = sk;
//	auto [M,N] = C.size();
//	for (size_t j = i; j < M; ++j){
//	    intptr_t Cjk = sign(C(i,k))* C(j,k);
//	    C(j,k) = C(j,i);
//	    C(j,i) = Cjk;
//	}
//	return false;
// }

// intptr_t searchRowPivot(const IntMatrix auto &A, size_t i) {
//     for (intptr_t k = i; k < intptr_t(A.size(0)); ++k) {
//         if (A(k, i)) {
//             return k;
//         }
//     }
//     return -1;
// }
/*
intptr_t searchColPivot(const IntMatrix auto &A, size_t i) {
    for (intptr_t k = i; k < intptr_t(A.size(1)); ++k) {
        if (A(i, k)) {
            return k;
        }
    }
    return -1;
}

// permute the columns of A so that every principal minor of A is nonsingular;
// do the corresponding row operations on K
// returns true if rank deficient.
bool permuteCols(IntMatrix auto &A, SquareMatrix<intptr_t> &K) {
    auto [M, N] = A.size();
    assert(N == K.size(0));
    const size_t minMN = std::min(M, N);
    Matrix<intptr_t, 0, 0> B = A;
    // we reduce `B`, and apply the pivots to `A` and `K`
    for (size_t i = 0; i < minMN; ++i) {
        intptr_t optPivot = searchColPivot(B, i);
        if (optPivot < 0) {
            return true;
        }
        size_t pivot = optPivot;
        if (pivot != i) {
            swapCols(B, pivot, i);
            swapCols(A, pivot, i);
            swapCols(K, pivot, i);
        }
        // reduce B; guarantees all principal minors are independent
        for (size_t k = i + 1; k < minMN; ++k) {
            intptr_t g = std::gcd(B(i, i), B(i, k));
            intptr_t a = B(i, i) / g;
            intptr_t b = B(i, k) / g;
            for (size_t j = i; j < minMN; ++j) {
                B(j, k) = a * B(j, k) - b * B(j, i);
            }
        }
    }
    return false;
}
*/

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

bool pivotCols(IntMatrix auto &A, IntMatrix auto &K, size_t i, size_t N) {
    size_t piv = i;
    while (A(i, piv) == 0) {
        if (++piv == N) {
            return true;
        }
    }
    if (i != piv) {
        swapCols(A, i, piv);
        swapCols(K, i, piv);
    }
    return false;
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
orthogonalize(IntMatrix auto A) {
    // we try to orthogonalize with respect to as many rows of `A` as we can
    // prioritizing earlier rows.
    auto [M, N] = A.size();
    SquareMatrix<intptr_t> K = SquareMatrix<intptr_t>::identity(N);
    llvm::SmallVector<unsigned> included;
    included.reserve(std::min(M,N));
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

} // namespace NormalForm
