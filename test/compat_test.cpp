#include "../include/Loops.hpp"
#include "../include/Macro.hpp"
#include "../include/Math.hpp"
#include "../include/MatrixStringParse.hpp"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>

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
    // swap and eliminate
    //
    // M >= 0
    // -1 + M - m >= 0
    // m >= 0
    // -2 + M -m >= 0
    // 1 + m >= 0
    auto A{stringToIntMatrix("[0 1 0; -1 1 -1; 0 0 1; -2 1 -1; 1 0 1]")};

    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty());

    llvm::Value *M = builder.CreateAlignedLoad(builder.getInt64Ty(), ptr,llvm::MaybeAlign(8));

    llvm::SmallVector<llvm::Value*> symbols{
        M};
    AffineLoopNest aff{A, symbols};
    aff.pruneBounds();
    llvm::errs() << aff << "\n";
    SHOWLN(aff.A);
    // M >= 0 is redundant
    // because M -1 >= m >= 0
    // hence, we should be left with 2 bounds
    EXPECT_EQ(aff.A.numRow(), 2);
    EXPECT_EQ(aff.A, stringToIntMatrix("[0 0 1; -2 1 -1]"));
}

TEST(TrivialPruneBounds2, BasicAssertions) {
    auto A{stringToIntMatrix(
        "[-1 0 0 0 1 0; -1 1 0 0 0 0; -1 0 1 0 -1 0; -1 0 1 0 0 0]")};
    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty());
    llvm::ConstantInt *One = builder.getInt64(1);
    
    llvm::Value *M = builder.CreateAlignedLoad(builder.getInt64Ty(), ptr,llvm::MaybeAlign(8));
    llvm::Value *N = builder.CreateAlignedLoad(builder.getInt64Ty(), builder.CreateGEP(builder.getInt64Ty(),ptr, llvm::SmallVector<llvm::Value *, 1>{One}),llvm::MaybeAlign(8));
    llvm::SmallVector<llvm::Value*> symbols{
	M, N};
    AffineLoopNest aff{A, symbols};
    aff.pruneBounds();
    aff.dump();
    SHOWLN(aff.A);
    EXPECT_EQ(aff.A.numRow(), 3);
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

    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty());
    
    llvm::Value *M = builder.CreateAlignedLoad(builder.getInt64Ty(), ptr,llvm::MaybeAlign(8));
    llvm::Value *N = builder.CreateAlignedLoad(builder.getInt64Ty(), builder.CreateGEP(builder.getInt64Ty(),ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(1)}),llvm::MaybeAlign(8));
    llvm::Value *K = builder.CreateAlignedLoad(builder.getInt64Ty(), builder.CreateGEP(builder.getInt64Ty(),ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(2)}),llvm::MaybeAlign(8));
    llvm::SmallVector<llvm::Value*> symbols{
	M, N,K};

    AffineLoopNest aff{A, symbols};
    aff.pruneBounds();
    llvm::errs() << "LessTrival test Bounds pruned:\n";
    aff.dump();
    SHOWLN(aff.A);
    EXPECT_EQ(aff.A.numRow(), 6);
    auto loop2Count = aff.countSigns(aff.A, 2 + aff.getNumSymbols());
    EXPECT_EQ(loop2Count.first, 1);
    EXPECT_EQ(loop2Count.second, 1);
    aff.removeLoopBang(2);
    auto loop1Count = aff.countSigns(aff.A, 1 + aff.getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    aff.removeLoopBang(1);
    auto loop0Count = aff.countSigns(aff.A, 0 + aff.getNumSymbols());
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

    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty());
    
    llvm::Value *M = builder.CreateAlignedLoad(builder.getInt64Ty(), ptr,llvm::MaybeAlign(8));
    llvm::Value *N = builder.CreateAlignedLoad(builder.getInt64Ty(), builder.CreateGEP(builder.getInt64Ty(),ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(1)}),llvm::MaybeAlign(8));
    llvm::SmallVector<llvm::Value*> symbols{
	M, N};

    llvm::errs() << "About to construct affine obj\n";

    AffineLoopNest aff{A, symbols};
    llvm::errs() << "Constructed affine obj\n";
    llvm::errs() << "About to run first compat test\n";
    llvm::errs() << "aff.A.size() = (" << aff.A.numRow() << ", "
              << aff.A.numCol() << ")\n";
    EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(0, false));
    EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(0, true));
    EXPECT_TRUE(aff.zeroExtraIterationsUponExtending(1, false));
    llvm::errs() << "About to run second compat test\n";
    EXPECT_FALSE(aff.zeroExtraIterationsUponExtending(1, true));
    aff.dump();
    llvm::errs() << "About to run first set of bounds tests\n";
    llvm::errs() << "\nPermuting loops 1 and 2\n";
    auto affp021{aff.rotate(stringToIntMatrix("[1 0 0; 0 0 1; 0 1 0]"))};
    // Now that we've swapped loops 1 and 2, we should have
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    affp021.dump();
    // For reference, the permuted loop bounds are:
    // for m in 0:M-1, k in 1:N-1, n in 0:k-1
    llvm::errs() << "Checking if the inner most loop iterates when adjusting "
                 "outer loops:"
              << "\n";
    llvm::errs() << "Constructed affine obj\n";
    llvm::errs() << "About to run first compat test\n";
    EXPECT_FALSE(affp021.zeroExtraIterationsUponExtending(1, false));
    llvm::errs() << "About to run second compat test\n";
    EXPECT_TRUE(affp021.zeroExtraIterationsUponExtending(1, true));

    // affp021.zeroExtraIterationsUponExtending(poset, 1, )
}
TEST(NonUnimodularExperiment, BasicAssertions) {
    llvm::errs() << "Starting affine test 1\n";
    IntMatrix A{stringToIntMatrix("[0 2 1 -1; "
                                  "-2 0 -1 1; "
                                  "0 2 1 1; "
                                  "-2 0 -1 -1; "
                                  " 0 1 0 0]")};
    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty());
    
    llvm::Value *M = builder.CreateAlignedLoad(builder.getInt64Ty(), ptr,llvm::MaybeAlign(8));
    llvm::SmallVector<llvm::Value*> symbols{
	M};
    AffineLoopNest aff{A, symbols};
    llvm::errs() << "Original order:\n";
    aff.dump();

    auto affp10{aff.rotate(stringToIntMatrix("[0 1; 1 0]"))};
    llvm::errs() << "Swapped order:\n";
    affp10.dump();

    EXPECT_FALSE(affp10.isEmpty());
}
