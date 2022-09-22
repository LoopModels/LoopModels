#include "../include/BitSets.hpp"
#include "../include/Math.hpp"
#include <gtest/gtest.h>
#include <iostream>

TEST(BitSetTest, BasicAssertions) {
    BitSet bs(1000);
    bs[4] = true;
    bs[10] = true;
    bs[200] = true;
    bs[117] = true;
    bs[87] = true;
    bs[991] = true;
    bs[0] = true;
    std::cout << bs << std::endl;
    llvm::SmallVector<size_t> bsc{0, 4, 10, 87, 117, 200, 991};
    size_t j = 0;
    for (auto I = bs.begin(); I != bs.end(); ++I) {
        EXPECT_EQ(*I, bsc[j]);
        EXPECT_TRUE(bs[*I]);
        printf("We get: %zu\n", *I);
        ++j;
    }
    j = 0;
    for (auto i : bs) {
        EXPECT_EQ(i, bsc[j]);
        EXPECT_TRUE(bs[i]);
        printf("We get: %zu\n", i);
        ++j;
    }
    EXPECT_EQ(j, bsc.size());
    EXPECT_EQ(j, bs.size());
}
