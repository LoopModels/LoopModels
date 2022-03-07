#pragma once
#include "Math.hpp"

// returns the lu factorization of `intptr_t` matrix B if it can be computed
// without overflow.
// TODO: need pivoting!!!
// llvm::Optional<std::pair<SquareMatrix<Rational>,llvm::SmallVector<size_t>>
// lufact(SquareMatrix<intptr_t> &B) {
llvm::Optional<std::pair<SquareMatrix<Rational>,llvm::SmallVector<unsigned>>> lufact(SquareMatrix<intptr_t> &B) {
    size_t M = B.M;
    SquareMatrix<Rational> A(M);
    llvm::SmallVector<unsigned> perm(M);
    for (size_t i = 0; i < M; ++i)
        perm[i] = i;
    for (size_t m = 0; m < M * M; ++m) {
        A[m] = B[m];
    }
    for (size_t k = 0; k < M; ++k) {
        size_t ipiv = k;
        for (; ipiv < M; ++ipiv) {
            if (A(ipiv, k) != 0) {
                std::swap(perm[ipiv], perm[k]);
                break;
            }
        }
        if (ipiv != k) {
            for (size_t j = 0; j < M; ++j)
                std::swap(A(ipiv, j), A(k, j));
        }
        Rational Akkinv = A(k, k).inv();
        for (size_t i = k + 1; i < M; ++i) {
            if (llvm::Optional<Rational> Aik = A(i, k) * Akkinv) {
                A(i, k) = Aik.getValue();
            } else {
                return {};
            }
        }
        for (size_t j = k + 1; j < M; ++j) {
            for (size_t i = k + 1; i < M; ++i) {
                if (llvm::Optional<Rational> Aikj = A(i, k) * A(k, j)) {
                    if (llvm::Optional<Rational> Aij =
                            A(i, j) - Aikj.getValue()) {
                        A(i, j) = Aij.getValue();
                        continue;
                    }
                }
                return {};
            }
        }
    }
    return std::make_pair(A,perm);
}

template <size_t MM, size_t NN>
Matrix<intptr_t, MM, NN> ldiv(Matrix<Rational,MM,NN> rhs, std::pair<SquareMatrix<Rational>,llvm::SmallVector<unsigned>> &LUF) {
    auto [M, N] = rhs.size();
    auto &F = LUF.first;
    auto &perm = LUF.second;
    auto FM = F.size(0);
    assert(FM == M);
    // // check unimodularity
    // Rational unit = 1;
    // for (size_t i = 0; i < FM; ++i)
    //     unit *= F(i, i);
    // assert(unit == 1);

    // permute rhs
    for (size_t i = 0; i < M; ++i) {
        auto ip = perm[i];
        if (i != ip){
            for (size_t j = 0; j < M; ++j)
                std::swap(rhs(ip, j), rhs(i, j));
        }
    }

    // LU x = rhs
    // L y = rhs // L is UnitLowerTriangular
    for (size_t k = 0; k < N; ++k) {
        for (size_t j = 0; j < M; ++j) {
            auto rhsj = rhs(j, k);
            for (size_t i = j+1; i < M; ++i) {
                rhs(i, k) -= F(i, j) * rhsj;
            }
        }
    }

    // U x = y
    for (size_t k = 0; k < N; ++k) {
        for (size_t j = M-1; j >= 0; --j) {
            auto rhsj = rhs(j, k) / F(j, j);
            for (size_t i = j-1; i >= 0; --i) {
                rhs(i, k) -= F(i, j) * rhsj;
            }
        }
    }
    return rhs;
}

template <size_t MM, size_t NN>
Matrix<intptr_t, MM, NN> rdiv(Matrix<Rational,MM,NN> rhs, std::pair<SquareMatrix<Rational>,llvm::SmallVector<unsigned>> &LUF) {
    auto [M, N] = rhs.size();
    auto &F = LUF.first;
    auto &perm = LUF.second;
    auto FN = F.size(0);
    assert(FN == N);
    // // check unimodularity
    // Rational unit = 1;
    // for (size_t i = 0; i < FN; ++i)
    //     unit *= F(i, i);
    // assert(unit == 1);

    // PA = LU
    // x LU = rhs
    // y U = rhs
    for (size_t n = 0; n < N; ++n){
        for (size_t m = 0; m < M; ++m){
            Rational Ymn = rhs(m,n);
            for (size_t k = 0; k < n; ++k){
                Ymn -= rhs(m,k)*F(k,n);
            }
            rhs(m,n) = Ymn / F(n,n);
        }
    }
    // x L = y
    for (size_t n = 0; n < N; ++n){
        for (size_t m = 0; m < M; ++m){
            Rational Xmn = rhs(m,n);
            for (size_t k = n+1; k < N; ++k){
                Xmn -= rhs(m,k)*F(k,n);
            }
            rhs(m,n) = Xmn;
        }
    }
    // permute rhs
    for (size_t j = N-1; j >= 0; --j) {
        auto jp = perm[j];
        if (j != jp){
            for (size_t i = 0; i < M; ++i)
                std::swap(rhs(i, jp), rhs(i, j));
        }
    }

    return rhs;
}
