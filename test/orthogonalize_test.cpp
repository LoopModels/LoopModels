#include "../include/Math.hpp"
#include "../include/Orthogonalize.hpp"
#include "../include/Symbolics.hpp"
#include "Loops.hpp"
#include "MatrixStringParse.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <random>

TEST(OrthogonalizeTest, BasicAssertions) {
    // for m = 0:M-1, n = 0:N-1, i = 0:I-1, j = 0:J-1
    //   W[m + i, n + j] += C[i,j] * B[m,n]
    //
    // Loops: m, n, i, j
    IntMatrix A{stringToIntMatrix("[-1 1 0 0 0 -1 0 0 0; "
                                  "0 0 0 0 0 1 0 0 0; "
                                  "-1 0 1 0 0 0 -1 0 0; "
                                  "0 0 0 0 0 0 1 0 0; "
                                  "-1 0 0 1 0 0 0 -1 0; "
                                  "0 0 0 0 0 0 0 1 0; "
                                  "-1 0 0 0 1 0 0 0 -1; "
                                  "0 0 0 0 0 0 0 0 1]")};

    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1}),
        Polynomial::Monomial(Polynomial::ID{2}),
        Polynomial::Monomial(Polynomial::ID{3}),
        Polynomial::Monomial(Polynomial::ID{4})};
    auto &M = symbols[0];
    // auto &N = symbols[1];
    auto &I = symbols[2];
    // auto &J = symbols[3];
    llvm::IntrusiveRefCntPtr<AffineLoopNest> alnp{
        AffineLoopNest::construct(A, symbols)};
    EXPECT_FALSE(alnp->isEmpty());

    // we have three array refs
    // W[i+m, j+n]
    // llvm::SmallVector<std::pair<MPoly,MPoly>>
    ArrayReference War{0, alnp, 2};
    {
        MutPtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(2, 0) = 1; // i
        IndMat(1, 1) = 1; // n
        IndMat(3, 1) = 1; // j
        War.strides[0] = 1;
        War.strides[1] = I + M - 1;
    }
    std::cout << "War = " << War << std::endl;

    // B[i, j]
    ArrayReference Bar{1, alnp, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(2, 0) = 1; // i
        IndMat(3, 1) = 1; // j
        Bar.strides[0] = 1;
        Bar.strides[1] = I;
    }
    std::cout << "Bar = " << Bar << std::endl;

    // C[m, n]
    ArrayReference Car{2, alnp, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        Car.strides[0] = 1;
        Car.strides[1] = M;
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
    EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 0)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 1)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 0)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 1)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 0)), 2);
    EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 1)), 2);
    std::cout << "A=" << newAlnp->A << std::endl;
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp->aln->b);
    std::cout << "Skewed loop nest:\n" << *newAlnp << std::endl;
    auto loop3Count =
        newAlnp->countSigns(newAlnp->A, 3 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop3Count.first, 2);
    EXPECT_EQ(loop3Count.second, 2);
    newAlnp->removeLoopBang(3);
    auto loop2Count =
        newAlnp->countSigns(newAlnp->A, 2 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop2Count.first, 2);
    EXPECT_EQ(loop2Count.second, 2);
    newAlnp->removeLoopBang(2);
    auto loop1Count =
        newAlnp->countSigns(newAlnp->A, 1 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    newAlnp->removeLoopBang(1);
    auto loop0Count =
        newAlnp->countSigns(newAlnp->A, 0 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop0Count.first, 1);
    EXPECT_EQ(loop0Count.second, 1);
    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        std::cout << ar << std::endl << std::endl;
    }
}

TEST(BadMul, BasicAssertions) {
    auto M = Polynomial::Monomial{Polynomial::ID{1}};
    auto N = Polynomial::Monomial{Polynomial::ID{2}};
    auto O = Polynomial::Monomial{Polynomial::ID{3}};
    llvm::SmallVector<Polynomial::Monomial> symbols{M, N, O};
    IntMatrix A{stringToIntMatrix("[-3 1 1 1 -1 0 0; "
				  "0 0 0 0 1 0 0; "
				  "-2 1 0 1 0 -1 0; "
				  "0 0 0 0 0 1 0; "
				  "0 0 0 0 1 -1 0; "
				  "-1 0 1 0 -1 1 0; "
				  "-1 1 0 0 0 0 -1; "
				  "0 0 0 0 0 0 1; "
				  "0 0 0 0 0 1 -1; "
				  "-1 0 0 1 0 -1 1]")};
    // auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    // auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    // for i in 0:M+N+O-3, l in max(0,i+1-N):min(M+O-2,i), j in
    // max(0,l+1-O):min(M-1,l)
    //       W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    //
    // Loops: i, l, j

    llvm::IntrusiveRefCntPtr<AffineLoopNest> alnp{
        AffineLoopNest::construct(A, symbols)};
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
        MutPtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        War.strides[0] = 1;
        War.strides[1] = M;
    }
    std::cout << "War = " << War << std::endl;

    // B[j, l - j]
    ArrayReference Bar(1, alnp, 2); //, axes, indTo
    {
        MutPtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(lId, 1) = 1;  // l
        IndMat(jId, 1) = -1; // j
        Bar.strides[0] = 1;
        Bar.strides[1] = M;
    }
    std::cout << "Bar = " << Bar << std::endl;

    // C[l-j,i-l]
    ArrayReference Car(2, alnp, 2); //, axes, indTo
    {
        MutPtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(lId, 0) = 1;  // l
        IndMat(jId, 0) = -1; // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        Car.strides[0] = 1;
        Car.strides[1] = O;
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

    SHOWLN(alnp->A);
    SHOWLN(newAlnp->A);
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp->aln->b);
    std::cout << "Skewed loop nest:\n" << *newAlnp << std::endl;
    auto loop2Count =
        newAlnp->countSigns(newAlnp->A, 2 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop2Count.first, 1);
    EXPECT_EQ(loop2Count.second, 1);
    newAlnp->removeLoopBang(2);
    SHOWLN(newAlnp->A);
    auto loop1Count =
        newAlnp->countSigns(newAlnp->A, 1 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    newAlnp->removeLoopBang(1);
    SHOWLN(newAlnp->A);
    auto loop0Count =
        newAlnp->countSigns(newAlnp->A, 0 + newAlnp->getNumSymbols());
    EXPECT_EQ(loop0Count.first, 1);
    EXPECT_EQ(loop0Count.second, 1);

    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs)
        std::cout << ar << std::endl << std::endl;
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
    for (size_t i = 0; i < iters; ++i) {
        for (auto &&a : A)
            a = distrib(gen);
        // std::cout << "Random A =\n" << A << std::endl;
        A = orthogonalize(std::move(A));
        // std::cout << "Orthogonal A =\n" << A << std::endl;
        // note, A'A is not diagonal
        // but AA' is
        B = A * A.transpose();
        // std::cout << "A'A =\n" << B << std::endl;
        for (size_t m = 0; m < M; ++m)
            for (size_t n = 0; n < N; ++n)
                if (m != n){
                    EXPECT_EQ(B(m, n), 0);
		}
    }
}
