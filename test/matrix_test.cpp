#include "../include/Math.hpp"
#include "MatrixStringParse.hpp"
#include <cstdint>
#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(HelloTest, BasicAssertions) {
    SmallSparseMatrix<int64_t> Asparse(3, 4);
    std::cout << "&Asparse = " << &Asparse << std::endl;
    Asparse(0, 1) = 5;
    Asparse(1, 3) = 3;
    Asparse(2, 0) = -1;
    Asparse(2, 1) = 4;
    Asparse(2, 2) = -2;
    IntMatrix A = Asparse;
    // std::cout << "A.size() = ("<<A.numRow()<<", "<<A.numCol()<<")\n";
    // std::cout << "\nAsparse = \n" << Asparse << std::endl;
    // std::cout << "\nA = \n" << A << std::endl << std::endl;
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 4; ++j)
            EXPECT_TRUE(A(i, j) == Asparse(i, j));
    // EXPECT_EQ(A(i, j), Asparse(i, j));
    IntMatrix B(4, 5);
    B(0, 0) = 3;
    B(0, 1) = -1;
    B(0, 2) = 0;
    B(0, 3) = -5;
    B(0, 4) = 1;
    B(1, 0) = -4;
    B(1, 1) = 5;
    B(1, 2) = -1;
    B(1, 3) = -1;
    B(1, 4) = -1;
    B(2, 0) = 1;
    B(2, 1) = 2;
    B(2, 2) = -5;
    B(2, 3) = 2;
    B(2, 4) = 3;
    B(3, 0) = -2;
    B(3, 1) = 1;
    B(3, 2) = 2;
    B(3, 3) = -3;
    B(3, 4) = 5;
    IntMatrix C{3, 5};
    C(0, 0) = -20;
    C(0, 1) = 25;
    C(0, 2) = -5;
    C(0, 3) = -5;
    C(0, 4) = -5;
    C(1, 0) = -6;
    C(1, 1) = 3;
    C(1, 2) = 6;
    C(1, 3) = -9;
    C(1, 4) = 15;
    C(2, 0) = -21;
    C(2, 1) = 17;
    C(2, 2) = 6;
    C(2, 3) = -3;
    C(2, 4) = -11;
    IntMatrix C2{matmul(A, B)};
    EXPECT_TRUE(C == C2);
    EXPECT_TRUE(C == matmultn(A.transpose(), B));
    EXPECT_TRUE(C == matmulnt(A, B.transpose()));
    EXPECT_TRUE(C == matmultt(A.transpose(), B.transpose()));
}

TEST(ExpressionTemplateTest, BasicAssertions) {
    auto A{stringToIntMatrix(
        "[3 -5 1 10 -4 6 4 4; 4 6 3 -1 6 1 -4 0; -7 -2 0 0 -10 -2 3 7; 2 -7 -5 "
        "-5 -7 -5 1 -7; 2 -8 2 7 4 9 6 -3; -2 -8 -5 0 10 -4 5 -3]")};

    auto A4{stringToIntMatrix("[12 -20 4 40 -16 24 16 16; 16 24 12 -4 24 4 -16 0; -28 -8 0 0 -40 -8 12 28; 8 -28 -20 -20 -28 -20 4 -28; 8 -32 8 28 16 36 24 -12; -8 -32 -20 0 40 -16 20 -12]")};
    // IntMatrix B{A*4};
    auto A4tmplate{A * 4};
    IntMatrix C = A4tmplate;
    IntMatrix B = A * 4;
    EXPECT_EQ(A4, B);
    EXPECT_EQ(A4, C);
    IntMatrix Z = A*4 - A4;
    for (auto z : Z)
	EXPECT_FALSE(z);
    auto D{stringToIntMatrix("[-5 6 -1 -4 7 -9 6; -3 -5 -1 -2 -9 -4 -1; -4 7 -6 10 -2 2 9; -4 -7 -1 -7 5 9 -10; 5 -7 -5 -1 -3 -8 -8; 3 -6 4 10 9 0 -5; 0 -1 4 -4 -9 -3 -10; 2 1 4 5 -7 0 -8]")};
    auto ADref{stringToIntMatrix("[-38 -28 62 6 116 105 -138; -13 -22 -69 29 -10 -99 42; -1 54 91 45 -95 142 -36; -13 118 31 -91 78 8 151; 19 -74 15 26 153 31 -145; 86 -61 -18 -111 -22 -55 -135]")};
    IntMatrix AD = A*D;
    EXPECT_EQ(AD, ADref);
    IntMatrix E{stringToIntMatrix("[-4 7 9 -4 2 9 -8; 3 -5 6 0 -1 8 7; -7 9 -1 1 -5 2 10; -3 10 -10 -3 6 5 5; -6 7 -4 -7 10 5 3; 9 -8 7 9 2 2 6]")};
    IntMatrix ADm7E = A*D - 7*E;
    auto ADm7Eref{stringToIntMatrix("[-10 -77 -1 34 102 42 -82; -34 13 -111 29 -3 -155 -7; 48 -9 98 38 -60 128 -106; 8 48 101 -70 36 -27 116; 61 -123 43 75 83 -4 -166; 23 -5 -67 -174 -36 -69 -177]")};
    EXPECT_EQ(ADm7E, ADm7Eref);
    
    
    // std::cout << "B = \n"<<B<<std::endl;
    // std::cout << "C = \n"<<C<<std::endl;
    // IntMatrix B;
    // B = A*4;
}
