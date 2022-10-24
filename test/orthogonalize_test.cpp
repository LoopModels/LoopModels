#include "../include/Math.hpp"
#include "../include/Orthogonalize.hpp"
#include "../include/Loops.hpp"
#include "../include/MatrixStringParse.hpp"
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Triple.h>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <random>

[[maybe_unused]] static llvm::Optional<std::pair<AffineLoopNest,llvm::SmallVector<ArrayReference,0>>>
orthogonalize(llvm::SmallVectorImpl<ArrayReference *> const &ai) {
    // need to construct matrix `A` of relationship
    // B*L = I
    // where L are the loop induct variables, and I are the array indices
    // e.g., if we have `C[i + j, j]`, then
    // B = [1 1; 0 1]
    // additionally, the loop is defined by the bounds
    // A*L = A*(B\^-1 * I) <= r
    // assuming that `B` is an invertible integer matrix (i.e. is unimodular),
    const AffineLoopNest &alnp = *(ai[0]->loop);
    const size_t numLoops = alnp.getNumLoops();
    const size_t numSymbols = alnp.getNumSymbols();
    size_t numRow = 0;
    for (auto a : ai)
        numRow += a->arrayDim();
    IntMatrix S(numLoops, numRow);
    size_t i = 0;
    for (auto a : ai) {
        PtrMatrix<int64_t> A = a->indexMatrix();
        for (size_t j = 0; j < numLoops; ++j)
            for (size_t k = 0; k < A.numCol(); ++k)
                S(j, k + i) = A(j, k);
        i += A.numCol();
    }
    auto [K, included] = NormalForm::orthogonalize(S);
    if (!included.size())
        return {};
    // We let
    // L = K'*J
    // Originally, the loop bounds were
    // A*L <= b
    // now, we have (A = alnp.aln->A, r = alnp.aln->r)
    // (A*K')*J <= r
    IntMatrix AK{alnp.A};
    AK(_, _(numSymbols, end)) = alnp.A(_, _(numSymbols, end)) * K.transpose();
    SHOWLN(alnp.A(_, _(numSymbols, end)));
    SHOWLN(AK(_, _(numSymbols, end)));
    AffineLoopNest alnNew{std::move(AK), alnp.symbols};
    alnNew.pruneBounds();
    IntMatrix KS{K * S};
    llvm::SmallVector<ArrayReference, 0> newArrayRefs;
    std::pair<AffineLoopNest,llvm::SmallVector<ArrayReference,0>> ret{std::make_pair(std::move(alnNew), newArrayRefs)};
    newArrayRefs.reserve(numRow);
    i = 0;
    for (auto a : ai) {
	newArrayRefs.emplace_back(*a, &ret.first, KS(_,_(i,i+a->arrayDim())));
	i += a->arrayDim();
    }
    return ret;
}

