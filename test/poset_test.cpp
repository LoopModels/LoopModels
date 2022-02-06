#include "../include/poset.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(POSet0, BasicAssertions) {
    PartiallyOrderedSet poset;
    poset.push(1, 0, Interval::negative());
    poset.push(1, 2, Interval::positive());
    EXPECT_EQ(poset.nVar, 3);
    EXPECT_EQ(poset(0,1).lowerBound, 1);
    EXPECT_EQ(poset(1,0).upperBound, -1);
    EXPECT_EQ(poset(1,2).lowerBound, 1);
    EXPECT_EQ(poset(2,1).upperBound, -1);
    EXPECT_EQ(poset(0,2).lowerBound, 2);
    EXPECT_EQ(poset(2,0).upperBound, -2);
}
TEST(POSet1, BasicAssertions) {
    PartiallyOrderedSet poset;
    poset.push(1, 2, Interval::positive() + 8);
    poset.push(0, 1, Interval::nonNegative() + 8);
    EXPECT_EQ(poset.nVar, 3);
    EXPECT_EQ(poset(0,2).lowerBound, 17);
    poset.push(1, 3, Interval::negative() + 28);
    poset.push(2, 3, Interval::nonNegative() + 18);
    EXPECT_EQ(poset.nVar, 4);
    EXPECT_FALSE(poset(0,1).isConstant());
    EXPECT_FALSE(poset(0,2).isConstant());
    EXPECT_TRUE(poset(1,2).isConstant());
    EXPECT_FALSE(poset(0,3).isConstant());
    EXPECT_TRUE(poset(1,3).isConstant());
    EXPECT_TRUE(poset(2,3).isConstant());
    EXPECT_EQ(poset(1,2).lowerBound, 9);
    EXPECT_EQ(poset(1,2).upperBound, 9);
    EXPECT_EQ(poset(0,3).lowerBound, 35);
    EXPECT_EQ(poset(1,3).lowerBound, 27);
    EXPECT_EQ(poset(1,3).upperBound, 27);
    EXPECT_EQ(poset(2,3).lowerBound, 18);
    EXPECT_EQ(poset(2,3).upperBound, 18);
    std::cout << poset(0,1) << std::endl;
    std::cout << poset(0,2) << std::endl;
    std::cout << poset(1,2) << std::endl;
    std::cout << poset(0,3) << std::endl;
    std::cout << poset(1,3) << std::endl;
    std::cout << poset(2,3) << std::endl;
}


