#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include <cstdint>
#include <gtest/gtest.h>

// Demonstrate some basic assertions.
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(SparseIndexingTest, BasicAssertions) {
    SmallSparseMatrix<int64_t> Asparse(Row{3}, Col{4});
    llvm::errs() << "&Asparse = " << &Asparse << "\n";
    Asparse(0, 1) = 5;
    Asparse(1, 3) = 3;
    Asparse(2, 0) = -1;
    Asparse(2, 1) = 4;
    Asparse(2, 2) = -2;
    IntMatrix A = Asparse;
    // llvm::errs() << "A.size() = ("<<A.numRow()<<", "<<A.numCol()<<")\n";
    // llvm::errs() << "\nAsparse = \n" << Asparse << "\n";
    // llvm::errs() << "\nA = \n" << A << "\n" << "\n";
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 4; ++j)
            EXPECT_TRUE(A(i, j) == Asparse(i, j));
    // EXPECT_EQ(A(i, j), Asparse(i, j));
    IntMatrix B(Row{4}, Col{5});
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
    IntMatrix C{Row{3}, Col{5}};
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
    EXPECT_EQ(A.numRow(), (A * B).numRow());
    EXPECT_EQ(B.numCol(), (A * B).numCol());
    EXPECT_TRUE(C == A * B);
    IntMatrix C2{A * B};
    llvm::errs() << "C=\n" << C << "\nC2=\n" << C2 << "\n";
    EXPECT_TRUE(C == C2);
    IntMatrix At = A.transpose();
    IntMatrix Bt = B.transpose();
    EXPECT_TRUE(C == At.transpose() * B);
    EXPECT_TRUE(C == A * Bt.transpose());
    EXPECT_TRUE(C == At.transpose() * Bt.transpose());
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(ExpressionTemplateTest, BasicAssertions) {
    auto A{stringToIntMatrix(
        "[3 -5 1 10 -4 6 4 4; 4 6 3 -1 6 1 -4 0; -7 -2 0 0 -10 -2 3 7; 2 -7 -5 "
        "-5 -7 -5 1 -7; 2 -8 2 7 4 9 6 -3; -2 -8 -5 0 10 -4 5 -3]")};

    auto A4{stringToIntMatrix(
        "[12 -20 4 40 -16 24 16 16; 16 24 12 -4 24 4 -16 0; -28 -8 0 0 -40 -8 "
        "12 28; 8 -28 -20 -20 -28 -20 4 -28; 8 -32 8 28 16 36 24 -12; -8 -32 "
        "-20 0 40 -16 20 -12]")};
    // IntMatrix B{A*4};
    auto A4tmplate{A * 4};
    IntMatrix C = A4tmplate;
    IntMatrix B = A * 4;
    EXPECT_EQ(A4, B);
    EXPECT_EQ(A4, C);
    IntMatrix Z = A * 4 - A4;
    for (size_t i = 0; i < Z.numRow(); ++i)
        for (size_t j = 0; j < Z.numCol(); ++j)
            EXPECT_FALSE(Z(i, j));
    auto D{stringToIntMatrix(
        "[-5 6 -1 -4 7 -9 6; -3 -5 -1 -2 -9 -4 -1; -4 7 -6 10 -2 2 9; -4 -7 -1 "
        "-7 5 9 -10; 5 -7 -5 -1 -3 -8 -8; 3 -6 4 10 9 0 -5; 0 -1 4 -4 -9 -3 "
        "-10; 2 1 4 5 -7 0 -8]")};
    auto ADref{stringToIntMatrix(
        "[-38 -28 62 6 116 105 -138; -13 -22 -69 29 -10 -99 42; -1 54 91 45 "
        "-95 142 -36; -13 118 31 -91 78 8 151; 19 -74 15 26 153 31 -145; 86 "
        "-61 -18 -111 -22 -55 -135]")};
    IntMatrix AD = A * D;
    EXPECT_EQ(AD, ADref);
    IntMatrix E{stringToIntMatrix(
        "[-4 7 9 -4 2 9 -8; 3 -5 6 0 -1 8 7; -7 9 -1 1 -5 2 10; -3 10 -10 -3 6 "
        "5 5; -6 7 -4 -7 10 5 3; 9 -8 7 9 2 2 6]")};
    IntMatrix ADm7E = A * D - 7 * E;
    auto ADm7Eref{stringToIntMatrix(
        "[-10 -77 -1 34 102 42 -82; -34 13 -111 29 -3 -155 -7; 48 -9 98 38 -60 "
        "128 -106; 8 48 101 -70 36 -27 116; 61 -123 43 75 83 -4 -166; 23 -5 "
        "-67 -174 -36 -69 -177]")};
    EXPECT_EQ(ADm7E, ADm7Eref);

    Vector<int64_t> a;
    a.push_back(-8);
    a.push_back(7);
    a.push_back(3);
    Vector<int64_t> b = a * 2;
    Vector<int64_t> c;
    c.push_back(-16);
    c.push_back(14);
    c.push_back(6);
    EXPECT_EQ(b, c);
    // llvm::errs() << "B = \n"<<B<<"\n";
    // llvm::errs() << "C = \n"<<C<<"\n";
    // IntMatrix B;
    // B = A*4;
    IntMatrix A1x1(Row{1}, Col{1});
    IntMatrix A2x2(Row{2}, Col{2});
    A1x1.antiDiag() = 1;
    EXPECT_EQ(A1x1(0, 0), 1);
    A2x2.antiDiag() = 1;
    EXPECT_EQ(A2x2(0, 0), 0);
    EXPECT_EQ(A2x2(0, 1), 1);
    EXPECT_EQ(A2x2(1, 0), 1);
    EXPECT_EQ(A2x2(1, 1), 0);
    for (size_t i = 1; i < 20; ++i) {
        IntMatrix A(Row{i}, Col{i});
        A.antiDiag() = 1;
        for (size_t j = 0; j < i; ++j)
            for (size_t k = 0; k < i; ++k)
                EXPECT_EQ(A(j, k), k + j == i - 1);
    }
}
