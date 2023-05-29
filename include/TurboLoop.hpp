#pragma once

#include "IR/Instruction.hpp"
#include "LoopBlock.hpp"
#include "LoopForest.hpp"
#include "Loops.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "MemoryAccess.hpp"
#include "RemarkAnalysis.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>
#include <ranges>
#include <utility>

// NOLINTNEXTLINE(misc-no-recursion)
inline auto countNumLoopsPlusLeaves(const llvm::Loop *L) -> size_t {
  const std::vector<llvm::Loop *> &subLoops = L->getSubLoops();
  if (subLoops.empty()) return 1;
  size_t numLoops = subLoops.size();
  for (const auto &SL : subLoops) numLoops += countNumLoopsPlusLeaves(SL);
  return numLoops;
}
template <typename T>
concept LoadOrStoreInst =
  std::same_as<llvm::LoadInst, std::remove_cvref_t<T>> ||
  std::same_as<llvm::StoreInst, std::remove_cvref_t<T>>;

class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
  [[no_unique_address]] llvm::SmallVector<NotNull<LoopTree>> loopForests;
  [[no_unique_address]] map<llvm::Loop *, LoopTree *> loopMap;
  [[no_unique_address]] const llvm::TargetLibraryInfo *TLI;
  [[no_unique_address]] const llvm::TargetTransformInfo *TTI;
  [[no_unique_address]] llvm::LoopInfo *LI;
  [[no_unique_address]] llvm::ScalarEvolution *SE;
  [[no_unique_address]] llvm::OptimizationRemarkEmitter *ORE;
  [[no_unique_address]] LinearProgramLoopBlock loopBlock;
  [[no_unique_address]] BumpAlloc<> allocator;
  [[no_unique_address]] Intr::Cache instrCache;
  [[no_unique_address]] unsigned registerCount;

  /// extend number of Cols, copying A[_(0,R),_] into dest, filling new cols
  /// with 0
  void extendDensePtrMatCols(MutDensePtrMatrix<int64_t> &A, Row R, Col C,
                             bool zExt) {
    MutDensePtrMatrix<int64_t> B{matrix<int64_t>(allocator, A.numRow(), C)};
    for (size_t j = 0; j < R; ++j) {
      B(j, _(0, A.numCol())) << A(j, _);
      if (zExt) B(j, _(A.numCol(), end)) << 0;
    }
    std::swap(A, B);
  }