TEST(OrthogonalizeTest, BasicAssertions) {
    // for m = 0:M-1, n = 0:N-1, i = 0:I-1, j = 0:J-1
    //   W[m + i, n + j] += C[i,j] * B[m,n]
    //
    // Loops: m, n, i, j
    IntMatrix A{stringToIntMatrix("[-1 1 0 0 0 -1 0 0 0; "
                                  "0 0 0 0 0 1 0 0 0; "
                                  "-1 0 1 0 0 0 -1 0 0; "
                                  "0 0 0 0 0 0 1 0 0; "
                                  "-1 0 0 1 0 0 0 -1 0; "
                                  "0 0 0 0 0 0 0 1 0; "
                                  "-1 0 0 0 1 0 0 0 -1; "
                                  "0 0 0 0 0 0 0 0 1]")};

    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    auto Int64 = builder.getInt64Ty();
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), Int64);
    
    llvm::Value *M = builder.CreateAlignedLoad(Int64, ptr,llvm::MaybeAlign(8));
    llvm::Value *N = builder.CreateAlignedLoad(Int64, builder.CreateGEP(Int64,ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(1)}),llvm::MaybeAlign(8));
    llvm::Value *I = builder.CreateAlignedLoad(Int64, builder.CreateGEP(Int64,ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(2)}),llvm::MaybeAlign(8));
    llvm::Value *J = builder.CreateAlignedLoad(Int64, builder.CreateGEP(Int64,ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(3)}),llvm::MaybeAlign(8));
    llvm::SmallVector<llvm::Value*> symbols{
	M, N,I,J};
    AffineLoopNest aln{A, symbols};
    EXPECT_FALSE(aln.isEmpty());

    // create a ScalarEvolution so we can build SCEVs
    llvm::LoopInfo LI;
    llvm::DominatorTree DT;
    llvm::FunctionType *FT{llvm::FunctionType::get(builder.getVoidTy(), llvm::SmallVector<llvm::Type*,0>(), false)};
    llvm::Function *F{llvm::Function::Create(FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage )};
    llvm::DataLayout dl("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
                        "n8:16:32:64-S128");
    llvm::TargetTransformInfo TTI{dl};

    llvm::Triple targetTripple("x86_64-redhat-linux");
    llvm::TargetLibraryInfoImpl TLII(targetTripple);
    llvm::TargetLibraryInfo TLI(TLII);
    llvm::AssumptionCache AC(*F, &TTI);
    llvm::ScalarEvolution SE(*F, TLI, AC, DT, LI);
    
    // we have three array refs
    // W[i+m, j+n]
    // llvm::SmallVector<std::pair<MPoly,MPoly>>
    ArrayReference War{0, &aln, 2};
    {
        MutPtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(2, 0) = 1; // i
        IndMat(1, 1) = 1; // n
        IndMat(3, 1) = 1; // j
	// I + M -1
        War.sizes[0] = SE.getAddExpr(SE.getSCEV(N),SE.getAddExpr(SE.getSCEV(J),SE.getMinusOne(Int64)));
        War.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "War = " << War << "\n";

    // B[i, j]
    ArrayReference Bar{1, &aln, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(2, 0) = 1; // i
        IndMat(3, 1) = 1; // j
        Bar.sizes[0] = SE.getSCEV(J);
        Bar.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "Bar = " << Bar << "\n";

    // C[m, n]
    ArrayReference Car{2, &aln, 2};
    {
        MutPtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(0, 0) = 1; // m
        IndMat(1, 1) = 1; // n
        Car.sizes[0] = SE.getSCEV(N);
        Car.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "Car = " << Car << "\n";

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<std::pair<AffineLoopNest,llvm::SmallVector<ArrayReference, 0>>> orth(
        orthogonalize(ai));

    EXPECT_TRUE(orth.hasValue());
    AffineLoopNest& newAln = orth->first;
    llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth->second;
    for (auto &ar : newArrayRefs) {
        EXPECT_EQ(&newAln, ar.loop);
    }
    EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 0)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 1)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 0)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 1)), 1);
    EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 0)), 2);
    EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 1)), 2);
    llvm::errs() << "A=" << newAln.A << "\n";
    // llvm::errs() << "b=" << PtrVector<MPoly>(newAln.aln->b);
    llvm::errs() << "Skewed loop nest:\n" << newAln << "\n";
    auto loop3Count =
        newAln.countSigns(newAln.A, 3 + newAln.getNumSymbols());
    EXPECT_EQ(loop3Count.first, 2);
    EXPECT_EQ(loop3Count.second, 2);
    newAln.removeLoopBang(3);
    auto loop2Count =
        newAln.countSigns(newAln.A, 2 + newAln.getNumSymbols());
    EXPECT_EQ(loop2Count.first, 2);
    EXPECT_EQ(loop2Count.second, 2);
    newAln.removeLoopBang(2);
    auto loop1Count =
        newAln.countSigns(newAln.A, 1 + newAln.getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    newAln.removeLoopBang(1);
    auto loop0Count =
        newAln.countSigns(newAln.A, 0 + newAln.getNumSymbols());
    EXPECT_EQ(loop0Count.first, 1);
    EXPECT_EQ(loop0Count.second, 1);
    llvm::errs() << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        llvm::errs() << ar << "\n" << "\n";
    }
}

