#pragma once
#include "Math.hpp"

struct LUFact {
    SquareMatrix<Rational> F;
    llvm::SmallVector<unsigned> perm;

    bool ldiv(RationalMatrix auto &rhs) const {
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
                for (size_t j = 0; j < M; ++j) {
                    std::swap(rhs(ip, j), rhs(i, j));
                }
            }
        }
        // LU x = rhs
        // L y = rhs // L is UnitLowerTriangular
        for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < M; ++m) {
                Rational Ymn = rhs(m, n);
                for (size_t k = 0; k < m; ++k) {
		    if (Ymn.fnmadd(F(m, k), rhs(k, n))){
			return true;
		    }
                }
                rhs(m, n) = Ymn;
            }
        }
        /*
        for (size_t k = 0; k < N; ++k) {
            for (size_t j = 0; j < M; ++j) {
                Rational rhsj = rhs(j, k);
                for (size_t i = j + 1; i < M; ++i) {
                    rhs(i, k) -= (F(i, j) * rhsj).getValue();
                }
            }
        }
        */
        // U x = y
        for (size_t n = 0; n < N; ++n) {
            for (intptr_t m = M - 1; m >= 0; --m) {
                Rational Ymn = rhs(m, n);
                for (size_t k = m + 1; k < M; ++k) {
		    if (Ymn.fnmadd(F(m, k), rhs(k, n))){
			return true;
		    }
                }
                if (auto div = Ymn / F(m, m)) {
                    rhs(m, n) = div.getValue();
                } else {
                    return true;
                }
            }
        }
        return false;
    }

    bool rdiv(RationalMatrix auto &rhs) const {
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
		    if (Ymn.fnmadd(rhs(m, k), F(k, n))){
			return true;
		    }
                }
                if (auto div = Ymn / F(n, n)) {
                    rhs(m, n) = div.getValue();
                } else {
                    return true;
                }
            }
        }
        // x L = y
        for (intptr_t n = N - 1; n >= 0; --n) {
            // for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < M; ++m) {
                Rational Xmn = rhs(m, n);
                for (size_t k = n + 1; k < N; ++k) {
		    if (Xmn.fnmadd(rhs(m, k), F(k, n))){
			return true;
		    }
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

        return false;
    }

    llvm::Optional<SquareMatrix<Rational>> inv() const {
        SquareMatrix<Rational> A = SquareMatrix<Rational>::identity(F.size(0));
        if (!ldiv(A)) {
            return std::move(A);
        } else {
            return {};
        }
    }
    llvm::Optional<Rational> det() {
        Rational d = F(0, 0);
        for (size_t i = 1; i < F.size(0); ++i) {
            if (auto di = d * F(i, i)) {
                d = di.getValue();
            } else {
                return {};
            }
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
                perm[k] = ipiv;
                // std::swap(perm[ipiv], perm[k]);
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
