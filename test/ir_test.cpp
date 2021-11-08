#include "../include/ir.hpp"
#include <cstdio>
#include <gtest/gtest.h>

TEST(IRTest, BasicAssertions) {
    EXPECT_EQ(3, 3);
    auto a = Const{.type = Int64, .bits = 0};
    showln(a);
    bool dense_knownStride_data[] = {true, true, false, true};
    Int stride_data[] = {1, 4};
    Matrix<bool, 2, 0> dense_knownStride(dense_knownStride_data, 2);
    auto array = Array{.dense_knownStride = dense_knownStride,
                       .stride = Vector<Int, 0>(stride_data, 2)};
}