TEST(BadMul, BasicAssertions) {
    llvm::LLVMContext ctx = llvm::LLVMContext();
    llvm::IRBuilder<> builder = llvm::IRBuilder(ctx);
    auto fmf = llvm::FastMathFlags();
    fmf.set();
    builder.setFastMathFlags(fmf);
    auto Int64 = builder.getInt64Ty();
    llvm::Value *ptr =
        builder.CreateIntToPtr(builder.getInt64(16000), Int64);
    
    llvm::Value *M = builder.CreateAlignedLoad(Int64, ptr,llvm::MaybeAlign(8));
    llvm::Value *N = builder.CreateAlignedLoad(Int64, builder.CreateGEP(Int64,ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(1)}),llvm::MaybeAlign(8));
    llvm::Value *K = builder.CreateAlignedLoad(Int64, builder.CreateGEP(Int64,ptr, llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(2)}),llvm::MaybeAlign(8));

    // create a ScalarEvolution so we can build SCEVs
    llvm::LoopInfo LI;
    llvm::DominatorTree DT;
    llvm::FunctionType *FT{llvm::FunctionType::get(builder.getVoidTy(), llvm::SmallVector<llvm::Type*,0>(), false)};
    llvm::Function *F{llvm::Function::Create(FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage )};
    llvm::DataLayout dl("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
                        "n8:16:32:64-S128");
    llvm::TargetTransformInfo TTI{dl};

    llvm::Triple targetTripple("x86_64-redhat-linux");
    llvm::TargetLibraryInfoImpl TLII(targetTripple);
    llvm::TargetLibraryInfo TLI(TLII);
    llvm::AssumptionCache AC(*F, &TTI);
    llvm::ScalarEvolution SE(*F, TLI, AC, DT, LI);

    
    llvm::SmallVector<llvm::Value*> symbols{M, N, K};
    IntMatrix A{stringToIntMatrix("[-3 1 1 1 -1 0 0; "
				  "0 0 0 0 1 0 0; "
				  "-2 1 0 1 0 -1 0; "
				  "0 0 0 0 0 1 0; "
				  "0 0 0 0 1 -1 0; "
				  "-1 0 1 0 -1 1 0; "
				  "-1 1 0 0 0 0 -1; "
				  "0 0 0 0 0 0 1; "
				  "0 0 0 0 0 1 -1; "
				  "-1 0 0 1 0 -1 1]")};
    // auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
    // auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
    // for i in 0:M+N+K-3, l in max(0,i+1-N):min(M+K-2,i), j in
    // max(0,l+1-K):min(M-1,l)
    //       W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    //
    // Loops: i, l, j

    AffineLoopNest aln{
        A, symbols};
    EXPECT_FALSE(aln.isEmpty());

    // for i in 0:M+N+K-3, l in max(0,i+1-N):min(M+K-2,i), j in
    // max(0,l+1-K):min(M-1,l)
    // W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    // 0, 1, 2
    // i, l, j
    // we have three array refs
    // W[j, i - l] // M x N
    const int iId = 0, lId = 1, jId = 2;
    ArrayReference War(0, aln, 2); //, axes, indTo
    {
        MutPtrMatrix<int64_t> IndMat = War.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        War.sizes[0] = SE.getSCEV(N);
        War.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "War = " << War << "\n";

    // B[j, l - j] // M x K
    ArrayReference Bar(1, aln, 2); //, axes, indTo
    {
        MutPtrMatrix<int64_t> IndMat = Bar.indexMatrix();
        IndMat(jId, 0) = 1;  // j
        IndMat(lId, 1) = 1;  // l
        IndMat(jId, 1) = -1; // j
        Bar.sizes[0] = SE.getSCEV(K);
        Bar.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "Bar = " << Bar << "\n";

    // C[l-j,i-l] // K x N
    ArrayReference Car(2, aln, 2); //, axes, indTo
    {
        MutPtrMatrix<int64_t> IndMat = Car.indexMatrix();
        IndMat(lId, 0) = 1;  // l
        IndMat(jId, 0) = -1; // j
        IndMat(iId, 1) = 1;  // i
        IndMat(lId, 1) = -1; // l
        Car.sizes[0] = SE.getSCEV(N);
        Car.sizes[1] = SE.getConstant(Int64, 8, /*isSigned=*/false);
    }
    llvm::errs() << "Car = " << Car << "\n";

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<std::pair<AffineLoopNest,llvm::SmallVector<ArrayReference, 0>>> orth(
        orthogonalize(ai));

    EXPECT_TRUE(orth.hasValue());

    AffineLoopNest &newAln = orth->first;
    llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth->second;
    
    for (auto &ar : newArrayRefs) {
        EXPECT_EQ(&newAln, ar.loop);
    }

    SHOWLN(aln.A);
    SHOWLN(newAln.A);
    // llvm::errs() << "b=" << PtrVector<MPoly>(newAln.aln->b);
    llvm::errs() << "Skewed loop nest:\n" << newAln << "\n";
    auto loop2Count =
        newAln.countSigns(newAln.A, 2 + newAln.getNumSymbols());
    EXPECT_EQ(loop2Count.first, 1);
    EXPECT_EQ(loop2Count.second, 1);
    newAln.removeLoopBang(2);
    SHOWLN(newAln.A);
    auto loop1Count =
        newAln.countSigns(newAln.A, 1 + newAln.getNumSymbols());
    EXPECT_EQ(loop1Count.first, 1);
    EXPECT_EQ(loop1Count.second, 1);
    newAln.removeLoopBang(1);
    SHOWLN(newAln.A);
    auto loop0Count =
        newAln.countSigns(newAln.A, 0 + newAln.getNumSymbols());
    EXPECT_EQ(loop0Count.first, 1);
    EXPECT_EQ(loop0Count.second, 1);

    llvm::errs() << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs)
        llvm::errs() << ar << "\n" << "\n";
}

TEST(OrthogonalizeMatricesTest, BasicAssertions) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-3, 3);

    const size_t M = 7;
    const size_t N = 7;
    IntMatrix A(M, N);
    IntMatrix B(N, N);
    const size_t iters = 1000;
    for (size_t i = 0; i < iters; ++i) {
        for (auto &&a : A)
            a = distrib(gen);
        // llvm::errs() << "Random A =\n" << A << "\n";
        A = orthogonalize(std::move(A));
        // llvm::errs() << "Orthogonal A =\n" << A << "\n";
        // note, A'A is not diagonal
        // but AA' is
        B = A * A.transpose();
        // llvm::errs() << "A'A =\n" << B << "\n";
        for (size_t m = 0; m < M; ++m)
            for (size_t n = 0; n < N; ++n)
                if (m != n){
                    EXPECT_EQ(B(m, n), 0);
		}
    }
}
