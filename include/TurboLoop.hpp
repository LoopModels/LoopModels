#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Cache.hpp"
#include "IR/CostModeling.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "LinearProgramming/LoopBlock.hpp"
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
#include <pthread.h>
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
  [[no_unique_address]] IR::Cache instructions;
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
    return instructions.getAllocator();
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
    if (LI->empty()) return;
    auto revLI = llvm::reverse(*LI);
    // should normally be stack allocated; we don't want to monomorphize
    // excessively, so we produce an `ArrayRef<llvm::Loop *>` here
    // `llvm::Loop->getSubLoops()`.
    // But we could consider specializing on top level vs not.
    llvm::SmallVector<llvm::Loop *> rLI{revLI};
    poly::NoWrapRewriter nwr(*SE);
    math::Vector<int, 8> omega{0};
    IR::TreeResult tr = runOnLoop(nullptr, rLI, omega, nwr);
    if (tr.accept(0)) optimize(tr);
  }

  // void addInstructions(IR::Addr *addr, llvm::Loop *L) {
  //   addr->forEach([&, L](Addr *a) { instructions.addParents(a, L); });
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
                                 poly::Loop *AL, IR::TreeResult tr)
    -> IR::TreeResult {
    // TODO: need to be able to connect instructions as we move out
    auto predMapAbridged =
      IR::Predicate::Map::descend(shortAllocator(), instructions, H, E, L);
    if (!predMapAbridged) return {};
    // Now we need to create Addrs
    size_t depth = omega.size() - 1;
    for (auto &[BB, P] : *predMapAbridged) { // rev order
      for (llvm::Instruction &J : llvm::reverse(*BB)) {
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
        auto [N, trret] = instructions.getArrayRef(&J, L, ptr, tr);
        tr = trret;
        if (tr.reject(depth)) return tr;
        IR::Addr *A = llvm::cast<IR::Addr>(A);
        // if we didn't reject, it must have been an `Addr`
        A->setFusionOmega(omega);
        instructions.addPredicate(A, P, &*predMapAbridged);
        A->setLoopNest(AL);
      }
    }
    return mergeInstructions(instructions, *predMapAbridged, TTI,
                             shortAllocator(), registers.getNumVectorBits(),
                             tr);
  }
  /// factored out codepath, returns number of rejected loops
  /// current depth is omega.size()-1
  auto initLoopTree(llvm::Loop *L, math::Vector<int, 8> &omega,
                    poly::NoWrapRewriter &nwr) -> IR::TreeResult {

    const auto *BT = poly::getBackedgeTakenCount(*SE, L);
    if (llvm::isa<llvm::SCEVCouldNotCompute>(BT)) return {};
    BumpAlloc<> &salloc{shortAllocator()};
    BumpAlloc<> &lalloc{longAllocator()};
    // TODO: check pointing seems dangerous, as
    // we'd have to make sure none of the allocated instructions
    // can be referenced again (e.g., through the free list)
    // auto p = lalloc.checkpoint();
    NotNull<poly::Loop> AL =
      poly::Loop::construct(lalloc, L, nwr.visit(BT), *SE);
    IR::TreeResult tr = parseExitBlocks(L);
    tr.rejectDepth =
      std::max(tr.rejectDepth, size_t(omega.size() - AL->getNumLoops()));
    omega.push_back(0); // we start with 0 at the end, walking backwards
    tr = parseBlocks(L->getHeader(), L->getLoopLatch(), L, omega, AL, tr);
    omega.pop_back();
    if (tr.accept(omega.size() - 1)) return tr;
    salloc.reset();
    // lalloc.rollback(p);
    return {};
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
        tr = instructions.getValue(J, nullptr, tr).second;
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
  // NOLINTNEXTLINE(misc-no-recursion)
  auto runOnLoop(llvm::Loop *L, llvm::ArrayRef<llvm::Loop *> subLoops,
                 math::Vector<int, 8> &omega, poly::NoWrapRewriter &nwr)
    -> IR::TreeResult {
    unsigned numSubLoops = subLoops.size();
    // This is a special case, as it is when we build poly::Loop
    if (!numSubLoops) return initLoopTree(L, omega, nwr);
    unsigned depth = omega.size();
    bool failed = false;
    IR::TreeResult tr = parseExitBlocks(L);
    omega.push_back(0); // we start with 0 at the end, walking backwards
    poly::Loop *AL = nullptr;
    llvm::BasicBlock *E = L->getLoopLatch();
    for (size_t i = numSubLoops; i--; --omega.back()) {
      llvm::Loop *subLoop = subLoops[i];
      // we need to parse backwards, so we first evaluate
      // TODO: support having multiple exit blocks?
      IR::TreeResult trec =
        runOnLoop(subLoop, subLoop->getSubLoops(), omega, nwr);

      if (trec.accept(depth)) {
        if (!AL) AL = trec.getLoop();
        if (AL) {
          // recursion succeeded; see if we can connect the path
          llvm::BasicBlock *subLoopExit = subLoop->getExitBlock();
          // for fusion, we need to build a path from subLoopExit to E
          // where E is the preheader of the preceding loopnest
          IR::TreeResult trblock =
            parseBlocks(subLoopExit, E, L, omega, AL, tr);
          if (trblock.accept(depth)) {
            tr = trblock;
            tr *= trec;
          } else {
            failed = true;
            if (tr.accept(depth)) optimize(tr);
            // we start now with trec
            tr = trec;
          }
          E = subLoop->getLoopPreheader(); // want to draw a path from trec
          continue;
        }
      }
      // we reject, because we failed to build a trec with a LoopNest
      failed = true;
      optimize(tr);
      tr = {};
      // we don't need to draw a path from anything, so only exit needed
      if (i) E = subLoops[i - 1]->getExitBlock();
    }
    if (failed) {
      if (tr.accept(depth)) optimize(tr);
      omega.pop_back();
      return {};
    }
    // now we try to go from E to H
    IR::TreeResult trblock = parseBlocks(L->getHeader(), E, L, omega, AL, tr);
    if (trblock.reject(depth)) {
      optimize(tr); // optimize old tr
      tr = {};
    } else tr = trblock;
    omega.pop_back();
    return tr;
  }

  void optimize(IR::TreeResult tr) {
    // now we build the LinearProgram
  }
  /*
    auto isLoopPreHeader(const llvm::BasicBlock *BB) const -> bool {
      if (const llvm::Instruction *term = BB->getTerminator())
        if (const auto *BI = llvm::dyn_cast<llvm::BranchInst>(term))
          if (!BI->isConditional()) return
  LI->isLoopHeader(BI->getSuccessor(0)); return false;
    }
    inline static auto containsPeeled(const llvm::SCEV *Sc, size_t numPeeled)
      -> bool {
      return llvm::SCEVExprContains(Sc, [numPeeled](const llvm::SCEV *S) {
        if (const auto *r = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
          if (r->getLoop()->getLoopDepth() <= numPeeled) return true;
        return false;
      });
  }
    */
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
