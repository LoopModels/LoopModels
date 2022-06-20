#include "../include/Loops.hpp"
#include "../include/Math.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>

TEST(AffineTest0, BasicAssertions) {
    std::cout << "Starting affine test 0" << std::endl;
    llvm::SmallVector<MPoly, 8> r;
    IntMatrix A(6, 3);
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    // auto nOne = Polynomial::Term{int64_t(-1), Polynomial::Monomial()};
    One.dump();
    std::cout << "initializing poset." << std::endl;
    PartiallyOrderedSet poset;
    std::cout << "initialized poset\n";
    // ids 1 and 2 are >= 0;
    poset.push(0, 1, Interval::nonNegative());
    std::cout << "first push into poset.";
    poset.push(0, 2, Interval::nonNegative());
    std::cout << "second push into poset.";
    // the loop is
    // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
    //
    // Bounds:
    // m <= M - 1;
    // r.push_back(Polynomial::Term{int64_t(1), M} - 1);
    r.push_back(M - 1);
    A(0, 0) = 1;
    A(0, 1) = 0;
    A(0, 2) = 0;
    // 0 <= m;
    // r.emplace_back(0);
    r.push_back(Zero);
    A(1, 0) = -1;
    A(1, 1) = 0;
    A(1, 2) = 0;
    // n <= N - 1;
    r.push_back(N - 1);
    A(2, 0) = 0;
    A(2, 1) = 1;
    A(2, 2) = 0;
    // 0 <= n;
    // r.emplace_back(0);
    r.push_back(Zero);
    A(3, 0) = 0;
    A(3, 1) = -1;
    A(3, 2) = 0;
    // k <= N - 1
    r.push_back(N - 1);
    A(4, 0) = 0;
    A(4, 1) = 0;
    A(4, 2) = 1;
    // n - k <= -1  ->  n + 1 <= k
    r.push_back(Polynomial::Term{int64_t(-1), Polynomial::Monomial()});
    A(5, 0) = 0;
    A(5, 1) = 1;
    A(5, 2) = -1;

    std::cout << "About to construct affine obj" << std::endl;

    AffineLoopNest affp(A, r, poset);
    std::cout << "Constructed affine obj" << std::endl;
    std::cout << "About to run first compat test" << std::endl;
    EXPECT_TRUE(affp.zeroExtraIterationsUponExtending(1, false));
    std::cout << "About to run second compat test" << std::endl;
    EXPECT_FALSE(affp.zeroExtraIterationsUponExtending(1, true));
    std::cout << affp << std::endl;
    std::cout << "About to run first set of bounds tests" << std::endl;
    { // lower bound tests
        EXPECT_EQ(affp.lowerb.size(), 3);
        EXPECT_EQ(affp.lowerb[0].size(), 1);
        EXPECT_EQ(affp.lowerb[1].size(), 1);
        EXPECT_EQ(affp.lowerb[2].size(), 1);
        EXPECT_TRUE(affp.lowerb[0][0] == 0);
        EXPECT_TRUE(affp.lowerb[1][0] == 0);
        llvm::SmallVector<int64_t, 4> a{0, 1, -1};
        MPoly b;
        b -= 1;
        EXPECT_TRUE(affp.lowerA[2].getRow(0) == a);
        EXPECT_TRUE(affp.lowerb[2][0] == b);
    }
    { // upper bound tests
        EXPECT_EQ(affp.upperb.size(), 3);
        EXPECT_EQ(affp.upperb[0].size(), 1);
        EXPECT_EQ(affp.upperb[1].size(), 1);
        EXPECT_EQ(affp.upperb[2].size(), 1);
        EXPECT_TRUE(affp.upperb[0][0] == M - 1);
        EXPECT_TRUE(affp.upperb[1][0] == N - 2);
        EXPECT_TRUE(affp.upperb[2][0] == N - 1);
    }
    std::cout << "\nPermuting loops 1 and 2" << std::endl;
    affp.swap(1, 2);
    // Now that we've swapped loops 1 and 2, we should have
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    affp.dump();
    // std::cout << "First lc: \n";
    // affp.lc[0][0].dump();
    { // lower bound tests
        EXPECT_EQ(affp.lowerb.size(), 3);
        EXPECT_EQ(affp.lowerb[0].size(), 1);
        EXPECT_EQ(affp.lowerb[1].size(), 1);
        EXPECT_EQ(affp.lowerb[2].size(), 1);
        EXPECT_TRUE(affp.lowerb[0][0] == 0);
        EXPECT_TRUE(affp.lowerb[2][0] == -1); // -j <= -1
        EXPECT_TRUE(affp.lowerb[1][0] == 0);
    }
    { // upper bound tests
        EXPECT_EQ(affp.upperb.size(), 3);
        EXPECT_EQ(affp.upperb[0].size(), 1);
        EXPECT_EQ(affp.upperb[1].size(), 1);
        EXPECT_EQ(affp.upperb[2].size(), 1);
        EXPECT_TRUE(affp.upperb[0][0] == M - 1);
        EXPECT_TRUE(affp.upperb[2][0] == N - 1);
        // EXPECT_TRUE(affp.uc[2][0] == N - 1);
        llvm::SmallVector<int64_t, 4> a{0, 1, -1};
        MPoly b;
        b -= 1;
        EXPECT_TRUE(affp.upperA[1].getRow(0) == a);
        EXPECT_TRUE(affp.upperb[1][0] == b);
    }

    /*
    std::cout << "\nExtrema of loops:" << std::endl;
    for (size_t i = 0; i < affp.getNumLoops(); ++i) {
        auto lbs = aff->lExtrema[i];
        auto ubs = aff->uExtrema[i];
        std::cout << "Loop " << i << " lower bounds: " << std::endl;
        for (auto &b : lbs) {
            auto lb = b;
            lb *= -1;
            lb.dump();
        }
        std::cout << "Loop " << i << " upper bounds: " << std::endl;
        for (auto &b : ubs) {
            b.dump();
        }
    }
    */
    // For reference, the permuted loop bounds are:
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    std::cout << "Checking if the inner most loop iterates when adjusting "
                 "outer loops:"
              << std::endl;
    std::cout << "Constructed affine obj" << std::endl;
    std::cout << "About to run first compat test" << std::endl;
    EXPECT_FALSE(affp.zeroExtraIterationsUponExtending(1, false));
    std::cout << "About to run second compat test" << std::endl;
    EXPECT_TRUE(affp.zeroExtraIterationsUponExtending(1, true));

    // affp.zeroExtraIterationsUponExtending(poset, 1, )
}
TEST(NonUnimodularExperiment, BasicAssertions) {
    std::cout << "Starting affine test 0" << std::endl;
    llvm::SmallVector<MPoly, 8> r;
    IntMatrix A(4, 2);
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    // auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    auto nOne = Polynomial::Term{int64_t(-1), Polynomial::Monomial()};
    PartiallyOrderedSet poset;
    // ids 1 and 2 are >= 0;
    poset.push(0, 1, Interval::nonNegative());
    poset.push(0, 2, Interval::nonNegative());
    // the loop is
    // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
    //
    // Bounds:
    // n - m <= M;
    // r.push_back(Polynomial::Term{int64_t(1), M} - 1);
    r.push_back(2 * M);
    A(0, 0) = -1;
    A(0, 1) = 1;
    // n - m >= 1;
    // r.emplace_back(0);
    r.push_back(nOne * 2);
    A(1, 0) = 1;
    A(1, 1) = -1;
    // m + n >= -M
    r.push_back(2 * M);
    A(2, 0) = -1;
    A(2, 1) = -1;
    // m + n <= -1;
    r.push_back(2 * nOne);
    A(3, 0) = 1;
    A(3, 1) = 1;
    std::cout << "A = \n"
              << A << "\nb = \n[ " << r[0] << ", " << r[1] << ", " << r[2]
              << ", " << r[3] << " ]" << std::endl;
    AffineLoopNest affp(A, r, poset);
    std::cout << "Original order:" << std::endl;
    std::cout << affp << std::endl;

    affp.swap(0, 1);
    std::cout << "Swapped order:" << std::endl;
    std::cout << affp << std::endl;

    EXPECT_FALSE(affp.isEmpty());
}
