#include "../include/LinearAlgebra.hpp"
#include "../include/Math.hpp"
#include "../include/NormalForm.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <random>

TEST(OrthogonalizeTest, BasicAssertions) {
    SquareMatrix<int64_t> A(4);
    std::cout << "\n\n\n========\n========\n========\n\n";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-10, 10);
    size_t orthAnyCount = 0;
    size_t orthMaxCount = 0;
    size_t orthCount = 0;
    size_t luFailedCount = 0;
    size_t invFailedCount = 0;
    size_t numIters = 1000;
    IntMatrix B(4, 8);
    SquareMatrix<int64_t> I4 = SquareMatrix<int64_t>::identity(4);
    for (size_t i = 0; i < numIters; ++i) {
        for (size_t n = 0; n < 4; ++n) {
            for (size_t m = 0; m < 8; ++m) {
                B(n, m) = distrib(gen);
            }
        }
        // std::cout << "\nB = " << B << std::endl;
        auto [K, included] = NormalForm::orthogonalize(B);
        orthCount += included.size();
        orthAnyCount += (included.size() > 0);
        orthMaxCount += (included.size() == 4);
        // std::cout << "included.size() = " << included.size() << std::endl;
        if (included.size() == 4) {
            for (size_t n = 0; n < 4; ++n) {
                size_t m = 0;
                for (auto mb : included) {
                    A(n, m) = B(n, mb);
                    ++m;
                }
            }
            printMatrix(std::cout << "K=\n", K) << std::endl;
            printMatrix(std::cout << "A=\n", A) << std::endl;
            EXPECT_TRUE(matmul(K, A) == I4);
        } else {
            // std::cout << "K= " << K << "\nB= " << B << std::endl;
            printVector(std::cout << "included = ", included) << std::endl;
            if (auto optlu = LU::fact(K)) {
                printMatrix(std::cout << "K=\n", K) << std::endl;
                if (auto optA2 = optlu.getValue().inv()) {
                    SquareMatrix<Rational, 4> &A2 = optA2.getValue();
                    std::cout << "A2 =\n" << A2 << std::endl;
                    std::cout << "B =\n" << B << std::endl;
                    for (size_t n = 0; n < 4; ++n) {
                        for (size_t j = 0; j < included.size(); ++j) {
                            // std::cout
                            //    << "A2(" << j << ", " << n << ") = " << A2(j,
                            //    n)
                            //    << "; B(" << n
                            //    << ", " << included[j] << ") = " << B(n,
                            //    included[j]) << std::endl;
                            EXPECT_EQ(A2(n, j), B(n, included[j]));
                        }
                    }
                } else {
                    ++invFailedCount;
                }
            } else {
                ++luFailedCount;
                std::cout << "B = " << B << "\nK = " << K << std::endl;
                continue;
            }
            // std::cout << "lu_F = " << optlu.getValue().F << "\nlu_perm = "
            // << Vector<unsigned, 0>(optlu.getValue().perm) << std::endl;
        }
        // std::cout << "\n\n";
    }
    std::cout << "Mean orthogonalized: " << double(orthCount) / double(numIters)
              << "\nOrthogonalization succeeded on at least one: "
              << orthAnyCount << " / " << numIters
              << "\nOrthogonalization succeeded on 4: " << orthMaxCount << " / "
              << numIters << "\nLU fact failed count: " << luFailedCount
              << " / " << numIters
              << "\nInv fact failed count: " << invFailedCount << " / "
              << numIters << std::endl;

    B(0, 0) = 1;
    B(1, 0) = 0;
    B(2, 0) = 1;
    B(3, 0) = 0;
    B(0, 1) = 0;
    B(1, 1) = 1;
    B(2, 1) = 0;
    B(3, 1) = 1;
    B(0, 2) = 1;
    B(1, 2) = 0;
    B(2, 2) = 0;
    B(3, 2) = 0;
    B(0, 3) = 0;
    B(1, 3) = 1;
    B(2, 3) = 0;
    B(3, 3) = 0;
    B(0, 4) = 0;
    B(1, 4) = 0;
    B(2, 4) = 1;
    B(3, 4) = 0;
    B(0, 5) = 0;
    B(1, 5) = 0;
    B(2, 5) = 0;
    B(3, 5) = 1;
    std::cout << "B_orth_motivating_example = " << B << std::endl;
    auto [K, included] = NormalForm::orthogonalize(B);
    printVector(std::cout << "K = " << K << "\nincluded = ", included)
        << std::endl;
    EXPECT_EQ(included.size(), 4);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(included[i], i);
    }
    for (size_t n = 0; n < 4; ++n) {
        size_t m = 0;
        for (auto mb : included) {
            A(n, m) = B(n, mb);
            ++m;
        }
    }
    std::cout << "A = " << A << "\nA * K = " << matmul(K, A) << std::endl;
    EXPECT_TRUE(matmul(K, A) == I4);
}

bool isHNF(PtrMatrix<int64_t> A) {
    const auto [M, N] = A.size();
    // l is lead
    size_t l = 0;
    for (size_t m = 0; m < M; ++m) {
        // all entries must be 0
        for (size_t n = 0; n < l; ++n) {
            if (A(m, n))
                return false;
        }
        // now search for next lead
        while ((l < N) && A(m, l) == 0) {
            ++l;
        }
        if (l == N)
            continue;
        int64_t Aml = A(m, l);
        if (Aml < 0)
            return false;
        for (size_t r = 0; r < m; ++r) {
            int64_t Arl = A(r, l);
            if ((Arl >= Aml) || (Arl < 0))
                return false;
        }
    }
    return true;
}

