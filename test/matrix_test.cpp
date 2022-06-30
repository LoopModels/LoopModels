#include "../include/Math.hpp"
#include <cstdint>
#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(HelloTest, BasicAssertions) {
    SparseMatrix<int64_t> Asparse(3, 4);
    Asparse(0, 1) = 5;
    Asparse(1, 3) = 3;
    Asparse(2, 0) = -1;
    Asparse(2, 1) = 4;
    Asparse(2, 2) = -2;
    IntMatrix A = Asparse;
    //std::cout << "A.size() = ("<<A.numRow()<<", "<<A.numCol()<<")\n";
    //std::cout << "\nAsparse = \n" << Asparse << std::endl;
    //std::cout << "\nA = \n" << A << std::endl << std::endl;
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
    IntMatrix C{3,5};
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
