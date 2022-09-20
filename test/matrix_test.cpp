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
    EXPECT_EQ(A.numRow(), (A * B).numRow());
    EXPECT_EQ(B.numCol(), (A * B).numCol());
    EXPECT_TRUE(C == A * B);
    IntMatrix C2{A * B};
    std::cout << "C=\n" << C << "\nC2=\n" << C2 << std::endl;
    EXPECT_TRUE(C == C2);
    IntMatrix At = A.transpose();
    IntMatrix Bt = B.transpose();
    EXPECT_TRUE(C == At.transpose() * B);
    EXPECT_TRUE(C == A * Bt.transpose());
    EXPECT_TRUE(C == At.transpose() * Bt.transpose());
}

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
    ElementwiseVectorBinaryOp<Mul, PtrVector<int64_t>, int64_t> a2 = a * int64_t(2);
    int64_t a2_0_a = get(a2.a, size_t(0));
    int64_t a2_0_b = get(a2.b, size_t(0));
    int64_t a2_0 = a2.op(a2_0_a, a2_0_b);
    auto a2_0_direct_size_t = a2(size_t(0));
    
    int64_t a2_0_a_int_index = a2.a(0);
    int64_t a2_0_a_int_get = get(a2.a, 0);
    int64_t a2_0_b_int = get(a2.b, 0);
    int64_t a2_0_int = a2.op(a2_0_a, a2_0_b);
    int64_t a2_0_direct_int = a2(0);
    const ElementwiseVectorBinaryOp<Mul, PtrVector<int64_t>, int64_t> a2const = a * int64_t(2);
    int64_t a2c_0_direct_int = a2const(0);
    int64_t a2c_0_direct_size_t = a2const(size_t(0));
    auto a0v = a2(VIndex{0});
    
    Vector<int64_t> btest;
    copyto(btest, a2);
    Vector<int64_t> b = a2;
    Vector<int64_t> c;
    c.push_back(-16);
    c.push_back(14);
    c.push_back(6);
    EXPECT_EQ(b, c);
    // std::cout << "B = \n"<<B<<std::endl;
    // std::cout << "C = \n"<<C<<std::endl;
    // IntMatrix B;
    // B = A*4;
}

TEST(SIMDVecTEST, BasicAssertions) {
    Vector<int64_t> a;
    a.push_back(-8);
    a.push_back(7);
    a.push_back(3);
    Vector<int64_t> b = a * size_t(2);
    b += a;
    Vector<int64_t> c;
    c.push_back(-24);
    c.push_back(21);
    c.push_back(9);
    std::cout << "b = " << b << std::endl;
    b -= a;
    c(0) = -16;
    c(1) = 14;
    c(2) = 6;
    std::cout << "c = " << c << std::endl;
    EXPECT_EQ(b, c);
    b *= a;
    c(0) = 128;
    c(1) = 98;
    c(2) = 18;
    EXPECT_EQ(b, c);

    // a.push_back(1);
    // b.push_back(1);
    // c.push_back(1);
    std::cout << a << std::endl;
    std::cout << b << std::endl;
    b /= a;
    c(0) = -16;
    c(1) = 14;
    c(2) = 6;
    // c(3) = 1;
    EXPECT_EQ(b, c);

    b += 2;
    b -= 2;
    b *= 2;
    b /= 2;
    std::cout << b << std::endl;
    EXPECT_EQ(b, c);

    Vector<int64_t> d;
    d.push_back(4);
    d.push_back(4);
    d.push_back(4);
    d.push_back(4);
    Vector<int64_t> e;
    // e = 8 / d;
    size_t x = -4;
    e = x - d;
    e = e + d;
    e = e / d;
    e = e * x;
    Vector<int64_t> f;
    f.push_back(4);
    f.push_back(4);
    f.push_back(4);
    f.push_back(4);
    std::cout << e << std::endl;
    EXPECT_EQ(e, f);

    // Test MutPtrVector
    Matrix<int64_t>  A(6, 8);
    for (size_t i = 0; i < 6; i++) {
        for (size_t j = 0; j < 8; j++) {
            A(i, j) = 1;
        }
    }
    auto Amutvec = A(0, _);
    Amutvec *= size_t(2);
    Amutvec /= 2;
    auto Bmutvec = 2 - Amutvec;
    auto Cmutvec = Bmutvec + Amutvec;
    for (size_t i = 0; i < 8; i++) {
        std::cout << Cmutvec(i) <<std::endl;
    }

    //Test StridedVector
    auto Astridedvec = A(_, 0);
    auto Bstridedvec = A(_, 1);
    Astridedvec += Bstridedvec;
    Astridedvec *= Bstridedvec;
    Astridedvec /= Bstridedvec;
    Astridedvec -= Bstridedvec;
    auto Cstridedvec = 2 - Astridedvec;
    // f /= 2;
    // std::cout << Cmutvec << std::endl;
    // Amutvec *= 2;
    // EXPECT_EQ(Cmutvec, Amutvec);


    // auto StridedA = A(_, 0);
    // StridedA *= 2;

    
    // b *= a;
    // std::cout<< "b = "<<b<<std::endl;
    // auto A{stringToIntMatrix(
    //     "[3 -5 1 10 -4 6 4 4; 4 6 3 -1 6 1 -4 0; -7 -2 0 0 -10 -2 3 7; 2 -7
    //     -5 "
    //     "-5 -7 -5 1 -7; 2 -8 2 7 4 9 6 -3; -2 -8 -5 0 10 -4 5 -3]")};
    // auto A2{stringToIntMatrix(
    //     "[3 -5 1 10 -4 6 4 4; 4 6 3 -1 6 1 -4 0; -7 -2 0 0 -10 -2 3 7; 2 -7
    //     -5 "
    //     "-5 -7 -5 1 -7; 2 -8 2 7 4 9 6 -3; -2 -8 -5 0 10 -4 5 -3]")};
    // A2 += A;
}

TEST(SIMDMatTEST, BasicAssertions) {
    IntMatrix A(6, 6);
    for (size_t i = 0; i < 6; i++) {
        for (size_t j = 0; j < 6; j++) {
            A(i, j) = 1;
        }
    }
    IntMatrix  B(6, 6);
    for (size_t i = 0; i < 6; i++) {
        for (size_t j = 0; j < 6; j++) {
            B(i, j) = 2;
        }
    }
    // auto C = A * B ;
    auto C = A * 2;
    // A *= 2;
}
