#include "../include/math.hpp"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>
#include <set>
#include <vector>

const size_t numloop = 5;
Int x[2 * numloop + 2];
auto p = Permutation(x, numloop).init();
std::set<std::vector<Int>> s;
std::vector<Int> tperm(numloop);

void check_partition(Permutation p, Int num_exterior) {
    for (Int j = 0; j < num_exterior; j++) tperm[j] = p(j, 0);
    std::printf("Testing partition 1: ");
    p.show();
    std::sort(tperm.begin(), tperm.begin() + num_exterior);
    for (Int j = 0; j < num_exterior; j++) {
        EXPECT_EQ(tperm[j], j);
    }
}

void recursive_iterator(Permutation p, Int lv = 0, Int num_exterior = 0) {
    Int nloops = getNLoops(p);
    assert(lv < nloops);
    if ((lv + 1) == num_exterior) check_partition(p, num_exterior);
    if ((lv + 1) == nloops) {
        for (Int j = 0; j < numloop; j++) tperm[j] = p(j, 0);
        p.show();
        std::vector<Int> perm = tperm;
        std::sort(tperm.begin(), tperm.end());
        for (Int j = 0; j < numloop; j++) {
            // Test if there's a bijection.
            auto ip = p(j, 1);
            EXPECT_EQ(p(ip, 0), j);
            EXPECT_EQ(tperm[j], j);
        }
        // This makes the test fail DELIBERATELY. We didn't know
        // what std::set does, so we used this to check if it's
        // by value or by pointer.
        //
        // std::sort(perm.begin(), perm.end());
        s.insert(perm);
        return;
    }
    PermutationLevelIterator pli = PermutationLevelIterator(
        p, lv, (num_exterior > lv ? nloops - num_exterior : 0));
    Int i = 0;
    while (true) {
        auto pr = advance_state(pli, i++);
        if ((lv + 1) == num_exterior) check_partition(p, num_exterior);
        recursive_iterator(p, lv + 1, num_exterior);
        if (!pr.second){
            return;
        }
    }
}

void recursive_iterator_2(PermutationLevelIterator pli, Int lv = 0, Int num_exterior = 0) {
    Int nloops = getNLoops(p);
    assert(lv < nloops);
    if ((lv + 1) == num_exterior) check_partition(p, num_exterior);
    if ((lv + 1) == nloops) {
        for (Int j = 0; j < numloop; j++) tperm[j] = p(j, 0);
        p.show();
        std::vector<Int> perm = tperm;
        std::sort(tperm.begin(), tperm.end());
        for (Int j = 0; j < numloop; j++) {
            // Test if there's a bijection.
            auto ip = p(j, 1);
            EXPECT_EQ(p(ip, 0), j);
            EXPECT_EQ(tperm[j], j);
        }
        // This makes the test fail DELIBERATELY. We didn't know
        // what std::set does, so we used this to check if it's
        // by value or by pointer.
        //
        // std::sort(perm.begin(), perm.end());
        s.insert(perm);
        return;
    }
    Int i = 0;
    while (true) {
        auto pr = advance_state(pli, i++);
        if ((lv + 1) == num_exterior) check_partition(p, num_exterior);
        recursive_iterator_2(pr.first, lv+1, num_exterior);
        if (!pr.second){
            return;
        }
    }
}

TEST(PermTest, BasicAssertions) {
    s.clear();
    p.init();
    recursive_iterator(p);

    // Test the number of permutations == numloop!
    EXPECT_EQ(s.size(), 5 * 4 * 3 * 2 * 1);
    std::printf("[Nice 1] Phew, we are done with PermTest!\n");

    s.clear();
    p.init();
    recursive_iterator_2(PermutationLevelIterator(p, 0, 0));
    EXPECT_EQ(s.size(), 5 * 4 * 3 * 2 * 1);
    std::printf("[Nice 2] Phew, we are done with PermTest!\n");

    s.clear();
    p.init();
    recursive_iterator(p, 0, 3);
    EXPECT_EQ(s.size(), 3 * 2 * 1 * (2 * 1));
    std::printf("[Nice 3] Phew, we are done with PermTest!\n");

    s.clear();
    p.init();
    recursive_iterator_2(PermutationLevelIterator(p, 0, 3), 0, 2);
    // Test the number of permutations == numloop!
    EXPECT_EQ(s.size(), 3 * 2 * 1 * (2 * 1));
    std::printf("[Nice 4] Phew, we are done with PermTest!\n");
}
