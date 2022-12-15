#include "../include/LinearAlgebra.hpp"
#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include "../include/NormalForm.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <numeric>
#include <random>

TEST(OrthogonalizeTest, BasicAssertions) {
    SquareMatrix<int64_t> A(4);
    llvm::errs() << "\n\n\n========\n========\n========\n\n";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-10, 10);
    size_t orthAnyCount = 0;
    size_t orthMaxCount = 0;
    size_t orthCount = 0;
    size_t luFailedCount = 0;
    size_t invFailedCount = 0;
    size_t numIters = 1000;
    IntMatrix B(4, 8);
    SquareMatrix<int64_t> I4 = SquareMatrix<int64_t>::identity(4);
    for (size_t i = 0; i < numIters; ++i) {
        for (size_t n = 0; n < 4; ++n)
            for (size_t m = 0; m < 8; ++m)
                B(n, m) = distrib(gen);
        // llvm::errs() << "\nB = " << B << "\n";
        auto [K, included] = NormalForm::orthogonalize(B);
        orthCount += included.size();
        orthAnyCount += (included.size() > 0);
        orthMaxCount += (included.size() == 4);
        // llvm::errs() << "included.size() = " << included.size() << "\n";
        if (included.size() == 4) {
            for (size_t n = 0; n < 4; ++n) {
                size_t m = 0;
                for (auto mb : included)
                    A(n, m++) = B(n, mb);
            }
            llvm::errs() << "K=\n" << K << "\n";
            llvm::errs() << "A=\n" << A << "\n";
            EXPECT_TRUE(K * A == I4);
        } else {
            // llvm::errs() << "K= " << K << "\nB= " << B << "\n";
            printVector(llvm::errs() << "included = ", included) << "\n";
            if (auto optlu = LU::fact(K)) {
                SHOWLN(K);
                if (auto optA2 = (*optlu).inv()) {
                    SquareMatrix<Rational> &A2 = *optA2;
                    SHOWLN(A2);
                    SHOWLN(B);

                    for (size_t n = 0; n < 4; ++n) {
                        for (size_t j = 0; j < included.size(); ++j) {
                            llvm::errs()
                                << "A2(" << n << ", " << j << ") = " << A2(n, j)
                                << "; B(" << n << ", " << included[j]
                                << ") = " << B(n, included[j]) << "\n";
                            EXPECT_EQ(A2(n, j), B(n, included[j]));
                        }
                    }
                } else {
                    ++invFailedCount;
                }
            } else {
                ++luFailedCount;
                llvm::errs() << "B = " << B << "\nK = " << K << "\n";
                continue;
            }
        }
    }
    llvm::errs() << "Mean orthogonalized: "
                 << double(orthCount) / double(numIters)
                 << "\nOrthogonalization succeeded on at least one: "
                 << orthAnyCount << " / " << numIters
                 << "\nOrthogonalization succeeded on 4: " << orthMaxCount
                 << " / " << numIters
                 << "\nLU fact failed count: " << luFailedCount << " / "
                 << numIters << "\nInv fact failed count: " << invFailedCount
                 << " / " << numIters << "\n";

    B(0, 0) = 1;
    B(1, 0) = 0;
    B(2, 0) = 1;
    B(3, 0) = 0;
    B(0, 1) = 0;
    B(1, 1) = 1;
    B(2, 1) = 0;
    B(3, 1) = 1;
    B(0, 2) = 1;
    B(1, 2) = 0;
    B(2, 2) = 0;
    B(3, 2) = 0;
    B(0, 3) = 0;
    B(1, 3) = 1;
    B(2, 3) = 0;
    B(3, 3) = 0;
    B(0, 4) = 0;
    B(1, 4) = 0;
    B(2, 4) = 1;
    B(3, 4) = 0;
    B(0, 5) = 0;
    B(1, 5) = 0;
    B(2, 5) = 0;
    B(3, 5) = 1;
    llvm::errs() << "B_orth_motivating_example = " << B << "\n";
    auto [K, included] = NormalForm::orthogonalize(B);
    printVector(llvm::errs() << "K = " << K << "\nincluded = ", included)
        << "\n";
    EXPECT_EQ(included.size(), 4);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(included[i], i);
    }
    for (size_t n = 0; n < 4; ++n) {
        size_t m = 0;
        for (auto mb : included) {
            A(n, m) = B(n, mb);
            ++m;
        }
    }
    IntMatrix KA{K * A};
    llvm::errs() << "A = " << A << "\nA * K = " << KA << "\n";
    EXPECT_TRUE(KA == I4);
}

