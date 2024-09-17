#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/OptimizationRemarkEmitter.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/KnownBits.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>
#include <optional>
#include <type_traits>
#include <vector>

#ifndef USE_MODULE
#include "Utilities/Valid.cxx"
#include "Target/Machine.cxx"
#include "RemarkAnalysis.cxx"
#include "Math/ManagedArray.cxx"
#include "IR/IR.cxx"
#include "Utilities/Invariant.cxx"
#include "Target/Host.cxx"
#include "Optimize/CostModeling.cxx"
#include "IR/ControlFlowMerging.cxx"
#include "Math/Comparisons.cxx"
#include "Alloc/Arena.cxx"
#else
export module LLVMFrontend;
import Arena;
import Comparisons;
import ControlFlowMerging;
import CostModeling;
import Host;
import Invariant;
import IR;
import ManagedArray;
import Remark;
import TargetMachine;
import Valid;
#endif

using alloc::Arena;
using math::MutPtrVector, math::DensePtrMatrix, math::MutDensePtrMatrix,
  math::_;
using utils::invariant;

// NOLINTNEXTLINE(misc-no-recursion)
inline auto countNumLoopsPlusLeaves(const llvm::Loop *L) -> size_t {
  const std::vector<llvm::Loop *> &sub_loops = L->getSubLoops();
  if (sub_loops.empty()) return 1;
  size_t num_loops = sub_loops.size();
  for (const auto &SL : sub_loops) num_loops += countNumLoopsPlusLeaves(SL);
  return num_loops;
}
template <typename T>
concept LoadOrStoreInst =
  std::same_as<llvm::LoadInst, std::remove_cvref_t<T>> ||
  std::same_as<llvm::StoreInst, std::remove_cvref_t<T>>;

class TurboLoop {
  const llvm::TargetLibraryInfo *tli_;
  const llvm::TargetTransformInfo *tti_;
  llvm::LoopInfo *li_;
  llvm::ScalarEvolution *se_;
  llvm::OptimizationRemarkEmitter *ore_;
  llvm::AssumptionCache &assumption_cache_;
  llvm::DominatorTree &dom_tree_;
  alloc::OwningArena<> short_alloc_;
  IR::Dependencies deps_; // needs to be cleared before use w/ loop block
  IR::Cache instructions_;
  dict::set<llvm::BasicBlock *> loop_bbs_;
  dict::set<llvm::CallBase *> erase_candidates_;
  // RegisterFile::CPURegisterFile registers_;
  target::MachineCore::Arch arch_;
  // this is an allocator that it is safe to reset completely when
  // a subtree fails, so it is not allowed to allocate anything
  // that we want to live longer than that.
  constexpr auto shortAllocator() -> Arena<> * { return &short_alloc_; }
  constexpr auto getTarget() -> target::Machine<true> {
    return {{arch_}, tti_};
  }

