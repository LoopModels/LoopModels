#pragma once
#include "Math.hpp"

struct LUFact {
    SquareMatrix<Rational> F;
    llvm::SmallVector<unsigned> perm;

    RationalMatrix auto ldiv(RationalMatrix auto rhs) const {
        auto [M, N] = rhs.size();
        auto FM = F.size(0);
        assert(FM == M);
        // // check unimodularity
        // Rational unit = 1;
        // for (size_t i = 0; i < FM; ++i)
        //     unit *= F(i, i);
        // assert(unit == 1);

        // permute rhs
        for (size_t i = 0; i < M; ++i) {
            unsigned ip = perm[i];
            if (i != ip) {
                for (size_t j = 0; j < M; ++j)
                    std::swap(rhs(ip, j), rhs(i, j));
            }
        }

        // LU x = rhs
        // L y = rhs // L is UnitLowerTriangular
        for (size_t k = 0; k < N; ++k) {
            for (size_t j = 0; j < M; ++j) {
                Rational rhsj = rhs(j, k);
                for (size_t i = j + 1; i < M; ++i) {
                    rhs(i, k) -= (F(i, j) * rhsj).getValue();
                }
            }
        }
        // U x = y
	for (size_t n = 0; n < N; ++n){
	    for (intptr_t m = M-1; m >= 0; --m){
		Rational Ymn = rhs(m,n);
		for (size_t k = m+1; k < M; ++k){
		    Ymn -= (F(m,k)*rhs(k,n)).getValue();
		}
		rhs(m,n) = (Ymn / F(m,m)).getValue();
	    }
	}
        return rhs;
    }

    RationalMatrix auto rdiv(RationalMatrix auto rhs) const {
        auto [M, N] = rhs.size();
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
        for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < M; ++m) {
                Rational Ymn = rhs(m, n);
                for (size_t k = 0; k < n; ++k) {
                    Ymn -= (rhs(m, k) * F(k, n)).getValue();
                }
                rhs(m, n) = (Ymn / F(n, n)).getValue();
            }
        }
        // x L = y
        for (intptr_t n = N-1; n >= 0; --n) {
        // for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < M; ++m) {
                Rational Xmn = rhs(m, n);
                for (size_t k = n + 1; k < N; ++k) {
                    Xmn -= (rhs(m, k) * F(k, n)).getValue();
                }
                rhs(m, n) = Xmn;
            }
        }
        // permute rhs
        for (intptr_t j = N - 1; j >= 0; --j) {
            unsigned jp = perm[j];
            if (j != jp) {
                for (size_t i = 0; i < M; ++i)
                    std::swap(rhs(i, jp), rhs(i, j));
            }
        }

        return rhs;
    }

    SquareMatrix<Rational> inv() const {
	return ldiv(SquareMatrix<Rational>::identity(F.size(0)));
    }
    Rational det(){
	Rational d = 1;
	for (size_t i = 0; i < F.size(0); ++i){
	    d *= F(i,i);
	}
	return d;
    }
};

llvm::Optional<LUFact> lufact(const SquareMatrix<intptr_t> &B) {
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
    return LUFact{std::move(A), std::move(perm)};
}
