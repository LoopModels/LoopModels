#pragma once
#include "./Math.hpp"
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
// 	intptr_t best = std::numeric_limits<intptr_t>::max();
// 	intptr_t bestInd = -1;
// 	for (size_t j=i; j < A.size(1); ++j){
//         if (intptr_t Aij = A(i, j)) {
// 		Aij = std::abs(Aij);
//             if (Aij == 1) {
//                 return j;
//             } else if (Aij < best) {
//                 best = Aij;
//                 bestInd = j;
//             }
//         }
//     }
// 	return bestInd;
// }

// // get C(i,i) to divide C(i,j), j = i+1...N
// // not all C(i,j), j = i+1...N equal 0
// // col ops go from rows i...M
// // returns `true` if it failed
// template <IntMatrix AM> bool rowReduce(AM &C, size_t i){
// 	intptr_t sk = findMinAbsCol(C, i);
// 	if (sk < 0){
// 	    return true;
// 	}
// 	size_t k = sk;
// 	auto [M,N] = C.size();
// 	for (size_t j = i; j < M; ++j){
// 	    intptr_t Cjk = sign(C(i,k))* C(j,k);
// 	    C(j,k) = C(j,i);
// 	    C(j,i) = Cjk;
// 	}
// 	return false;
// }

template <IntMatrix AM> intptr_t searchColPivot(const AM &A, size_t i) {
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
bool permuteCols(Matrix<intptr_t, 0, 0> &A, SquareMatrix<intptr_t> &K) {
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

template <IntMatrix T, IntMatrix S>
void reduceOffDiagonal(T &A, S &K, size_t k) {
    intptr_t Akk = A(k, k);
    auto [M, N] = A.size();
    if (Akk < 0) {
        Akk = -Akk;
        for (size_t i = 0; i < M; ++i) {
            A(i, k) *= -1;
            K(i, k) *= -1;
        }
        for (size_t i = M; i < N; ++i) {
            K(i, k) *= -1;
        }
    }
    for (size_t z = 0; z < k; ++z) {
        intptr_t Akz = A(k, z);
        // std::cout << "Rem: " << Akz % Akk << std::endl;
        intptr_t Akzkk = (Akz / Akk) + ((Akz % Akk) > 0);
        for (size_t i = 0; i < M; ++i) {
            A(i, z) -= Akzkk * A(i, k);
            K(i, z) -= Akzkk * K(i, k);
        }
        for (size_t i = M; i < N; ++i) {
            K(i, z) -= Akzkk * K(i, k);
        }
    }
}

llvm::Optional<std::pair<Matrix<intptr_t, 0, 0>, SquareMatrix<intptr_t>>>
hermite(Matrix<intptr_t, 0, 0> A) {
    auto [M, N] = A.size();
    SquareMatrix<intptr_t> K = SquareMatrix<intptr_t>::identity(N);
    if (permuteCols(A, K)) {
        return {}; // rank deficient
    }
    for (size_t i = 1; i < N; ++i) {
        // we put the A(0:i,0:i) block into HNF
        for (size_t j = 0; j < i; ++j) {
            intptr_t Ajj = A(j, j);
            intptr_t Aji = A(j, i);
            auto [r, p, q] = gcdx(Ajj, Aji);
            Ajj /= r;
            Aji /= r;
            for (size_t k = 0; k < M; ++k) {
                intptr_t Akj = A(k, j);
                intptr_t Aki = A(k, i);
                intptr_t Kkj = K(k, j);
                intptr_t Kki = K(k, i);
                A(k, j) = q * Aki + p * Akj;
                A(k, i) = Ajj * Aki - Aji * Akj;
                K(k, j) = q * Kki + p * Kkj;
                K(k, i) = Ajj * Kki - Aji * Kkj;
                // std::cout << "Akj=A(" << k << ", " << j << ")" << Akj
                //<< std::endl;
                // std::cout << "Aki=A(" << k << ", " << i << ")" << Aki
                //<< std::endl;
            }
            for (size_t k = M; k < N; ++k) {
                intptr_t Kkj = K(k, j);
                intptr_t Kki = K(k, i);
                K(k, j) = q * Kki + p * Kkj;
                K(k, i) = Ajj * Kki - Aji * Kkj;
            }
            // std::cout << "Aji=A(" << j << ", " << i << ") = " << A(j, i)
            //           << std::endl;
            if ((j != 0) & (j < M)) {
                reduceOffDiagonal(A, K, j);
            }
        }
        if (i < M) {
            reduceOffDiagonal(A, K, i);
        }
    }
    return std::make_pair(std::move(A), std::move(K));
}

} // namespace NormalForm
