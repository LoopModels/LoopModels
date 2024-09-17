#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <cstddef>
#include <cstdint>

#ifndef USE_MODULE
#include "IR/IR.cxx"
#include "Math/Array.cxx"
#else
export module Builder;
import Array;
import IR;
#endif

#ifdef USE_MODULE
export namespace builder { // julia: module poly; module builder
#else
namespace builder { // julia: module poly; module builder
#endif
using namespace math;      // julia: using Math

/// Used to construct an `IR::Cache` and an `IR::TreeResult`, that can be fed to
/// `lp::LoopBlock`'s optimize. Has some convenience functions for defining poly
/// loops and IR statements.
class Builder {
  IR::Cache &ir;
  IR::TreeResult tr{};
  // llvm::ScalarEvolution *SE{nullptr}; // allowed to be null?

public:
  constexpr Builder(IR::Cache &ir_) : ir(ir_) {}
  explicit constexpr operator IR::TreeResult() const { return tr; }

  /// addLoop(PtrMatrix<int64_t> A, ptrdiff_t numLoops,
  ///         llvm::SCEV *const* symSource=nullptr)
  /// `Ax >= 0`
  /// `A` is a `numConstraints` x (1 + numLoops + numSymbols)` matrix
  /// If we have symbols, a ptr giving the `SCEV`s may be provided.
  /// Otherwise, the builder generates dynamic symbols???
  ///  In that case, how should the generated code receive them as arguments?
  /// Perhaps, we should add an incremental loop/subloop interface, that assumes
  /// ordered adds?
  auto addLoop(ptrdiff_t numLoops, ptrdiff_t numSym,
               ptrdiff_t numConstraints) -> poly::Loop * {
    return poly::Loop::allocate(ir.getAllocator(), nullptr, numConstraints,
                                numLoops, numSym, true);
  }
  auto addLoop(PtrMatrix<int64_t> A, ptrdiff_t numLoops,
               PtrVector<IR::Value *> symbols) -> poly::Loop * {
    ptrdiff_t numSym = ptrdiff_t(A.numCol()) - numLoops - 1;
    invariant(numSym == symbols.size());
    poly::Loop *L = addLoop(numLoops, numSym, ptrdiff_t(A.numRow()));
    L->getA() << A;
    L->getSyms() << symbols;
    return L;
  }
};

} // namespace builder