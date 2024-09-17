#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

#ifndef NDEBUG
#define DEBUGUSED [[gnu::used]]
#else
#define DEBUGUSED
#endif

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Pair.cxx"
#include "Dicts/Dict.cxx"
#include "IR/Address.cxx"
#include "IR/Cache.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#include "IR/Phi.cxx"
#include "Math/Comparisons.cxx"
#include "Math/Constraints.cxx"
#include "Math/GreatestCommonDivisor.cxx"
#include "Math/ManagedArray.cxx"
#include "Math/UniformScaling.cxx"
#include "Polyhedra/Polyhedra.cxx"
#include "RemarkAnalysis.cxx"
#include "Support/LLVMUtils.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/Optional.cxx"
#include "Utilities/Valid.cxx"
#else
export module IR:AffineLoops;
import Arena;
import Comparisons;
import Constraints;
import GCD;
import Invariant;
import LLVMUtils;
import ManagedArray;
import Optional;
import Pair;
import Polyhedra;
import Remark;
import UniformScaling;
import Valid;
import :Address;
import :Cache;
import :Dict;
import :Instruction;
import :Node;
import :Phi;
#endif

#ifdef USE_MODULE
export namespace poly {
#else
namespace poly {
#endif
using math::IntMatrix, math::PtrVector, math::MutPtrVector, math::PtrMatrix,
  math::MutPtrMatrix;
using utils::Optional, utils::Valid, utils::invariant;
inline auto isKnownOne(llvm::ScalarEvolution &SE, llvm::Value *v) -> bool {
  return v && SE.getSCEV(v)->isOne();
}

[[nodiscard]] inline auto
getBackedgeTakenCount(llvm::ScalarEvolution &SE,
                      llvm::Loop *L) -> const llvm::SCEV * {
  auto b = L->getBounds(SE);
  if (!b || (!isKnownOne(SE, b->getStepValue())))
    return SE.getBackedgeTakenCount(L);
  const llvm::SCEV *LB = SE.getSCEV(&b->getInitialIVValue());
  const llvm::SCEV *UB = SE.getSCEV(&b->getFinalIVValue());
  if (const auto *umm = llvm::dyn_cast<llvm::SCEVUMaxExpr>(UB)) {
    const llvm::SCEV *m0 =
      SE.getMinusSCEV(umm->getOperand(0), LB, llvm::SCEV::NoWrapFlags::FlagNUW);
    const llvm::SCEV *m1 =
      SE.getMinusSCEV(umm->getOperand(1), LB, llvm::SCEV::NoWrapFlags::FlagNUW);
    // Does checking known negative make sense if we have NUW?
    if (SE.isKnownNegative(m0)) return m1;
    if (SE.isKnownNegative(m1)) return m0;
  } else if (const auto *smm = llvm::dyn_cast<llvm::SCEVSMaxExpr>(UB)) {
    const llvm::SCEV *m0 =
      SE.getMinusSCEV(smm->getOperand(0), LB, llvm::SCEV::NoWrapFlags::FlagNSW);
    const llvm::SCEV *m1 =
      SE.getMinusSCEV(smm->getOperand(1), LB, llvm::SCEV::NoWrapFlags::FlagNSW);
    if (SE.isKnownNegative(m0)) return m1;
    if (SE.isKnownNegative(m1)) return m0;
  }
  return SE.getMinusSCEV(UB, LB, llvm::SCEV::NoWrapMask);
}

struct NoWrapRewriter : public llvm::SCEVRewriteVisitor<NoWrapRewriter> {
  NoWrapRewriter(llvm::ScalarEvolution &ScEv) : SCEVRewriteVisitor(ScEv) {}
  auto visitAddRecExpr(const llvm::SCEVAddRecExpr *ex) -> const llvm::SCEV * {
    llvm::SmallVector<const llvm::SCEV *, 2> operands;
    for (const llvm::SCEV *Op : ex->operands()) operands.push_back(visit(Op));
    return SE.getAddRecExpr(operands, ex->getLoop(), llvm::SCEV::NoWrapMask);
  }
  auto visitMulExpr(const llvm::SCEVMulExpr *ex) -> const llvm::SCEV * {
    return SE.getMulExpr(visit(ex->getOperand(0)), visit(ex->getOperand(1)),
                         llvm::SCEV::NoWrapMask);
  }
  auto visitAddExpr(const llvm::SCEVAddExpr *ex) -> const llvm::SCEV * {
    return SE.getAddExpr(visit(ex->getOperand(0)), visit(ex->getOperand(1)),
                         llvm::SCEV::NoWrapMask);
  }
};

template <typename T>
inline auto findFirst(llvm::ArrayRef<T> v, const T &x) -> Optional<ptrdiff_t> {
  for (ptrdiff_t i = 0; i < v.size(); ++i)
    if (v[i] == x) return i;
  return {};
}

/// returns 1-based index, to match the pattern we use where index 0 refers to a
/// constant offset this function returns 0 if S not found in `symbols`.
[[nodiscard]] inline auto
findSymbolicIndex(llvm::ArrayRef<const llvm::SCEV *> symbols,
                  const llvm::SCEV *S) -> ptrdiff_t {
  for (ptrdiff_t i = 0; i < std::ssize(symbols);)
    if (symbols[i++] == S) return i;
  return 0;
}

[[nodiscard]] inline auto getMinMaxValueSCEV(llvm::ScalarEvolution &SE,
                                             const llvm::SCEVAddRecExpr *S)
  -> containers::Pair<const llvm::SCEV *, const llvm::SCEV *> {
  // if (!SE.containsAddRecurrence(S))
  // 	return S;
  if ((!S) || (!(S->isAffine()))) return {S, S};
  const auto *opStart = S->getStart();
  const auto *opStep = S->getStepRecurrence(SE);
  const auto *opFinal = SE.getSCEVAtScope(S, nullptr);
  // auto opFinal = SE.getSCEVAtScope(S, S->getLoop()->getParentLoop());
  // FIXME: what if there are more AddRecs nested inside?
  if (SE.isKnownNonNegative(opStep)) return {opStart, opFinal};
  if (SE.isKnownNonPositive(opStep)) return {opFinal, opStart};
  return {S, S};
}
// TODO: strengthen through recursion
[[nodiscard]] inline auto getMinMaxValueSCEV(llvm::ScalarEvolution &SE,
                                             const llvm::SCEV *S)
  -> containers::Pair<const llvm::SCEV *, const llvm::SCEV *> {
  if (const auto *T = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
    return getMinMaxValueSCEV(SE, T);
  return {S, S};
}
[[nodiscard]] inline auto
simplifyMinMax(llvm::ScalarEvolution &SE,
               const llvm::SCEVMinMaxExpr *S) -> const llvm::SCEV * {
  // FIXME: This is probably a bit aggressive...
  bool isMin =
    llvm::isa<llvm::SCEVSMinExpr>(S) || llvm::isa<llvm::SCEVUMinExpr>(S);
  bool isSigned =
    llvm::isa<llvm::SCEVSMinExpr>(S) || llvm::isa<llvm::SCEVSMaxExpr>(S);
  auto GE = isSigned ? llvm::ICmpInst::Predicate::ICMP_SGE
                     : llvm::ICmpInst::Predicate::ICMP_UGE;

  const llvm::SCEV *op0 = S->getOperand(0);
  const llvm::SCEV *op1 = S->getOperand(1);
  auto [LB0, UB0] = getMinMaxValueSCEV(SE, op0);
  auto [LB1, UB1] = getMinMaxValueSCEV(SE, op1);
  // op0 >= op1
  if (SE.isKnownPredicate(GE, LB0, UB1)) return isMin ? op1 : op0;
  // op1 >= op0
  if (SE.isKnownPredicate(GE, LB1, UB0)) return isMin ? op0 : op1;
  return S;
}
[[nodiscard]] inline auto
simplifyMinMax(llvm::ScalarEvolution &SE,
               const llvm::SCEV *S) -> const llvm::SCEV * {
  if (const auto *MM = llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(S))
    return simplifyMinMax(SE, MM);
  return S;
}

namespace loopNestCtor {
/// add a symbol to row `r` of A
/// we try to break down value `v`, so that adding
/// N, N - 1, N - 3 only adds the variable `N`, and adds the constant
/// offsets
inline void addSymbol(IntMatrix<math::StridedDims<>> &A,
                      llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                      const llvm::SCEV *v, math::Range<ptrdiff_t, ptrdiff_t> lu,
                      int64_t mlt) {
  assert(lu.size());
  symbols.push_back(v);
  A.resize(++auto{A.numCol()});
  A[lu, symbols.size()] << mlt;
}
inline auto addRecMatchesLoop(const llvm::SCEV *S, llvm::Loop *L) -> bool {
  if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S))
    return x->getLoop() == L;
  return false;
}
[[nodiscard]] inline auto // NOLINTNEXTLINE(misc-no-recursion)
addSymbol(std::array<IntMatrix<math::StridedDims<>>, 2> &AB,
          llvm::SmallVectorImpl<const llvm::SCEV *> &symbols, llvm::Loop *L,
          const llvm::SCEV *v, llvm::ScalarEvolution &SE,
          math::Range<ptrdiff_t, ptrdiff_t> lu, int64_t mlt,
          ptrdiff_t minDepth) -> ptrdiff_t {
  auto &[A, B] = AB;
  // first, we check if `v` in `Symbols`
  if (ptrdiff_t i = findSymbolicIndex(symbols, v)) {
    A[lu, i] += mlt;
    return minDepth;
  }
  if (std::optional<int64_t> c = utils::getConstantInt(v)) {
    A[lu, 0] += mlt * (*c);
    return minDepth;
  }
  if (const auto *ar = llvm::dyn_cast<const llvm::SCEVAddExpr>(v)) {
    const llvm::SCEV *op0 = ar->getOperand(0);
    const llvm::SCEV *op1 = ar->getOperand(1);
    Row M = A.numRow();
    minDepth = addSymbol(AB, symbols, L, op0, SE, lu, mlt, minDepth);
    if (M != A.numRow())
      minDepth =
        addSymbol(AB, symbols, L, op1, SE, _(M, A.numRow()), mlt, minDepth);
    return addSymbol(AB, symbols, L, op1, SE, lu, mlt, minDepth);
  }
  if (const auto *m = llvm::dyn_cast<const llvm::SCEVMulExpr>(v)) {
    if (auto op0 = utils::getConstantInt(m->getOperand(0)))
      return addSymbol(AB, symbols, L, m->getOperand(1), SE, lu, mlt * (*op0),
                       minDepth);
    if (auto op1 = utils::getConstantInt(m->getOperand(1)))
      return addSymbol(AB, symbols, L, m->getOperand(0), SE, lu, mlt * (*op1),
                       minDepth);
  } else if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(v)) {
    ptrdiff_t recDepth = x->getLoop()->getLoopDepth();
    if (x->isAffine()) {
      minDepth =
        addSymbol(AB, symbols, L, x->getOperand(0), SE, lu, mlt, minDepth);
      if (auto opc = utils::getConstantInt(x->getOperand(1))) {
        B[lu, recDepth - 1] << mlt * (*opc);
        return minDepth;
      }
      v = SE.getAddRecExpr(SE.getZero(x->getOperand(0)->getType()),
                           x->getOperand(1), x->getLoop(), x->getNoWrapFlags());
    }
    // we only support affine SCEVAddRecExpr with constant steps
    // we use a flag "minSupported", which defaults to 0
    // 0 means we support all loops, as the outer most depth is 1
    // Depth of 0 means toplevel.
    minDepth = std::max(minDepth, recDepth);
  } else if (const auto *mm = llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(v)) {
    const auto *Sm = simplifyMinMax(SE, mm);
    if (Sm != v) return addSymbol(AB, symbols, L, Sm, SE, lu, mlt, minDepth);
    bool isMin =
      llvm::isa<llvm::SCEVSMinExpr>(mm) || llvm::isa<llvm::SCEVUMinExpr>(mm);
    const llvm::SCEV *op0 = mm->getOperand(0);
    const llvm::SCEV *op1 = mm->getOperand(1);
    if (isMin ^ (mlt < 0)) { // we can represent this as additional constraints
      Row M = A.numRow();
      Row Mp = math::row(ptrdiff_t(M) + std::ssize(lu));
      A.resize(Mp);
      B.resize(Mp);
      A[_(M, Mp), _] = A[lu, _];
      B[_(M, Mp), _] = B[lu, _];
      minDepth = addSymbol(AB, symbols, L, op0, SE, lu, mlt, minDepth);
      minDepth = addSymbol(AB, symbols, L, op1, SE, _(M, Mp), mlt, minDepth);
    } else if (addRecMatchesLoop(op0, L)) {
      return addSymbol(AB, symbols, L, op1, SE, lu, mlt, minDepth);
    } else if (addRecMatchesLoop(op1, L)) {
      return addSymbol(AB, symbols, L, op0, SE, lu, mlt, minDepth);
    }
  } else if (const auto *ex = llvm::dyn_cast<llvm::SCEVCastExpr>(v))
    return addSymbol(AB, symbols, L, ex->getOperand(0), SE, lu, mlt, minDepth);
  addSymbol(A, symbols, v, lu, mlt);
  return minDepth;
}
inline auto
areSymbolsLoopInvariant(IntMatrix<math::StridedDims<>> &A,
                        llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                        llvm::Loop *L, llvm::ScalarEvolution &SE) -> bool {
  for (ptrdiff_t i = 0; i < std::ssize(symbols); ++i)
    if ((!allZero(A[_, i + 1])) && (!SE.isLoopInvariant(symbols[i], L)))
      return false;
  return true;
}
inline auto // NOLINTNEXTLINE(misc-no-recursion)
addBackedgeTakenCount(std::array<IntMatrix<math::StridedDims<>>, 2> &AB,
                      llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                      llvm::Loop *L, const llvm::SCEV *BT,
                      llvm::ScalarEvolution &SE, ptrdiff_t minDepth,
                      llvm::OptimizationRemarkEmitter *ORE) -> ptrdiff_t {
  // A contains syms
  auto &[A, B] = AB;
  Row M = A.numRow(), MM = M;
  A.resize(++MM);
  B.resize(MM);
  minDepth = addSymbol(AB, symbols, L, BT, SE, _(M, MM), 1, minDepth);
  assert(A.numRow() == B.numRow());
  ptrdiff_t depth = L->getLoopDepth() - 1;
  for (auto m = ptrdiff_t(M); m < A.numRow(); ++m) B[m, depth] = -1; // indvar
  // recurse, if possible to add an outer layer
  if (llvm::Loop *P = L->getParentLoop()) {
    if (areSymbolsLoopInvariant(A, symbols, P, SE)) {
      // llvm::SmallVector<const llvm::SCEVPredicate *, 4> predicates;
      // auto *BTI = SE.getPredicatedBackedgeTakenCount(L,
      // predicates);
      if (const llvm::SCEV *BTP = getBackedgeTakenCount(SE, P)) {
        if (!llvm::isa<llvm::SCEVCouldNotCompute>(BTP))
          return addBackedgeTakenCount(AB, symbols, P, BTP, SE, minDepth, ORE);
        if (ORE) [[unlikely]] {
          llvm::SmallVector<char, 128> msg;
          llvm::raw_svector_ostream os(msg);
          os << "SCEVCouldNotCompute from loop: " << *P << "\n";
          llvm::OptimizationRemarkAnalysis analysis{
            utils::remarkAnalysis("AffineLoopConstruction", L)};
          ORE->emit(analysis << os.str());
        }
      }
    } else if (ORE) [[unlikely]] {
      llvm::SmallVector<char, 256> msg;
      llvm::raw_svector_ostream os(msg);
      os << "Fail because symbols are not loop invariant in loop:\n"
         << *P << "\n";
      if (auto b = L->getBounds(SE))
        os << "Loop Bounds:\nInitial: " << b->getInitialIVValue()
           << "\nStep: " << *b->getStepValue()
           << "\nFinal: " << b->getFinalIVValue() << "\n";
      for (const auto *s : symbols) os << *s << "\n";
      llvm::OptimizationRemarkAnalysis analysis{
        utils::remarkAnalysis("AffineLoopConstruction", L)};
      ORE->emit(analysis << os.str());
    }
  }
  return std::max(depth, minDepth);
}
} // namespace loopNestCtor
#ifndef NDEBUG
[[gnu::used]] inline void dumpSCEV(const llvm::SCEV *S) { llvm::errs() << *S; }
#endif

