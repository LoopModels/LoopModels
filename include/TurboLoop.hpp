#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Cache.hpp"
#include "IR/CostModeling.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "LoopBlock.hpp"
#include "Polyhedra/Loops.hpp"
#include "RemarkAnalysis.hpp"
#include <Math/Array.hpp>
#include <Math/Math.hpp>
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

namespace poly {

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
  [[no_unique_address]] IR::Loop *forest;
  [[no_unique_address]] dict::map<llvm::Loop *, IR::Loop *> loopMap;
  [[no_unique_address]] const llvm::TargetLibraryInfo *TLI;
  [[no_unique_address]] const llvm::TargetTransformInfo *TTI;
  [[no_unique_address]] llvm::LoopInfo *LI;
  [[no_unique_address]] llvm::ScalarEvolution *SE;
  [[no_unique_address]] llvm::OptimizationRemarkEmitter *ORE;
  [[no_unique_address]] LinearProgramLoopBlock loopBlock;
  [[no_unique_address]] IR::Cache instrCache;
  [[no_unique_address]] CostModeling::CPURegisterFile registers;

  // this is an allocator that it is safe to reset completely when
  // a subtree fails, so it is not allowed to allocate anything
  // that we want to live longer than that.
  constexpr auto shortAllocator() -> BumpAlloc<> & {
    return loopBlock.getAllocator();
  }
  // It is of course safe to checkpoint & rollback or scope
  // this allocator, but best to avoid excessive allocations
  // that result in extra slabs being allocated that
  // then are not going to get freed until we are done with the
  // `TurboLoopPass`
  constexpr auto longAllocator() -> BumpAlloc<> & {
    return instrCache.getAllocator();
  }

  /// the process of building the LoopForest has the following steps:
  /// 1. build initial forest of trees
  /// 2. instantiate poly::Loops; any non-affine loops
  ///    are pruned, and their inner loops added as new, separate forests.
  /// 3. Existing forests are searched for indirect control flow between
  ///    successive loops. In all such cases, the loops at that level are
  ///    split into separate forests.
  void initializeLoopForest() {
    // NOTE: LoopInfo stores loops in reverse program order
    auto revLI = std::ranges::reverse(*LI);
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
    // should normally be stack allocated; we don't want to monomorphize
    // excessively, so we produce an `ArrayRef<llvm::Loop *>` here
    // `llvm::Loop->getSubLoops()`.
    // But we could consider specializing on top level vs not.
    llvm::SmallVector<llvm::Loop *> rLI{ib, ie + 1};
    NoWrapRewriter nwr(*SE);
    pushLoopTree(nullptr, rLI, H, E, nwr);
    // for (auto forest : loopForests) forest->addZeroLowerBounds(loopMap);
  }

  // void addInstructions(IR::Addr *addr, llvm::Loop *L) {
  //   addr->forEach([&, L](Addr *a) { instrCache.addParents(a, L); });
  // }

