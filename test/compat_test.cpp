#include "../include/Loops.hpp"
#include "../include/Macro.hpp"
#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>

TEST(TrivialPruneBounds, BasicAssertions) {
    // A(5, 3) [1, M, m] constants, symbolic vars, loop vars
    //[0 1 0;
    //  -1 1 -1;
    //  0 0 1;
    //  -2 1 -1;
    // 1 0 1;]
    //  query = [1 0 0];
    // Constraints: {
    //  0 <= M; (0)
    //  -1 + M - m >= 0; (1)
    //  m >= 0; (2)
    //-2 + M - m >= 0 (3)
    // 1 + m >= 0;(4)
    //  diff = (3) - （4）
    // }
    // Our test: whether we could erase (1) or (3). query = (1) - (3)
    // auto affp{AffineLoopNest::construct(A)};
    // swap and eliminate
    //
    // M >= 0
    // -1 + M - m >= 0
    // m >= 0
    // -2 + M -m >= 0
    // 1 + m >= 0
    auto A{stringToIntMatrix("[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]")};
    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1})};
    auto affp{AffineLoopNest::construct(A, symbols)};
    affp->pruneBounds();
    llvm::errs() << *affp << "\n";
    SHOWLN(affp->A);
    // M >= 0 is redundant
    // because M -1 >= m >= 0
    // hence, we should be left with 2 bounds
    EXPECT_EQ(affp->A.numRow(), 2);
    EXPECT_EQ(affp->A, stringToIntMatrix("[0 0 1; -2 1 -1]"));
}

TEST(TrivialPruneBounds2, BasicAssertions) {
    auto A{stringToIntMatrix(
        "[-1 0 0 0 1 0; -1 1 0 0 0 0; -1 0 1 0 -1 0; -1 0 1 0 0 0]")};
    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1}),
        Polynomial::Monomial(Polynomial::ID{2})};
    auto affp{AffineLoopNest::construct(A, symbols)};
    affp->pruneBounds();
    affp->dump();
    SHOWLN(affp->A);
    EXPECT_EQ(affp->A.numRow(), 3);
}
TEST(LessTrivialPruneBounds, BasicAssertions) {

    // Ax * b >= 0
    IntMatrix A{stringToIntMatrix("[-3 1 1 1 -1 -1 -1; "
                                  "0 0 0 0 1 1 1; "
                                  "-2 1 0 1 -1 0 -1; "
                                  "0 0 0 0 1 0 1; "
                                  "0 0 0 0 0 1 0; "
                                  "-1 0 1 0 0 -1 0; "
                                  "-1 1 0 0 -1 0 0; "
                                  "0 0 0 0 1 0 0; "
                                  "0 0 0 0 0 0 1; "
                                  "-1 0 0 1 0 0 -1]")};
    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1}),
        Polynomial::Monomial(Polynomial::ID{2}),
        Polynomial::Monomial(Polynomial::ID{3})};
    llvm::IntrusiveRefCntPtr<AffineLoopNest> affp{
        AffineLoopNest::construct(A, symbols)};
    affp->pruneBounds();
    llvm::errs() << "LessTrival test Bounds pruned:\n";
    affp->dump();
    SHOWLN(affp->A);
    EXPECT_EQ(affp->A.numRow(), 6);
    auto loop2Count = affp->countSigns(affp->A, 2 + affp->getNumSymbols());
    EXPECT_EQ(loop2Count.first, 1);
    EXPECT_EQ(loop2Count.second, 1);
    affp->removeLoopBang(2);
    auto loop1Count = affp->countSigns(affp->A, 1 + affp->getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    affp->removeLoopBang(1);
    auto loop0Count = affp->countSigns(affp->A, 0 + affp->getNumSymbols());
    EXPECT_EQ(loop0Count.first, 1);
    EXPECT_EQ(loop0Count.second, 1);
}

