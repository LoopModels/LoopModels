#include "../include/LinearDiophantine.hpp"
#include "../include/Math.hpp"
#include "../include/Unimodularization.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>

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
    std::cout << "VB:\n" << VB << std::endl;

    SquareMatrix<intptr_t> A(3);
    A(0, 0) = 9;
    A(0, 1) = -5;
    A(0, 2) = 1;
    A(1, 0) = 5;
    A(1, 1) = -2;
    A(1, 2) = 0;
    auto B = unimodularization(A, 2);
    std::cout << "B:\n" << B << std::endl;
    // EXPECT_EQ(j, length(bsc));
    // EXPECT_EQ(j, length(bs));
    auto A2 =
        unimodularize2x3(A(0, 0), A(0, 1), A(0, 2), A(1, 0), A(1, 1), A(1, 2));
    EXPECT_TRUE(A2.hasValue());
    auto [A20, A21, A22] = A2.getValue();
    std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", " << A22 << " ]\n";
}
