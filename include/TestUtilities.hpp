#pragma once
#include "Loops.hpp"
#include "Math.hpp"
#include <cstdint>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

struct TestLoopFunction {
    llvm::LLVMContext ctx;
    llvm::IRBuilder<> builder;
    llvm::FastMathFlags fmf;
    llvm::Module Mod;
    llvm::LoopInfo LI{};
    llvm::DominatorTree DT{};
    llvm::FunctionType *FT;
    llvm::Function *F;
    llvm::DataLayout dl;
    llvm::TargetTransformInfo TTI;
    llvm::Triple targetTripple;
    llvm::TargetLibraryInfoImpl TLII;
    llvm::TargetLibraryInfo TLI;
    llvm::AssumptionCache AC;
    llvm::ScalarEvolution SE;
    AffineLoopNest aln;
    TestLoopFunction(IntMatrix A, size_t numLoops)
        : ctx{llvm::LLVMContext()}, builder{llvm::IRBuilder(ctx)},
          fmf{llvm::FastMathFlags()}, Mod("TestModule", ctx), LI{}, DT{},
          FT{llvm::FunctionType::get(builder.getVoidTy(),
                                     llvm::SmallVector<llvm::Type *, 0>(),
                                     false)},
          F{llvm::Function::Create(
              FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "foo",
              Mod)},
          dl{"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
             "n8:16:32:64-S128"},
          TTI{dl}, targetTripple{"x86_64-redhat-linux"}, TLII{targetTripple},
          TLI{TLII}, AC{*F, &TTI}, SE{*F, TLI, AC, DT, LI}, aln{std::move(A)} {

        fmf.set();
        builder.setFastMathFlags(fmf);
        auto Int64 = builder.getInt64Ty();
        llvm::Value *ptr =
            builder.CreateIntToPtr(builder.getInt64(16000), Int64);

        size_t numSym = aln.A.numCol() - numLoops - 1;
        llvm::SmallVector<llvm::Value *> &symbols = aln.symbols;
        symbols.reserve(numSym);
        if (numSym) {
            symbols.push_back(
                builder.CreateAlignedLoad(Int64, ptr, llvm::MaybeAlign(8)));
            for (size_t i = 1; i < numSym; ++i) {
                symbols.push_back(builder.CreateAlignedLoad(
                    Int64,
                    builder.CreateGEP(Int64, ptr,
                                      llvm::SmallVector<llvm::Value *, 1>{
                                          builder.getInt64(i)}),
                    llvm::MaybeAlign(8)));
            }
        }
    }
};
