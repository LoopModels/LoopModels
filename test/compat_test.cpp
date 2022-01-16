#include "../include/math.hpp"
#include "../include/loops.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(CompatTest, BasicAssertions) {
    constexpr size_t int_size =
        ((MAX_PROGRAM_VARIABLES * 8) + 3 * 3) + 6 * 2 + 4;
    Int memory[int_size] = {0};
    Int *mem = memory;
    auto rectl = RectangularLoopNest(mem, 2);
    std::cout << "length(rectl) = " << length(rectl) << "\n";
    mem += length(rectl);
    auto trial = TriangularLoopNest(mem, 3);
    std::cout << "length(trial) = " << length(trial) << "\n";
    mem += length(trial);
    auto perm_rec = Permutation(mem, 2);
    std::cout << "length(perm_rec) = " << length(perm_rec) << "\n";
    mem += length(perm_rec);
    auto perm_tri = Permutation(mem, 3);
    std::cout << "length(perm_tri) = " << length(perm_tri) << "\n";
    mem += length(perm_tri);
    auto perm_tr2 = Permutation(mem, 3);
    std::cout << "length(perm_tr2) = " << length(perm_tr2) << "\n";
    EXPECT_EQ(length(rectl) + length(trial) + length(perm_rec) +
                  length(perm_tri) + length(perm_tr2),
              int_size);
    EXPECT_TRUE(allzero(memory, int_size));

    auto gd = rectl.data;
    gd(1, 0) = 1;
    gd(2, 1) = 1;
    auto gd1 = getRekt(trial).data;
    auto A = getTrit(trial);
    gd1(1, 0) = 1;
    gd1(2, 1) = 1;
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
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);

    EXPECT_EQ(compatible(rectl, rectl, perm_rec, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(rectl, rectl, perm_rec, perm_rec, 1, 1), true);

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    EXPECT_EQ(compatible(rectl, rectl, perm_rec, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(rectl, rectl, perm_rec, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(rectl, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(rectl, rectl, perm_tri, perm_rec, 1, 1), false);
    swap(perm_tri, 0, 1);

    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    swap(perm_tri, 0, 1);
    swap(perm_tri, 1, 2);
    // (0,2,1), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    swap(perm_tri, 0, 1);
    // (2,0,1), (1,0)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);

    swap(perm_tri, 0, 1);
    swap(perm_tri, 1, 2);
    // (0, 1, 2), (1, 0)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tri, 1, 2);
    // (0,2,1), (1,0)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tri, 0, 2);
    swap(perm_tri, 1, 2);
    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (1,0,2), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tri, 0, 2);
    // (2,0,1), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tri, 0, 2);
    swap(perm_tri, 1, 2);
    // (1,2,0), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (1,2,0), (1,0)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tri, 0, 1);
    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (2,1,0), (0,1)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), false);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_rec, 0, 1);
    swap(perm_tr2, 0, 1);
    // (2,1,0), (1,0)
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 0, 0), true);
    EXPECT_EQ(compatible(trial, rectl, perm_tri, perm_rec, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);

    swap(perm_tr2, 1, 2);
    // (2,1,0), (1,2,0) // k <-> n, n <-> k, m <-> m
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 2, 2), true);

    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 2, 2), true);

    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tri, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tri, 2, 2), true);

    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tr2, 1, 1), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tr2, 2, 2), true);

    swap(perm_tr2, 0, 1);
    swap(perm_tr2, 1, 2);
    // (2,1,0), (2,0,1) // k <-> k, n <-> m, m <-> n
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tri, perm_tr2, 1, 1), false);

    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 0, 0), true);
    EXPECT_EQ(compatible(trial, trial, perm_tr2, perm_tri, 1, 1), false);
}