// A * x >= 0
// if constexpr(NonNegative)
//   x >= 0
class Loop : public BasePolyhedra<false, true, true, Loop> {
  using BaseT = BasePolyhedra<false, true, true, Loop>;

  [[nodiscard]] constexpr auto getSymCapacity() const -> ptrdiff_t {
    return numDynSymbols + numLoops;
  }
  llvm::Loop *L{nullptr};
  unsigned int numConstraints;
  unsigned int numLoops;
  unsigned int numDynSymbols;
  unsigned int nonNegative; // initially stores orig numloops
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) // FAM
  alignas(int64_t) char memory[];
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

public:
  Loop(const Loop &) = delete;
  [[nodiscard]] constexpr auto isNonNegative() const -> bool {
    return nonNegative;
  }
  static inline auto
  construct(IR::Cache &cache, llvm::Loop *L, const llvm::SCEV *BT,
            IR::LLVMIRBuilder LB,
            llvm::OptimizationRemarkEmitter *ORE = nullptr) -> Valid<Loop> {
    // A holds symbols
    // B holds loop bounds
    // they're separate so we can grow them independently
    std::array<IntMatrix<math::StridedDims<>>, 2> AB;
    auto &[A, B] = AB;
    // once we're done assembling these, we'll concatenate A and B
    unsigned maxDepth = L->getLoopDepth();
    invariant(maxDepth > 0);
    // ptrdiff_t maxNumSymbols = BT->getExpressionSize();
    A.resizeForOverwrite(math::StridedDims<>{
      {}, math::col(1), math::stride(ptrdiff_t(1) + BT->getExpressionSize())});
    B.resizeForOverwrite(math::StridedDims<>{{}, math::col(maxDepth)});
    llvm::SmallVector<const llvm::SCEV *> symbols;
    llvm::ScalarEvolution &SE{*LB.SE_};
    ptrdiff_t minDepth =
      loopNestCtor::addBackedgeTakenCount(AB, symbols, L, BT, SE, 0, ORE);
    // We first check for loops in B that are shallower than minDepth
    // we include all loops such that L->getLoopDepth() > minDepth
    // note that the outer-most loop has a depth of 1.
    // We turn these loops into `getAddRecExprs`s, so that we can
    // add them as variables to `A`.
    for (ptrdiff_t d = 0; d < ptrdiff_t(minDepth); ++d) {
      // loop at depth d+1
      llvm::Loop *P = nullptr;
      // search B(_,d) for references
      for (ptrdiff_t i = 0; i < B.numRow(); ++i) {
        // TODO; confirm `last` vs `end`
        if (int64_t Bid = B[i, d]) {
          if (!P) { // find P
            P = L;
            for (ptrdiff_t r = d + 1; r < maxDepth; ++r) P = P->getParentLoop();
          }
          // TODO: find a more efficient way to get IntTyp
          llvm::Type *intTyp = P->getInductionVariable(SE)->getType();
          loopNestCtor::addSymbol(A, symbols,
                                  SE.getAddRecExpr(SE.getZero(intTyp),
                                                   SE.getOne(intTyp), P,
                                                   llvm::SCEV::NoWrapMask),
                                  _(i, i + 1), Bid);
        }
      }
    }
    invariant(1 + std::ssize(symbols), ptrdiff_t(A.numCol()));
    ptrdiff_t depth = maxDepth - minDepth;
    ptrdiff_t numConstraints = ptrdiff_t(A.numRow()), N = ptrdiff_t(A.numCol());
    Valid<Loop> aln{Loop::allocate(cache.getAllocator(), L, numConstraints,
                                   depth, symbols.size(), true)};
    if ((depth > 0) && (!symbols.empty())) {
      llvm::SCEVExpander expdr(SE, cache.dataLayout(), "ConstructLoop");
      llvm::Type *intTyp = L->getInductionVariable(SE)->getType();
      llvm::Loop *LL = L;
      for (ptrdiff_t i = depth; --i;) LL = LL->getParentLoop();
      // we require loops to be canonicalized into loop simplify form.
      // that is, we require a preheader, so `getLoopPreheader()` should
      // return non-null
      llvm::Instruction *loc = LL->getLoopPreheader()->getTerminator();
      for (ptrdiff_t i = 0; i < std::ssize(symbols); ++i) {
        llvm::Value *S = expdr.expandCodeFor(symbols[i], intTyp, loc);

        aln->getSyms().begin()[i] = cache.getValueOutsideLoop(S, LB);
      }
    }
    aln->getA()[_, _(0, N)] << A;
    // copy the included loops from B
    // we use outer <-> inner order, so we skip unsupported outer loops.
    aln->getA()[_, _(N, N + depth)] << B[_, _(end - depth, end)];
    return aln;
    // addZeroLowerBounds();
    // NOTE: pruneBounds() is not legal here if we wish to use
    // removeInnerMost later.
    // pruneBounds();
  }
  static constexpr uint32_t dyn_loop_est = 1024;
  /// Gives a very rough trip count estimate (second return value)
  /// with a boolean first arg indicating whether it is exact or estimated.
  /// The estimation approach here can be seriously improved.
  /// Currently, if not exact, it simply returns `dyn_loop_est`.
  [[nodiscard]] auto
  tripCount(ptrdiff_t depth1) const -> containers::Pair<bool, uint32_t> {
    DensePtrMatrix<int64_t> A{getA()};
    // `i` is position of depth's indvar
    ptrdiff_t i = numDynSymbols + depth1, j = -1, k = -1;
    // `A * loopindvars >= 0`
    // Aci >= 0 is a lower bound
    // Aci <= 0 is an upper bound
    for (ptrdiff_t c = 0; c < A.numRow(); ++c) {
      int64_t Aci = A[c, i];
      if (Aci > 0) {
        if ((j >= 0) || (!math::allZero(A[c, _(1, i)])))
          return {false, dyn_loop_est};
        j = c;
      } else if (Aci < 0) {
        if ((k >= 0) || (!math::allZero(A[c, _(1, i)])))
          return {false, dyn_loop_est};
        k = c;
      }
    }
    invariant(j >= 0); // must have lower bound
    invariant(k >= 0); // must have upper bound
    auto tc = A[k, 0] - A[j, 0];
    static constexpr uint32_t maxval = std::numeric_limits<uint32_t>::max();
    return {true, tc <= maxval ? uint32_t(tc) : maxval};
  }
  /// A.rotate( R )
  /// A(_,const) + A(_,var)*var >= 0
  /// this method applies rotation matrix R
  /// A(_,const) + (A(_,var)*R)*(R^{-1}*var) >= 0
  /// So that our new loop nest has matrix
  /// [A(_,const) (A(_,var)*R)]
  /// while the new `var' is `(R^{-1}*var)`
  /// offset the loops by `offsets`, e.g. if we have
  /// offsets[0] = 2, then the first loop is shifted by 2.
  /// this shifting is applied before rotation.
  [[nodiscard]] auto rotate(Arena<> *alloc, DensePtrMatrix<int64_t> R,
                            const int64_t *offsets) const -> Valid<Loop> {
    // if offsets is not null, we have the equivalent of
    // A * O * [I 0; 0 R]
    // where O = I - [0 0; offsets 0],
    // where offsets is a vector of length getNumLoops() and O is square
    ptrdiff_t numExtraVar = 0, numConst = this->getNumSymbols();
    bool thisNonNeg = isNonNegative(), nonNeg = thisNonNeg && allGEZero(R),
         addExtra = thisNonNeg != nonNeg;
    if (addExtra) numExtraVar = getNumLoops();
    invariant(ptrdiff_t(R.numCol()), getNumLoops());
    invariant(ptrdiff_t(R.numRow()), getNumLoops());
    auto A{getA()};
    const auto [M, N] = shape(A);
    Valid<Loop> aln{Loop::allocate(alloc, L, ptrdiff_t(M) + numExtraVar,
                                   numLoops, getSyms(), nonNeg)};
    auto B{aln->getA()};
    invariant(B.numRow() == M + numExtraVar);
    invariant(B.numCol() == N);
    B[_(0, M), _(0, numConst)] << A[_, _(0, numConst)];
    B[_(0, M), _(numConst, end)] << A[_, _(numConst, end)] * R;
    if (addExtra) {
      B[_(M, end), _(0, numConst)] << 0;
      B[_(M, end), _(numConst, end)] << R;
    }
    // A * O * [I 0; 0 R] = A * [I 0; 0 R] - A * [0 0; offs 0] * [I 0; 0 R]
    // above, we computed `A * [I 0; 0 R]`, now if offsets != nullptr,
    // we subtract A * [0 0; offs 0] * [I 0; 0 R].
    // note that we have (s = number of dynamic symbols, l = number of loops)
    //      1    s  l         1  s l            1    s  l
    // 1  [ 0    0  0       [ 1  0 0          [ 0    0  0
    // s    0    0  0    *    0  I 0      =     0    0  0
    // l   offs  0  0  ]      0  0 R ]          offs 0  0 ]
    // thus, we can ignore R here, and simply update the result using A.
    if (offsets) {
      for (ptrdiff_t l = 0, D = getNumLoops(); l < D; ++l) {
        if (int64_t mlt = offsets[l]) {
          B[_(0, M), 0] -= mlt * A[_, numConst + l];
          if (addExtra) B[M + l, 0] = -mlt;
        }
      }
    }
    // aln->pruneBounds(alloc);
    return aln;
  }
  [[nodiscard]] constexpr auto getLLVMLoop() const -> llvm::Loop * { return L; }
  [[nodiscard]] constexpr auto rotate(Arena<> *alloc, DensePtrMatrix<int64_t> R,
                                      const int64_t *offsets) -> Valid<Loop> {
    if (R == math::I) return this;
    return ((const Loop *)this)->rotate(alloc, R, offsets);
  }

  /// When/Why would we want to use this???
  [[nodiscard]] auto removeInnerMost(Arena<> *alloc) const -> Valid<Loop> {
    // order is outer<->inner
    auto A{getA()};
    auto ret = Loop::allocate(alloc, L->getParentLoop(), ptrdiff_t(A.numRow()),
                              getNumLoops() - 1, getSyms(), isNonNegative());
    MutPtrMatrix<int64_t> B{ret->getA()};
    B << A[_, _(0, last)];
    // no loop may be conditioned on the innermost loop, so we should be able to
    // safely remove all constraints that reference it
    for (Row m = B.numRow(); m--;) {
      if (A[m, last]) {
        if (m != --auto{B.numRow()}) B[m, _] << B[last, _];
        B.truncate(--B.numRow());
      }
    }
    ret->truncateConstraints(ptrdiff_t(B.numRow()));
    return ret;
  }
  constexpr void truncateConstraints(ptrdiff_t newNumConstraints) {
    assert(newNumConstraints <= numConstraints);
    numConstraints = newNumConstraints;
  }
  constexpr void clear() {
    numConstraints = 0;
    numLoops = 0;
    numDynSymbols = 0;
  }
  // L is the inner most loop getting removed
  void removeOuterMost(IR::Cache &cache, ptrdiff_t numToRemove,
                       IR::LLVMIRBuilder LB, llvm::SCEVExpander &scevexpdr) {
    // basically, we move the outermost loops to the symbols section,
    // and add the appropriate addressees
    // order is outer<->inner
    ptrdiff_t oldNumLoops = getNumLoops();
    // NOTE: originally, `nonNegative` stores the original number of loops. We
    // use this to check how many loops we have already pealed, to avoid
    // re-pealing. Initially, pre-affine transform, all loops are canonicalized
    // as starting at 0, so that non-negative is true (hence why we do not
    // initially need this field).
    invariant(nonNegative >= oldNumLoops);
    numToRemove -= (nonNegative - oldNumLoops);
    if (numToRemove == 0) return;
    if (numToRemove >= oldNumLoops) return clear();
    ptrdiff_t newNumLoops = oldNumLoops - numToRemove,
              oldNumDynSymbols = numDynSymbols;
    numDynSymbols += numToRemove;
    auto S{getSyms()};
    auto &SE{*LB.SE_};
    // LL is exterior to the outermost loop
    llvm::Loop *LL = L;
    for (ptrdiff_t d = newNumLoops; d--;) LL = LL->getParentLoop();
    // Array `A` goes from outer->inner
    // as we peel loops, we go from inner->outer
    // so we iterate `i` backwards
    // TODO: use `SCEVExpander`'s `expandCodeFor` method
    for (ptrdiff_t i = numToRemove; i;) {
      llvm::Type *intTyp = LL->getInductionVariable(SE)->getType();
      const auto *TC = SE.getAddRecExpr(SE.getZero(intTyp), SE.getOne(intTyp),
                                        LL, llvm::SCEV::NoWrapMask);
      llvm::Instruction *IP = L->getLoopPreheader()->getFirstNonPHI();
      llvm::Value *TCV = scevexpdr.expandCodeFor(TC, intTyp, IP);
      S[--i + oldNumDynSymbols] = cache.getValueOutsideLoop(TCV, LB);
      LL = LL->getParentLoop();
    }
    numLoops = newNumLoops;
  }

  void addZeroLowerBounds() {
    if (this->isEmpty()) return;
    if (isNonNegative()) return; // this->pruneBounds(alloc);
    // return initializeComparator();
    if (!numLoops) return;
    ptrdiff_t M = numConstraints;
    numConstraints += numLoops;
    auto A{getA()};
    A[_(M, end), _] << 0;
    for (ptrdiff_t i = 0; i < numLoops; ++i) A[M + i, end - numLoops + i] = 1;
    // this->pruneBounds(alloc);
  }

  [[nodiscard]] constexpr auto
  getProgVars(ptrdiff_t j) const -> PtrVector<int64_t> {
    return getA()[j, _(0, getNumSymbols())];
  }
  [[nodiscard]] auto copy(Arena<> *alloc) const -> Valid<Loop> {
    auto ret = Loop::allocate(alloc, L, numConstraints, numLoops, getSyms(),
                              isNonNegative());
    ret->getA() << getA();
    return ret;
  }
  [[nodiscard]] auto removeLoop(Arena<> *alloc, ptrdiff_t v) const -> Loop * {
    auto A{getA()};
    v += getNumSymbols();
    auto zeroNegPos = indsZeroNegPos(A[_, v]);
    auto &[zer, neg, pos] = zeroNegPos;
    ptrdiff_t numCon =
      ptrdiff_t(A.numRow()) - pos.size() + neg.size() * pos.size();
    if (!isNonNegative()) numCon -= neg.size();
    auto p = checkpoint(alloc);
    auto ret = Loop::allocate(alloc, nullptr, numCon, numLoops - 1, getSyms(),
                              isNonNegative());
    ret->numConstraints = ptrdiff_t(
      isNonNegative()
        ? fourierMotzkinCore<true>(ret->getA(), getA(), v, zeroNegPos)
        : fourierMotzkinCore<false>(ret->getA(), getA(), v, zeroNegPos));
    // FIXME: bounds don't appear pruned in tests?
    ret->pruneBounds(*alloc);
    if (ret->getNumLoops() == 0) {
      rollback(alloc, p);
      return nullptr;
    }
    // either we remove one loop, or remaining loops are empty
    assert((ret->getNumLoops() == getNumLoops() - 1)); // didn't remove loop
    return ret;
  }
  constexpr void eraseConstraint(ptrdiff_t c) {
    eraseConstraintImpl(getA(), math::row(c));
    --numConstraints;
  }
  [[nodiscard]] auto
  zeroExtraItersUponExtending(Arena<> alloc, ptrdiff_t _i,
                              bool extendLower) const -> bool {
    auto p = alloc.scope();
    Loop *tmp = copy(&alloc);
    // question is, does the inner most loop have 0 extra iterations?
    const ptrdiff_t numPrevLoops = getNumLoops() - 1;
    // we changed the behavior of removeLoop to actually drop loops that are
    // no longer present.
    for (ptrdiff_t i = 0; i < numPrevLoops - 1; ++i)
      tmp = tmp->removeLoop(&alloc, i >= _i);
    // loop _i is now loop 0
    // innermost loop is now loop 1
    bool indep = true;
    const ptrdiff_t numConst = getNumSymbols();
    auto A{tmp->getA()};
    for (ptrdiff_t n = 0; n < A.numRow(); ++n)
      if ((A[n, numConst] != 0) && (A[n, 1 + numConst] != 0)) indep = false;
    if (indep) return false;
    Loop *margi = tmp->removeLoop(&alloc, 1), *tmp2;
    invariant(margi->getNumLoops(), ptrdiff_t(1));
    invariant(tmp->getNumLoops(), ptrdiff_t(2));
    invariant(++auto{margi->getA().numCol()}, tmp->getA().numCol());
    // margi contains extrema for `_i`
    // we can substitute extended for value of `_i`
    // in `tmp`
    auto p2 = alloc.checkpoint();
    int64_t sign = 2 * extendLower - 1; // extendLower ? 1 : -1
    for (ptrdiff_t c = 0; c < margi->getNumInequalityConstraints(); ++c) {
      int64_t b = sign * margi->getA()[c, numConst];
      if (b <= 0) continue;
      alloc.rollback(p2);
      tmp2 = tmp->copy(&alloc);
      invariant(tmp2->getNumLoops(), ptrdiff_t(2));
      invariant(margi->getNumLoops() + 1, tmp2->getNumLoops());
      // increment to increase bound
      // this is correct for both extending lower and extending upper
      // lower: a'x + i + b >= 0 -> i >= -a'x - b
      // upper: a'x - i + b >= 0 -> i <=  a'x + b
      // to decrease the lower bound or increase the upper, we increment
      // `b`
      ++(margi->getA())[c, 0];
      // our approach here is to set `_i` equal to the extended bound
      // and then check if the resulting polyhedra is empty.
      // if not, then we may have >0 iterations.
      for (ptrdiff_t cc = 0; cc < tmp2->getNumCon(); ++cc) {
        if (int64_t d = tmp2->getA()[cc, numConst]) {
          tmp2->getA()[cc, _(0, last)] << b * tmp2->getA()[cc, _(0, last)] -
                                            (d * sign) * margi->getA()[c, _];
        }
      }
      for (auto cc = ptrdiff_t(tmp2->getNumCon()); cc;)
        if (tmp2->getA()[--cc, 1 + numConst] == 0) tmp2->eraseConstraint(cc);
      if (!(tmp2->calcIsEmpty(alloc))) return false;
    }
    if (isNonNegative()) {
      if (extendLower) {
        // increment to increase bound
        // this is correct for both extending lower and extending upper
        // lower: a'x + i + b >= 0 -> i >= -a'x - b
        // upper: a'x - i + b >= 0 -> i <=  a'x + b
        // to decrease the lower bound or increase the upper, we
        // increment `b` our approach here is to set `_i` equal to the
        // extended bound and then check if the resulting polyhedra is
        // empty. if not, then we may have >0 iterations.
        for (ptrdiff_t cc = 0; cc < tmp->getNumCon(); ++cc) {
          if (int64_t d = tmp->getA()[cc, numConst]) {
            // lower bound is i >= 0
            // so setting equal to the extended lower bound now
            // means that i = -1 so we decrement `d` from the column
            tmp->getA()[cc, 0] -= d;
            tmp->getA()[cc, numConst] = 0;
          }
        }
        for (auto cc = ptrdiff_t(tmp->getNumCon()); cc;)
          if (tmp->getA()[--cc, 1 + numConst] == 0) tmp->eraseConstraint(cc);
        if (!(tmp->calcIsEmpty(alloc))) return false;
      }
    }
    return true;
  }

  auto printSymbol(std::ostream &os, PtrVector<int64_t> x,
                   int64_t mul) const -> bool {
    bool printed = false;
    for (ptrdiff_t i = 1; i < x.size(); ++i)
      if (int64_t xi = x[i] * mul) {
        if (printed) os << (xi > 0 ? " + " : " - ");
        printed = true;
        int64_t absxi = math::constexpr_abs(xi);
        if (absxi != 1) os << absxi << " * ";
        os << *getSyms()[i - 1];
      }
    if (int64_t x0 = x[0]) {
      if (printed)
        os << (mul * x0 > 0 ? " + " : " - ") << math::constexpr_abs(x0);
      else os << mul * x0;
      printed = true;
    }
    return printed;
  }
  constexpr void setNumConstraints(ptrdiff_t numCon) {
    numConstraints = numCon;
  }
  static constexpr void setNumEqConstraints(ptrdiff_t) {}
  constexpr void decrementNumConstraints() { --numConstraints; }

  void printBound(std::ostream &os, int64_t sign, ptrdiff_t numVarMinus1,
                  ptrdiff_t numConst, ptrdiff_t j) const {
    PtrVector<int64_t> b = getProgVars(j);
    DensePtrMatrix<int64_t> A{getA()};
    bool printed = printSymbol(os, b, -sign);
    for (ptrdiff_t k = 0; k < numVarMinus1; ++k) {
      if (int64_t lakj = A[j, k + numConst]) {
        if (lakj * sign > 0) os << " - ";
        else if (printed) os << " + ";
        lakj = math::constexpr_abs(lakj);
        if (lakj != 1) os << lakj << "*";
        os << "i_" << k;
        printed = true;
      }
    }
    if (!printed) os << 0;
  }
  void printBoundShort(std::ostream &os, int64_t sign, ptrdiff_t numVarMinus1,
                       ptrdiff_t numConst, int64_t allAj, ptrdiff_t numRow,
                       bool separateLines) const {
    bool isUpper = sign < 0,
         printed = (numRow > 1) && (separateLines || isUpper);
    if (separateLines || isUpper) {
      if (allAj == 1) os << "i_" << numVarMinus1;
      else os << allAj << "*i_" << numVarMinus1;
      os << (isUpper ? " ≤ " : " ≥ ");
    }
    if (numRow > 1) os << (isUpper ? "min(" : "max(");
    DensePtrMatrix<int64_t> A{getA()};
    ptrdiff_t k = 0;
    for (ptrdiff_t j = 0; j < A.numRow(); ++j) {
      if (A[j, last] * sign <= 0) continue;
      if (k++) os << ", ";
      printBound(os, sign, numVarMinus1, numConst, j);
      printed = true;
    }
    // k < numRow indicates we need to add a `0` to `max`
    // as `numRow > k` only if no `0` was included.
    if (isNonNegative() && (!isUpper) && (k < numRow))
      os << (printed ? ", 0" : "0");
    if (numRow > 1) os << ")";
    if (!(separateLines || isUpper)) os << " ≤ ";
  }
  // prints the inner most loop.
  // it is assumed that you iteratively pop off the inner most loop with
  // `removeLoop` to print all bounds.

  void printBound(std::ostream &os, int64_t sign) const {
    const ptrdiff_t numVar = getNumLoops();
    if (numVar == 0) return;
    const ptrdiff_t numVarM1 = numVar - 1, numConst = getNumSymbols();
    bool hasPrintedLine = isNonNegative() && (sign == 1), isUpper = sign < 0;
    DensePtrMatrix<int64_t> A{getA()};
    ptrdiff_t numRow = 0;
    int64_t allAj = 0;
    for (ptrdiff_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A[j, last], Aj = Ajr * sign;
      if (Aj <= 0) continue;
      if (allAj) allAj = allAj == Aj ? allAj : -1;
      else allAj = Aj;
      ++numRow;
    }
    if (numRow == 0) {
      if (isNonNegative())
        if (!isUpper) os << "i_" << numVarM1 << " ≥ 0";
      return;
    }
    if (isNonNegative())
      if (!isUpper) ++numRow;
    if (allAj > 0)
      return printBoundShort(os, sign, numVarM1, numConst, allAj, numRow, true);
    for (ptrdiff_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A[j, end - 1], Aj = Ajr * sign;
      if (Aj <= 0) continue;
      if (hasPrintedLine)
        for (ptrdiff_t k = 0; k < 21; ++k) os << ' ';
      hasPrintedLine = true;
      if (Ajr != sign)
        os << Aj << "*i_" << numVarM1 << (isUpper ? " ≤ " : " ≥ ");
      else os << "i_" << numVarM1 << (isUpper ? " ≤ " : " ≥ ");
      printBound(os, sign, numVarM1, numConst, j);
      os << "\n";
    }
    if (isNonNegative())
      if (!isUpper) os << "i_" << numVarM1 << " ≥ 0\n";
  }
  void printBounds(std::ostream &os) const {
    const ptrdiff_t numVar = getNumLoops();
    if (numVar == 0) return;
    DensePtrMatrix<int64_t> A{getA()};
    int64_t allAj = 0; // if all A[j,last] are equal, is that. Otherwise, -1
    ptrdiff_t numPos = 0, numNeg = 0;
    bool addZeroLB = isNonNegative();
    for (ptrdiff_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A[j, last];
      if (Ajr == 0) continue;
      if (Ajr > 0) {
        ++numPos;
        addZeroLB = addZeroLB && math::anyNEZero(A[j, _(0, last)]);
      } else ++numNeg;
      int64_t x = std::abs(Ajr);
      if (allAj) allAj = allAj == x ? allAj : -1;
      else allAj = x;
    }
    if (allAj > 0) {
      ptrdiff_t numVarMinus1 = numVar - 1, numConst = getNumSymbols();
      if (addZeroLB) ++numPos;
      printBoundShort(os, 1, numVarMinus1, numConst, allAj, numPos, false);
      printBoundShort(os, -1, numVarMinus1, numConst, allAj, numNeg, false);
    } else {
      printBound(os, 1);
      printBound(os << " && ", -1);
    }
  }
  void dump(std::ostream &os, Arena<> *alloc) const {
    const Loop *tmp = this;
    for (ptrdiff_t i = getNumLoops(); tmp;) {
      assert((i == tmp->getNumLoops()) && "loop count mismatch");
      tmp->printBounds(os << "\nLoop " << --i << ": ");
      if (!i) break;
      tmp = tmp->removeLoop(alloc, i);
    }
  }

  // prints loops from inner most to outer most.
  // outer most loop is `i_0`, subscript increments for each level inside
  // We pop off the outer most loop on every iteration.
  friend inline auto operator<<(std::ostream &os,
                                const Loop &aln) -> std::ostream & {
    alloc::OwningArena<> alloc;
    aln.dump(os, &alloc);
    return os;
  }