bool isHNF(PtrMatrix<int64_t> A) {
    const auto [M, N] = A.size();
    // l is lead
    size_t l = 0;
    for (size_t m = 0; m < M; ++m) {
        // all entries must be 0
        for (size_t n = 0; n < l; ++n) {
            if (A(m, n))
                return false;
        }
        // now search for next lead
        while ((l < N) && A(m, l) == 0) {
            ++l;
        }
        if (l == N)
            continue;
        int64_t Aml = A(m, l);
        if (Aml < 0)
            return false;
        for (size_t r = 0; r < m; ++r) {
            int64_t Arl = A(r, l);
            if ((Arl >= Aml) || (Arl < 0))
                return false;
        }
    }
    return true;
}

TEST(Hermite, BasicAssertions) {
    {
        IntMatrix A4x3(4, 3);
        A4x3(0, 0) = 2;
        A4x3(1, 0) = 3;
        A4x3(2, 0) = 6;
        A4x3(3, 0) = 2;
        A4x3(0, 1) = 5;
        A4x3(1, 1) = 6;
        A4x3(2, 1) = 1;
        A4x3(3, 1) = 6;
        A4x3(0, 2) = 8;
        A4x3(1, 2) = 3;
        A4x3(2, 2) = 1;
        A4x3(3, 2) = 1;
        llvm::errs() << "A=\n" << A4x3 << "\n";
        auto [H, U] = NormalForm::hermite(A4x3);
        llvm::errs() << "H=\n" << H << "\nU=\n" << U << "\n";

        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(H == U * A4x3);

        for (size_t i = 0; i < 3; ++i) {
            A4x3(2, i) = A4x3(0, i) + A4x3(1, i);
        }
        llvm::errs() << "\n\n\n=======\n\nA=\n" << A4x3 << "\n";
        auto [H2, U2] = NormalForm::hermite(A4x3);
        llvm::errs() << "H=\n" << H2 << "\nU=\n" << U2 << "\n";
        EXPECT_TRUE(isHNF(H2));
        EXPECT_TRUE(H2 == U2 * A4x3);
    }
    {
        SquareMatrix<int64_t> A(4);
        A(0, 0) = 3;
        A(1, 0) = -6;
        A(2, 0) = 7;
        A(3, 0) = 7;
        A(0, 1) = 7;
        A(1, 1) = -8;
        A(2, 1) = 10;
        A(3, 1) = 6;
        A(0, 2) = -5;
        A(1, 2) = 8;
        A(2, 2) = 7;
        A(3, 2) = 3;
        A(0, 3) = -5;
        A(1, 3) = -6;
        A(2, 3) = 8;
        A(3, 3) = -1;
        auto [H3, U3] = NormalForm::hermite(A);
        llvm::errs() << "\n\n\n====\n\nH=\n" << H3 << "\nU=\n" << U3 << "\n";
        EXPECT_TRUE(isHNF(H3));
        EXPECT_TRUE(H3 == U3 * A);
    }
    {
        IntMatrix A{stringToIntMatrix("[1 -3 0 -2 0 0 -1 -1 0 0 -1 0 0 0 0 0 0 "
                                      "0 0 0 0 0; 0 1 0 1 0 0 0 1 0 "
                                      "0 0 0 0 0 0 0 0 0 0 0 0 0; 0 1 0 0 0 0 "
                                      "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
                                      "0; 0 1 0 1 0 0 0 0 0 0 1 0 0 0 0 0 0 0 "
                                      "0 0 0 0; 0 -1 1 -1 1 0 0 -1 1 "
                                      "0 0 0 0 0 0 0 0 0 0 0 0 0; 0 -1 1 0 0 1 "
                                      "-1 0 0 0 0 0 0 0 0 0 0 0 0 0 "
                                      "0 0; 0 -1 1 -1 1 0 0 0 0 1 -1 0 0 0 0 0 "
                                      "0 0 0 0 0 0; -1 0 0 0 0 0 0 0 "
                                      "0 0 0 1 0 0 0 0 0 0 0 0 0 0; 0 -1 0 0 0 "
                                      "0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 "
                                      "0 0; 0 0 -1 0 0 0 0 0 0 0 0 0 0 1 0 0 0 "
                                      "0 0 0 0 0; 0 0 0 -1 0 0 0 0 0 "
                                      "0 0 0 0 0 1 0 0 0 0 0 0 0; 0 0 0 0 -1 0 "
                                      "0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 "
                                      "0; 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 0 0 1 0 "
                                      "0 0 0 0; 0 0 0 0 0 0 -1 0 0 0 "
                                      "0 0 0 0 0 0 0 1 0 0 0 0; 0 0 0 0 0 0 0 "
                                      "-1 0 0 0 0 0 0 0 0 0 0 1 0 0 "
                                      "0; 0 0 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 0 "
                                      "0 1 0 0; 0 0 0 0 0 0 0 0 0 -1 "
                                      "0 0 0 0 0 0 0 0 0 0 1 0; 0 0 0 0 0 0 0 "
                                      "0 0 0 -1 0 0 0 0 0 0 0 0 0 0 "
                                      "1]")};
        auto [H3, U3] = NormalForm::hermite(A);
        llvm::errs() << "\n\n\n====\n\nH=\n" << H3 << "\nU=\n" << U3 << "\n";
        EXPECT_TRUE(isHNF(H3));
        EXPECT_TRUE(H3 == U3 * A);
    }
    {
        IntMatrix A(2, 3);
        A(0, 0) = -3;
        A(0, 1) = -1;
        A(0, 2) = 1;
        A(1, 0) = 0;
        A(1, 1) = 0;
        A(1, 2) = -2;
        std::optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> B =
            NormalForm::hermite(A);
        EXPECT_TRUE(B.has_value());
        auto &[H, U] = *B;
        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(U * A == H);
        llvm::errs() << "A = \n"
                     << A << "\nH =\n"
                     << H << "\nU =\n"
                     << U << "\n";
    }
    {
        IntMatrix A(3, 11);
        A(0, 0) = 3;
        A(0, 1) = 3;
        A(0, 2) = -3;
        A(0, 3) = 1;
        A(0, 4) = 0;
        A(0, 5) = -1;
        A(0, 6) = -2;
        A(0, 7) = 1;
        A(0, 8) = 1;
        A(0, 9) = 2;
        A(0, 10) = -1;

        A(1, 0) = 3;
        A(1, 1) = 3;
        A(1, 2) = -3;
        A(1, 3) = 1;
        A(1, 4) = 1;
        A(1, 5) = -3;
        A(1, 6) = 2;
        A(1, 7) = 0;
        A(1, 8) = 3;
        A(1, 9) = 0;
        A(1, 10) = -3;

        A(2, 0) = 2;
        A(2, 1) = -3;
        A(2, 2) = -2;
        A(2, 3) = -1;
        A(2, 4) = 1;
        A(2, 5) = -2;
        A(2, 6) = 3;
        A(2, 7) = 3;
        A(2, 8) = 3;
        A(2, 9) = 3;
        A(2, 10) = -3;
        auto [H, U] = NormalForm::hermite(A);
        EXPECT_TRUE(isHNF(H));
        EXPECT_TRUE(U * A == H);
        llvm::errs() << "A = \n"
                     << A << "\nH =\n"
                     << H << "\nU =\n"
                     << U << "\n";
    }
}