  /// the process of building the LoopForest has the following steps:
  /// 1. build initial forest of trees
  /// 2. instantiate poly::Loops; any non-affine loops
  ///    are pruned, and their inner loops added as new, separate forests.
  /// 3. Existing forests are searched for indirect control flow between
  ///    successive loops. In all such cases, the loops at that level are
  ///    split into separate forests.
  auto
  initializeLoopForest(dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap)
    -> IR::TreeResult {
    // NOTE: LoopInfo stores loops in reverse program order
    if (li_->empty()) return {};
    auto rev_li = llvm::reverse(*li_);
    // should normally be stack allocated; we don't want to monomorphize
    // excessively, so we produce an `ArrayRef<llvm::Loop *>` here
    // `llvm::Loop->getSubLoops()`.
    // But we could consider specializing on top level vs not.
    llvm::SmallVector<llvm::Loop *> rLI{rev_li};
    poly::NoWrapRewriter nwr(*se_);
    math::Vector<int, 8> omega{0};
    return runOnLoop(nullptr, rLI, llvmToInternalMap, omega, nwr);
  }

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
  [[nodiscard]] auto
  parseBlocks(llvm::BasicBlock *H, llvm::BasicBlock *E, llvm::Loop *L,
              dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap,
              MutPtrVector<int> omega, poly::Loop *AL,
              IR::TreeResult tr) -> IR::TreeResult {
    // TODO: need to be able to connect instructions as we move out
    std::optional<IR::Predicate::Map> pred_map_abridged = instructions_.descend(
      shortAllocator(), H, E, L, {llvmToInternalMap, li_, se_}, tr);
    if (!pred_map_abridged) return {};
    // Now we need to create Addrs
    int depth = int(omega.size()) - 1;
    tr.maxDepth = std::max(tr.maxDepth, depth);
    llvm::KnownBits known;
    for (auto &[BB, P] : *pred_map_abridged) { // rev order
      for (llvm::Instruction &J : llvm::reverse(*BB)) {
#ifndef NDEBUG
        if (L && !L->contains(&J)) __builtin_trap();
#endif
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
        auto [V, trret] = instructions_.getArrayRef(
          &J, L, ptr, &(*pred_map_abridged), {llvmToInternalMap, li_, se_}, tr);
        tr = trret;
        // TODO: create array objects
        // `llvm::computeKnownBits(v, &known, DL, /*Depth=*/ 0,
        // AssumptionCache,/*CxtI=*/ nullptr, DomTree, /*UseInstrInfo=*/ true)`
        if (tr.reject(depth)) return tr;
        auto *A = llvm::cast<IR::Addr>(V);
        IR::Array array = A->getArray();
        auto *cva = llvm::cast<IR::CVal>(array.basePointer());
        llvm::computeKnownBits(cva->getVal(), known, instructions_.dataLayout(),
                               0, &assumption_cache_, nullptr, &dom_tree_);
        array.setAlignmentShift(known.countMinTrailingZeros());
        known.resetAll();
        // if we didn't reject, it must have been an `Addr`
        A->setFusionOmega(omega);
        instructions_.addPredicate(A, P, &(*pred_map_abridged));
        A->setLoopNest(AL);
      }
      loop_bbs_.insert(BB);
    }
    // TODO: need to be able to construct `target::Machine` from TTI; how to
    // infer arch?
    return IR::mergeInstructions(instructions_, *pred_map_abridged,
                                 target::machine(*tti_, H->getContext()),
                                 *shortAllocator(),
                                 getTarget().getVectorRegisterBitWidth(),
                                 {llvmToInternalMap, li_, se_}, tr);
  }
  /// current depth is omega.size()-1
  /// Shuold be called for leaves, i.e. deepest levels/innermost loops.
  auto initLoopTree(llvm::Loop *L,
                    dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap,
                    math::Vector<int, 8> &omega,
                    poly::NoWrapRewriter &nwr) -> IR::TreeResult {
    const auto *BT = poly::getBackedgeTakenCount(*se_, L);
    if (llvm::isa<llvm::SCEVCouldNotCompute>(BT)) return {};
    Arena<> *salloc{shortAllocator()};
    // TODO: check pointing seems dangerous, as
    // we'd have to make sure none of the allocated instructions
    // can be referenced again (e.g., through the free list)
    // TODO: use llvm::getLoopEstimatedTripCount
    utils::Valid<poly::Loop> AL = poly::Loop::construct(
      instructions_, L, nwr.visit(BT), {llvmToInternalMap, li_, se_});
    IR::TreeResult tr = parseExitBlocks(L, llvmToInternalMap);
    tr.rejectDepth =
      std::max(tr.rejectDepth, int(omega.size() - AL->getNumLoops()));
    omega.push_back(0); // we start with 0 at the end, walking backwards
    tr = parseBlocks(L->getHeader(), L->getLoopLatch(), L, llvmToInternalMap,
                     omega, AL, tr);
    omega.pop_back();
    if (tr.accept(int(omega.size()) - 1)) return tr;
    salloc->reset();
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
  auto parseExitBlocks(llvm::Loop *L,
                       dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap)
    -> IR::TreeResult {
    IR::TreeResult tr;
    for (auto &P : L->getExitBlock()->phis()) {
      for (unsigned i = 0, N = P.getNumIncomingValues(); i < N; ++i) {
        auto *J = llvm::dyn_cast<llvm::Instruction>(P.getIncomingValue(i));
        if (!J || !L->contains(J)) continue;
        tr =
          instructions_.getValue(J, nullptr, {llvmToInternalMap, li_, se_}, tr)
            .second;
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
  /// can optimize the continuous successful block we've produced, and return a
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
                 dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap,
                 math::Vector<int, 8> &omega,
                 poly::NoWrapRewriter &nwr) -> IR::TreeResult {
    unsigned n_sub_loops = subLoops.size();
    // This is a special case, as it is when we build poly::Loop
    if (!n_sub_loops) return initLoopTree(L, llvmToInternalMap, omega, nwr);
    int depth = int(omega.size());
    bool failed = false;
    IR::TreeResult tr = parseExitBlocks(L, llvmToInternalMap);
    omega.push_back(0); // we start with 0 at the end, walking backwards
    poly::Loop *AL = nullptr;
    llvm::BasicBlock *E = L->getLoopLatch();
    for (size_t i = n_sub_loops; i--; --omega.back()) {
      llvm::Loop *sub_loop = subLoops[i];
      // we need to parse backwards, so we first evaluate
      // TODO: support having multiple exit blocks?
      IR::TreeResult trec = runOnLoop(sub_loop, sub_loop->getSubLoops(),
                                      llvmToInternalMap, omega, nwr);
      if (trec.accept(depth)) {
        if (!AL) AL = trec.getLoop();
        if (AL) {
          // recursion succeeded; see if we can connect the path
          llvm::BasicBlock *sub_loop_exit = sub_loop->getExitBlock();
          // for fusion, we need to build a path from subLoopExit to E
          // where E is the preheader of the preceding loopnest
          IR::TreeResult trblock =
            parseBlocks(sub_loop_exit, E, L, llvmToInternalMap, omega, AL, tr);
          if (trblock.accept(depth)) {
            tr = trblock;
            tr *= trec;
          } else {
            failed = true;
            if (tr.accept(depth)) optimize(tr, llvmToInternalMap);
            // we start now with trec
            tr = trec;
          }
          E = sub_loop->getLoopPreheader(); // want to draw a path from trec
          continue;
        }
      }
      // we reject, because we failed to build a trec with a LoopNest
      failed = true;
      optimize(tr, llvmToInternalMap);
      tr = {};
      // we don't need to draw a path from anything, so only exit needed
      if (i) E = subLoops[i - 1]->getExitBlock();
    }
    if (failed) {
      if (tr.accept(depth)) optimize(tr, llvmToInternalMap);
      omega.pop_back();
      return {};
    }
    // now we try to go from E to H
    IR::TreeResult trblock =
      parseBlocks(L->getHeader(), E, L, llvmToInternalMap, omega, AL, tr);
    if (trblock.reject(depth)) {
      optimize(tr, llvmToInternalMap); // optimize old tr
      tr = {};
    } else tr = trblock;
    omega.pop_back();
    return tr;
  }
  void peelLoops(IR::TreeResult tr,
                 dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap) {
    llvm::SCEVExpander scevexpdr(*se_, instructions_.dataLayout(),
                                 "DoNotOptOuterLoops");
    // TODO: define insertion point IP
    if (unsigned num_reject = tr.rejectDepth)
      for (IR::Addr *addr : tr.getAddr())
        peelLoops(instructions_, addr, num_reject,
                  {llvmToInternalMap, li_, se_}, scevexpdr);
  }
  /// void Addr::updateOffsMat(Arena<> *alloc, size_t numToPeel,
  ///                             llvm::ScalarEvolution *SE);
  /// remove the `numToPeel` outermost loops from `this`.
  static void updateOffsMat(IR::Cache &cache, IR::Addr *A, ptrdiff_t numToPeel,
                            IR::LLVMIRBuilder LB,
                            llvm::SCEVExpander &scevexpdr) {
    utils::assume(numToPeel > 0);
    // need to condition on loop
    // remove the numExtraLoopsToPeel from Rt
    // that is, we want to move Rt(_,_(end-numExtraLoopsToPeel,end))
    // would this code below actually be expected to boost
    // performance?
    // if(Bt.numCol()+numExtraLoopsToPeel>Bt.rowStride())
    //   Bt.resize(Bt.numRow(),Bt.numCol(),Bt.numCol()+numExtraLoopsToPeel);
    // order of loops in Rt is outermost->innermost
    const IR::Addr *CA = A;
    DensePtrMatrix<int64_t> oldOffsMat{CA->offsetMatrix()},
      Rt{CA->indexMatrix()};
    ptrdiff_t dynSymInd = A->getSymbolicOffsets().size();
    A->incrementNumDynSym(numToPeel);
    MutPtrVector<IR::Value *> sym{A->getSymbolicOffsets()};
    A->setOffSym(
      cache.getAllocator()->allocate<int64_t>(sym.size() * A->numDim()));
    MutDensePtrMatrix<int64_t> offsMat{A->offsetMatrix()};
    if (dynSymInd) offsMat[_, _(0, dynSymInd)] << oldOffsMat;
    llvm::Loop *L = A->getAffLoop()->getLLVMLoop();
    for (ptrdiff_t d = A->getAffLoop()->getNumLoops() - numToPeel; d--;)
      L = L->getParentLoop();
    llvm::ScalarEvolution *SE = LB.SE_;
    for (ptrdiff_t i = numToPeel; i;) {
      L = L->getParentLoop();
      if (allZero(Rt[_, --i])) continue;
      // push the SCEV
      auto *iTyp = L->getInductionVariable(*SE)->getType();
      const llvm::SCEV *S = SE->getAddRecExpr(
        SE->getZero(iTyp), SE->getOne(iTyp), L, llvm::SCEV::NoWrapMask);
      llvm::Instruction *IP = L->getLoopPreheader()->getFirstNonPHI();
      llvm::Value *TCV = scevexpdr.expandCodeFor(S, iTyp, IP);
      offsMat[_, dynSymInd] << Rt[_, i];
      sym[dynSymInd++] = cache.getValueOutsideLoop(TCV, LB);
    }
  }

  // remove outer `numToPeel` loops
  // FIXME: should become idemppotent;
  // Two approaches:
  // 1. arg should be numToKeep?
  // But the problem with this is, how do we then compare across
  // addr and loops?
  // 2. Keep track of whether we have already peeled.
  // Alternatively, could guarantee that `loop->removeOuterMost` only gets
  // called once.
  static void peelLoops(IR::Cache &cache, IR::Addr *A, ptrdiff_t numToPeel,
                        IR::LLVMIRBuilder LB, llvm::SCEVExpander &scevexpdr) {
    /// Addr's maxDepth = tr.maxDepth
    /// natDepth is Addr's number of loops upon construction
    invariant(numToPeel > 0);
    A->getAffLoop()->removeOuterMost(cache, numToPeel, LB, scevexpdr);
    ptrdiff_t num_loops = A->getCurrentDepth();
    invariant(numToPeel <= A->getMaxDepth());
    // we need to compare numToPeel with actual depth
    // because we might have peeled some loops already
    invariant(A->getMaxDepth() >= num_loops);
    numToPeel -= A->getMaxDepth() - num_loops;
    if (numToPeel == 0) return;
    // we're dropping the outer-most `numToPeel` loops
    // first, we update offsMat
    updateOffsMat(cache, A, numToPeel, LB, scevexpdr);
    // current memory layout (outer <-> inner):
    // - denom (1)
    // - offsetOmega (arrayDim)
    // - indexMatrix (arrayDim x numLoops)
    // - fusionOmegas (numLoops+1)
    int64_t *dst = A->indMatPtr(), *src = dst + numToPeel;
    ptrdiff_t dim = A->numDim(), old_nat_depth = A->getNaturalDepth(),
              natural_depth = old_nat_depth = num_loops - numToPeel,
              curr_depth1 = A->peelLoops(numToPeel);
    invariant(A->getCurrentDepth() < num_loops);
    // we want d < dim for indexMatrix, and then == dim for fusion omega
    for (ptrdiff_t d = dim;;) {
      std::copy_n(src, (d) ? natural_depth : (curr_depth1 + 1), dst);
      if (!(d--)) break;
      src += old_nat_depth;
      dst += natural_depth;
    }
  }

  /// optimize(
  ///   IR::TreeResult tr,
  ///   dict::map<llvm::Value*,IR::Value*> *llvmToInternalMap,
  ///   llvm::Instruction *IP
  /// )
  /// optimizes and replaces the LLVM IR referred to by `tr`
  /// IP is end of the loop preheader, where we can
  /// insert hoisted expressions.
  /// TODO: do we need this?
  void optimize(IR::TreeResult tr,
                dict::map<llvm::Value *, IR::Value *> *llvmToInternalMap) {
    // now we build the LinearProgram
    deps_.clear();
    // first, we peel loops for which affine repr failed
    peelLoops(tr, llvmToInternalMap);
    lp::LoopBlock loop_block{deps_, *shortAllocator()};
    lp::LoopBlock::OptimizationResult lpor =
      loop_block.optimize(instructions_, tr);
    if (!lpor.nodes) return;
    for (IR::Addr *addr : lpor.addr.getAddr())
      if (llvm::BasicBlock *BB = addr->getBasicBlock()) loop_bbs_.insert(BB);
    CostModeling::optimize(short_alloc_, loop_block.getDependencies(),
                           instructions_, loop_bbs_, erase_candidates_, lpor,
                           getTarget());
    loop_bbs_.clear();
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

    llvm::OptimizationRemarkAnalysis analysis{
      utils::remarkAnalysis(remarkName, L, J)};
    ore_->emit(analysis << remarkMessage);
  }
  // void buildInstructionGraph(LoopTree &root) {
  //     // predicates
  // }
  // ~TurboLoopPass() {
  //   for (auto l : loopForests) l->~LoopTree();
  // }
public:
  TurboLoop(llvm::Function &F, llvm::FunctionAnalysisManager &FAM)
    : tli_{&FAM.getResult<llvm::TargetLibraryAnalysis>(F)},
      tti_{&FAM.getResult<llvm::TargetIRAnalysis>(F)},
      li_{&FAM.getResult<llvm::LoopAnalysis>(F)},
      se_{&FAM.getResult<llvm::ScalarEvolutionAnalysis>(F)},
      ore_{&FAM.getResult<llvm::OptimizationRemarkEmitterAnalysis>(F)},
      assumption_cache_(FAM.getResult<llvm::AssumptionAnalysis>(F)),
      dom_tree_(FAM.getResult<llvm::DominatorTreeAnalysis>(F)),
      instructions_(F.getParent()),
      arch_{target::machine(*tti_, F.getContext()).arch_} {}
  // llvm::LoopNest LA = FAM.getResult<llvm::LoopNestAnalysis>(F);
  // llvm::AssumptionCache &AC = FAM.getResult<llvm::AssumptionAnalysis>(F);
  // llvm::DominatorTree &DT = FAM.getResult<llvm::DominatorTreeAnalysis>(F);
  // TLI = &FAM.getResult<llvm::TargetLibraryAnalysis>(F);
  auto run() -> llvm::PreservedAnalyses {

    if (!ore_->enabled()) ore_ = nullptr; // cheaper check
    if (ore_) {
      // llvm::OptimizationRemarkAnalysis
      // analysis{utils::remarkAnalysis("RegisterCount", *LI->begin())};
      // ORE->emit(analysis << "There are
      // "<<TTI->getNumberOfRegisters(0)<<" scalar registers");
      llvm::SmallString<32> str = llvm::formatv(
        "there are {0} scalar registers", tti_->getNumberOfRegisters(0));

      remark("ScalarRegisterCount", *li_->begin(), str);
      str = llvm::formatv("there are {0} vector registers",
                          getTarget().getVectorRegisterBitWidth());
      remark("VectorRegisterCount", *li_->begin(), str);
    }
    // Builds the loopForest, constructing predicate chains and loop nests
    dict::map<llvm::Value *, IR::Value *> llvm_to_internal_map;
    IR::TreeResult tr = initializeLoopForest(&llvm_to_internal_map);
    if (tr.accept(0)) optimize(tr, &llvm_to_internal_map);
    return llvm::PreservedAnalyses::none();
  }
};
