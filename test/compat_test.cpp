#include "../include/math.hpp"
#include "../include/loops.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(CompatTest, BasicAssertions) {
    
    auto rectl = RectangularLoopNest(2);
    auto trial = TriangularLoopNest(3);
    auto perm_rec = Permutation(2);
    auto perm_tri = Permutation(3);
    auto perm_tr2 = Permutation(3);

    UpperBounds& gd = rectl.data;
    gd[0] += Polynomial::Monomial(Polynomial::ID{0});
    gd[1] += Polynomial::Monomial(Polynomial::ID{1});
    UpperBounds& gd1 = trial.getRekt().data;
    SquareMatrix<Int>& A = trial.getTrit();
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

    init(perm_rec);
    init(perm_tri);
    init(perm_tr2);
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
    
    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    
    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(rectl, rectl, perm_rec, perm_rec, 1, 1));
    
    EXPECT_FALSE(compatible(rectl, rectl, perm_tri, perm_rec, 0, 0));
    
    EXPECT_FALSE(compatible(rectl, rectl, perm_tri, perm_rec, 1, 1));
    swap(perm_tri, 0, 1);

    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    swap(perm_tri, 0, 1);
    swap(perm_tri, 1, 2);
    // (0,2,1), (0,1)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    swap(perm_tri, 0, 1);
    // (2,0,1), (1,0)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 2, 2));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 2, 2));

    swap(perm_tri, 0, 1);
    swap(perm_tri, 1, 2);
    // (0, 1, 2), (1, 0)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tri, 1, 2);
    // (0,2,1), (1,0)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tri, 0, 2);
    swap(perm_tri, 1, 2);
    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (1,0,2), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tri, 0, 2);
    // (2,0,1), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tri, 0, 2);
    swap(perm_tri, 1, 2);
    // (1,2,0), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (1,2,0), (1,0)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tri, 0, 1);
    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (2,1,0), (0,1)
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (2,1,0), (1,0)
    EXPECT_TRUE(compatible(trial, rectl, perm_tri, perm_rec, 0, 0));
    EXPECT_FALSE(compatible(trial, rectl, perm_tri, perm_rec, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));
    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));

    swap(perm_tr2, 1, 2);
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

    swap(perm_tr2, 0, 1);
    swap(perm_tr2, 1, 2);
    // (2,1,0), (2,0,1) // k <-> k, n <-> m, m <-> n
    EXPECT_TRUE(compatible(trial, trial, perm_tri, perm_tr2, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tri, perm_tr2, 1, 1));

    EXPECT_TRUE(compatible(trial, trial, perm_tr2, perm_tri, 0, 0));
    EXPECT_FALSE(compatible(trial, trial, perm_tr2, perm_tri, 1, 1));
}