TEST(NullSpaceTests, BasicAssertions) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-10, 100);

    // size_t numIters = 1000;
    size_t numIters = 1;
    for (size_t numCol = 2; numCol < 11; numCol += 2) {
        IntMatrix B(8, numCol);
        size_t nullDim = 0;
        IntMatrix Z, NS;
        for (size_t i = 0; i < numIters; ++i) {
            for (auto &&b : B.mem) {
                b = distrib(gen);
                b = b > 10 ? 0 : b;
            }
            NS = NormalForm::nullSpace(B);
            nullDim += NS.numRow();
            Z = NS * B;
            for (auto &z : Z.mem)
                EXPECT_EQ(z, 0);
            EXPECT_EQ(NormalForm::nullSpace(std::move(NS)).numRow(), 0);
        }
        llvm::errs() << "Average tested null dim = "
                     << double(nullDim) / double(numIters) << "\n";
    }
}

TEST(SimplifySystemTests, BasicAssertions) {
    IntMatrix A = stringToIntMatrix(
        "[2 4 5 5 -5; -4 3 -4 -3 -1; 1 0 -2 1 -4; -4 -2 3 -2 -1]");
    IntMatrix B =
        stringToIntMatrix("[-6 86 -27 46 0 -15; -90 -81 91 44 -2 78; 4 -54 -98 "
                          "80 -10 82; -98 -15 -28 98 82 87]");
    NormalForm::solveSystem(A, B);
    IntMatrix sA = stringToIntMatrix("[-3975 0 0 0 -11370; 0 -1325 0 0 -1305; "
                                     "0 0 -265 0 -347; 0 0 0 -265 1124]");
    IntMatrix trueB = stringToIntMatrix(
        "[-154140 -128775 -205035 317580 83820 299760; -4910 -21400 -60890 "
        "44820 14480 43390; -1334 -6865 -7666 8098 -538 9191; 6548 9165 "
        "24307 -26176 -4014 -23332]");

    EXPECT_EQ(sA, A);
    EXPECT_EQ(trueB, B);

    IntMatrix C = stringToIntMatrix("[1 1 0; 0 1 1; 1 2 1]");
    IntMatrix D = stringToIntMatrix("[1 0 0; 0 1 0; 0 0 1]");
    NormalForm::simplifySystem(C, D);
    IntMatrix trueC = stringToIntMatrix("[1 0 -1; 0 1 1]");
    IntMatrix trueD = stringToIntMatrix("[1 -1 0; 0 1 0]");
    EXPECT_EQ(trueC, C);
    EXPECT_EQ(trueD, D);
}

TEST(BareissTests, BasicAssertions) {
    IntMatrix A = stringToIntMatrix(
        "[-4 3 -2 2 -5; -5 1 -1 2 -5; -1 0 5 -3 2; -4 5 -4 -2 -4]");
    NormalForm::bareiss(A);
    IntMatrix B = stringToIntMatrix(
        "[-4 3 -2 2 -5; 0 11 -6 2 -5; 0 0 56 -37 32; 0 0 0 -278 136]");
    EXPECT_EQ(A, B);

    IntMatrix C = stringToIntMatrix("[-2 -2 -1 -2 -1; 1 1 2 2 -2; -2 2 2 -1 "
                                    "-1; 0 0 -2 1 -1; -1 -2 2 1 -1]");
    IntMatrix D = stringToIntMatrix("[-2 -2 -1 -2 -1; 0 -8 -6 -2 0; 0 0 -12 -8 "
                                    "20; 0 0 0 -28 52; 0 0 0 0 -142]");
    auto pivots = NormalForm::bareiss(C);
    EXPECT_EQ(C, D);
    auto truePivots = llvm::SmallVector<size_t, 16>{0, 2, 2, 3, 4};
    EXPECT_EQ(pivots, truePivots);
}
