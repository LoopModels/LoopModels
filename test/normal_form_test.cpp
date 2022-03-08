#include "../include/Math.hpp"
#include "../include/NormalForm.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>

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
    auto [H3,U3] = hnfsm.getValue();
    std::cout << "\n\n\n====\n\nH=\n" << H3 << "\nU=\n" << U3 << std::endl;
    EXPECT_TRUE(H3 == matmul(A, U3));
}
