#include "../include/POSet.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(POSet0, BasicAssertions) {
    PartiallyOrderedSet poset;
    int varX = 0;
    int varY = 1;
    int varZ = 2;
    // (varX - varY) in (typemin(Int):-1)
    // i.e., varY > varX
    poset.push(varY, varX, Interval::negative()); // typemin(Int):-1
    // varY < varZ; (varZ - varY) in (1:typemax(Int))
    poset.push(varY, varZ, Interval::positive()); // 1:typemax(Int)
    EXPECT_EQ(poset.nVar, 3);
    // poset(idx, idy) returns the value of the difference (idy - idx)
    // as an interval.
    EXPECT_EQ(poset(varX,varY).lowerBound, 1);

    EXPECT_EQ(poset(varY,varX).upperBound, -1);
    EXPECT_EQ(poset(varY,varZ).lowerBound, 1);
    EXPECT_EQ(poset(varZ,varY).upperBound, -1);
    EXPECT_EQ(poset(varX,varZ).lowerBound, 2); // interval of varZ - varX
    EXPECT_EQ(poset(varZ,varX).upperBound, -2);
}
TEST(POSet1, BasicAssertions) {
    PartiallyOrderedSet poset;
    int varV = 0;
    int varW = 1;
    int varX = 2;
    int varY = 3;
    int varZ = 4;
    poset.push(varW, varX, Interval::positive() + 8);   // 9:typemax(Int)
    poset.push(varV, varW, Interval::nonNegative() + 8);// 8:typemax(Int)
    EXPECT_EQ(poset.nVar, varY);
    EXPECT_EQ(poset(varV,varX).lowerBound, 17);
    poset.push(varW, varY, Interval::negative() + 28);
    poset.push(varX, varY, Interval::nonNegative() + 18);
    EXPECT_EQ(poset.nVar, varZ);
    EXPECT_FALSE(poset(varV,varW).isConstant());
    EXPECT_FALSE(poset(varV,varX).isConstant());
    EXPECT_TRUE(poset(varW,varX).isConstant());
    EXPECT_FALSE(poset(varV,varY).isConstant());
    EXPECT_TRUE(poset(varW,varY).isConstant());
    EXPECT_TRUE(poset(varX,varY).isConstant());
    EXPECT_EQ(poset(varW,varX).lowerBound, 9);
    EXPECT_EQ(poset(varW,varX).upperBound, 9);
    EXPECT_EQ(poset(varV,varY).lowerBound, 35);
    EXPECT_EQ(poset(varW,varY).lowerBound, 27);
    EXPECT_EQ(poset(varW,varY).upperBound, 27);
    EXPECT_EQ(poset(varX,varY).lowerBound, 18);
    EXPECT_EQ(poset(varX,varY).upperBound, 18);
    std::cout << poset(varV,varW) << std::endl;
    std::cout << poset(varV,varX) << std::endl;
    std::cout << poset(varW,varX) << std::endl;
    std::cout << poset(varV,varY) << std::endl;
    std::cout << poset(varW,varY) << std::endl;
    std::cout << poset(varX,varY) << std::endl;
    poset.push(varY, varZ, Interval{0,0});
    EXPECT_EQ(poset.nVar, 5);
    EXPECT_FALSE(poset(varV,varZ).isConstant());
    EXPECT_TRUE(poset(varW,varZ).isConstant());
    EXPECT_TRUE(poset(varX,varZ).isConstant());
    EXPECT_EQ(poset(varV,varZ).lowerBound, 35);
    EXPECT_EQ(poset(varW,varZ).lowerBound, 27);
    EXPECT_EQ(poset(varW,varZ).upperBound, 27);
    EXPECT_EQ(poset(varX,varZ).lowerBound, 18);
    EXPECT_EQ(poset(varX,varZ).upperBound, 18);
}
TEST(PolynomialCmp, BasicAssertions) {
    PartiallyOrderedSet poset;
    const int varZ = 0; // Zero == 0
    const int varM = 1;
    const int varN = 2;
    const int varO = 3;
    // const int varP = 4;
    auto M = Polynomial::Monomial(Polynomial::ID{varM});
    auto N = Polynomial::Monomial(Polynomial::ID{varN});
    auto O = Polynomial::Monomial(Polynomial::ID{varO});
    // auto P = Polynomial::Monomial(Polynomial::ID{varP});
    // M >= 0
    poset.push(varZ, varM, Interval::nonNegative());
    // N > M; N >= 1
    poset.push(varN, varM, Interval::negative());
    // O >= 3
    poset.push(varZ, varO, Interval::LowerBound(3));
    // // P <= 3
    // poset.push(varZ, varP, Interval::UpperBound(3));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N - M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*N - M*N));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*N - M*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*M - M*M));
    EXPECT_FALSE(poset.knownGreaterEqualZero(M - N));
    EXPECT_FALSE(poset.knownGreaterEqualZero(M*N - N*N));
    EXPECT_FALSE(poset.knownGreaterEqualZero(M*M - N*N));
    EXPECT_FALSE(poset.knownGreaterEqualZero(M*M - N*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*N - M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*M - M));
    EXPECT_FALSE(poset.knownGreaterEqualZero(N*M - N));
    EXPECT_TRUE(poset.knownGreaterEqualZero(N*(M+1) - N));
    
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*N - 2*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*N - 3*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*N - 2*N));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*N - 3*N));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*M - 2*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*M - 3*M));
    EXPECT_FALSE(poset.knownGreaterEqualZero(O*N - 4*M));
    EXPECT_TRUE(poset.knownGreaterEqualZero(O*N - O*M));
    EXPECT_FALSE(poset.knownGreaterEqualZero(3*N - O*M));
}

