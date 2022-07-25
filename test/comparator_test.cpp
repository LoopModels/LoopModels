#include <iostream>
#include <gtest/gtest.h>
#include <memory>
#include "Comparators.hpp"
#include "MatrixStringParse.hpp"
#include "llvm/ADT/SmallVector.h"

TEST(BasicCompare, BasicAssertions) {
    IntMatrix A = stringToIntMatrix("[0 -1 0 1 0 0; 0 0 -1 1 0 0; 0 0 0 1 -1 0; 0 0 0 1 0 -1]");
    auto comp = LinearSymbolicComparator::construct(std::move(A));
    llvm::SmallVector<int64_t, 16> query{-1, 0, 0, 1, 0};
    EXPECT_TRUE(comp.greaterEqualZero(query));
}