#ifndef NDEBUG
  [[gnu::used]] void dump() const { std::cout << *this; }
#endif
  [[nodiscard]] constexpr auto getNumCon() const -> ptrdiff_t {
    return numConstraints;
  }
  /// returns the `A` where `A * [1
  ///                         dynamic symbols in loop bounds
  ///                         indvars] >= 0`,
  /// `i` are loop indvars
  /// Number of rows indicate number of constraints, columns are
  /// /// returns the `A` where `A * i >= 0`, `i` are loop indvars
  /// Number of rows indicate number of constraints, columns are
  /// 1 (constant) + numDynSymbols + number of loops
  [[nodiscard]] constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    const void *ptr =
      memory + sizeof(const llvm::SCEV *const *) * numDynSymbols;
    auto *p = (int64_t *)const_cast<void *>(ptr);
    return {p, math::DenseDims<>{math::row(numConstraints),
                                 math::col(numLoops + numDynSymbols + 1)}};
  };
  /// returns the `A` where `A * [1
  ///                         dynamic symbols in loop bounds
  ///                         indvars] >= 0`,
  /// `i` are loop indvars
  /// Number of rows indicate number of constraints, columns are
  /// /// returns the `A` where `A * i >= 0`, `i` are loop indvars
  /// Number of rows indicate number of constraints, columns are
  /// 1 (constant) + numDynSymbols + number of loops
  [[nodiscard]] constexpr auto getA() const -> DensePtrMatrix<int64_t> {
    const void *ptr =
      memory + sizeof(const llvm::SCEV *const *) * numDynSymbols;
    auto *p = (int64_t *)const_cast<void *>(ptr);
    return {p, math::DenseDims<>{math::row(numConstraints),
                                 math::col(numLoops + numDynSymbols + 1)}};
  };
  [[nodiscard]] constexpr auto
  getOuterA(ptrdiff_t subLoop) -> MutPtrMatrix<int64_t> {
    const void *ptr =
      memory + sizeof(const llvm::SCEV *const *) * numDynSymbols;
    auto *p = (int64_t *)const_cast<void *>(ptr);
    ptrdiff_t numSym = numDynSymbols + 1;
    return {p, math::StridedDims<>{math::row(numConstraints),
                                   math::col(subLoop + numSym),
                                   math::stride(numLoops + numSym)}};
  };
  [[nodiscard]] constexpr auto
  getOuterA(ptrdiff_t subLoop) const -> PtrMatrix<int64_t> {
    const void *ptr =
      memory + sizeof(const llvm::SCEV *const *) * numDynSymbols;
    auto *p = (int64_t *)const_cast<void *>(ptr);
    ptrdiff_t numSym = numDynSymbols + 1;
    return {p, math::StridedDims<>{math::row(numConstraints),
                                   math::col(subLoop + numSym),
                                   math::stride(numLoops + numSym)}};
  };
  [[nodiscard]] auto getSyms() -> MutPtrVector<IR::Value *> {
    return {reinterpret_cast<IR::Value **>(memory),
            math::length(numDynSymbols)};
  }
  [[nodiscard]] auto getSyms() const -> PtrVector<IR::Value *> {
    return {
      const_cast<IR::Value **>(reinterpret_cast<IR::Value *const *>(memory)),
      math::length(numDynSymbols)};
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> ptrdiff_t {
    return numLoops;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> ptrdiff_t {
    return numDynSymbols + 1;
  }
  constexpr void truncNumInEqCon(Row<> r) {
    invariant(r < numConstraints);
    numConstraints = ptrdiff_t(r);
  }

  [[nodiscard]] static auto allocate(Arena<> *alloc, llvm::Loop *L,
                                     unsigned numCon, unsigned numLoops,
                                     unsigned numDynSym,
                                     bool nonNegative) -> Valid<Loop> {
    unsigned N = numLoops + numDynSym + 1;
    // extra capacity for adding 0 lower bounds later, see
    // `addZeroLowerBounds`.
    unsigned M = nonNegative ? numCon : numCon + numLoops;
    // extra capacity for moving loops into symbols, see `removeOuterMost`.
    unsigned symCapacity = numDynSym + numLoops - 1;
    size_t memNeeded = size_t(M) * N * sizeof(int64_t) +
                       symCapacity * sizeof(const llvm::SCEV *const *);
    auto *mem = static_cast<Loop *>(alloc->allocate(sizeof(Loop) + memNeeded));
    auto *aln = std::construct_at(mem, L, numCon, numLoops, numDynSym, M);
    return Valid<Loop>{aln};
  }
  [[nodiscard]] static auto allocate(Arena<> *alloc, llvm::Loop *L,
                                     unsigned numCon, unsigned numLoops,
                                     math::PtrVector<IR::Value *> syms,
                                     bool nonNegative) -> Valid<Loop> {

    unsigned numDynSym = syms.size();
    Valid<Loop> aln =
      allocate(alloc, L, numCon, numLoops, numDynSym, nonNegative);
    std::copy_n(syms.begin(), numDynSym, aln->getSyms().begin());
    return aln;
  }
  explicit constexpr Loop(llvm::Loop *loop, unsigned _numConstraints,
                          unsigned _numLoops, unsigned _numDynSymbols,
                          bool _nonNegative)
    : L(loop), numConstraints(_numConstraints), numLoops(_numLoops),
      numDynSymbols(_numDynSymbols), nonNegative(_nonNegative) {}
};
} // namespace poly
#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif

inline auto operator<<(std::ostream &os, const Loop &L) -> std::ostream & {
  if (L.getCurrentDepth() > 0) {
    alloc::OwningArena<> alloc{};
    ::poly::Loop *tmp = L.getAffineLoop()->copy(&alloc);
    tmp->pruneBounds(alloc);
    for (ptrdiff_t i = tmp->getNumLoops(), d = L.getCurrentDepth(); tmp;) {
      invariant((i == tmp->getNumLoops()));
      if (i-- == d) {
        tmp->printBounds(os << "Loop " << i << ": ");
        break;
      }
      tmp = tmp->removeLoop(&alloc, i);
    }
  } else os << "Top Level:";
  return os;
}
inline void printShort(std::ostream &os, const Addr *A);
// NOLINTNEXTLINE(misc-no-recursion)
DEBUGUSED inline void dumpGraph(std::ostream &os, Node *N) {
  /// Loop `currentDepth1` gives the depth of the loop's contents,
  /// but its placement is 1 less. So the outermost loop has depth 1,
  /// even though it is at top level.
  for (int i = 0, D = N->getCurrentDepth() - (N->getKind() == Node::VK_Loop);
       i < D; ++i)
    os << "  ";
  if (const auto *A = llvm::dyn_cast<Addr>(N)) printShort(os, A);
  else if (const auto *C = llvm::dyn_cast<Compute>(N)) os << *C;
  else if (const auto *L = llvm::dyn_cast<Loop>(N))
    dumpGraph(os << *L << "\n", L->getChild());
  else if (const auto *P = llvm::dyn_cast<Phi>(N)) P->dump(os);
  os << "\n";
  if (IR::Node *V = N->getNext()) dumpGraph(os, V);
}
DEBUGUSED inline void dumpGraph(Node *N) { dumpGraph(std::cout, N); };