TEST(AffineTest0, BasicAssertions) {
    llvm::errs() << "Starting affine test 0\n";
    // the loop is
    // for m in 0:M-1, n in 0:N-1, k in n+1:N-1
    //
    IntMatrix A{stringToIntMatrix("[-1 1 0 -1 0 0; "
                                  "0 0 0 1 0 0; "
                                  "-1 0 1 0 -1 0; "
                                  "0 0 0 0 1 0; "
                                  "-1 0 1 0 0 -1; "
                                  "-1 0 0 0 -1 1; "
                                  "0 1 0 0 0 0; "
                                  "0 0 1 0 0 0]")};
    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1}),
        Polynomial::Monomial(Polynomial::ID{2})};

    llvm::errs() << "About to construct affine obj\n";

    auto affp{AffineLoopNest::construct(A, symbols)};
    llvm::errs() << "Constructed affine obj\n";
    llvm::errs() << "About to run first compat test\n";
    llvm::errs() << "affp->A.size() = (" << affp->A.numRow() << ", "
              << affp->A.numCol() << ")\n";
    EXPECT_FALSE(affp->zeroExtraIterationsUponExtending(0, false));
    EXPECT_FALSE(affp->zeroExtraIterationsUponExtending(0, true));
    EXPECT_TRUE(affp->zeroExtraIterationsUponExtending(1, false));
    llvm::errs() << "About to run second compat test\n";
    EXPECT_FALSE(affp->zeroExtraIterationsUponExtending(1, true));
    affp->dump();
    llvm::errs() << "About to run first set of bounds tests\n";
    // { // lower bound tests
    //     EXPECT_EQ(affp.lowerA.size(), 3);
    //     EXPECT_EQ(affp.lowerA[0].numRow(), 1);
    //     EXPECT_EQ(affp.lowerA[1].numRow(), 1);
    //     EXPECT_EQ(affp.lowerA[2].numRow(), 1);
    //     EXPECT_TRUE(affp.C.equal(affp.getSymbol(affp.lowerA[0], 0)));
    //     EXPECT_TRUE(affp.C.equal(affp.getSymbol(affp.lowerA[1], 0)));
    //     //                              0  M  N  m  n   k
    //     llvm::SmallVector<int64_t, 6> a{-1, 0, 0, 0, 1, -1};
    //     EXPECT_TRUE(affp.lowerA[2].getRow(0) == a);
    // }
    // { // upper bound tests
    //     EXPECT_EQ(affp.upperA.size(), 3);
    //     EXPECT_EQ(affp.upperA[0].numRow(), 1);
    //     EXPECT_EQ(affp.upperA[1].numRow(), 1);
    //     EXPECT_EQ(affp.upperA[2].numRow(), 1);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a0{-1, 1, 0, 0, 0, 0};
    //     EXPECT_TRUE(affp.upperA[0].getRow(0) == a0);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a1{-2, 0, 1, 0, 0, 0};
    //     EXPECT_TRUE(affp.upperA[1].getRow(0) == a1);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a2{-1, 0, 1, 0, 0, 0};
    //     EXPECT_TRUE(affp.upperA[2].getRow(0) == a2);
    // }
    llvm::errs() << "\nPermuting loops 1 and 2\n";
    auto affp021{affp->rotate(stringToIntMatrix("[1 0 0; 0 0 1; 0 1 0]"))};
    // Now that we've swapped loops 1 and 2, we should have
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    affp021->dump();
    // llvm::errs() << "First lc: \n";
    // affp021.lc[0][0].dump();
    // { // lower bound tests
    //     EXPECT_EQ(affp021.lowerA.size(), 3);
    //     EXPECT_EQ(affp021.lowerA[0].numRow(), 1);
    //     EXPECT_EQ(affp021.lowerA[1].numRow(), 1);
    //     EXPECT_EQ(affp021.lowerA[2].numRow(), 1);
    //     EXPECT_TRUE(allZero(affp021.lowerA[0].getRow(0)));
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a1{-1, 0, 0, 0, 0, 0};
    //     EXPECT_TRUE(affp021.lowerA[2].getRow(0) == a1);
    //     EXPECT_TRUE(allZero(affp021.lowerA[1].getRow(0)));
    // }
    // { // upper bound tests
    //     EXPECT_EQ(affp021.upperA.size(), 3);
    //     EXPECT_EQ(affp021.upperA[0].numRow(), 1);
    //     EXPECT_EQ(affp021.upperA[1].numRow(), 1);
    //     EXPECT_EQ(affp021.upperA[2].numRow(), 1);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a0{-1, 1, 0, 0, 0, 0};
    //     EXPECT_TRUE(affp021.upperA[0].getRow(0) == a0);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a1{-1, 0, 1, 0, 0, 0};
    //     EXPECT_TRUE(affp021.upperA[2].getRow(0) == a1);
    //     //                               0  M  N  m  n  k
    //     llvm::SmallVector<int64_t, 6> a2{-1, 0, 0, 0, 1, -1};
    //     EXPECT_TRUE(affp021.upperA[1].getRow(0) == a2);
    // }

    /*
    llvm::errs() << "\nExtrema of loops:\n";
    for (size_t i = 0; i < affp021.getNumLoops(); ++i) {
        auto lbs = aff->lExtrema[i];
        auto ubs = aff->uExtrema[i];
        llvm::errs() << "Loop " << i << " lower bounds: \n";
        for (auto &b : lbs) {
            auto lb = b;
            lb *= -1;
            lb.dump();
        }
        llvm::errs() << "Loop " << i << " upper bounds: \n";
        for (auto &b : ubs) {
            b.dump();
        }
    }
    */
    // For reference, the permuted loop bounds are:
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    llvm::errs() << "Checking if the inner most loop iterates when adjusting "
                 "outer loops:"
              << "\n";
    llvm::errs() << "Constructed affine obj\n";
    llvm::errs() << "About to run first compat test\n";
    EXPECT_FALSE(affp021->zeroExtraIterationsUponExtending(1, false));
    llvm::errs() << "About to run second compat test\n";
    EXPECT_TRUE(affp021->zeroExtraIterationsUponExtending(1, true));

    // affp021.zeroExtraIterationsUponExtending(poset, 1, )
}
TEST(NonUnimodularExperiment, BasicAssertions) {
    llvm::errs() << "Starting affine test 1\n";
    IntMatrix A{stringToIntMatrix("[0 2 1 -1; "
                                  "-2 0 -1 1; "
                                  "0 2 1 1; "
                                  "-2 0 -1 -1; "
                                  " 0 1 0 0]")};
    llvm::SmallVector<Polynomial::Monomial> symbols{
        Polynomial::Monomial(Polynomial::ID{1})};
    auto affp{AffineLoopNest::construct(A, symbols)};
    llvm::errs() << "Original order:\n";
    affp->dump();

    auto affp10{affp->rotate(stringToIntMatrix("[0 1; 1 0]"))};
    llvm::errs() << "Swapped order:\n";
    affp10->dump();

    EXPECT_FALSE(affp10->isEmpty());
}
