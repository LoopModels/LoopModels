#pragma once

#include "./RemarkAnalysis.hpp"
#include "Math/Array.hpp"
#include "Math/Comparators.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Constraints.hpp"
#include "Math/Indexing.hpp"
#include "Math/Math.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Polyhedra.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Optional.hpp"
#include "Utilities/Valid.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <utility>

inline auto isKnownOne(llvm::ScalarEvolution &SE, llvm::Value *v) -> bool {
  return v && SE.getSCEV(v)->isOne();
}

[[nodiscard]] inline auto getBackedgeTakenCount(llvm::ScalarEvolution &SE,
                                                llvm::Loop *L)
  -> const llvm::SCEV * {
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

inline auto getConstantInt(const llvm::SCEV *v) -> std::optional<int64_t> {
  if (const auto *sc = llvm::dyn_cast<const llvm::SCEVConstant>(v)) {
    llvm::ConstantInt *c = sc->getValue();
    // we need bit width of 64, for sake of negative numbers
    if (c->getBitWidth() <= 64) return c->getSExtValue();
  }
  return {};
}

template <typename T>
inline auto findFirst(llvm::ArrayRef<T> v, const T &x) -> Optional<size_t> {
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] == x) return i;
  return {};
}

/// returns 1-based index, to match the pattern we use where index 0 refers to a
/// constant offset this function returns 0 if S not found in `symbols`.
[[nodiscard]] inline auto
findSymbolicIndex(llvm::ArrayRef<const llvm::SCEV *> symbols,
                  const llvm::SCEV *S) -> size_t {
  for (size_t i = 0; i < symbols.size();)
    if (symbols[i++] == S) return i;
  return 0;
}

