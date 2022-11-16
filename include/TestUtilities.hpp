#pragma once
#include "ArrayReference.hpp"
#include "Loops.hpp"
#include "Math.hpp"
#include <cstdint>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Casting.h>
#include <string>

struct TestLoopFunction {
    llvm::LLVMContext ctx;
    llvm::IRBuilder<> builder;
    llvm::FastMathFlags fmf;
    llvm::Module mod;
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
    llvm::SmallVector<AffineLoopNest, 0> alns;
    llvm::SmallVector<std::string, 0> names;
    // llvm::SmallVector<llvm::Value*> symbols;
    llvm::Value *ptr;
    size_t ptrIntOffset{0};

    // std::pair<ArrayReference,llvm::Value*> arrayRef(size_t loopId, ){

    // }

    void addLoop(IntMatrix A, size_t numLoops) {
        size_t numSym = A.numCol() - numLoops - 1;
        llvm::SmallVector<const llvm::SCEV *> symbols;
        symbols.reserve(numSym);
        if (numSym) {
            // we're going to assume there's some chance of recycling old
            // symbols, so we are only going to be creating new ones if we have
            // to.
            AffineLoopNest *symbolSource = nullptr;
            size_t numSymbolSource = 0;
            for (auto &aln : alns) {
                if (numSymbolSource < aln.symbols.size()) {
                    numSymbolSource = aln.symbols.size();
                    symbolSource = &aln;
                }
            }
            for (size_t i = 0; i < std::min(numSym, numSymbolSource); ++i)
                symbols.push_back(symbolSource->symbols[i]);
            for (size_t i = numSymbolSource; i < numSym; ++i)
                symbols.push_back(SE.getUnknown(createInt64()));
        }
        alns.emplace_back(std::move(A), std::move(symbols));
    }
    // for creating some black box value
    llvm::Value *loadValueFromPtr(llvm::Type *typ) {
        names.emplace_back("value_" + std::to_string(names.size()));
        return builder.CreateAlignedLoad(
            typ,
            builder.CreateGEP(builder.getInt64Ty(), ptr,
                              llvm::SmallVector<llvm::Value *, 1>{
                                  builder.getInt64(ptrIntOffset++)}),
            llvm::MaybeAlign(8), names.back());
    }
    llvm::Value *createArray() { return loadValueFromPtr(builder.getPtrTy()); }
    llvm::Value *createInt64() {
        return loadValueFromPtr(builder.getInt64Ty());
    }

    TestLoopFunction()
        : ctx{llvm::LLVMContext()}, builder{llvm::IRBuilder(ctx)},
          fmf{llvm::FastMathFlags()}, mod("TestModule", ctx), LI{}, DT{},
          FT{llvm::FunctionType::get(builder.getVoidTy(),
                                     llvm::SmallVector<llvm::Type *, 0>(),
                                     false)},
          F{llvm::Function::Create(
              FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "foo",
              mod)},
          dl{"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
             "n8:16:32:64-S128"},
          TTI{dl}, targetTripple{"x86_64-redhat-linux"}, TLII{targetTripple},
          TLI{TLII}, AC{*F, &TTI}, SE{*F, TLI, AC, DT, LI}, alns{},
          ptr{builder.CreateIntToPtr(builder.getInt64(16000),
                                     builder.getInt64Ty())} {

        fmf.set();
        builder.setFastMathFlags(fmf);
    }
    const llvm::SCEVUnknown *getSCEVUnknown(llvm::Value *v) {
        return llvm::dyn_cast<llvm::SCEVUnknown>(SE.getUnknown(v));
    }
};