inline void printDotName(std::ostream &os, const Addr *A) {
  if (A->isLoad()) os << "... = ";
  os << *A->getArrayPointer();
  DensePtrMatrix<int64_t> I{A->indexMatrix()};
  DensePtrMatrix<int64_t> B{A->offsetMatrix()};
  PtrVector<int64_t> b{A->getOffsetOmega()};
  ptrdiff_t num_loops = ptrdiff_t(I.numCol());
  for (ptrdiff_t i = 0; i < I.numRow(); ++i) {
    if (i) os << ", ";
    bool print_plus = false;
    for (ptrdiff_t j = 0; j < num_loops; ++j) {
      if (int64_t Aji = I[i, j]) {
        if (print_plus) {
          if (Aji <= 0) {
            Aji *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (Aji != 1) os << Aji << '*';
        os << "i_" << j;
        print_plus = true;
      }
    }
    for (ptrdiff_t j = 0; j < B.numCol(); ++j) {
      if (int64_t offij = j ? B[i, j] : b[i]) {
        if (print_plus) {
          if (offij <= 0) {
            offij *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (j) {
          if (offij != 1) os << offij << '*';
          os << *A->getAffLoop()->getSyms()[j - 1];
        } else os << offij;
        print_plus = true;
      }
    }
  }
  os << "]";
  if (A->isStore()) os << " = ...";
}
inline void printSubscripts(std::ostream &os, const Addr *A) {
  os << "[";
  DensePtrMatrix<int64_t> I{A->indexMatrix()};
  ptrdiff_t num_loops = ptrdiff_t(I.numCol());
  DensePtrMatrix<int64_t> offs = A->offsetMatrix();
  for (ptrdiff_t i = 0; i < I.numRow(); ++i) {
    if (i) os << ", ";
    bool print_plus = false;
    for (ptrdiff_t j = 0; j < num_loops; ++j) {
      if (int64_t Aji = I[i, j]) {
        if (print_plus) {
          if (Aji <= 0) {
            Aji *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (Aji != 1) os << Aji << '*';
        os << "i_" << j;
        print_plus = true;
      }
    }
    for (ptrdiff_t j = 0; j < offs.numCol(); ++j) {
      if (int64_t offij = offs[i, j]) {
        if (print_plus) {
          if (offij <= 0) {
            offij *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (j) {
          if (offij != 1) os << offij << '*';
          os << A->getAffLoop()->getSyms()[j - 1];
        } else os << offij;
        print_plus = true;
      }
    }
  }
  os << "]";
}

inline void printShort(std::ostream &os, const Addr *A) {
  if (A->isLoad()) A->printName(os) << " = ";
  os << A->getArray().name();
  printSubscripts(os, A);
  if (!A->isLoad()) A->getStoredVal()->printName(os << " = ");
}
inline auto operator<<(std::ostream &os, const Addr &m) -> std::ostream & {
  if (m.isLoad()) os << "Load: ";
  else os << "Store: ";
  DensePtrMatrix<int64_t> I{m.indexMatrix()};
  // os << *m.getInstruction();
  os << "\nArrayIndex " << *m.getArrayPointer() << " (dim = " << m.numDim()
     << ", natural depth: " << m.getNaturalDepth();
  if (m.numDim()) os << ", element size: " << *m.getSizes().back();
  os << "):\n";
  os << "Sizes: [";
  if (m.numDim()) {
    os << " unknown";
    for (ptrdiff_t i = 0; i < ptrdiff_t(I.numRow()) - 1; ++i)
      os << ", " << *m.getSizes()[i];
  }
  printSubscripts(os << "]\nSubscripts: ", &m);
  return os << "\nInitial Fusion Omega: " << m.getFusionOmega()
            << "\npoly::Loop:" << *m.getAffLoop();
}

} // namespace IR
