#ifdef USE_MODULE
module;
#else
#pragma once
#endif

// #include <llvm/Analysis/ScalarEvolution.h>
// #include <llvm/Analysis/TargetTransformInfo.h>
// #include <llvm/IR/IRBuilder.h>
// #include <llvm/IR/LLVMContext.h>
// #include <memory>

#ifdef USE_MODULE
export module CodeGen;
#endif

#ifdef USE_MODULE
export namespace codegen {
#else
namespace codegen {
#endif

// class Builder {
//   std::unique_ptr<llvm::LLVMContext> ctx;
//   std::unique_ptr<llvm::Module> mod;
//   std::unique_ptr<llvm::IRBuilder<>> builder;
//   // llvm::TargetTransformInfo &TTI;

//   void init() {
//     ctx = std::make_unique<llvm::LLVMContext>();
//     mod = std::make_unique<llvm::Module>("LoopModels", *ctx);
//     builder = std::make_unique<llvm::IRBuilder<>>(*ctx);
//   }
// };

} // namespace codegen