[[nodiscard]] inline auto getMinMaxValueSCEV(llvm::ScalarEvolution &SE,
                                             const llvm::SCEVAddRecExpr *S)
  -> std::pair<const llvm::SCEV *, const llvm::SCEV *> {
  // if (!SE.containsAddRecurrence(S))
  // 	return S;
  if ((!S) || (!(S->isAffine()))) return std::make_pair(S, S);
  const auto *opStart = S->getStart();
  const auto *opStep = S->getStepRecurrence(SE);
  const auto *opFinal = SE.getSCEVAtScope(S, nullptr);
  // auto opFinal = SE.getSCEVAtScope(S, S->getLoop()->getParentLoop());
  // FIXME: what if there are more AddRecs nested inside?
  if (SE.isKnownNonNegative(opStep)) return std::make_pair(opStart, opFinal);
  if (SE.isKnownNonPositive(opStep)) return std::make_pair(opFinal, opStart);
  return std::make_pair(S, S);
}
// TODO: strengthen through recursion
[[nodiscard]] inline auto getMinMaxValueSCEV(llvm::ScalarEvolution &SE,
                                             const llvm::SCEV *S)
  -> std::pair<const llvm::SCEV *, const llvm::SCEV *> {
  if (const auto *T = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
    return getMinMaxValueSCEV(SE, T);
  return std::make_pair(S, S);
}
[[nodiscard]] inline auto simplifyMinMax(llvm::ScalarEvolution &SE,
                                         const llvm::SCEVMinMaxExpr *S)
  -> const llvm::SCEV * {
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
[[nodiscard]] inline auto simplifyMinMax(llvm::ScalarEvolution &SE,
                                         const llvm::SCEV *S)
  -> const llvm::SCEV * {
  if (const auto *MM = llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(S))
    return simplifyMinMax(SE, MM);
  return S;
}

namespace loopNestCtor {
/// add a symbol to row `r` of A
/// we try to break down value `v`, so that adding
/// N, N - 1, N - 3 only adds the variable `N`, and adds the constant
/// offsets
inline void addSymbol(IntMatrix &A,
                      llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                      const llvm::SCEV *v, Range<size_t, size_t> lu,
                      int64_t mlt) {
  assert(lu.size());
  symbols.push_back(v);
  A.resize(A.numCol() + 1);
  A(lu, symbols.size()) << mlt;
}
inline auto addRecMatchesLoop(const llvm::SCEV *S, llvm::Loop *L) -> bool {
  if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S))
    return x->getLoop() == L;
  return false;
}
[[nodiscard]] inline auto
addSymbol(std::array<IntMatrix, 2> &AB, // NOLINT(misc-no-recursion)
          llvm::SmallVectorImpl<const llvm::SCEV *> &symbols, llvm::Loop *L,
          const llvm::SCEV *v, llvm::ScalarEvolution &SE,
          Range<size_t, size_t> lu, int64_t mlt, size_t minDepth) -> size_t {
  auto &[A, B] = AB;
  // first, we check if `v` in `Symbols`
  if (size_t i = findSymbolicIndex(symbols, v)) {
    A(lu, i) += mlt;
    return minDepth;
  }
  if (std::optional<int64_t> c = getConstantInt(v)) {
    A(lu, 0) += mlt * (*c);
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
    if (auto op0 = getConstantInt(m->getOperand(0)))
      return addSymbol(AB, symbols, L, m->getOperand(1), SE, lu, mlt * (*op0),
                       minDepth);
    if (auto op1 = getConstantInt(m->getOperand(1)))
      return addSymbol(AB, symbols, L, m->getOperand(0), SE, lu, mlt * (*op1),
                       minDepth);
  } else if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(v)) {
    size_t recDepth = x->getLoop()->getLoopDepth();
    if (x->isAffine()) {
      minDepth =
        addSymbol(AB, symbols, L, x->getOperand(0), SE, lu, mlt, minDepth);
      if (auto opc = getConstantInt(x->getOperand(1))) {
        // swap order vs recDepth to go inner<->outer
        B(lu, recDepth - 1) << mlt * (*opc);
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
      Row Mp = M + lu.size();
      A.resize(Mp);
      B.resize(Mp);
      A(_(M, Mp), _) = A(lu, _);
      B(_(M, Mp), _) = B(lu, _);
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
areSymbolsLoopInvariant(IntMatrix &A,
                        llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                        llvm::Loop *L, llvm::ScalarEvolution &SE) -> bool {
  for (size_t i = 0; i < symbols.size(); ++i)
    if ((!allZero(A(_, i + 1))) && (!SE.isLoopInvariant(symbols[i], L)))
      return false;
  return true;
}
inline auto // NOLINTNEXTLINE(misc-no-recursion)
addBackedgeTakenCount(std::array<IntMatrix, 2> &AB,
                      llvm::SmallVectorImpl<const llvm::SCEV *> &symbols,
                      llvm::Loop *L, const llvm::SCEV *BT,
                      llvm::ScalarEvolution &SE, size_t minDepth,
                      llvm::OptimizationRemarkEmitter *ORE) -> size_t {
  // A contains syms
  auto &[A, B] = AB;
  Row M = A.numRow();
  A.resize(M + 1);
  B.resize(M + 1);
  minDepth = addSymbol(AB, symbols, L, BT, SE, _(M, M + 1), 1, minDepth);
  assert(A.numRow() == B.numRow());
  size_t depth = L->getLoopDepth() - 1;
  for (auto m = size_t(M); m < A.numRow(); ++m) B(m, depth) = -1; // indvar
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
            remarkAnalysis("AffineLoopConstruction", L)};
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
        remarkAnalysis("AffineLoopConstruction", L)};
      ORE->emit(analysis << os.str());
    }
  }
  return std::max(depth, minDepth);
}
} // namespace loopNestCtor

