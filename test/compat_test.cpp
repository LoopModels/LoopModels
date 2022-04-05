#include "../include/Loops.hpp"
#include "../include/Math.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>

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

TEST(AffineTest0, BasicAssertions) {
    std::cout << "Starting affine test 0" << std::endl;
    llvm::SmallVector<MPoly, 8> r;
    Matrix<Int, 0, 0, 0> A(3, 6);
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto Zero = Polynomial::Term{intptr_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{intptr_t(1), Polynomial::Monomial()};
    // auto nOne = Polynomial::Term{intptr_t(-1), Polynomial::Monomial()};
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
    // r.push_back(Polynomial::Term{intptr_t(1), M} - 1);
    r.push_back(M - 1);
    A(0, 0) = 1;
    A(1, 0) = 0;
    A(2, 0) = 0;
    // 0 <= m;
    // r.emplace_back(0);
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
    // r.emplace_back(0);
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
    std::shared_ptr<AffineLoopNest> aff(
        std::make_shared<AffineLoopNest>(AffineLoopNest{A, r, poset}));
    AffineLoopNestBounds affp(aff);
    std::cout << "Constructed affine obj" << std::endl;
    std::cout << "About to run first compat test" << std::endl;
    EXPECT_TRUE(affp.zeroExtraIterationsUponExtending(1, false));
    std::cout << "About to run second compat test" << std::endl;
    EXPECT_FALSE(affp.zeroExtraIterationsUponExtending(1, true));
    std::cout << affp << std::endl;
    std::cout << "About to run first set of bounds tests" << std::endl;
    { // lower bound tests
        EXPECT_EQ(affp.lowerB.size(), 3);
        EXPECT_EQ(affp.lowerB[0].size(), 1);
        EXPECT_EQ(affp.lowerB[1].size(), 1);
        EXPECT_EQ(affp.lowerB[2].size(), 1);
        EXPECT_TRUE(affp.lowerB[0][0] == 0);
        EXPECT_TRUE(affp.lowerB[1][0] == 0);
        llvm::SmallVector<intptr_t, 4> a{0, 1, -1};
        MPoly b;
        b -= 1;
        EXPECT_TRUE(affp.lowerA[2].getCol(0) == a);
        EXPECT_TRUE(affp.lowerB[2][0] == b);
    }
    { // upper bound tests
        EXPECT_EQ(affp.upperB.size(), 3);
        EXPECT_EQ(affp.upperB[0].size(), 1);
        EXPECT_EQ(affp.upperB[1].size(), 1);
        EXPECT_EQ(affp.upperB[2].size(), 1);
        EXPECT_TRUE(affp.upperB[0][0] == M - 1);
        EXPECT_TRUE(affp.upperB[1][0] == N - 2);
        EXPECT_TRUE(affp.upperB[2][0] == N - 1);
    }
    std::cout << "\nPermuting loops 1 and 2" << std::endl;
    affp.swap(1, 2);
    // Now that we've swapped loops 1 and 2, we should have
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    affp.dump();
    // std::cout << "First lc: \n";
    // affp.lc[0][0].dump();
    { // lower bound tests
        EXPECT_EQ(affp.lowerB.size(), 3);
        EXPECT_EQ(affp.lowerB[0].size(), 1);
        EXPECT_EQ(affp.lowerB[1].size(), 1);
        EXPECT_EQ(affp.lowerB[2].size(), 1);
        EXPECT_TRUE(affp.lowerB[0][0] == 0);
        EXPECT_TRUE(affp.lowerB[2][0] == -1); // -j <= -1
        EXPECT_TRUE(affp.lowerB[1][0] == 0);
    }
    { // upper bound tests
        EXPECT_EQ(affp.upperB.size(), 3);
        EXPECT_EQ(affp.upperB[0].size(), 1);
        EXPECT_EQ(affp.upperB[1].size(), 1);
        EXPECT_EQ(affp.upperB[2].size(), 1);
        EXPECT_TRUE(affp.upperB[0][0] == M - 1);
        EXPECT_TRUE(affp.upperB[2][0] == N - 1);
        // EXPECT_TRUE(affp.uc[2][0] == N - 1);
        llvm::SmallVector<intptr_t, 4> a{0, 1, -1};
        MPoly b;
        b -= 1;
        EXPECT_TRUE(affp.upperA[1].getCol(0) == a);
        EXPECT_TRUE(affp.upperB[1][0] == b);
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
    Matrix<Int, 0, 0, 0> A(2, 4);
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    // auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto Zero = Polynomial::Term{intptr_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{intptr_t(1), Polynomial::Monomial()};
    auto nOne = Polynomial::Term{intptr_t(-1), Polynomial::Monomial()};
    PartiallyOrderedSet poset;
    // ids 1 and 2 are >= 0;
    poset.push(0, 1, Interval::nonNegative());
    poset.push(0, 2, Interval::nonNegative());
    // the loop is
    // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
    //
    // Bounds:
    // n - m <= M;
    // r.push_back(Polynomial::Term{intptr_t(1), M} - 1);
    r.push_back(2 * M);
    A(0, 0) = -1;
    A(1, 0) = 1;
    // n - m >= 1;
    // r.emplace_back(0);
    r.push_back(nOne * 2);
    A(0, 1) = 1;
    A(1, 1) = -1;
    // m + n >= -M
    r.push_back(2 * M);
    A(0, 2) = -1;
    A(1, 2) = -1;
    // m + n <= -1;
    r.push_back(2 * nOne);
    A(0, 3) = 1;
    A(1, 3) = 1;
    std::cout << "A = \n"
              << A << "\nb = \n[ " << r[0] << ", " << r[1] << ", " << r[2]
              << ", " << r[3] << " ]" << std::endl;
    std::shared_ptr<AffineLoopNest> aff(
        std::make_shared<AffineLoopNest>(AffineLoopNest{A, r, poset}));
    AffineLoopNestBounds affp(aff);
    std::cout << "Original order:" << std::endl;
    std::cout << affp << std::endl;

    affp.swap(0, 1);
    std::cout << "Swapped order:" << std::endl;
    std::cout << affp << std::endl;
}