  /// parse a from `H` to `E`, nested within loop `L`
  /// we try to form a chain of blocks from `H` to `E`, representing
  /// contiguous control flow. If we have
  /// H-->A-->E
  ///  \->B-/
  /// Then we would try to merge blocks A and B, predicating the associated
  /// instructions, and attempting to merge when possible.
  /// We parse in reverse order, decrementing `omega.back()` for
  /// each address.
  /// The initial store construction leaves the stored value incomplete;
  /// as we also parse the different H->E sets in reverse order,
  /// we build up all incomplete instructions we care about in the current H->E
  /// block within the `TreeResult tr` we receive as an argument.
  /// This is needed by the mergeInstructions function, which parses
  /// these, and continues searching parents until it leaves our block chain,
  /// building the relevant part of the instruction graph.
  [[nodiscard]] auto parseBlocks(llvm::BasicBlock *H, llvm::BasicBlock *E,
                                 llvm::Loop *L, MutPtrVector<int> omega,
                                 NotNull<poly::Loop> AL, IR::TreeResult tr)
    -> IR::TreeResult {
    // TODO: need to be able to connect instructions as we move out
    auto predMapAbridged =
      Predicate::Map::descend(shortAllocator(), instrCache, H, E, L);
    if (!predMapAbridged) return {};
    // Now we need to create Addrs
    size_t depth = omega.size() - 1;
    for (auto &[BB, P] : *predMapAbridged) { // rev order
      for (llvm::Instruction &J : std::ranges::reverse(*BB)) {
        if (L) assert(L->contains(&J));
        llvm::Value *ptr{nullptr};
        if (J.mayReadFromMemory())
          if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&J))
            ptr = load->getPointerOperand();
          else return {};
        else if (J.mayWriteToMemory())
          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&J))
            ptr = store->getPointerOperand();
          else return {};
        else continue;
        if (ptr == nullptr) return {};
        auto [A, trret] = cache.getArrayRef(&J, L, ptr, tr);
        tr = tret;
        if (tr.reject(depth)) return tr;
        // if we didn't reject, it must have been an `Addr`
        llvm::cast<Addr>(A)->setFusionOmega(omega);
        tr = cache.addPredicate(A, P, &*predMapAbridged, tr);
      }
    }
    return mergeInstructions(instrCache, *predMapAbridged, TTI,
                             shortAllocator(), registers.getNumVectorBits(),
                             tr);
  }
  /// factored out codepath, returns number of rejected loops
  /// current depth is omega.size()-1
  auto initLoopTree(llvm::BasicBlock *H, llvm::BasicBlock *E, llvm::Loop *L,
                    math::Vector<int, 8> &omega, poly::NoWrapRewriter &nwr)
    -> IR::TreeResult {

    const auto *BT = getBackedgeTakenCount(*SE, L);
    if (llvm::isa<llvm::SCEVCouldNotCompute>(BT))
      return {nullptr, omega.size()};
    BumpAlloc<> &alloc{shortAllocator()};
    auto p = alloc.checkpoint();
    NotNull<poly::Loop> AL =
      poly::Loop::construct(alloc, L, nwr.visit(BT), *SE);
    IR::TreeResult tr = parseExitBlocks(L);
    tr = parseBlocks(H, E, L, omega, AL, tr);
    if (tr.reject(omega.size() - 1)) alloc.reset();
    return tr;
  }
  /// parseExitBlocks(llvm::Loop *L) -> IR::TreeResult
  /// We require loops be in LCSSA form
  /// parseExitBlock
  /// FIXME: some of these phis are likely to either be stored,
  /// or otherwise be values accumulated in the loop, and we
  /// currently have no way of representing things as simple as a sum.
  /// If we ultimately fail to expand outwards (i.e. if we can't represent the
  /// outer loop in an affine way, or if it is not a loop at all, but is
  /// toplevel) then we should represent these phis internally as storing to a
  /// zero-dimensional address.
  auto parseExitBlocks(llvm::Loop *L) -> IR::TreeResult {
    IR::TreeResult tr;
    for (auto &P : L->getExitBlock()->phis()) {
      for (unsigned i = 0, N = P.getNumIncomingValues(); i < N; ++i) {
        auto *J = llvm::dyn_cast<llvm::Instruction>(P.getIncomingValue(i));
        if (!J || !L->contains(J)) continue;
        tr = cache.getValue(J, nullptr, tr).second;
      }
    }
    return tr;
  }
  /// runOnLoop, parses LLVM
  /// We construct our linear programs first, which means creating
  /// `poly::Loop`s and `Addr`s, and tracking original locations.
  /// We also build the instruction graphs in order to perform control flow
  /// merging, prior to analyzing the linear program. The linear program
  /// produces its own loop forest, different in general from the original, so
  /// it is here that we finish creating our own internal IR with `Loop`s.
  ///
  ///
  /// We parse the loop forest depth-first
  /// on each failure, we run the analysis on what we can.
  /// E.g.
  /// invalid -> [A] valid -> valid
  ///        \-> [B] valid -> valid
  ///                     \-> valid
  /// Here, we would run on [A] and [B] separately.
  /// valid -> [A] valid ->     valid
  ///      \->     valid -> [B] valid
  ///                   \->   invalid
  /// Here, we would also run on [A] and [B] separately.
  /// We evaluate all branches before evaluating a node itself.
  ///
  /// On each level, we get information on how far out we can go,
  /// building up a IR::Cache::TreeResult, which accumulates
  /// the memory accesses, as well as instructions in need
  /// of completion, and the number of outer loops we need to reject.
  ///
  /// At each level of `runOnLoop`, we iterate over the subloops in reverse
  /// order, checking if the subtrees are valid, and if we have a direct flow of
  /// instructions allowing us to represent all of them as a single affine nest.
  /// If so, then return up the tree, continuing the process of building up a
  /// large nest.
  ///
  /// If any of the subloops fail, or we fail to draw the connection, then we
  /// can optimize the continuous succesful block we've produced, and return a
  /// failure up the tree.
  ///
  ///
  ///
  ///
  /// arguments:
  /// 0. `llvm::Loop *L`: the loop we are currently processing, exterior to this
  /// 1. `llvm::ArrayRef<llvm::Loop *> subLoops`: the sub-loops of `L`; we
  /// don't access it directly via `L->getSubLoops` because we use
  /// `L==nullptr` to repesent the top level nest, in which case we get the
  /// sub-loops from the `llvm::LoopInfo*` object.
  /// 2. `llvm::BasicBlock *H`: Header - we need a direct path from here to
  /// the first sub-loop's preheader
  /// 3. `llvm::BasicBlock *E`: Exit - we need a direct path from the last
  /// sub-loop's exit block to this.
  /// 4. `Vector<int,8> &omega`: the current position within the loopnest
  auto runOnLoop(llvm::Loop *L, llvm::ArrayRef<llvm::Loop *> subLoops,
                 llvm::BasicBlock *H, llvm::BasicBlock *E,
                 Vector<int, 8> &omega, NoWrapRewriter &nwr)
    -> IR::Cache::TreeResult {
    size_t numSubLoops = subLoops.size();
    if (!numSubLoops) return initLoopTree(L, H, E, omega, nwr);
    Loop *chain = nullptr;
    omega.push_back(0); // we start with 0 at the end, walking backwards
    IR::Cache::TreeResult ret;
    for (size_t i = numSubLoops; i--;) {
      llvm::Loop *subLoop = subLoops[i];
      IR::Cache::TreeResult tr =
        runOnLoop(subLoop, subLoop->getSubLoops(), subLoop->getHeader(),
                  subLoop->getExitingBlock(), omega, nwr);
      if (tr.accept(omega.size() - 1)) {
        llvm::BasicBlock *subLoopPreheader = subLoop->getLoopPreheader();
        // now, we need to see if we can can chart a path from H to
        // subLoopPreheader we should build predicated IR in doing so.

      } else {
        // we reject
      }
      --omega.back();
    }
    omega.pop_back();
    return nullptr;
  }

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
  auto pushLoopTree(llvm::Loop *L, llvm::ArrayRef<llvm::Loop *> subLoops,
                    llvm::BasicBlock *H, llvm::BasicBlock *E,
                    NoWrapRewriter &nwr) -> size_t {

    size_t numSubLoops = subLoops.size();
    if (!numSubLoops) return initLoopTree(pForest, L, H, E, nwr);
    // branches of this tree;
    llvm::SmallVector<NotNull<LoopTree>> branches;
    branches.reserve(numSubLoops);
    llvm::SmallVector<Predicate::Map> branchBlocks;
    branchBlocks.reserve(numSubLoops + 1);
    bool anyFail = false;
    size_t interiorDepth = 0;
    for (size_t i = 0; i < numSubLoops; ++i) {
      llvm::Loop *subLoop = subLoops[i];
      if (Loop *lret =
            pushLoopTree(subLoop, subLoop->getSubLoops(), subLoop->getHeader(),
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
  auto zeroDimRef(MutPtrVector<unsigned> omega, NotNull<poly::Loop> aln,
                  llvm::Instruction *loadOrStore,
                  llvm::SCEVUnknown const *arrayPtr, TreeResult tr)
    -> TreeResult {
    Addr *op = Addr::construct(allocator, arrayPtr, *aln, loadOrStore, omega);
    ++omega.back();
    return tr *= op;
  }
  auto arrayRef(llvm::Loop *L, MutPtrVector<unsigned> omega,
                NotNull<poly::Loop> aln, llvm::Instruction *loadOrStore,
                llvm::Instruction *ptr, TreeResult tr, const llvm::SCEV *elSz)
    -> TreeResult {
    // code modified from
    // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
    const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);

    const llvm::SCEV *pb = SE->getPointerBase(accessFn);
    const auto *arrayPtr = llvm::dyn_cast<llvm::SCEVUnknown>(pb);
    // Do not delinearize if we cannot find the base pointer.
    if (!arrayPtr) {
      if (ORE && L) [[unlikely]] {
        remark("ArrayRefDeliniarize", L,
               "ArrayReference failed because !basePointer\n", ptr);
      }
      // L is invalid, so we optimize the subloops and return `nullptr`
      return {nullptr};
    }
    accessFn = SE->getMinusSCEV(accessFn, arrayPtr);
    llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
    llvm::delinearize(*SE, accessFn, subscripts, sizes, elSz);
    size_t numDims = subscripts.size();
    invariant(numDims, sizes.size());
    if (numDims == 0) return zeroDimRef(omega, aln, loadOrStore, arrayPtr, tr);
    size_t numPeeled = tr.rejectDepth, numLoops = omega.size() - 1 - numPeeled;
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
          extendDensePtrMatCols(allocator, offsMat, Row{i},
                                Col{offsets.size()});
        offsMat(i, _) << offsets;
      }
    }
    size_t numExtraLoopsToPeel = 64 - std::countl_zero(blackList);
    Addr *op = Addr::construct(allocator, arrayPtr, *aln, loadOrStore,
                               Rt(_, _(numExtraLoopsToPeel, end)),
                               {std::move(sizes), std::move(symbolicOffsets)},
                               coffsets, offsMat.data(), omega);
    ++omega.back();
    if (ORE) [[unlikely]] {
      llvm::SmallVector<char> x;
      llvm::raw_svector_ostream os{x};
      os << "Found ref: " << *op;
      remark("AffineRef", L, os.str());
    }
    return {op, numExtraLoopsToPeel + numPeeled};
  }
  // LoopTree &getLoopTree(unsigned i) { return loopTrees[i]; }
  auto getLoopTree(llvm::Loop *L) -> Loop * { return loopMap[L]; }
  auto addRef(llvm::Loop *L, MutPtrVector<unsigned> omega,
              NotNull<poly::Loop> AL, llvm::Instruction *J, llvm::Value *ptr,
              TreeResult tr) -> TreeResult {
    const llvm::SCEV *elSz = SE->getElementSize(J);
    if (L) { // TODO: support top level array refs
      if (auto *iptr = llvm::dyn_cast<llvm::Instruction>(ptr)) {
        if (tr = arrayRef(L, omega, AL, J, iptr, tr, elSz);
            tr.accept(omega.size() - 1))
          return tr;
        if (ORE) [[unlikely]] {
          llvm::SmallVector<char> x;
          llvm::raw_svector_ostream os{x};
          if (llvm::isa<llvm::LoadInst>(J))
            os << "No affine representation for load: " << *J << "\n";
          else os << "No affine representation for store: " << *J << "\n";
          remark("AddAffineLoad", L, os.str(), J);
        }
      }
      return {nullptr};
    }
    // FIXME: this pointer may alias other array references!!!
    auto *arrayPtr =
      llvm::dyn_cast<llvm::SCEVUnknown>(SE->getPointerBase(SE->getSCEV(ptr)));
    return zeroDimRef(omega, AL, J, arrayPtr, tr);
  }
  // void parseBB(LoopTree &LT, llvm::BasicBlock *BB, Vector<unsigned> &omega) {
  //   for (llvm::Instruction &J : *BB) {
  //     if (LT.loop) assert(LT.loop->contains(&J));
  //     if (J.mayReadFromMemory()) {
  //       if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&J))
  //         if (addRef(LT, load, omega)) return;
  //     } else if (J.mayWriteToMemory())
  //       if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&J))
  //         if (addRef(LT, store, omega)) return;
  //   }
  // }
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
  // we fill omega, we have loop pos only, not shifts
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
  // void conditionOnLoop(llvm::Loop *L) { conditionOnLoop(loopMap[L]); }
  // void conditionOnLoop(LoopTree *LT) { // NOLINT(misc-no-recursion)
  //   if (!LT->parentLoop) return;
  //   LoopTree &PT = *LT->parentLoop;
  //   size_t numLoops = LT->getNumLoops();
  //   for (auto ST : *LT) peelOuterLoops(*ST, numLoops);

  //   LT->parentLoop = nullptr; // LT is now top of the tree
  //   loopForests.push_back(LT);
  //   llvm::SmallVector<NotNull<LoopTree>> &friendLoops = PT.subLoops;
  //   if (friendLoops.front() != LT) {
  //     // we're cutting off the front
  //     size_t numFriendLoops = friendLoops.size();
  //     assert(numFriendLoops);
  //     size_t loopIndex = 0;
  //     for (size_t i = 1; i < numFriendLoops; ++i) {
  //       if (friendLoops[i] == LT) {
  //         loopIndex = i;
  //         break;
  //       }
  //     }
  //     assert(loopIndex);
  //     size_t j = loopIndex + 1;
  //     if (j != numFriendLoops) {
  //       // we have some remaining paths we split off
  //       llvm::SmallVector<NotNull<LoopTree>> tmp;
  //       tmp.reserve(numFriendLoops - j);
  //       // for paths, we're dropping LT
  //       // thus, our paths are paths(_(0,j)), paths(_(j,end))
  //       llvm::SmallVector<Predicate::Map> paths;
  //       paths.reserve(numFriendLoops - loopIndex);
  //       for (size_t i = j; i < numFriendLoops; ++i) {
  //         peelOuterLoops(*friendLoops[i], numLoops - 1);
  //         tmp.push_back(friendLoops[i]);
  //         paths.push_back(std::move(PT.paths[i]));
  //       }
  //       paths.push_back(std::move(PT.paths[numFriendLoops]));
  //       auto *newTree =
  //         new (allocator) LoopTree(std::move(tmp), std::move(paths));
  //       loopForests.push_back(newTree);
  //       // TODO: split paths
  //     }
  //     friendLoops.truncate(loopIndex);
  //     PT.paths.truncate(j);
  //   } else {
  //     friendLoops.erase(friendLoops.begin());
  //     PT.paths.erase(PT.paths.begin());
  //   }
  //   conditionOnLoop(&PT);
  // }
  void conditionOnLoop(Loop *L) {
    L->forEachSubLoop([this](Loop *L) { optimizeLoop(L); });
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
public:
  auto run(llvm::Function &F, llvm::FunctionAnalysisManager &AM)
    -> llvm::PreservedAnalyses;
  TurboLoopPass() = default;
  TurboLoopPass(const TurboLoopPass &) = delete;
  TurboLoopPass(TurboLoopPass &&) = default;
  // ~TurboLoopPass() {
  //   for (auto l : loopForests) l->~LoopTree();
  // }
};
} // namespace poly
