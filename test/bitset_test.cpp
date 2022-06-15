#include "../include/BitSets.hpp"
#include "../include/Math.hpp"
#include <iostream>
#include <gtest/gtest.h>

TEST(BitSetTest, BasicAssertions) {
    BitSet bs(1000);
    push(bs, 4);
    push(bs, 10);
    push(bs, 200);
    push(bs, 117);
    push(bs, 87);
    push(bs, 991);
    std::cout << bs << std::endl;
    llvm::SmallVector<size_t> bsc{4, 10, 87, 117, 200, 991};
    size_t j = 0;
    for (auto I = bs.begin(); I != bs.end(); ++I) {
        EXPECT_EQ(*I, bsc[j]);
        EXPECT_EQ(contains(bs, *I) != 0, true);
        printf("We get: %zu\n", *I);
        ++j;
    }
    EXPECT_EQ(j, bsc.size());
    EXPECT_EQ(j, length(bs));
}
