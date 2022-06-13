#include "../include/LinearDiophantine.hpp"
#include "../include/Math.hpp"
#include "../include/Unimodularization.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <random>

TEST(UnimodularizationTest, BasicAssertions) {
    IntMatrix VE(4, 2);
    VE(0, 0) = 0;
    VE(1, 0) = 1;
    VE(2, 0) = 0;
    VE(3, 0) = 1;
    VE(0, 1) = 1;
    VE(1, 1) = 0;
    VE(2, 1) = 1;
    VE(3, 1) = 0;
    std::cout << "VE=\n" << VE << std::endl;
    auto VB = unimodularize(VE);
    EXPECT_TRUE(VB.hasValue());
    std::cout << "VB:\n" << VB.getValue() << std::endl;

    IntMatrix A23(3, 2);
    A23(0, 0) = 9;
    A23(1, 0) = -5;
    A23(2, 0) = 1;
    A23(0, 1) = 5;
    A23(1, 1) = -2;
    A23(2, 1) = 0;
    auto B = unimodularize(A23);
    EXPECT_TRUE(B.hasValue());
    std::cout << "B:\n" << B.getValue() << std::endl;
    // EXPECT_EQ(j, length(bsc));
    // EXPECT_EQ(j, length(bs));

    IntMatrix A13(3, 1);
    A13(0, 0) = 6;
    A13(1, 0) = -5;
    A13(2, 0) = 15;
    auto test6_10_15 = unimodularize(A13); //, 1, 93, 1001);
    EXPECT_TRUE(test6_10_15.hasValue());
    // if (test6_10_15.hasValue()) {
    //     auto [r1, r2] = test6_10_15.getValue();
    //     auto [A10, A11, A12] = r1;
    //     auto [A20, A21, A22] = r2;
    //     std::cout << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 << ", "
    //               << A12 << " ]\n";
    //     std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", "
    //               << A22 << " ]\n";
    // }
    A13(0, 0) = 102;
    A13(1, 0) = 190;
    A13(2, 0) = 345;
    auto test102_190_345 = unimodularize(A13); //, 1, 93, 1001);
    EXPECT_TRUE(test102_190_345.hasValue());
    // auto test102_190_345 = unimodularize2x3(102, 190, 345, 1, 0, 1);
    //  if (test102_190_345.hasValue()) {
    //      auto [r1, r2] = test102_190_345.getValue();
    //      auto [A10, A11, A12] = r1;
    //      auto [A20, A21, A22] = r2;
    //      std::cout << "\n\n\n======\nA(1,:): [ " << A10 << ", " << A11 << ",
    //      "
    //                << A12 << " ]\n";
    //      std::cout << "A(2,:): [ " << A20 << ", " << A21 << ", "
    //                << A22 << " ]\n";
    //  }
}
