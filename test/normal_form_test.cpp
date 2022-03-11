#include "../include/LinearAlgebra.hpp"
#include "../include/Math.hpp"
#include "../include/NormalForm.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <random>

TEST(NormalFormTest, BasicAssertions) {
    // Matrix<intptr_t,0,0> A4x4(4,4);
    Matrix<intptr_t, 0, 0> A3x4(3, 4);
    A3x4(0, 0) = 2;
    A3x4(0, 1) = 3;
    A3x4(0, 2) = 6;
    A3x4(0, 3) = 2;
    A3x4(1, 0) = 5;
    A3x4(1, 1) = 6;
    A3x4(1, 2) = 1;
    A3x4(1, 3) = 6;
    A3x4(2, 0) = 8;
    A3x4(2, 1) = 3;
    A3x4(2, 2) = 1;
    A3x4(2, 3) = 1;
    std::cout << "A=\n" << A3x4 << std::endl;
    auto hnf = NormalForm::hermite(A3x4);
    EXPECT_TRUE(hnf.hasValue());
    auto [H, U] = hnf.getValue();
    std::cout << "H=\n" << H << "\nU=\n" << U << std::endl;

    EXPECT_TRUE(H == matmul(A3x4, U));

    for (size_t i = 0; i < 3; ++i) {
        A3x4(i, 2) = A3x4(i, 0) + A3x4(i, 1);
    }
    std::cout << "\n\n\n=======\n\nA=\n" << A3x4 << std::endl;
    hnf = NormalForm::hermite(A3x4);
    EXPECT_TRUE(hnf.hasValue());
    auto [H2, U2] = hnf.getValue();
    std::cout << "H=\n" << H2 << "\nU=\n" << U2 << std::endl;

    EXPECT_TRUE(H2 == matmul(A3x4, U2));

    SquareMatrix<intptr_t> A(4);
    A(0, 0) = 3;
    A(0, 1) = -6;
    A(0, 2) = 7;
    A(0, 3) = 7;
    A(1, 0) = 7;
    A(1, 1) = -8;
    A(1, 2) = 10;
    A(1, 3) = 6;
    A(2, 0) = -5;
    A(2, 1) = 8;
    A(2, 2) = 7;
    A(2, 3) = 3;
    A(3, 0) = -5;
    A(3, 1) = -6;
    A(3, 2) = 8;
    A(3, 3) = -1;
    auto hnfsm = NormalForm::hermite(A);
    EXPECT_TRUE(hnfsm.hasValue());
    auto [H3, U3] = hnfsm.getValue();
    std::cout << "\n\n\n====\n\nH=\n" << H3 << "\nU=\n" << U3 << std::endl;
    EXPECT_TRUE(H3 == matmul(A, U3));

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
    Matrix<intptr_t, 0, 0> B(6, 4);
    SquareMatrix<intptr_t> I4 = SquareMatrix<intptr_t>::identity(4);
    for (size_t i = 0; i < numIters; ++i) {
        for (size_t n = 0; n < 4; ++n) {
            for (size_t m = 0; m < 6; ++m) {
                B(m, n) = distrib(gen);
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
                    A(m, n) = B(mb, n);
                    ++m;
                }
            }
            EXPECT_TRUE(matmul(A, K) == I4);
        } else {
            // std::cout << "K= " << K << "\n";
            if (auto optlu = lufact(K)) {
                if (auto optA2 = optlu.getValue().inv()) {
                    auto A2 = optA2.getValue();
                    for (size_t n = 0; n < 4; ++n) {
                        for (size_t j = 0; j < included.size(); ++j) {
                            EXPECT_TRUE(A2(j, n) == B(included[j], n));
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

    B(0,0) = 1;
    B(0,1) = 0;
    B(0,2) = 1;
    B(0,3) = 0;
    B(1,0) = 0;
    B(1,1) = 1;
    B(1,2) = 0;
    B(1,3) = 1;
    B(2,0) = 1;
    B(2,1) = 0;
    B(2,2) = 0;
    B(2,3) = 0;
    B(3,0) = 0;
    B(3,1) = 1;
    B(3,2) = 0;
    B(3,3) = 0;
    B(4,0) = 0;
    B(4,1) = 0;
    B(4,2) = 1;
    B(4,3) = 0;
    B(5,0) = 0;
    B(5,1) = 0;
    B(5,2) = 0;
    B(5,3) = 1;
    std::cout << "B_orth_motivating_example = " << B << std::endl;
    auto [K, included] = NormalForm::orthogonalize(B);
    std::cout << "K = " << K << "\nincluded = " << Vector<unsigned,0>(included) << std::endl;
    EXPECT_EQ(included.size(), 4);
    for (size_t i = 0; i < 4; ++i){
	EXPECT_EQ(included[i], i);
    }
    for (size_t n = 0; n < 4; ++n) {
        size_t m = 0;
        for (auto mb : included) {
            A(m, n) = B(mb, n);
            ++m;
        }
    }
    EXPECT_TRUE(matmul(A, K) == I4);
}
