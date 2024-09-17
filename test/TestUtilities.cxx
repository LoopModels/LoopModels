#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/CodeGen/BasicTTIImpl.h>
#include <llvm/CodeGen/ISDOpcodes.h>
#include <llvm/CodeGen/TargetLowering.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <llvm/Support/TypeSize.h>
#include <optional>
#include <string>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "IR/IR.cxx"
#include "Math/Constructors.cxx"
#include "Math/ManagedArray.cxx"
#include "Optimize/Legality.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Valid.cxx"
#else
export module TestUtilities;

export import Arena;
export import ArrayConstructors;
export import IR;
export import Legality;
export import ManagedArray;
export import TargetMachine;
import Invariant;
import Valid;
#endif

using math::DenseMatrix, math::PtrMatrix, math::MutPtrMatrix, alloc::Arena,
  math::PtrVector, math::DenseDims, math::DenseDims, utils::Valid;

#ifdef USE_MODULE
export {
#endif

  class TestLoopFunction {
    llvm::LLVMContext ctx;
    llvm::Module *mod;
    llvm::LoopInfo LI;
    llvm::DominatorTree DT;
    llvm::FunctionType *FT;
    llvm::Function *F;
    llvm::DataLayout dl;
    llvm::TargetTransformInfo TTI;
    target::Machine<false> target;
    llvm::Triple targetTriple;
    llvm::TargetLibraryInfo TLI;
    llvm::AssumptionCache AC;
    llvm::ScalarEvolution SE;
    llvm::SmallVector<poly::Loop *, 0> alns;
    llvm::SmallVector<std::string, 0> names;
    llvm::BasicBlock *BB;
    llvm::IRBuilder<> builder;
    llvm::Value *ptrToLoadFrom{};
    poly::Dependencies deps;
    IR::TreeResult tr{};
    IR::Cache ir;
    ptrdiff_t numArgs{0};
    // dict::map<llvm::Value *, IR::Value *> llvmToInternalMap;
    auto createAddr(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t> indMat,
                    PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas,
                    bool isStow, poly::Loop *pl,
                    unsigned int align_shift = 3) -> IR::Addr * {
      utils::invariant(omegas.size() - 1, ptrdiff_t(indMat.numCol()));
      // TODO: poison this memory once we're done?
      math::MutPtrVector<int64_t> const_offset =
        math::vector<int64_t>(getAlloc(), ptrdiff_t(indMat.numRow()));
      const_offset << 0;
      IR::Array array = ir.push_array(ptr, sizes);
      IR::Addr *ma =
        IR::Addr::construct(getAlloc(), array, elt, indMat, 0, const_offset,
                            nullptr, ptrdiff_t(indMat.numCol()), isStow, pl);
      ma->getArray().setAlignmentShift(align_shift);
      ma->getFusionOmega() << omegas;
      tr.addAddr(ma);
      return ma;
    }
    auto createAddr(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t> indMat,
                    PtrVector<int64_t> constOffsets,
                    PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas,
                    bool isStow, poly::Loop *pl) -> IR::Addr * {
      // we do not trust the lifetime of `offMat`, so we allocate here
      // offMat is arrayDim x numDynSym
      utils::invariant(constOffsets.size() == indMat.numRow());
      IR::Array array = ir.push_array(ptr, sizes);
      IR::Addr *ma =
        IR::Addr::construct(getAlloc(), array, elt, indMat, 0, constOffsets,
                            nullptr, ptrdiff_t(indMat.numCol()), isStow, pl);
      ma->getFusionOmega() << omegas;
      tr.addAddr(ma);
      return ma;
    }

  public:
    auto getAlloc() -> alloc::Arena<> * { return ir.getAllocator(); }
    auto getIRC() -> IR::Cache & { return ir; }
    auto getTreeResult() const -> IR::TreeResult { return tr; }
    auto getLoopNest(size_t i) -> poly::Loop * { return alns[i]; }
    auto getNumLoopNests() -> size_t { return alns.size(); }
    // auto getTTI() -> llvm::TargetTransformInfo & { return TTI; }
    auto getTarget() const -> target::Machine<false> { return target; }
    auto addLoop(PtrMatrix<int64_t> A, ptrdiff_t numLoops) -> poly::Loop * {
      ptrdiff_t num_sym = ptrdiff_t(A.numCol()) - numLoops - 1;
      math::Vector<IR::Value *> symbols;
      symbols.reserve(num_sym);
      if (num_sym) {
        // we assume there's some chance of recycling old symbols, so we only
        // create new ones if we have to.
        poly::Loop *symbol_source = nullptr;
        ptrdiff_t num_symbol_source = 0;
        for (poly::Loop *aln : alns) {
          if (num_symbol_source < aln->getSyms().size()) {
            num_symbol_source = aln->getSyms().size();
            symbol_source = aln;
          }
        }
        for (ptrdiff_t i = 0; i < std::min(num_sym, num_symbol_source); ++i)
          symbols.push_back(symbol_source->getSyms()[i]);
        for (ptrdiff_t i = num_symbol_source; i < num_sym; ++i)
          symbols.push_back(createInt64());
      }
      return addLoop(A, numLoops, symbols);
    }
    auto addLoop(PtrMatrix<int64_t> A, ptrdiff_t numLoops,
                 PtrVector<IR::Value *> symbols) -> poly::Loop * {
      ptrdiff_t num_sym = ptrdiff_t(A.numCol()) - numLoops - 1;
      utils::invariant(num_sym == symbols.size());
      poly::Loop *L = poly::Loop::allocate(ir.getAllocator(), nullptr,
                                           unsigned(ptrdiff_t(A.numRow())),
                                           numLoops, symbols.size(), true);
      L->getA() << A;
      L->getSyms() << symbols;
      alns.push_back(L);
      tr.maxDepth = std::max(tr.maxDepth, int(numLoops));
      return L;
    }
    /// createLoad(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t> indMat,
    ///            PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas) ->
    ///            Addr*
    /// `omegas` gives the lexicographical indexing into the loop tree
    auto createLoad(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t> indMat,
                    PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas,
                    poly::Loop *pl) -> IR::Addr * {
      return createAddr(ptr, elt, indMat, sizes, omegas, false, pl);
    }
    // auto createStow(IR::Value *ptr, IR::Value *stored, PtrMatrix<int64_t>
    // indMat,
    //                 PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas)
    /// `omegas` gives the lexicographical indexing into the loop tree
    auto createStow(IR::Value *ptr, IR::Value *stored,
                    PtrMatrix<int64_t> indMat, PtrVector<IR::Value *> sizes,
                    PtrVector<int64_t> omegas, poly::Loop *pl) -> IR::Addr * {
      IR::Addr *S =
        createAddr(ptr, stored->getType(), indMat, sizes, omegas, true, pl);
      IR::Stow(S).setVal(getAlloc(), stored);
      return S;
    }
    // auto createLoad(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t>
    // indMat,
    //                 PtrMatrix<int64_t> offMat, PtrVector<IR::Value *> sizes,
    //                 PtrVector<int64_t> omegas) -> IR::Addr * {
    /// `omegas` gives the lexicographical indexing into the loop tree
    auto createLoad(IR::Value *ptr, llvm::Type *elt, PtrMatrix<int64_t> indMat,
                    PtrVector<int64_t> constOffsets,
                    PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas,
                    poly::Loop *pl) -> IR::Addr * {
      return createAddr(ptr, elt, indMat, constOffsets, sizes, omegas, false,
                        pl);
    }
    // auto createStow(IR::Value *ptr, IR::Value *stored, PtrMatrix<int64_t>
    // indMat,
    //                 PtrMatrix<int64_t> offMat, PtrVector<IR::Value *> sizes,
    //                 PtrVector<int64_t> omegas) -> IR::Addr * {
    /// `omegas` gives the lexicographical indexing into the loop tree
    auto createStow(IR::Value *ptr, IR::Value *stored,
                    PtrMatrix<int64_t> indMat, PtrVector<int64_t> constOffsets,
                    PtrVector<IR::Value *> sizes, PtrVector<int64_t> omegas,
                    poly::Loop *pl) -> IR::Addr * {
      IR::Addr *S = createAddr(ptr, stored->getType(), indMat, constOffsets,
                               sizes, omegas, true, pl);
      IR::Stow(S).setVal(getAlloc(), stored);
      return S;
    }

    auto functionArg(llvm::Type *typ) -> IR::FunArg * {
      return ir.getArgument(typ, numArgs++);
    }
    auto createInt64() -> IR::FunArg * { return functionArg(getInt64Ty()); }
    // for creating some black box value
    auto getInt64Ty() -> llvm::IntegerType * { return builder.getInt64Ty(); }
    auto getDoubleTy() -> llvm::Type * { return builder.getDoubleTy(); }
    // auto createInt64() -> IR::FunArg * { return createArray(); }
    auto createArray() -> IR::FunArg * {
      return functionArg(builder.getPtrTy());
    }
    TestLoopFunction(
      target::MachineCore::Arch arch = target::MachineCore::Arch::SkylakeServer)
      : mod(new llvm::Module("TestModule", ctx)),
        FT{llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                   llvm::SmallVector<llvm::Type *, 0>(),
                                   false)},
        F{llvm::Function::Create(
          FT, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "foo", mod)},
        dl{"e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"}, TTI{dl},
        target{{arch}}, TLI{llvm::TargetLibraryInfoImpl{targetTriple}, F},
        AC{*F, &TTI}, SE{*F, TLI, AC, DT, LI},
        BB{llvm::BasicBlock::Create(ctx, "entry", F)},
        builder{llvm::IRBuilder(BB)}, ir{mod} {
      auto fmf{llvm::FastMathFlags()};
      fmf.set();
      builder.setFastMathFlags(fmf);

      auto *offset = builder.getInt64(16000);
      ptrToLoadFrom = builder.CreateIntToPtr(offset, builder.getInt64Ty());
    }
    auto getConstInt(int64_t i) -> IR::Cint * {
      return ir.createConstant(getInt64Ty(), i);
    }

    auto getSE() -> llvm::ScalarEvolution & { return SE; }
    /// obselete llvm funs
    auto getSCEVUnknown(llvm::Value *v) -> const llvm::SCEVUnknown * {
      return llvm::dyn_cast<llvm::SCEVUnknown>(SE.getUnknown(v));
    }
    auto getLLVMConstInt(int64_t i) -> llvm::ConstantInt * {
      return builder.getInt64(i);
      // return llvm::ConstantInt::get(ctx, llvm::APInt(64, i));
    }
    auto getBuilder() -> llvm::IRBuilder<> & { return builder; }
    // ~TestLoopFunction() = default;
    auto CreateLoad(llvm::Value *ptr, llvm::Value *offset) -> llvm::LoadInst * {
      llvm::Type *f64 = builder.getDoubleTy();
      auto *loadM = builder.CreateAlignedLoad(
        f64,
        builder.CreateGEP(f64, ptr,
                          llvm::SmallVector<llvm::Value *, 1>{offset}),
        llvm::MaybeAlign(8));
      return loadM;
    }
    auto CreateStore(llvm::Value *val, llvm::Value *ptr,
                     llvm::Value *offset) -> llvm::StoreInst * {
      llvm::Type *f64 = builder.getDoubleTy();
      auto *storeM = builder.CreateAlignedStore(
        val,
        builder.CreateGEP(f64, ptr,
                          llvm::SmallVector<llvm::Value *, 1>{offset}),
        llvm::MaybeAlign(8));
      return storeM;
    }
    auto getZeroF64() -> llvm::Value * {
      auto *z = llvm::ConstantFP::getZero(builder.getDoubleTy());
      return z;
    }
    auto CreateUIToF64(llvm::Value *v) -> llvm::Value * {
      auto *uitofp = builder.CreateUIToFP(v, builder.getDoubleTy());
      return uitofp;
    }
    auto CreateFAdd(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
      auto *fadd = builder.CreateFAdd(lhs, rhs);
      return fadd;
    }
    auto CreateFSub(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
      auto *fsub = builder.CreateFSub(lhs, rhs);
      return fsub;
    }
    auto CreateFMul(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
      auto *fmul = builder.CreateFMul(lhs, rhs);
      return fmul;
    }
    auto CreateFDiv(llvm::Value *lhs, llvm::Value *rhs) -> llvm::Value * {
      auto *fdiv = builder.CreateFDiv(lhs, rhs);
      return fdiv;
    }
    auto CreateFDiv(llvm::Value *lhs, llvm::Value *rhs,
                    const char *s) -> llvm::Value * {
      auto *fdiv = builder.CreateFDiv(lhs, rhs, s);
      return fdiv;
    }
    auto CreateSqrt(llvm::Value *v) -> llvm::Value * {
      llvm::Type *f64 = builder.getDoubleTy();
      llvm::Function *sqrt =
        llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::sqrt, f64);
      llvm::FunctionType *sqrtTyp =
        llvm::Intrinsic::getType(ctx, llvm::Intrinsic::sqrt, {f64});
      auto *sqrtCall = builder.CreateCall(sqrtTyp, sqrt, {v});
      // auto sqrtCall = builder.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, v);
      return sqrtCall;
    }
  };
#ifdef USE_MODULE
}
#endif
