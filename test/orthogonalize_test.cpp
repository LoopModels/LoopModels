#include "../include/Math.hpp"
#include "../include/Orthogonalize.hpp"
#include "../include/Symbolics.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <random>

TEST(OrthogonalizeTest, BasicAssertions) {
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto I = Polynomial::Monomial(Polynomial::ID{3});
    auto J = Polynomial::Monomial(Polynomial::ID{4});
    auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    // for m = 0:M-1, n = 0:N-1, i = 0:I-1, j = 0:J-1
    //   W[m + i, n + j] += C[i,j] * B[m,n]
    //
    // Loops: m, n, i, j
    IntMatrix A(8, 4);
    // A = [ 1 -1  0  0  0  0  0  0
    //       0  0  1 -1  0  0  0  0
    //       0  0  0  0  1 -1  0  0
    //       0  0  0  0  0  0  1 -1 ]'
    // r = [ M - 1, 0, N - 1, 0, I - 1, 0, J - 1, 0 ]
    // Loop: A*i <= b
    llvm::SmallVector<MPoly, 8> b;
    // m <= M-1
    A(0, 0) = 1;
    b.push_back(M - 1);
    // m >= 0;
    A(1, 0) = -1;
    b.push_back(Zero);
    // n <= N-1
    A(2, 1) = 1;
    b.push_back(N - 1);
    // n >= 0;
    A(3, 1) = -1;
    b.push_back(Zero);
    // i <= I-1
    A(4, 2) = 1;
    b.push_back(I - 1);
    // i >= 0;
    A(5, 2) = -1;
    b.push_back(Zero);
    // j <= J-1
    A(6, 3) = 1;
    b.push_back(J - 1);
    // j >= 0;
    A(7, 3) = -1;
    b.push_back(Zero);
    for (auto &b : b) {
        if (auto c = b.getCompileTimeConstant()) {
            if (c.getValue() == 0) {
                assert(b.terms.size() == 0);
            }
        }
    }
    PartiallyOrderedSet poset;
    llvm::IntrusiveRefCntPtr<AffineLoopNest> alnp(
        llvm::makeIntrusiveRefCnt<AffineLoopNest>(A, b, poset));
    EXPECT_FALSE(alnp->isEmpty());

    // we have three array refs
    // W[i+m, j+n]
    // llvm::SmallVector<std::pair<MPoly,MPoly>>
    ArrayReference War{0, alnp, 2};
    {
        PtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(2, 0) = 1; // i
        IndMat(1, 1) = 1; // n
        IndMat(3, 1) = 1; // j
        War.stridesOffsets[0] = std::make_pair(One, Zero);
        War.stridesOffsets[1] = std::make_pair(I + M - One, Zero);
    }
    std::cout << "War = " << War << std::endl;

    // B[i, j]
    ArrayReference Bar{1, alnp, 2};
    {
        PtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(2, 0) = 1; // i
        IndMat(3, 1) = 1; // j
        Bar.stridesOffsets[0] = std::make_pair(One, Zero);
        Bar.stridesOffsets[1] = std::make_pair(I, Zero);
    }
    std::cout << "Bar = " << Bar << std::endl;

    // C[m, n]
    ArrayReference Car{2, alnp, 2};
    {
        PtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        Car.stridesOffsets[0] = std::make_pair(One, Zero);
        Car.stridesOffsets[1] = std::make_pair(M, Zero);
    }
    std::cout << "Car = " << Car << std::endl;

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<llvm::SmallVector<ArrayReference, 0>> orth(
        orthogonalize(ai));

    EXPECT_TRUE(orth.hasValue());

    llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth.getValue();
    AffineLoopNest *newAlnp = newArrayRefs.begin()->loop.get();
    for (auto &ar : newArrayRefs) {
        EXPECT_EQ(newAlnp, ar.loop.get());
    }
    EXPECT_EQ(newArrayRefs[0][0].rank(), 1);
    EXPECT_EQ(newArrayRefs[0][1].rank(), 1);
    EXPECT_EQ(newArrayRefs[1][0].rank(), 1);
    EXPECT_EQ(newArrayRefs[1][1].rank(), 1);
    EXPECT_EQ(newArrayRefs[2][0].rank(), 2);
    EXPECT_EQ(newArrayRefs[2][1].rank(), 2);
    std::cout << "A=" << newAlnp->A << std::endl;
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp->aln->b);
    EXPECT_EQ(newAlnp->lowerb[0].size(), 1);
    EXPECT_EQ(newAlnp->lowerb[1].size(), 1);
    EXPECT_EQ(newAlnp->lowerb[2].size(), 2);
    EXPECT_EQ(newAlnp->lowerb[3].size(), 2);
    EXPECT_EQ(newAlnp->upperb[0].size(), 1);
    EXPECT_EQ(newAlnp->upperb[1].size(), 1);
    EXPECT_EQ(newAlnp->upperb[2].size(), 2);
    EXPECT_EQ(newAlnp->upperb[3].size(), 2);
    std::cout << "Skewed loop nest:\n" << *newAlnp << std::endl;
    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        std::cout << ar << std::endl << std::endl;
    }
}

