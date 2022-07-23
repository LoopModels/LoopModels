#include "../include/Simplex.hpp"
#include "Math.hpp"
#include "MatrixStringParse.hpp"
#include <gtest/gtest.h>

TEST(SimplexTest, BasicAssertions){
    IntMatrix A{stringToIntMatrix("[10 3 2 1; 15 2 5 3]")};
    IntMatrix B{0,4};
    llvm::Optional<Simplex> optS{Simplex::positiveVariables(A, B)};
    EXPECT_TRUE(optS.hasValue());
    Simplex &S{optS.getValue()};
    llvm::MutableArrayRef<int64_t> C{S.getCost()};
    C[0] = 0;
    C[1] = 0;
    C[2] = 0;
    C[3] = -2;
    C[4] = -3;
    C[5] = -4;
    std::cout << "S.tableau = \n" << S.tableau << std::endl;
    EXPECT_EQ(S.run(), 20);
}
