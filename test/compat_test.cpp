#include "../include/math.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(CompatTest, BasicAssertions) {
    constexpr size_t int_size =
        ((MAX_PROGRAM_VARIABLES * 5) + 3 * 3) + 6 * 2 + 4;
    Int memory[int_size] = {0};
    Int* mem = memory;
    auto rectl = RectangularLoopNest(mem, 2);
    mem += length(rectl);
    auto trial = TriangularLoopNest(mem, 3);
    mem += length(trial);
    auto perm_rec = Permutation(mem, 2);
    mem += length(perm_rec);
    auto perm_tri = Permutation(mem, 3);
    mem += length(perm_tri);
    auto perm_tr2 = Permutation(mem, 3);
    EXPECT_EQ(length(rectl) + length(trial) + length(perm_rec) +
                  length(perm_tri) + length(perm_tr2),
              int_size);
    EXPECT_EQ(allzero(memory, int_size), true);


    auto gd = rectl.data;
    gd(1, 0) = 1;
    gd(2, 1) = 1;
    auto gd1 = getRekt(trial).data;
    auto A = getTrit(trial);
    gd1(1,0) = 1;
    gd1(2,1) = 1;
    A(0,0) = 1; A(0,1) =  0; A(0,2) =  0;
    A(1,0) = 0; A(1,1) =  1; A(1,2) = -1; // set to `-1` because of assumed reflection
    A(2,0) = 0; A(2,1) = -1; A(2,2) =  1;

    init(perm_rec);
    init(perm_tri);
    init(perm_tr2);
    // (0,1,2), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);
}
