#pragma once
#include "./Loops.hpp"
#include "Utilities/Allocators.hpp"
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Casting.h>
#include <string>

class TestLoopFunction {
  BumpAlloc<> alloc;
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
  llvm::SmallVector<AffineLoopNest<true>, 0> alns;
  llvm::SmallVector<std::string, 0> names;
  // llvm::SmallVector<llvm::Value*> symbols;
  llvm::Value *ptrToLoadFrom;
  llvm::SmallPtrSet<llvm::Value *, 32> symsToDelete;
  size_t ptrIntOffset{0};

public:
  auto getAlloc() -> BumpAlloc<> & { return alloc; }
  auto getLoopNest(size_t i) -> AffineLoopNest<true> * { return &alns[i]; }
  auto getNumLoopNests() -> size_t { return alns.size(); }
  void addLoop(IntMatrix A, size_t numLoops) {
    size_t numSym = size_t(A.numCol()) - numLoops - 1;
    llvm::SmallVector<const llvm::SCEV *> symbols;
    symbols.reserve(numSym);
    if (numSym) {
      // we're going to assume there's some chance of recycling old
      // symbols, so we are only going to be creating new ones if we have
      // to.
      AffineLoopNest<true> *symbolSource = nullptr;
      size_t numSymbolSource = 0;
      for (auto &aln : alns) {
        if (numSymbolSource < aln.getSyms().size()) {
          numSymbolSource = aln.getSyms().size();
          symbolSource = &aln;
        }
      }
      for (size_t i = 0; i < std::min(numSym, numSymbolSource); ++i)
        symbols.push_back(symbolSource->getSyms()[i]);
      for (size_t i = numSymbolSource; i < numSym; ++i)
        symbols.push_back(SE.getUnknown(createInt64()));
    }
    alns.emplace_back(std::move(A), std::move(symbols));
  }
  // for creating some black box value
  auto loadValueFromPtr(llvm::Type *typ) -> llvm::Value * {
    names.emplace_back("value_" + std::to_string(names.size()));
    return builder.CreateAlignedLoad(
      typ,
      builder.CreateGEP(
        builder.getInt64Ty(), ptrToLoadFrom,
        llvm::SmallVector<llvm::Value *, 1>{builder.getInt64(ptrIntOffset++)}),
      llvm::MaybeAlign(8), names.back());
  }
  auto createArray() -> llvm::Value * {
    return loadValueFromPtr(builder.getPtrTy());
  }
  auto createInt64() -> llvm::Value * {
    return loadValueFromPtr(builder.getInt64Ty());
  }
  TestLoopFunction()
    : ctx{llvm::LLVMContext()}, builder{llvm::IRBuilder(ctx)},
      fmf{llvm::FastMathFlags()}, mod("TestModule", ctx), LI{}, DT{},
      FT{llvm::FunctionType::get(builder.getVoidTy(),
                                 llvm::SmallVector<llvm::Type *, 0>(), false)},
      F{llvm::Function::Create(
        FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "foo", mod)},
      dl{&mod}, TTI{dl}, targetTripple{}, TLII{targetTripple}, TLI{TLII},
      AC{*F, &TTI}, SE{*F, TLI, AC, DT, LI}, alns{},
      ptrToLoadFrom{
        builder.CreateIntToPtr(builder.getInt64(16000), builder.getInt64Ty())} {

    fmf.set();
    builder.setFastMathFlags(fmf);
  }
  auto getSCEVUnknown(llvm::Value *v) -> const llvm::SCEVUnknown * {
    return llvm::dyn_cast<llvm::SCEVUnknown>(SE.getUnknown(v));
  }
  ~TestLoopFunction() = default;
  // ~TestLoopFunction() {
  //   for (auto s : symsToDelete) s->deleteValue();
  // }
  auto CreateLoad(llvm::Value *ptr, llvm::Value *offset) -> llvm::LoadInst * {
    llvm::Type *Float64 = builder.getDoubleTy();
    auto load_m = builder.CreateAlignedLoad(
      Float64,
      builder.CreateGEP(Float64, ptr,
                        llvm::SmallVector<llvm::Value *, 1>{offset}),
      llvm::MaybeAlign(8));
    symsToDelete.insert(load_m);
    return load_m;
  }
  auto CreateStore(llvm::Value *val, llvm::Value *ptr, llvm::Value *offset)
    -> llvm::StoreInst * {
    llvm::Type *Float64 = builder.getDoubleTy();
    auto store_m = builder.CreateAlignedStore(
      val,
      builder.CreateGEP(Float64, ptr,
                        llvm::SmallVector<llvm::Value *, 1>{offset}),
      llvm::MaybeAlign(8));
    symsToDelete.insert(store_m);
    return store_m;
  }
  auto getZeroF64() -> llvm::Value * {
    auto z = llvm::ConstantFP::getZero(builder.getDoubleTy());
    symsToDelete.insert(z);
    return z;
  }
  auto CreateUIToF64(llvm::Value *v) -> llvm::Value * {
    auto uitofp = builder.CreateUIToFP(v, builder.getDoubleTy());
    symsToDelete.insert(uitofp);
    return uitofp;
  }
  auto CreateFAdd(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
    auto fadd = builder.CreateFAdd(lhs, rhs);
    symsToDelete.insert(fadd);
    return fadd;
  }
  auto CreateFSub(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
    auto fsub = builder.CreateFSub(lhs, rhs);
    symsToDelete.insert(fsub);
    return fsub;
  }
  auto CreateFMul(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
    auto fmul = builder.CreateFMul(lhs, rhs);
    symsToDelete.insert(fmul);
    return fmul;
  }
  auto CreateFDiv(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
    auto fdiv = builder.CreateFDiv(lhs, rhs);
    symsToDelete.insert(fdiv);
    return fdiv;
  }
  auto CreateFDiv(llvm::Value *lhs, llvm::Value *rhs, const char *s)
    -> llvm::Value * {
    auto fdiv = builder.CreateFDiv(lhs, rhs, s);
    symsToDelete.insert(fdiv);
    return fdiv;
  }
  auto CreateSqrt(llvm::Value *v) -> llvm::Value * {
    llvm::Type *Float64 = builder.getDoubleTy();
    llvm::Function *sqrt =
      llvm::Intrinsic::getDeclaration(&mod, llvm::Intrinsic::sqrt, Float64);
    llvm::FunctionType *sqrtTyp =
      llvm::Intrinsic::getType(ctx, llvm::Intrinsic::sqrt, {Float64});
    auto sqrtCall = builder.CreateCall(sqrtTyp, sqrt, {v});
    // auto sqrtCall = builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, v);
    symsToDelete.insert(sqrtCall);
    return sqrtCall;
  }
};
