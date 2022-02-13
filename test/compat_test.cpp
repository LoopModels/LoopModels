#include "../include/loops.hpp"
#include "../include/math.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>

TEST(CompatTest, BasicAssertions) {

    auto rectl = RectangularLoopNest(2);
    auto trial = TriangularLoopNest(3);
    auto perm_rec = Permutation(2);
    auto perm_tri = Permutation(3);
    auto perm_tr2 = Permutation(3);

    UpperBounds &gd = rectl.data;
    gd[0] += Polynomial::Monomial(Polynomial::ID{0});
    gd[1] += Polynomial::Monomial(Polynomial::ID{1});
    UpperBounds &gd1 = trial.getRekt().data;
    SquareMatrix<Int> &A = trial.getTrit();
    gd1[0] += Polynomial::Monomial(Polynomial::ID{0});
    gd1[1] += Polynomial::Monomial(Polynomial::ID{1});
    A(0, 0) = 1;
    A(0, 1) = 0;
    A(0, 2) = 0;
    A(1, 0) = 0;
    A(1, 1) = 1;
    A(1, 2) = -1; // set to `-1` because of assumed reflection
    A(2, 0) = 0;
    A(2, 1) = -1;
    A(2, 2) = 1;

    perm_rec.init();
    perm_tri.init();
    perm_tr2.init();
    // (0,1,2), (0,1)

    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));

    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 1, 1));

    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);

    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 1, 1));

    EXPECT_FALSE(compatible(rectl, rectl, perm_tri, perm_rec, 0, 0));

    EXPECT_FALSE(compatible(rectl, rectl, perm_tri, perm_rec, 1, 1));
    perm_tri.swap(0, 1);

    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    perm_tri.swap(0, 1);
    perm_tri.swap(1, 2);
    // (0,2,1), (0,1)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    perm_tri.swap(0, 1);
    // (2,0,1), (1,0)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    perm_tri.swap(0, 1);
    perm_tri.swap(1, 2);
    // (0, 1, 2), (1, 0)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tri.swap(1, 2);
    // (0,2,1), (1,0)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tri.swap(0, 2);
    perm_tri.swap(1, 2);
    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    // (1,0,2), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tri.swap(0, 2);
    // (2,0,1), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tri.swap(0, 2);
    perm_tri.swap(1, 2);
    // (1,2,0), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    // (1,2,0), (1,0)

    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tri.swap(0, 1);
    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    // (2,1,0), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_rec.swap(0, 1);
    perm_tr2.swap(0, 1);
    // (2,1,0), (1,0)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    perm_tr2.swap(1, 2);
    // (2,1,0), (1,2,0) // k <-> n, n <-> k, m <-> m
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));

    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tri, 2, 2));

    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tr2, 2, 2));

    perm_tr2.swap(0, 1);
    perm_tr2.swap(1, 2);
    // (2,1,0), (2,0,1) // k <-> k, n <-> m, m <-> n
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));

    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
}

