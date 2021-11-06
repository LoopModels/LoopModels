#include <cstdio>
#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <algorithm>
#include "../include/math.hpp"

TEST(PermTest, BasicAssertions)
{
    size_t numloop = 5;
    Int x[2 * numloop + 2];
    auto p = Permutation(x, numloop).init();
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
                    }
                }
            }
        }
    }
    // Test the number of permutations == numloop!
    EXPECT_EQ(s.size(), 5 * 4 * 3 * 2 * 1);
    s.clear();

    p.init();
    for (Int i_0 = 0; i_0 < 5; i_0++) {
        auto state1 = advance_state(PermutationLevelIterator(p,0,0), i_0);
        for (Int i_1 = 0; i_1 < 4; i_1++) {
            auto state2 = advance_state(PermutationLevelIterator(state1), i_1);
            for (Int i_2 = 0; i_2 < 3; i_2++) {
                auto state3 = advance_state(PermutationLevelIterator(state2), i_2);
                for (Int i_3 = 0; i_3 < 2; i_3++) {
                    auto state4 = advance_state(PermutationLevelIterator(state3), i_3);
                    for (Int i_4 = 0; i_4 < 1; i_4++) {
                        auto state5 = advance_state(PermutationLevelIterator(state4), i_4);
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
                    }
                }
            }
        }
    }
    std::printf("[Nice] Phew, we are done with PermTest!\n");
}
