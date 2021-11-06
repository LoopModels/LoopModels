#include <cstdio>
#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <algorithm>
#include "../include/matrix.hpp"

TEST(PermTest, BasicAssertions)
{
    size_t numloop = 5;
    Int x[2 * numloop + 2];
    auto p = Permutation(x, numloop);
    set_original_order(p);
    std::set<std::vector<Int>> s;
    std::vector<Int> tperm(numloop);

    for (Int i_0 = 0; i_0 < 5; i_0++) {
        advance_state(PermutationLevelIterator(p,0,0), i_0);
        for (Int i_1 = 0; i_1 < 4; i_1++) {
            advance_state(PermutationLevelIterator(p,1,0), i_1);
            for (Int i_2 = 0; i_2 < 3; i_2++) {
                advance_state(PermutationLevelIterator(p,2,0), i_2);
                for (Int i_3 = 0; i_3 < 2; i_3++) {
                    advance_state(PermutationLevelIterator(p,3,0), i_3);
                    for (Int i_4 = 0; i_4 < 1; i_4++) {
                        advance_state(PermutationLevelIterator(p,4,0), i_4);
                        for (Int j = 0; j < numloop; j++) tperm[j] = p(j, 0);

                        std::vector<Int> perm = tperm;
                        std::sort(tperm.begin(), tperm.end());
                        for (Int j = 0; j < numloop; j++) EXPECT_EQ(tperm[j], j);
                        s.insert(perm);
                    }
                }
            }
        }
    }

    s.clear();
}