// A * x >= 0
// if constexpr(NonNegative)
//   x >= 0
template <bool NonNegative = true>
struct AffineLoopNest
  : BasePolyhedra<false, true, NonNegative, AffineLoopNest<NonNegative>> {
  using BaseT =
    BasePolyhedra<false, true, NonNegative, AffineLoopNest<NonNegative>>;

  static inline auto construct(BumpAlloc<> &alloc, llvm::Loop *L,
                               const llvm::SCEV *BT, llvm::ScalarEvolution &SE,
                               llvm::OptimizationRemarkEmitter *ORE = nullptr)
    -> NotNull<AffineLoopNest> {
    // A holds symbols
    // B holds loop bounds
    // they're separate so we can grow them independently
    std::array<IntMatrix, 2> AB;
    auto &[A, B] = AB;
    // once we're done assembling these, we'll concatenate A and B
    size_t maxDepth = L->getLoopDepth();
    // size_t maxNumSymbols = BT->getExpressionSize();
    A.resizeForOverwrite(
      StridedDims{0, 1, unsigned(1) + BT->getExpressionSize()});
    B.resizeForOverwrite(StridedDims{0, maxDepth, maxDepth});
    llvm::SmallVector<const llvm::SCEV *, 3> symbols;
    size_t minDepth =
      loopNestCtor::addBackedgeTakenCount(AB, symbols, L, BT, SE, 0, ORE);
    // We first check for loops in B that are shallower than minDepth
    // we include all loops such that L->getLoopDepth() > minDepth
    // note that the outer-most loop has a depth of 1.
    // We turn these loops into `getAddRecExprs`s, so that we can
    // add them as variables to `A`.
    for (size_t d = 0; d < minDepth; ++d) {
      // loop at depth d+1
      llvm::Loop *P = nullptr;
      // search B(_,d) for references
      for (size_t i = 0; i < B.numRow(); ++i) {
        // TODO; confirm `last` vs `end`
        if (int64_t Bid = B(i, d)) {
          if (!P) { // find P
            P = L;
            for (size_t r = d + 1; r < maxDepth; ++r) P = P->getParentLoop();
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
    invariant(1 + symbols.size(), size_t(A.numCol()));
    size_t depth = maxDepth - minDepth;
    unsigned numConstraints = unsigned(A.numRow()), N = unsigned(A.numCol());
    NotNull<AffineLoopNest> aln{
      AffineLoopNest::allocate(alloc, numConstraints, depth, symbols)};
    aln->getA()(_, _(0, N)) << A;
    // copy the included loops from B
    // we use outer <-> inner order, so we skip unsupported outer loops.
    aln->getA()(_, _(N, N + depth)) << B(_, _(end - depth, end));
    return aln;
    // addZeroLowerBounds();
    // NOTE: pruneBounds() is not legal here if we wish to use
    // removeInnerMost later.
    // pruneBounds();
  }

  auto findIndex(const llvm::SCEV *v) const -> size_t {
    return findSymbolicIndex(getSyms(), v);
  }
  /// A.rotate( R )
  /// A(_,const) + A(_,var)*var >= 0
  /// this method applies rotation matrix R
  /// A(_,const) + (A(_,var)*R)*(R^{-1}*var) >= 0
  /// So that our new loop nest has matrix
  /// [A(_,const) (A(_,var)*R)]
  /// while the new `var' is `(R^{-1}*var)`
  [[nodiscard]] auto rotate(BumpAlloc<> &alloc, PtrMatrix<int64_t> R) const
    -> NotNull<AffineLoopNest<false>> {
    size_t numExtraVar = 0;
    if constexpr (NonNegative) numExtraVar = getNumLoops();
    assert(R.numCol() == numExtraVar);
    assert(R.numRow() == numExtraVar);
    const size_t numConst = this->getNumSymbols();
    MutDensePtrMatrix<int64_t> A{getA()};
    const auto [M, N] = A.size();
    auto syms{getSyms()};
    NotNull<AffineLoopNest<false>> aln{AffineLoopNest<false>::allocate(
      alloc, size_t(M) + numExtraVar, numLoops, syms)};
    auto B{aln->getA()};
    invariant(B.numRow(), M + numExtraVar);
    invariant(B.numCol(), N);
    B(_(0, M), _(begin, numConst)) << A(_, _(begin, numConst));
    B(_(0, M), _(numConst, end)) << A(_, _(numConst, end)) * R;
    if constexpr (NonNegative) {
      B(_(M, end), _(0, numConst)) << 0;
      B(_(M, end), _(numConst, end)) << R;
    }
    // ret->initializeComparator();
    aln->pruneBounds(alloc);
    return aln;
  }
  /// like rotate(identity Matrix)
  [[nodiscard]] auto explicitLowerBounds(BumpAlloc<> &alloc)
    -> NotNull<AffineLoopNest<false>> {
    if constexpr (!NonNegative) return this;
    const size_t numExtraVar = getNumLoops();
    const size_t numConst = this->getNumSymbols();
    auto A{getA()};
    const auto [M, N] = A.size();
    auto symbols{getSyms()};
    NotNull<AffineLoopNest<false>> ret{AffineLoopNest<false>::allocate(
      alloc, M + numExtraVar, numLoops, symbols)};
    auto B{ret->getA()};
    B(_(0, M), _) << A;
    B(_(M, end), _) << 0;
    B(_(M, end), _(numConst, end)).diag() << 1;
    // ret->initializeComparator();
    ret->pruneBounds(alloc);
    return ret;
  }

  // static auto construct(llvm::Loop *L, llvm::ScalarEvolution &SE)
  //   -> AffineLoopNest<NonNegative>* {
  //   auto BT = getBackedgeTakenCount(SE, L);
  //   if (!BT || llvm::isa<llvm::SCEVCouldNotCompute>(BT)) return nullptr;
  //   return AffineLoopNest<NonNegative>(L, BT, SE);
  // }

  [[nodiscard]] auto removeInnerMost(BumpAlloc<> &alloc) const
    -> NotNull<AffineLoopNest<NonNegative>> {
    // order is outer<->inner
    auto A{getA()};
    auto ret = AffineLoopNest<NonNegative>::allocate(
      alloc, unsigned(A.numRow()), getNumLoops() - 1, getSyms());
    MutPtrMatrix<int64_t> B{ret->getA()};
    B << A(_, _(0, last));
    // no loop may be conditioned on the innermost loop, so we should be able to
    // safely remove all constraints that reference it
    for (Row m = B.numRow(); m--;) {
      if (A(m, last)) {
        if (m != B.numRow() - 1) B(m, _) << B(last, _);
        B.truncate(B.numRow() - 1);
      }
    }
    ret->truncateConstraints(unsigned(B.numRow()));
    return ret;
  }
  constexpr void truncateConstraints(unsigned newNumConstraints) {
    assert(newNumConstraints <= numConstraints);
    numConstraints = newNumConstraints;
  }
  constexpr void clear() {
    numConstraints = 0;
    numLoops = 0;
    numDynSymbols = 0;
  }
  void removeOuterMost(size_t numToRemove, llvm::Loop *L,
                       llvm::ScalarEvolution &SE) {
    // basically, we move the outermost loops to the symbols section,
    // and add the appropriate addressees
    // order is outer<->inner
    size_t oldNumLoops = getNumLoops();
    if (numToRemove >= oldNumLoops) return clear();
    size_t innermostLoopInd = getNumSymbols();
    size_t numRemainingLoops = oldNumLoops - numToRemove;
    auto A{getA()};
    auto [M, N] = A.size();
    if (numRemainingLoops != numToRemove) {
      Vector<int64_t> tmp;
      if (numRemainingLoops > numToRemove) {
        tmp.resizeForOverwrite(numToRemove);
        for (size_t m = 0; m < M; ++m) {
          // fill tmp
          tmp << A(m, _(innermostLoopInd + numRemainingLoops, N));
          for (size_t i = innermostLoopInd;
               i < numRemainingLoops + innermostLoopInd; ++i)
            A(m, i + numToRemove) = A(m, i);
          A(m, _(numToRemove + innermostLoopInd, N)) << tmp;
        }
      } else {
        tmp.resizeForOverwrite(numRemainingLoops);
        for (size_t m = 0; m < M; ++m) {
          // fill tmp
          tmp = A(m, _(innermostLoopInd, innermostLoopInd + numRemainingLoops));
          for (size_t i = innermostLoopInd; i < numToRemove + innermostLoopInd;
               ++i)
            A(m, i) = A(m, i + numRemainingLoops);
          A(m, _(numToRemove + innermostLoopInd, N)) << tmp;
        }
      }
    } else
      for (size_t m = 0; m < M; ++m)
        for (size_t i = 0; i < numToRemove; ++i)
          std::swap(A(m, innermostLoopInd + i),
                    A(m, innermostLoopInd + i + numToRemove));

    for (size_t i = 0; i < numRemainingLoops; ++i) L = L->getParentLoop();
    // L is now inner most loop getting removed
    size_t oldNumDynSymbols = numDynSymbols;
    numDynSymbols += numToRemove;
    auto S{getSyms()};
    for (size_t i = 0; i < numToRemove; ++i) {
      llvm::Type *intTyp = L->getInductionVariable(SE)->getType();
      S[i + oldNumDynSymbols] = SE.getAddRecExpr(
        SE.getZero(intTyp), SE.getOne(intTyp), L, llvm::SCEV::NoWrapMask);
    }
    numLoops = numRemainingLoops;
    // initializeComparator();
  }

  void addZeroLowerBounds(LinAlg::Alloc<int64_t> auto &alloc) {
    if (this->isEmpty()) return;
    if constexpr (NonNegative) return this->pruneBounds(alloc);
    // return initializeComparator();
    if (!numLoops) return;
    size_t M = numConstraints;
    numConstraints += numLoops;
    auto A{getA()};
    A(_(M, end), _) << 0;
    for (size_t i = 0; i < numLoops; ++i) A(M + i, end - numLoops + i) = 1;
    this->pruneBounds(alloc);
  }

  [[nodiscard]] constexpr auto getProgVars(size_t j) const
    -> PtrVector<int64_t> {
    return getA()(j, _(0, getNumSymbols()));
  }
  [[nodiscard]] constexpr auto copy(BumpAlloc<> &alloc) const
    -> NotNull<AffineLoopNest<NonNegative>> {
    auto ret = AffineLoopNest<NonNegative>::allocate(alloc, numConstraints,
                                                     numLoops, getSyms());
    ret->getA() << getA();
    return ret;
  }
  [[nodiscard]] constexpr auto removeLoop(BumpAlloc<> &alloc, size_t v) const
    -> AffineLoopNest<NonNegative> * {
    auto A{getA()};
    v += getNumSymbols();
    auto zeroNegPos = indsZeroNegPos(A(_, v));
    auto &[zer, neg, pos] = zeroNegPos;
    unsigned numCon =
      unsigned(A.numRow()) - pos.size() + neg.size() * pos.size();
    if constexpr (!NonNegative) numCon -= neg.size();
    auto p = checkpoint(alloc);
    auto ret = AffineLoopNest<NonNegative>::allocate(alloc, numCon,
                                                     numLoops - 1, getSyms());
    ret->numConstraints = unsigned(
      fourierMotzkinCore<NonNegative>(ret->getA(), getA(), v, zeroNegPos));
    ret->pruneBounds(alloc);
    if (ret->getNumLoops() == 0) {
      rollback(alloc, p);
      return nullptr;
    }
    // either we remove one loop, or remaining loops are empty
    assert((ret->getNumLoops() == getNumLoops() - 1) && "didn't remove loop");
    return ret;
  }
  auto perm(PtrVector<unsigned> x)
    -> llvm::SmallVector<AffineLoopNest<NonNegative>, 0> {
    llvm::SmallVector<AffineLoopNest<NonNegative>, 0> ret;
    // llvm::SmallVector<UnboundedAffineLoopNest, 0> ret;
    ret.resize_for_overwrite(x.size());
    ret.back() = *this;
    for (size_t i = x.size() - 1; i != 0;) {
      AffineLoopNest<NonNegative> &prev = ret[i];
      size_t oldi = i;
      ret[--i] = prev.removeLoop(x[oldi]);
    }
    return ret;
  }
  [[nodiscard]] auto bounds(size_t i) const -> std::array<IntMatrix, 2> {
    auto A{getA()};
    const auto [numNeg, numPos] = countSigns(A, i);
    std::array<IntMatrix, 2> ret{{numNeg, A.numCol()}, {numPos, A.numCol()}};
    size_t negCount = 0, posCount = 0;
    for (size_t j = 0; j < A.numRow(); ++j)
      if (int64_t Aji = A(j, i))
        (ret[Aji > 0])(Aji < 0 ? negCount++ : posCount++, _) = A(j, _);
    return ret;
  }
  // auto getBounds(BumpAlloc<> &alloc, PtrVector<unsigned> x)
  //   -> llvm::SmallVector<std::pair<IntMatrix, IntMatrix>, 0> {
  //   llvm::SmallVector<std::pair<IntMatrix, IntMatrix>, 0> ret;
  //   size_t i = x.size();
  //   ret.resize_for_overwrite(i);
  //   auto check = alloc.checkpoint();
  //   auto *tmp = this;
  //   while (true) {
  //     size_t xi = x[--i];
  //     ret[i] = tmp->bounds(xi);
  //     if (i == 0) break;
  //     tmp = tmp->removeLoop(alloc, xi);
  //   }
  //   alloc.rollback(check);
  //   return ret;
  // }
  constexpr void eraseConstraint(size_t c) {
    eraseConstraintImpl(getA(), c);
    --numConstraints;
  }
  [[nodiscard]] auto
  zeroExtraItersUponExtending(LinAlg::Alloc<int64_t> auto &alloc, size_t _i,
                              bool extendLower) const -> bool {
    auto p = alloc.scope();
    AffineLoopNest<NonNegative> *tmp = copy(alloc);
    // question is, does the inner most loop have 0 extra iterations?
    const size_t numPrevLoops = getNumLoops() - 1;
    // we changed the behavior of removeLoop to actually drop loops that are
    // no longer present.
    for (size_t i = 0; i < numPrevLoops - 1; ++i)
      tmp = tmp->removeLoop(alloc, i >= _i);
    // for (size_t i = 0; i < numPrevLoops; ++i)
    //   if (i != _i) tmp = tmp->removeLoop(alloc, i);
    // loop _i is now loop 0
    // innermost loop is now loop 1
    bool indep = true;
    const size_t numConst = getNumSymbols();
    auto A{tmp->getA()};
    for (size_t n = 0; n < A.numRow(); ++n)
      if ((A(n, numConst) != 0) && (A(n, 1 + numConst) != 0)) indep = false;
    if (indep) return false;
    AffineLoopNest<NonNegative> *margi = tmp->removeLoop(alloc, 1);
    AffineLoopNest<NonNegative> *tmp2;
    invariant(margi->getNumLoops(), size_t(1));
    invariant(tmp->getNumLoops(), size_t(2));
    invariant(margi->getA().numCol() + 1, tmp->getA().numCol());
    // margi contains extrema for `_i`
    // we can substitute extended for value of `_i`
    // in `tmp`
    auto p2 = alloc.checkpoint();
    int64_t sign = 2 * extendLower - 1; // extendLower ? 1 : -1
    for (size_t c = 0; c < margi->getNumInequalityConstraints(); ++c) {
      int64_t b = sign * margi->getA()(c, numConst);
      if (b <= 0) continue;
      alloc.rollback(p2);
      tmp2 = tmp->copy(alloc);
      invariant(tmp2->getNumLoops(), size_t(2));
      invariant(margi->getNumLoops() + 1, tmp2->getNumLoops());
      // increment to increase bound
      // this is correct for both extending lower and extending upper
      // lower: a'x + i + b >= 0 -> i >= -a'x - b
      // upper: a'x - i + b >= 0 -> i <=  a'x + b
      // to decrease the lower bound or increase the upper, we increment
      // `b`
      ++(margi->getA())(c, 0);
      // our approach here is to set `_i` equal to the extended bound
      // and then check if the resulting polyhedra is empty.
      // if not, then we may have >0 iterations.
      for (size_t cc = 0; cc < tmp2->getNumCon(); ++cc) {
        if (int64_t d = tmp2->getA()(cc, numConst)) {
          tmp2->getA()(cc, _(0, last)) << b * tmp2->getA()(cc, _(0, last)) -
                                            (d * sign) * margi->getA()(c, _);
        }
      }
      for (size_t cc = size_t(tmp2->getNumCon()); cc;)
        if (tmp2->getA()(--cc, 1 + numConst) == 0) tmp2->eraseConstraint(cc);
      if (!(tmp2->calcIsEmpty(alloc))) return false;
    }
    if constexpr (NonNegative) {
      if (extendLower) {
        // increment to increase bound
        // this is correct for both extending lower and extending upper
        // lower: a'x + i + b >= 0 -> i >= -a'x - b
        // upper: a'x - i + b >= 0 -> i <=  a'x + b
        // to decrease the lower bound or increase the upper, we
        // increment `b` our approach here is to set `_i` equal to the
        // extended bound and then check if the resulting polyhedra is
        // empty. if not, then we may have >0 iterations.
        for (size_t cc = 0; cc < tmp->getNumCon(); ++cc) {
          if (int64_t d = tmp->getA()(cc, numConst)) {
            // lower bound is i >= 0
            // so setting equal to the extended lower bound now
            // means that i = -1 so we decrement `d` from the column
            tmp->getA()(cc, 0) -= d;
            tmp->getA()(cc, numConst) = 0;
          }
        }
        for (size_t cc = size_t(tmp->getNumCon()); cc;)
          if (tmp->getA()(--cc, 1 + numConst) == 0) tmp->eraseConstraint(cc);
        if (!(tmp->calcIsEmpty(alloc))) return false;
      }
    }
    return true;
  }

  auto printSymbol(llvm::raw_ostream &os, PtrVector<int64_t> x,
                   int64_t mul) const -> bool {
    bool printed = false;
    for (size_t i = 1; i < x.size(); ++i)
      if (int64_t xi = x[i] * mul) {
        if (printed) os << (xi > 0 ? " + " : " - ");
        printed = true;
        int64_t absxi = constexpr_abs(xi);
        if (absxi != 1) os << absxi << " * ";
        os << *getSyms()[i - 1];
      }
    if (int64_t x0 = x[0]) {
      if (printed) os << (mul * x0 > 0 ? " + " : " - ") << constexpr_abs(x0);
      else os << mul * x[0];
    }
    return printed;
  }
  constexpr void setNumConstraints(size_t numCon) { numConstraints = numCon; }
  static constexpr void setNumEqConstraints(size_t) {}
  constexpr void decrementNumConstraints() { --numConstraints; }

  void printBound(llvm::raw_ostream &os, int64_t sign, size_t numVarMinus1,
                  size_t numConst, size_t j) const {
    PtrVector<int64_t> b = getProgVars(j);
    DensePtrMatrix<int64_t> A{getA()};
    bool printed = printSymbol(os, b, -sign);
    for (size_t k = 0; k < numVarMinus1; ++k) {
      if (int64_t lakj = A(j, k + numConst)) {
        if (lakj * sign > 0) os << " - ";
        else if (printed) os << " + ";
        lakj = constexpr_abs(lakj);
        if (lakj != 1) os << lakj << "*";
        os << "i_" << k;
        printed = true;
      }
    }
    if (!printed) os << 0;
  }
  void printBoundShort(llvm::raw_ostream &os, int64_t sign, size_t numVarMinus1,
                       size_t numConst, int64_t allAj, size_t numRow,
                       bool separateLines) const {
    bool isUpper = sign < 0;
    if (separateLines || isUpper) {
      if (allAj == 1) os << "i_" << numVarMinus1;
      else os << allAj << "*i_" << numVarMinus1;
      os << (isUpper ? " <= " : " >= ");
    }
    if (numRow > 1) os << (isUpper ? "min(" : "max(");
    DensePtrMatrix<int64_t> A{getA()};
    for (size_t j = 0, k = 0; j < A.numRow(); ++j) {
      if (A(j, last) * sign <= 0) continue;
      if (k++) os << ", ";
      printBound(os, sign, numVarMinus1, numConst, j);
    }
    if constexpr (NonNegative) {
      if (!isUpper) os << ", 0";
    }
    if (numRow > 1) os << ")";
    if (!(separateLines || isUpper)) os << " <= ";
  }
  // prints the inner most loop.
  // it is assumed that you iteratively pop off the inner most loop with
  // `removeLoop` to print all bounds.

  void printBound(llvm::raw_ostream &os, int64_t sign) const {
    const size_t numVar = getNumLoops();
    if (numVar == 0) return;
    const size_t numVarM1 = numVar - 1, numConst = getNumSymbols();
    bool hasPrintedLine = NonNegative && (sign == 1), isUpper = sign < 0;
    DensePtrMatrix<int64_t> A{getA()};
    size_t numRow = 0;
    int64_t allAj = 0;
    for (size_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A(j, last);
      int64_t Aj = Ajr * sign;
      if (Aj <= 0) continue;
      if (allAj) allAj = allAj == Aj ? allAj : -1;
      else allAj = Aj;
      ++numRow;
    }
    if (numRow == 0) {
      if constexpr (NonNegative)
        if (!isUpper) os << "i_" << numVarM1 << " >= 0";
      return;
    }
    if constexpr (NonNegative)
      if (!isUpper) ++numRow;
    if (allAj > 0)
      return printBoundShort(os, sign, numVarM1, numConst, allAj, numRow, true);
    for (size_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A(j, end - 1);
      int64_t Aj = Ajr * sign;
      if (Aj <= 0) continue;
      if (hasPrintedLine)
        for (size_t k = 0; k < 21; ++k) os << ' ';
      hasPrintedLine = true;
      if (Ajr != sign)
        os << Aj << "*i_" << numVarM1 << (isUpper ? " <= " : " >= ");
      else os << "i_" << numVarM1 << (isUpper ? " <= " : " >= ");
      printBound(os, sign, numVarM1, numConst, j);
      os << "\n";
    }
    if constexpr (NonNegative)
      if (!isUpper) os << "i_" << numVarM1 << " >= 0\n";
  }
  void printBounds(llvm::raw_ostream &os) const {
    const size_t numVar = getNumLoops();
    if (numVar == 0) return;
    DensePtrMatrix<int64_t> A{getA()};
    int64_t allAj = 0;
    size_t numPos = 0, numNeg = 0;
    for (size_t j = 0; j < A.numRow(); ++j) {
      int64_t Ajr = A(j, last);
      if (Ajr == 0) continue;
      numPos += Ajr > 0;
      numNeg += Ajr < 0;
      int64_t x = std::abs(Ajr);
      if (allAj) allAj = allAj == x ? allAj : -1;
      else allAj = x;
    }
    if (allAj > 0) {
      size_t numVarMinus1 = numVar - 1, numConst = getNumSymbols();
      if constexpr (NonNegative) ++numPos;
      printBoundShort(os, 1, numVarMinus1, numConst, allAj, numPos, false);
      printBoundShort(os, -1, numVarMinus1, numConst, allAj, numNeg, false);
    } else {
      printBound(os, 1);
      printBound(os << " && ", -1);
    }
  }
  void dump(llvm::raw_ostream &os, BumpAlloc<> &alloc) const {
    const AffineLoopNest *tmp = this;
    for (size_t i = getNumLoops(); tmp;) {
      assert((i == tmp->getNumLoops()) && "loop count mismatch");
      tmp->printBounds(os << "\nLoop " << --i << ": ");
      if (!i) break;
      tmp = tmp->removeLoop(alloc, i);
    }
  }

  // prints loops from inner most to outer most.
  // outer most loop is `i_0`, subscript increments for each level inside
  // We pop off the outer most loop on every iteration.
  friend inline auto operator<<(llvm::raw_ostream &os,
                                const AffineLoopNest &aln)
    -> llvm::raw_ostream & {
    BumpAlloc<> alloc;
    aln.dump(os, alloc);
    return os;
  }
#ifndef NDEBUG
  [[gnu::used]] void dump() const { llvm::errs() << *this; }
#endif
  [[nodiscard]] constexpr auto getNumCon() const -> unsigned {
    return numConstraints;
  }
  [[nodiscard]] constexpr auto getA() -> MutDensePtrMatrix<int64_t> {
    char *ptr = memory;
    return {reinterpret_cast<int64_t *>(
              ptr + sizeof(const llvm::SCEV *const *) * numDynSymbols),
            DenseDims{numConstraints, numLoops + numDynSymbols + 1}};
  };
  [[nodiscard]] constexpr auto getA() const -> DensePtrMatrix<int64_t> {
    const char *ptr = memory;
    return {const_cast<int64_t *>(reinterpret_cast<const int64_t *>(
              ptr + sizeof(const llvm::SCEV *const *) * numDynSymbols)),
            DenseDims{numConstraints, numLoops + numDynSymbols + 1}};
  };
  [[nodiscard]] constexpr auto getSyms()
    -> llvm::MutableArrayRef<const llvm::SCEV *> {
    return {reinterpret_cast<const llvm::SCEV **>(memory), numDynSymbols};
  }
  [[nodiscard]] constexpr auto getSyms() const
    -> llvm::ArrayRef<const llvm::SCEV *> {
    return {reinterpret_cast<const llvm::SCEV *const *>(memory), numDynSymbols};
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return numLoops;
  }
  [[nodiscard]] constexpr auto getNumSymbols() const -> unsigned {
    return numDynSymbols + 1;
  }
  constexpr void truncNumInEqCon(Row r) {
    invariant(r < numConstraints);
    numConstraints = unsigned(r);
  }

  [[nodiscard]] static auto construct(BumpAlloc<> &alloc, PtrMatrix<int64_t> A,
                                      llvm::ArrayRef<const llvm::SCEV *> syms)
    -> AffineLoopNest * {
    unsigned numLoops = unsigned(A.numCol()) - 1 - syms.size();
    AffineLoopNest *aln = allocate(alloc, unsigned(A.numRow()), numLoops, syms);
    aln->getA() << A;
    return aln;
  }
  [[nodiscard]] static auto allocate(BumpAlloc<> &alloc, unsigned int numCon,
                                     unsigned int numLoops,
                                     llvm::ArrayRef<const llvm::SCEV *> syms)
    -> NotNull<AffineLoopNest> {
    unsigned numDynSym = syms.size();
    unsigned N = numLoops + numDynSym + 1;
    // extra capacity for adding 0 lower bounds later, see
    // `addZeroLowerBounds`.
    unsigned M = NonNegative ? numCon : numCon + numLoops;
    // extra capacity for moving loops into symbols, see `removeOuterMost`.
    unsigned symCapacity = numDynSym + numLoops;
    size_t memNeeded = size_t(M) * N * sizeof(int64_t) +
                       symCapacity * sizeof(const llvm::SCEV *const *);
    auto *mem = alloc.allocate(sizeof(AffineLoopNest) + memNeeded,
                               alignof(AffineLoopNest));
    auto *aln = new (mem) AffineLoopNest({numCon, numLoops, numDynSym, M});
    std::copy_n(syms.begin(), numDynSym, aln->getSyms().begin());
    return NotNull<AffineLoopNest>{aln};
  }

private:
  // AffineLoopNest(llvm::Loop *L, const llvm::SCEV *BT, llvm::ScalarEvolution
  // &SE,
  //                llvm::OptimizationRemarkEmitter *ORE = nullptr) {
  //   // static_assert(false);
  // }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  constexpr AffineLoopNest(unsigned int _numConstraints, unsigned int _numLoops,
                           unsigned int _numDynSymbols,
                           unsigned int _rowCapacity)
    : numConstraints(_numConstraints), numLoops(_numLoops),
      numDynSymbols(_numDynSymbols), rowCapacity(_rowCapacity) {}

  [[nodiscard]] constexpr auto getRowCapacity() const -> size_t {
    return rowCapacity;
  }
  [[nodiscard]] constexpr auto getSymCapacity() const -> size_t {
    return numDynSymbols + numLoops;
  }

  unsigned int numConstraints;
  unsigned int numLoops;
  unsigned int numDynSymbols;
  unsigned int rowCapacity;
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
};