public:
  auto run(llvm::Function &F, llvm::FunctionAnalysisManager &AM)
    -> llvm::PreservedAnalyses;
  TurboLoopPass() = default;
  TurboLoopPass(const TurboLoopPass &) = delete;
  TurboLoopPass(TurboLoopPass &&) = default;
  ~TurboLoopPass() {
    for (auto l : loopForests) l->~LoopTree();
  }

  /// the process of building the LoopForest has the following steps:
  /// 1. build initial forest of trees
  /// 2. instantiate AffineLoopNests; any non-affine loops
  ///    are pruned, and their inner loops added as new, separate forests.
  /// 3. Existing forests are searched for indirect control flow between
  ///    successive loops. In all such cases, the loops at that level are
  ///    split into separate forests.
  void initializeLoopForest() {
    // NOTE: LoopInfo stores loops in reverse program order (opposite of
    // loops)
    auto revLI = llvm::reverse(*LI);
    auto ib = revLI.begin();
    auto ie = revLI.end();
    if (ib == ie) return;
    // pushLoopTree wants a direct path from the last loop's exit block to
    // E; we drop loops until we find one for which this is trivial.
    llvm::BasicBlock *E = (*--ie)->getExitBlock();
    while (!E) {
      if (ie == ib) return;
      E = (*--ie)->getExitingBlock();
    }
    // pushLoopTree wants a direct path from H to the first loop's header;
    // we drop loops until we find one for which this is trivial.
    llvm::BasicBlock *H = (*ib)->getLoopPreheader();
    while (!H) {
      if (ie == ib) return;
      H = (*++ib)->getLoopPreheader();
    }
    // should normally be stack allocated; we want to avoid different
    // specializations for `llvm::reverse(*LoopInfo)` and
    // `llvm::Loop->getSubLoops()`.
    // But we could consider specializing on top level vs not.
    // Track position within the loop nest
    {
      llvm::SmallVector<llvm::Loop *> rLI{ib, ie + 1};
      NoWrapRewriter nwr(*SE);
      pushLoopTree(loopForests, nullptr, rLI, H, E, nwr);
    }
    for (auto forest : loopForests) forest->addZeroLowerBounds(loopMap);
  }

  auto initLoopTree(llvm::SmallVector<NotNull<LoopTree>> &pForest,
                    llvm::Loop *L, llvm::ArrayRef<llvm::Loop *> subLoops,
                    llvm::BasicBlock *H, llvm::BasicBlock *E,
                    NoWrapRewriter &nwr) -> size_t {
    invariant(subLoops.empty());
    if (const auto *BT = getBackedgeTakenCount(*SE, L);
        !llvm::isa<llvm::SCEVCouldNotCompute>(BT)) {
      // we're at the bottom of the recursion
      if (auto predMapAbridged =
            Predicate::Map::descend(allocator, instrCache, H, E, L)) {
        auto *newTree = new (allocator) LoopTree{
          allocator, L, nwr.visit(BT), *SE, {std::move(*predMapAbridged)}};
        pForest.push_back(newTree);
        return 1;
      }
    }
    // Finally, we need `H` to have a direct path to `E`.
    return 0;
  }

  ///
  /// pushLoopTree
  ///
  /// pushLoopTree pushes `llvm::Loop* L` into a `LoopTree` object
  /// if `L == nullptr`, then this represents a top level loop.
  /// If we fail at some level of the recursion, we push the tree we have
  /// successfully built into loopForests as its own loop forest.
  /// If we succeed, we push the tree into the parent tree.
  ///
  /// To be successful, the following conditions need to be met:
  /// 1. We can represent that and all inner levels as an affine loop nest.
  /// 2. We can represent all indices as affine expressions.
  /// 3. We have a direct path between exits of one loop at a level and the
  /// header of the next.
  ///
  /// The arguments are:
  /// 1. `llvm::SmallVectorImpl<llvm::Loop *> &pForest`: the forest in which
  /// we are planting our tree.
  /// 2. `llvm::Loop* loop`: the loop we are trying to plant.
  /// 3. `llvm::SmallVector<unsigned> &omega`: The current position of the
  /// parser, for recording in memory accesses.
  /// 4. `llvm::ArrayRef<llvm::Loop *> subLoops`: the sub-loops of `L`; we
  /// don't access it directly via `L->getSubLoops` because we use
  /// `L==nullptr` to repesent the top level nest, in which case we get the
  /// sub-loops from the `llvm::LoopInfo*` object.
  /// 5. `llvm::BasicBlock *H`: Header - we need a direct path from here to
  /// the first sub-loop's preheader
  /// 6. `llvm::BasicBlock *E`: Exit - we need a direct path from the last
  /// sub-loop's exit block to this.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto pushLoopTree(llvm::SmallVector<NotNull<LoopTree>> &pForest,
                    llvm::Loop *L, llvm::ArrayRef<llvm::Loop *> subLoops,
                    llvm::BasicBlock *H, llvm::BasicBlock *E,
                    NoWrapRewriter &nwr) -> size_t {

    size_t numSubLoops = subLoops.size();
    if (!numSubLoops) return initLoopTree(pForest, L, subLoops, H, E, nwr);
    // branches of this tree;
    llvm::SmallVector<NotNull<LoopTree>> branches;
    branches.reserve(numSubLoops);
    llvm::SmallVector<Predicate::Map> branchBlocks;
    branchBlocks.reserve(numSubLoops + 1);
    bool anyFail = false;
    size_t interiorDepth = 0;
    for (size_t i = 0; i < numSubLoops; ++i) {
      llvm::Loop *subLoop = subLoops[i];
      if (size_t depth = pushLoopTree(branches, subLoop, subLoop->getSubLoops(),
                                      subLoop->getHeader(),
                                      subLoop->getExitingBlock(), nwr)) {
        // pushLoopTree succeeded, and we have `depth` inner loops
        // within `subLoop` (inclusive, i.e. `depth == 1` would
        // indicate that `subLoop` doesn't have any subLoops itself,
        // which we check with the following assertion:
        assert((depth > 1) || (subLoop->getSubLoops().empty()));

        // Now we check if we can create a direct path from `H` to
        // `subLoop->getLoopPreheader();`
        llvm::BasicBlock *subLoopPreheader = subLoop->getLoopPreheader();
        if (auto predMap = Predicate::Map::descend(allocator, instrCache, H,
                                                   subLoopPreheader, L)) {
          interiorDepth = std::max(interiorDepth, depth);
          branchBlocks.push_back(std::move(*predMap));
          H = subLoop->getExitBlock();
        } else {
          // oops, no direct path, we split
          anyFail = true;
          if (branches.size() > 1) {
            // we need to split off the last tree
            LoopTree *lastTree = branches.pop_back_val();
            // so we can push previous branches as one set
            // for the final branchBlocks, we'll push H->H
            split(branches, branchBlocks, H, L);
            // reinsert last tree
            branches.push_back(lastTree);
          }
          if (i + 1 < numSubLoops) H = subLoops[i + 1]->getLoopPreheader();
        }
        // for the next loop, we'll want a path to its preheader
        // from this loop's exit block.
      } else {
        // `depth == 0` indicates failure, therefore we need to
        // split loops
        anyFail = true;
        if (!branches.empty()) split(branches, branchBlocks, H, L);
        if (i + 1 < numSubLoops) H = subLoops[i + 1]->getLoopPreheader();
      }
    }
    if (!anyFail) {
      // branches.size() > 0 because we have numSubLoops > 0 and !anyFail
      // !anyFail means we called pushLoopTree, and returned depth > 0
      // which means it must have called pushLoopTree, pushing into branches
      // (pushLoopTree pushes into first arg, pForest, whenever ret > 0)
      invariant(!branches.empty());
      if (auto predMapAbridged =
            Predicate::Map::descend(allocator, instrCache, H, E, L)) {
        branchBlocks.push_back(std::move(*predMapAbridged));

        auto *newTree = new (allocator)
          LoopTree{allocator, L,
                   branches.front()->affineLoop->removeInnerMost(allocator),
                   std::move(branches), std::move(branchBlocks)};
        pForest.push_back(newTree);
        return ++interiorDepth;
      }
    }
    if (!branches.empty()) split(branches, branchBlocks, H, L);
    return 0;
  }
  void split(llvm::SmallVector<NotNull<LoopTree>> &branches,
             llvm::SmallVector<Predicate::Map> &branchBlocks,
             llvm::BasicBlock *BB, llvm::Loop *L) {

    auto predMapAbridged =
      Predicate::Map::descend(allocator, instrCache, BB, BB, L);
    assert(predMapAbridged);
    branchBlocks.push_back(std::move(*predMapAbridged));
    LoopTree::split(allocator, loopForests, branchBlocks, branches);
  }

  auto isLoopPreHeader(const llvm::BasicBlock *BB) const -> bool {
    if (const llvm::Instruction *term = BB->getTerminator())
      if (const auto *BI = llvm::dyn_cast<llvm::BranchInst>(term))
        if (!BI->isConditional()) return LI->isLoopHeader(BI->getSuccessor(0));
    return false;
  }
  inline static auto containsPeeled(const llvm::SCEV *Sc, size_t numPeeled)
    -> bool {
    return llvm::SCEVExprContains(Sc, [numPeeled](const llvm::SCEV *S) {
      if (const auto *r = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
        if (r->getLoop()->getLoopDepth() <= numPeeled) return true;
      return false;
    });
  }
  static void addSymbolic(Vector<int64_t> &offsets,
                          llvm::SmallVector<const llvm::SCEV *, 3> &symbols,
                          const llvm::SCEV *S, int64_t x = 1) {
    if (auto *j = std::ranges::find(symbols, S); j != symbols.end()) {
      offsets[std::distance(symbols.begin(), j)] += x;
    } else {
      symbols.push_back(S);
      offsets.push_back(x);
    }
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto blackListAllDependentLoops(const llvm::SCEV *S) -> uint64_t {
    uint64_t flag{0};
    if (const auto *x = llvm::dyn_cast<const llvm::SCEVNAryExpr>(S)) {
      if (const auto *y = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(x))
        flag |= uint64_t(1) << y->getLoop()->getLoopDepth();
      for (size_t i = 0; i < x->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(x->getOperand(i));
    } else if (const auto *c = llvm::dyn_cast<const llvm::SCEVCastExpr>(S)) {
      for (size_t i = 0; i < c->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(c->getOperand(i));
      return flag;
    } else if (const auto *d = llvm::dyn_cast<const llvm::SCEVUDivExpr>(S)) {
      for (size_t i = 0; i < d->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(d->getOperand(i));
      return flag;
    }
    return flag;
  }
  static auto blackListAllDependentLoops(const llvm::SCEV *S, size_t numPeeled)
    -> uint64_t {
    return blackListAllDependentLoops(S) >> (numPeeled + 1);
  }
  // translates scev S into loops and symbols
  auto // NOLINTNEXTLINE(misc-no-recursion)
  fillAffineIndices(MutPtrVector<int64_t> v, int64_t *coffset,
                    Vector<int64_t> &offsets,
                    llvm::SmallVector<const llvm::SCEV *, 3> &symbolicOffsets,
                    const llvm::SCEV *S, int64_t mlt, size_t numPeeled)
    -> uint64_t {
    uint64_t blackList{0};
    if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S)) {
      const llvm::Loop *L = x->getLoop();
      size_t depth = L->getLoopDepth();
      if (depth <= numPeeled) {
        // we effectively have an offset
        // we'll add an
        addSymbolic(offsets, symbolicOffsets, S, 1);
        for (size_t i = 1; i < x->getNumOperands(); ++i)
          blackList |= blackListAllDependentLoops(x->getOperand(i));

        return blackList;
      }
      // outermost loop has loopInd 0
      ptrdiff_t loopInd = ptrdiff_t(depth) - ptrdiff_t(numPeeled + 1);
      if (x->isAffine()) {
        if (loopInd >= 0) {
          if (auto c = getConstantInt(x->getOperand(1))) {
            // we want the innermost loop to have index 0
            v[loopInd] += *c;
            return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                     x->getOperand(0), mlt, numPeeled);
          }
          blackList |= (uint64_t(1) << uint64_t(loopInd));
        }
        // we separate out the addition
        // the multiplication was either peeled or involved
        // non-const multiple
        blackList |= fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                       x->getOperand(0), mlt, numPeeled);
        // and then add just the multiple here as a symbolic offset
        const llvm::SCEV *addRec = SE->getAddRecExpr(
          SE->getZero(x->getOperand(0)->getType()), x->getOperand(1),
          x->getLoop(), x->getNoWrapFlags());
        addSymbolic(offsets, symbolicOffsets, addRec, mlt);
        return blackList;
      }
      if (loopInd >= 0) blackList |= (uint64_t(1) << uint64_t(loopInd));
    } else if (std::optional<int64_t> c = getConstantInt(S)) {
      *coffset += *c;
      return 0;
    } else if (const auto *ar = llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(0), mlt, numPeeled) |
             fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(1), mlt, numPeeled);
    } else if (const auto *m = llvm::dyn_cast<const llvm::SCEVMulExpr>(S)) {
      if (auto op0 = getConstantInt(m->getOperand(0))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(1), mlt * (*op0), numPeeled);
      }
      if (auto op1 = getConstantInt(m->getOperand(1))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(0), mlt * (*op1), numPeeled);
      }
    } else if (const auto *ca = llvm::dyn_cast<llvm::SCEVCastExpr>(S))
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ca->getOperand(0), mlt, numPeeled);
    addSymbolic(offsets, symbolicOffsets, S, mlt);
    return blackList | blackListAllDependentLoops(S, numPeeled);
  }
  auto arrayRef(LoopTree &LT, llvm::Instruction *ptr, const llvm::SCEV *elSize,
                llvm::Instruction *loadOrStore, MutPtrVector<unsigned> omegas)
    -> bool {
    llvm::Loop *L = LT.loop;
    // code modified from
    // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
    const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);

    const llvm::SCEV *pb = SE->getPointerBase(accessFn);
    const auto *basePointer = llvm::dyn_cast<llvm::SCEVUnknown>(pb);
    // Do not delinearize if we cannot find the base pointer.
    if (!basePointer) {
      if (ORE && L) [[unlikely]] {
        remark("ArrayRefDeliniarize", LT.loop,
               "ArrayReference failed because !basePointer\n", ptr);
      }
      conditionOnLoop(L);
      return true;
    }
    accessFn = SE->getMinusSCEV(accessFn, basePointer);
    llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
    llvm::delinearize(*SE, accessFn, subscripts, sizes, elSize);
    size_t numDims = subscripts.size();
    invariant(numDims, sizes.size());
    AffineLoopNest *aln = loopMap[L]->affineLoop;
    if (numDims == 0) {
      LT.memAccesses.push_back(
        Addr::construct(allocator, basePointer, *aln, loadOrStore, omegas));
      ++omegas.back();
      return false;
    }
    size_t numLoops{aln->getNumLoops()};
    // numLoops x arrayDim
    // IntMatrix R(numLoops, numDims);
    size_t numPeeled = L->getLoopDepth() - numLoops;
    // numLoops x arrayDim
    IntMatrix Rt{StridedDims{numDims, numLoops}, 0};
    llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
    uint64_t blackList{0};
    Vector<int64_t> coffsets{unsigned(numDims), 0};
    MutDensePtrMatrix<int64_t> offsMat{nullptr, DenseDims{numDims, 0}};
    {
      Vector<int64_t> offsets;
      for (size_t i = 0; i < numDims; ++i) {
        offsets << 0;
        blackList |=
          fillAffineIndices(Rt(i, _), &coffsets[i], offsets, symbolicOffsets,
                            subscripts[i], 1, numPeeled);
        if (offsets.size() > offsMat.numCol())
          extendDensePtrMatCols(offsMat, Row{i}, Col{offsets.size()}, true);
        offsMat(i, _) << offsets;
      }
    }
    uint64_t numExtraLoopsToPeel = 0;
    if (blackList) {
      // blacklist: inner - outer
      uint64_t leadingZeros = std::countl_zero(blackList);
      numExtraLoopsToPeel = 64 - leadingZeros;
      // need to condition on loop
      // remove the numExtraLoopsToPeel from Rt
      // that is, we want to move Rt(_,_(end-numExtraLoopsToPeel,end))
      // to would this code below actually be expected to boost
      // performance? if
      // (Bt.numCol()+numExtraLoopsToPeel>Bt.rowStride())
      // 	Bt.resize(Bt.numRow(),Bt.numCol(),Bt.numCol()+numExtraLoopsToPeel);
      // order of loops in Rt is innermost -> outermost
      size_t remainingLoops = numLoops - numExtraLoopsToPeel;
      llvm::Loop *P = L;
      for (size_t i = 1; i < remainingLoops; ++i) P = P->getParentLoop();
      // remove
      conditionOnLoop(P->getParentLoop());
      for (size_t i = 0; i < numExtraLoopsToPeel; ++i) {
        P = P->getParentLoop();
        if (allZero(Rt(_, i))) continue;
        // push the SCEV
        auto *intType = P->getInductionVariable(*SE)->getType();
        const llvm::SCEV *S = SE->getAddRecExpr(
          SE->getZero(intType), SE->getOne(intType), P, llvm::SCEV::NoWrapMask);
        if (auto *j = std::ranges::find(symbolicOffsets, S);
            j != symbolicOffsets.end()) {
          offsMat(_, std::distance(symbolicOffsets.begin(), j)) += Rt(_, i);
        } else {
          extendDensePtrMatCols(offsMat, offsMat.numRow(), offsMat.numCol() + 1,
                                false);
          offsMat(_, last) << Rt(_, i);
          symbolicOffsets.push_back(S);
        }
      }
    }
    LT.memAccesses.push_back(
      Addr::construct(allocator, basePointer, *aln, loadOrStore,
                      Rt(_, _(numExtraLoopsToPeel, end)),
                      {std::move(sizes), std::move(symbolicOffsets)}, coffsets,
                      offsMat.data(), omegas));
    ++omegas.back();
    if (ORE) [[unlikely]] {
      llvm::SmallVector<char> x;
      llvm::raw_svector_ostream os{x};
      os << "Found ref: " << *LT.memAccesses.back();
      remark("AffineRef", LT.loop, os.str());
    }
    return false;
  }
  // LoopTree &getLoopTree(unsigned i) { return loopTrees[i]; }
  auto getLoopTree(llvm::Loop *L) -> LoopTree * { return loopMap[L]; }
  auto addRef(LoopTree &LT, LoadOrStoreInst auto *J, Vector<unsigned> &omega)
    -> bool {
    llvm::Value *ptr = J->getPointerOperand();
    // llvm::Type *type = I->getPointerOperandType();
    const llvm::SCEV *elSize = SE->getElementSize(J);
    // TODO: support top level array refs
    if (LT.loop) {
      if (auto *iptr = llvm::dyn_cast<llvm::Instruction>(ptr)) {
        if (!arrayRef(LT, iptr, elSize, J, omega)) return false;
        if (ORE) [[unlikely]] {
          llvm::SmallVector<char> x;
          llvm::raw_svector_ostream os{x};
          if (llvm::isa<llvm::LoadInst>(J))
            os << "No affine representation for load: " << *J << "\n";
          else os << "No affine representation for store: " << *J << "\n";
          remark("AddAffineLoad", LT.loop, os.str(), J);
        }
      }
      return true;
    }
    return false;
  }
  void parseBB(LoopTree &LT, llvm::BasicBlock *BB, Vector<unsigned> &omega) {
    for (llvm::Instruction &J : *BB) {
      if (LT.loop) assert(LT.loop->contains(&J));
      if (J.mayReadFromMemory()) {
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&J))
          if (addRef(LT, load, omega)) return;
      } else if (J.mayWriteToMemory())
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&J))
          if (addRef(LT, store, omega)) return;
    }
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void visit(LoopTree &LT, Predicate::Map &map, Vector<unsigned> &omega,
             aset<llvm::BasicBlock *> &visited, llvm::BasicBlock *BB) {
    if ((!map.isInPath(BB)) || visited.contains(BB)) return;
    visited.insert(BB);
    for (llvm::BasicBlock *pred : llvm::predecessors(BB))
      visit(LT, map, omega, visited, pred);
    parseBB(LT, BB, omega);
  }
  void parseBBMap(LoopTree &LT, Predicate::Map &map, Vector<unsigned> &omega) {
    aset<llvm::BasicBlock *> visited{allocator};
    for (auto &pair : map) visit(LT, map, omega, visited, pair.first);
  }
  // we fill omegas, we have loop pos only, not shifts
  // pR: 0
  // pL: 0
  // pL: 0
  //
  // [0, 0]
  // NOLINTNEXTLINE(misc-no-recursion)
  void parseLoop(LoopTree &LT, Vector<unsigned> &omega) {
#ifndef NDEBUG
    size_t numOmegaInitial = omega.size();
    // FIXME:
    // two issues, currently:
    // 1. multiple parses produce the same omega
    // 2. we have the same BB showing up multiple times
    // for (auto &&path : LT.paths)
    //     for (auto PBB : path) {
    //         assert(!paths.contains(PBB.basicBlock));
    //         paths.insert(PBB.basicBlock);
    //     }
    assert(LT.subLoops.size() + 1 == LT.paths.size());
#endif
    omega.push_back(0);
    // now we walk blocks
    // auto &subLoops = L->getSubLoops();
    for (size_t i = 0; i < LT.subLoops.size(); ++i) {
      parseBBMap(LT, LT.paths[i], omega);
      parseLoop(*LT.subLoops[i], omega);
      ++omega.back();
    }
    parseBBMap(LT, LT.paths.back(), omega);
    omega.pop_back();
#ifndef NDEBUG
    assert(omega.size() == numOmegaInitial);
#endif
  }
  void parseNest() {
    Vector<unsigned> omega;
    for (auto forest : loopForests) {
      omega.clear();
      parseLoop(*forest, omega);
    }
  }

  void peelOuterLoops(llvm::Loop *L, size_t numToPeel) {
    peelOuterLoops(*loopMap[L], numToPeel);
  }
  // peelOuterLoops is recursive inwards
  // NOLINTNEXTLINE(misc-no-recursion)
  void peelOuterLoops(LoopTree &LT, size_t numToPeel) {
    for (auto SL : LT) peelOuterLoops(*SL, numToPeel);
    for (auto &MA : LT.memAccesses) MA->peelLoops(numToPeel);
    LT.affineLoop->removeOuterMost(numToPeel, LT.loop, *SE);
  }
  // conditionOnLoop(llvm::Loop *L)
  // means to remove the loop L, and all those exterior to it.
  //
  //        /-> C /-> F  -> J
  // -A -> B -> D  -> G \-> K
  //  |     \-> E  -> H  -> L
  //  |           \-> I
  //   \-> M -> N
  // if we condition on D
  // then we get
  //
  //     /-> J
  // _/ F -> K
  //  \ G
  // -C
  // -E -> H -> L
  //   \-> I
  // -M -> N
  // algorithm:
  // 1. peel the outer loops from D's children (peel 3)
  // 2. add each of D's children as new forests
  // 3. remove D from B's subLoops; add prev and following loops as
  // separate new forests
  // 4. conditionOnLoop(B)
  //
  // approach: remove LoopIndex, and all loops that follow, unless it is
  // first in which case, just remove LoopIndex
  void conditionOnLoop(llvm::Loop *L) { conditionOnLoop(loopMap[L]); }
  void conditionOnLoop(LoopTree *LT) { // NOLINT(misc-no-recursion)
    if (!LT->parentLoop) return;
    LoopTree &PT = *LT->parentLoop;
    size_t numLoops = LT->getNumLoops();
    for (auto ST : *LT) peelOuterLoops(*ST, numLoops);

    LT->parentLoop = nullptr; // LT is now top of the tree
    loopForests.push_back(LT);
    llvm::SmallVector<NotNull<LoopTree>> &friendLoops = PT.subLoops;
    if (friendLoops.front() != LT) {
      // we're cutting off the front
      size_t numFriendLoops = friendLoops.size();
      assert(numFriendLoops);
      size_t loopIndex = 0;
      for (size_t i = 1; i < numFriendLoops; ++i) {
        if (friendLoops[i] == LT) {
          loopIndex = i;
          break;
        }
      }
      assert(loopIndex);
      size_t j = loopIndex + 1;
      if (j != numFriendLoops) {
        // we have some remaining paths we split off
        llvm::SmallVector<NotNull<LoopTree>> tmp;
        tmp.reserve(numFriendLoops - j);
        // for paths, we're dropping LT
        // thus, our paths are paths(_(0,j)), paths(_(j,end))
        llvm::SmallVector<Predicate::Map> paths;
        paths.reserve(numFriendLoops - loopIndex);
        for (size_t i = j; i < numFriendLoops; ++i) {
          peelOuterLoops(*friendLoops[i], numLoops - 1);
          tmp.push_back(friendLoops[i]);
          paths.push_back(std::move(PT.paths[i]));
        }
        paths.push_back(std::move(PT.paths[numFriendLoops]));
        auto *newTree =
          new (allocator) LoopTree(std::move(tmp), std::move(paths));
        loopForests.push_back(newTree);
        // TODO: split paths
      }
      friendLoops.truncate(loopIndex);
      PT.paths.truncate(j);
    } else {
      friendLoops.erase(friendLoops.begin());
      PT.paths.erase(PT.paths.begin());
    }
    conditionOnLoop(&PT);
  }
  auto isLoopDependent(llvm::Value *v) const -> bool {
    return !std::ranges::all_of(
      *LI, [v](const auto &L) { return L->isLoopInvariant(v); });
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto mayReadOrWriteMemory(llvm::Value *v) -> bool {
    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(v))
      if (inst->mayReadOrWriteMemory()) return true;
    return false;
  }
  void fillLoopBlock(LoopTree &root) { // NOLINT(misc-no-recursion)
    for (auto mem : root.memAccesses) loopBlock.addMemory(mem);
    for (auto sub : root.subLoops) fillLoopBlock(*sub);
  }
  // https://llvm.org/doxygen/LoopVectorize_8cpp_source.html#l00932
  void remark(const llvm::StringRef remarkName, llvm::Loop *L,
              const llvm::StringRef remarkMessage,
              llvm::Instruction *J = nullptr) const {

    llvm::OptimizationRemarkAnalysis analysis{remarkAnalysis(remarkName, L, J)};
    ORE->emit(analysis << remarkMessage);
  }
  // void buildInstructionGraph(LoopTree &root) {
  //     // predicates
  // }
};