TEST(BadMul, BasicAssertions) {
    auto M =
        Polynomial::Term{int64_t(1), Polynomial::Monomial(Polynomial::ID{1})};
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto O = Polynomial::Monomial(Polynomial::ID{3});
    auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    // for i in 0:M+N+O-3, l in max(0,i+1-N):min(M+O-2,i), j in
    // max(0,l+1-O):min(M-1,l)
    //       W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    //
    // Loops: i, l, j
    IntMatrix A(10, 3);
    //       0  1  2  3  4  5  6  7  8  9
    // A = [ 1 -1  0  0 -1  1  0  0  0  0
    //       0  0  1 -1  1 -1  0  0 -1  1
    //       0  0  0  0  0  0  1 -1  1 -1 ]'
    // 0 r = [ M + N + O - 3,
    // 1       0,
    // 2       M + O - 2,
    // 3       0,
    // 4       0,
    // 5       N-1,
    // 6       M-1,
    // 7       0,
    // 8       0,
    // 9       O-1 ]
    // Loop: A*i <= r
    llvm::SmallVector<MPoly, 8> r;
    // i <= M + N + O - 3
    A(0, 0) = 1;
    r.push_back(M + N + O - 3);
    // i >= 0;
    A(1, 0) = -1;
    r.push_back(Zero);
    // l <= M + O - 2
    A(2, 1) = 1;
    r.push_back(M + O - 2);
    // l >= 0;
    A(3, 1) = -1;
    r.push_back(Zero);
    // l <= i
    A(4, 0) = -1;
    A(4, 1) = 1;
    r.push_back(Zero);
    // l >= i - (N - 1);
    A(5, 0) = 1;
    A(5, 1) = -1;
    r.push_back(N - 1);
    // j <= M - 1
    A(6, 2) = 1;
    r.push_back(M - 1);
    // j >= 0;
    A(7, 2) = -1;
    r.push_back(Zero);
    // j <= l
    A(8, 1) = -1;
    A(8, 2) = 1;
    r.push_back(Zero);
    // j >= l - (O - 1)
    A(9, 1) = 1;
    A(9, 2) = -1;
    r.push_back(O - 1);
    for (auto &b : r) {
        if (auto c = b.getCompileTimeConstant()) {
            if (c.getValue() == 0) {
                assert(b.terms.size() == 0);
            }
        }
    }
    PartiallyOrderedSet poset;
    llvm::IntrusiveRefCntPtr<AffineLoopNest> alnp(
        llvm::makeIntrusiveRefCnt<AffineLoopNest>(A, r, poset));
    EXPECT_FALSE(alnp->isEmpty());

    // for i in 0:M+N+O-3, l in max(0,i+1-N):min(M+O-2,i), j in
    // max(0,l+1-O):min(M-1,l)
    // W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    // 0, 1, 2
    // i, l, j
    // we have three array refs
    // W[j, i - l]
    const int iId = 0, lId = 1, jId = 2;
    ArrayReference War(0, alnp, 2); //, axes, indTo
    {
        PtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        War.stridesOffsets[0] = std::make_pair(One, Zero);
        War.stridesOffsets[1] = std::make_pair(M, Zero);
    }
    std::cout << "War = " << War << std::endl;

    // B[j, l - j]
    ArrayReference Bar(1, alnp, 2); //, axes, indTo
    {
        PtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(lId, 1) = 1;  // l
        IndMat(jId, 1) = -1; // j
        Bar.stridesOffsets[0] = std::make_pair(One, Zero);
        Bar.stridesOffsets[1] = std::make_pair(M, Zero);
    }
    std::cout << "Bar = " << Bar << std::endl;

    // C[l-j,i-l]
    ArrayReference Car(2, alnp, 2); //, axes, indTo
    {
        PtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(lId, 0) = 1;  // l
        IndMat(jId, 0) = -1; // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        Car.stridesOffsets[0] = std::make_pair(One, Zero);
        Car.stridesOffsets[1] = std::make_pair(O, Zero);
    }
    std::cout << "Car = " << Car << std::endl;

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<llvm::SmallVector<ArrayReference, 0>> orth(
        orthogonalize(ai));

    EXPECT_TRUE(orth.hasValue());

    llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth.getValue();
    AffineLoopNest *newAlnp = newArrayRefs.begin()->loop.get();
    for (auto &ar : newArrayRefs) {
        EXPECT_EQ(newAlnp, ar.loop.get());
    }

    std::cout << "A=" << newAlnp->A << std::endl;
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp->aln->b);
    EXPECT_EQ(newAlnp->lowerb[0].size(), 1);
    EXPECT_EQ(newAlnp->lowerb[1].size(), 1);
    EXPECT_EQ(newAlnp->lowerb[2].size(), 1);
    EXPECT_EQ(newAlnp->upperb[0].size(), 1);
    EXPECT_EQ(newAlnp->upperb[1].size(), 1);
    EXPECT_EQ(newAlnp->upperb[2].size(), 1);
    std::cout << "Skewed loop nest:\n" << *newAlnp << std::endl;
    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        std::cout << ar << std::endl << std::endl;
    }
}

TEST(OrthogonalizeMatricesTest, BasicAssertions) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-3, 3);

    const size_t M = 7;
    const size_t N = 7;
    IntMatrix A(M, N);
    IntMatrix B(N, N);
    const size_t iters = 1000;
    for (size_t i = 0; i < iters; ++i){
	for (auto &&a : A)
	    a = distrib(gen);
	// std::cout << "Random A =\n" << A << std::endl;
	A = orthogonalize(std::move(A));
	// std::cout << "Orthogonal A =\n" << A << std::endl;
	// note, A'A is not diagonal
	// but AA' is
	matmulnt(B, A, A);
	// std::cout << "A'A =\n" << B << std::endl;
	for (size_t m = 0; m < M; ++m)
	    for (size_t n = 0; n < N; ++n)
		if (m != n)
		    EXPECT_EQ(B(m,n), 0);
    }
}