TEST(AffineTest, BasicAssertions) {
    std::cout << "Starting affine tests" << std::endl;
    llvm::SmallVector<MPoly, 8> r;
    Matrix<Int, 0, 0> A = Matrix<Int, 0, 0>(3, 6);
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto Zero = Polynomial::Term{intptr_t(0), Polynomial::Monomial()};

    // m <= M - 1;
    // r.push_back(Polynomial::Term{intptr_t(1), M} - 1);
    r.push_back(M - 1);
    A(0, 0) = 1;
    A(1, 0) = 0;
    A(2, 0) = 0;
    // 0 <= m;
    r.push_back(Zero);
    A(0, 1) = -1;
    A(1, 1) = 0;
    A(2, 1) = 0;
    // n <= N - 1;
    r.push_back(N - 1);
    A(0, 2) = 0;
    A(1, 2) = 1;
    A(2, 2) = 0;
    // 0 <= n;
    r.push_back(Zero);
    A(0, 3) = 0;
    A(1, 3) = -1;
    A(2, 3) = 0;
    // k <= N - 1
    r.push_back(N - 1);
    A(0, 4) = 0;
    A(1, 4) = 0;
    A(2, 4) = 1;
    // n - k <= -1  ->  n + 1 <= k
    r.push_back(Polynomial::Term{intptr_t(-1), Polynomial::Monomial()});
    A(0, 5) = 0;
    A(1, 5) = 1;
    A(2, 5) = -1;

    std::cout << "About to construct affine obj" << std::endl;
    AffineLoopNest aff(A, r);
    std::cout << "Constructed affine obj" << std::endl;
    { // lower bound tests
        EXPECT_EQ(aff.lc.size(), 3);
        EXPECT_EQ(aff.lc[0].size(), 1);
        EXPECT_EQ(aff.lc[1].size(), 1);
        EXPECT_EQ(aff.lc[2].size(), 1);
        EXPECT_TRUE(aff.lc[0][0] == 0);
        EXPECT_TRUE(aff.lc[1][0] == 0);
        llvm::SmallVector<intptr_t, 4> a;
        a.push_back(0);
        a.push_back(1);
        a.push_back(0);
        MPoly b;
        b -= 1;
        EXPECT_TRUE(aff.lc[2][0] == Affine(a, b, -1));
    }
    { // upper bound tests
        EXPECT_EQ(aff.uc.size(), 3);
        EXPECT_EQ(aff.uc[0].size(), 1);
        EXPECT_EQ(aff.uc[1].size(), 1);
        EXPECT_EQ(aff.uc[2].size(), 1);
        EXPECT_TRUE(aff.uc[0][0] == M - 1);
        EXPECT_TRUE(aff.uc[1][0] == N - 1);
        EXPECT_TRUE(aff.uc[2][0] == N - 1);
    }
    std::cout << aff << std::endl;
    std::cout << "\nPermuting loops 1 and 2" << std::endl;
    aff.swap(1, 2);
    std::cout << aff << std::endl;
    { // lower bound tests
        EXPECT_EQ(aff.lc.size(), 3);
        EXPECT_EQ(aff.lc[0].size(), 1);
        EXPECT_EQ(aff.lc[1].size(), 1);
        EXPECT_EQ(aff.lc[2].size(), 1);
        EXPECT_TRUE(aff.lc[0][0] == 0);
        EXPECT_TRUE(aff.lc[1][0] == -1); // -j <= -1
        EXPECT_TRUE(aff.lc[2][0] == 0);
    }
    { // upper bound tests
        EXPECT_EQ(aff.uc.size(), 3);
        EXPECT_EQ(aff.uc[0].size(), 1);
        EXPECT_EQ(aff.uc[1].size(), 1);
        EXPECT_EQ(aff.uc[2].size(), 2); // FIXME: prune to 1
        EXPECT_TRUE(aff.uc[0][0] == M - 1);
        EXPECT_TRUE(aff.uc[1][0] == N - 1);
        EXPECT_TRUE(aff.uc[2][0] == N - 1);
        llvm::SmallVector<intptr_t, 4> a;
        a.push_back(0);
        a.push_back(0);
        a.push_back(-1);
        MPoly b;
        b -= 1;
        EXPECT_TRUE(aff.uc[2][1] == Affine(a, b, 1));
    }

    std::cout << "\nExtrema of loops:" << std::endl;
    for (size_t i = 0; i < aff.getNumLoops(); ++i) {
        auto lbs = aff.lExtrema[i];
        auto ubs = aff.uExtrema[i];
        std::cout << "Loop " << i << " lower bounds: " << std::endl;
        for (auto &b : lbs) {
            auto lb = b;
            lb *= -1;
            std::cout << lb << std::endl;
        }
        std::cout << "Loop " << i << " upper bounds: " << std::endl;
        for (auto &b : ubs) {
            std::cout << b << std::endl;
        }
    }
}