TEST(Hermite, BasicAssertions) {
    {
        IntMatrix A4x3(4, 3);
        A4x3(0, 0) = 2;
        A4x3(1, 0) = 3;
        A4x3(2, 0) = 6;
        A4x3(3, 0) = 2;
        A4x3(0, 1) = 5;
        A4x3(1, 1) = 6;
        A4x3(2, 1) = 1;
        A4x3(3, 1) = 6;
        A4x3(0, 2) = 8;
        A4x3(1, 2) = 3;
        A4x3(2, 2) = 1;
        A4x3(3, 2) = 1;
        std::cout << "A=\n" << A4x3 << std::endl;
        auto hnf = NormalForm::hermite(A4x3);
        EXPECT_TRUE(hnf.hasValue());
        auto [H, U] = hnf.getValue();
        std::cout << "H=\n" << H << "\nU=\n" << U << std::endl;

        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(H == matmul(U, A4x3));

        for (size_t i = 0; i < 3; ++i) {
            A4x3(2, i) = A4x3(0, i) + A4x3(1, i);
        }
        std::cout << "\n\n\n=======\n\nA=\n" << A4x3 << std::endl;
        hnf = NormalForm::hermite(A4x3);
        EXPECT_TRUE(hnf.hasValue());
        auto [H2, U2] = hnf.getValue();
        std::cout << "H=\n" << H2 << "\nU=\n" << U2 << std::endl;
        EXPECT_TRUE(isHNF(H2));
        EXPECT_TRUE(H2 == matmul(U2, A4x3));
    }
    {
        SquareMatrix<int64_t> A(4);
        A(0, 0) = 3;
        A(1, 0) = -6;
        A(2, 0) = 7;
        A(3, 0) = 7;
        A(0, 1) = 7;
        A(1, 1) = -8;
        A(2, 1) = 10;
        A(3, 1) = 6;
        A(0, 2) = -5;
        A(1, 2) = 8;
        A(2, 2) = 7;
        A(3, 2) = 3;
        A(0, 3) = -5;
        A(1, 3) = -6;
        A(2, 3) = 8;
        A(3, 3) = -1;
        auto hnfsm = NormalForm::hermite(A);
        EXPECT_TRUE(hnfsm.hasValue());
        auto [H3, U3] = hnfsm.getValue();
        std::cout << "\n\n\n====\n\nH=\n" << H3 << "\nU=\n" << U3 << std::endl;
        EXPECT_TRUE(isHNF(H3));
        EXPECT_TRUE(H3 == matmul(U3, A));
    }
    {
        IntMatrix A(2, 3);
        A(0, 0) = -3;
        A(0, 1) = -1;
        A(0, 2) = 1;
        A(1, 0) = 0;
        A(1, 1) = 0;
        A(1, 2) = -2;
        llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> B =
            NormalForm::hermite(A);
        EXPECT_TRUE(B.hasValue());
        auto [H, U] = B.getValue();
        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(matmul(U, A) == H);
        std::cout << "A = \n"
                  << A << "\nH =\n"
                  << H << "\nU =\n"
                  << U << std::endl;
    }
    {
        IntMatrix A(3, 11);
        A(0, 0) = 3;
        A(0, 1) = 3;
        A(0, 2) = -3;
        A(0, 3) = 1;
        A(0, 4) = 0;
        A(0, 5) = -1;
        A(0, 6) = -2;
        A(0, 7) = 1;
        A(0, 8) = 1;
        A(0, 9) = 2;
        A(0, 10) = -1;

        A(1, 0) = 3;
        A(1, 1) = 3;
        A(1, 2) = -3;
        A(1, 3) = 1;
        A(1, 4) = 1;
        A(1, 5) = -3;
        A(1, 6) = 2;
        A(1, 7) = 0;
        A(1, 8) = 3;
        A(1, 9) = 0;
        A(1, 10) = -3;

        A(2, 0) = 2;
        A(2, 1) = -3;
        A(2, 2) = -2;
        A(2, 3) = -1;
        A(2, 4) = 1;
        A(2, 5) = -2;
        A(2, 6) = 3;
        A(2, 7) = 3;
        A(2, 8) = 3;
        A(2, 9) = 3;
        A(2, 10) = -3;
        llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> B =
            NormalForm::hermite(A);
        EXPECT_TRUE(B.hasValue());
        auto [H, U] = B.getValue();
        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(matmul(U, A) == H);
        std::cout << "A = \n"
                  << A << "\nH =\n"
                  << H << "\nU =\n"
                  << U << std::endl;
    }
}

TEST(NullSpaceTests, BasicAssertions) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-10, 100);

    size_t numIters = 1000;
    for (size_t numCol = 2; numCol < 11; numCol += 2) {
        IntMatrix B(8, numCol);
        size_t nullDim = 0;
	IntMatrix Z, NS;
        for (size_t i = 0; i < numIters; ++i) {
            for (size_t n = 0; n < B.length(); ++n) {
                B[n] = distrib(gen);
                if (B[n] > 10) {
                    B[n] = 0;
                }
            }
            NS = NormalForm::nullSpace(B);
            nullDim += NS.numRow();
            Z = matmul(NS, B);
            for (size_t j = 0; j < Z.length(); ++j) {
                EXPECT_EQ(Z[j], 0);
            }
            EXPECT_EQ(NormalForm::nullSpace(std::move(NS)).numRow(), 0);
        }
        std::cout << "Average tested null dim = "
                  << double(nullDim) / double(numIters) << std::endl;
    }
}
