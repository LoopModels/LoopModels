#include "../include/MatrixStringParse.hpp"
#include "Math.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(StringParse, BasicAssertions) {
    IntMatrix A{stringToIntMatrix("[0 3 -2 1; 3 -1 -2 -2; 2 0 -3 0]")};
    std::cout << "A = \n" << A << std::endl;
    EXPECT_EQ(A(0,0), 0);
    EXPECT_EQ(A(0,1), 3);
    EXPECT_EQ(A(0,2), -2);
    EXPECT_EQ(A(0,3), 1);
    EXPECT_EQ(A(1,0), 3);
    EXPECT_EQ(A(1,1), -1);
    EXPECT_EQ(A(1,2), -2);
    EXPECT_EQ(A(1,3), -2);
    EXPECT_EQ(A(2,0), 2);
    EXPECT_EQ(A(2,1), 0);
    EXPECT_EQ(A(2,2), -3);
    EXPECT_EQ(A(2,3), 0);
    

}
