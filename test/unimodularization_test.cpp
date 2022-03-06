#include "../include/LinearDiophantine.hpp"
#include "../include/Math.hpp"
#include "../include/Unimodularization.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <random>

TEST(UnimodularizationTest, BasicAssertions) {
    SquareMatrix<intptr_t> VE(4);
    VE(0, 0) = 0;
    VE(0, 1) = 1;
    VE(0, 2) = 0;
    VE(0, 3) = 1;
    VE(1, 0) = 1;
    VE(1, 1) = 0;
    VE(1, 2) = 1;
    VE(1, 3) = 0;
    auto VB = unimodularization(VE, 2);
    EXPECT_TRUE(VB.hasValue());
    std::cout << "VB:\n" << VB.getValue() << std::endl;

    SquareMatrix<intptr_t> A(3);
    A(0, 0) = 9;
    A(0, 1) = -5;
    A(0, 2) = 1;
    A(1, 0) = 5;
    A(1, 1) = -2;
    A(1, 2) = 0;
    auto B = unimodularization(A, 2);
    EXPECT_TRUE(B.hasValue());
    std::cout << "B:\n" << B.getValue() << std::endl;
    // EXPECT_EQ(j, length(bsc));
    // EXPECT_EQ(j, length(bs));
    auto A2 =
        unimodularize2x3(A(0, 0), A(0, 1), A(0, 2), A(1, 0), A(1, 1), A(1, 2));
    EXPECT_TRUE(A2.hasValue());
    auto [A20, A21, A22] = A2.getValue();
    std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", " << A22 << " ]\n";

    //auto test6_10_15 = unimodularize2x3(6, 10, 15, 1, 1, 0);
    //auto test6_10_15 = unimodularize2x3(6, 10, 15, 1, 1, 2);
    auto test6_10_15 = unimodularize1x3(6, 10, 15);//, 1, 93, 1001);
    EXPECT_TRUE(test6_10_15.hasValue());
    if (test6_10_15.hasValue()) {
        auto [r1, r2] = test6_10_15.getValue();
        auto [A10, A11, A12] = r1;
        auto [A20, A21, A22] = r2;
        std::cout << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 << ", "
                  << A12 << " ]\n";
        std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", "
                  << A22 << " ]\n";
    }
    auto test102_190_345 = unimodularize1x3(102, 190, 345);//, 1, 93, 1001);
    //auto test102_190_345 = unimodularize2x3(102, 190, 345, 1, 0, 1);
    if (test102_190_345.hasValue()) {
        auto [r1, r2] = test102_190_345.getValue();
        auto [A10, A11, A12] = r1;
        auto [A20, A21, A22] = r2;
        std::cout << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 << ", "
                  << A12 << " ]\n";
        std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", "
                  << A22 << " ]\n";
    }
}
