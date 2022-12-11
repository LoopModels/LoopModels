#pragma once

#include "./ArrayReference.hpp"
#include "./CostModeling.hpp"
#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./LoopForest.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Predicate.hpp"
#include "./Schedule.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
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

[[maybe_unused]] static size_t countNumLoopsPlusLeaves(const llvm::Loop *L) {
    const std::vector<llvm::Loop *> &subLoops = L->getSubLoops();
    if (subLoops.size() == 0)
        return 1;
    size_t numLoops = subLoops.size();
    for (auto &SL : subLoops)
        numLoops += countNumLoopsPlusLeaves(SL);
    return numLoops;
}

// [[maybe_unused]] static bool isKnownOne(llvm::Value *x) {
//     if (llvm::ConstantInt *constInt = llvm::dyn_cast<llvm::ConstantInt>(x)) {
//         return constInt->isOne();
//     } else if (llvm::Constant *constVal = llvm::dyn_cast<llvm::Constant>(x))
//     {
//         return constVal->isOneValue();
//     }
//     return false;
// }

// requires `isRecursivelyLCSSAForm`
class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
  public:
    auto run(llvm::Function &F, llvm::FunctionAnalysisManager &AM)
        -> llvm::PreservedAnalyses;
    // llvm::SmallVector<AffineLoopNest<true>, 0> affineLoopNests;
    // one reason to prefer SmallVector is because it bounds checks `ifndef
    // NDEBUG`
    // [[no_unique_address]] llvm::SmallVector<LoopTree, 0> loopTrees;
    [[no_unique_address]] llvm::SmallVector<LoopTree *> loopForests;
    [[no_unique_address]] llvm::DenseMap<llvm::Loop *, LoopTree *> loopMap;
    // [[no_unique_address]] BlockPredicates predicates;
    // llvm::AssumptionCache *AC;
    [[no_unique_address]] const llvm::TargetLibraryInfo *TLI;
    [[no_unique_address]] const llvm::TargetTransformInfo *TTI;
    [[no_unique_address]] llvm::LoopInfo *LI;
    [[no_unique_address]] llvm::ScalarEvolution *SE;
    [[no_unique_address]] LinearProgramLoopBlock loopBlock;
    [[no_unique_address]] llvm::BumpPtrAllocator allocator;
    [[no_unique_address]] Instruction::Cache instrCache;
    [[no_unique_address]] unsigned registerCount;

    /// the process of building the LoopForest has the following steps:
    /// 1. build initial forest of trees
    /// 2. instantiate AffineLoopNest<true>s; any non-affine loops
    ///    are pruned, and their inner loops added as new, separate forests.
    /// 3. Existing forests are searched for indirect control flow between
    ///    successive loops. In all such cases, the loops at that level are
    ///    split into separate forests.
    void initializeLoopForest() {
        // NOTE: LoopInfo stores loops in reverse program order (opposite of
        // loops)
        auto RLI = llvm::reverse(*LI);
        auto RLIB = RLI.begin();
        auto RLIE = RLI.end();
        if (RLIB == RLIE)
            return;
        // pushLoopTree wants a direct path from the last loop's exit block to
        // E; we drop loops until we find one for which this is trivial.
        llvm::BasicBlock *E = (*--RLIE)->getExitBlock();
        while (!E) {
            if (RLIE == RLIB)
                return;
            E = (*--RLIE)->getExitingBlock();
        }
        // pushLoopTree wants a direct path from H to the first loop's header;
        // we drop loops until we find one for which this is trivial.
        llvm::BasicBlock *H = (*RLIB)->getLoopPreheader();
        while (!H) {
            if (RLIE == RLIB)
                return;
            H = (*++RLIB)->getLoopPreheader();
        }
        // should normally be stack allocated; we want to avoid different
        // specializations for `llvm::reverse(*LoopInfo)` and
        // `llvm::Loop->getSubLoops()`.
        // But we could consider specializing on top level vs not.
        llvm::SmallVector<llvm::Loop *> revLI{RLIB, RLIE + 1};
        // Track position within the loop nest
        llvm::SmallVector<unsigned> omega;
        llvm::SmallVector<LoopTree *> forest;
        pushLoopTree(forest, nullptr, omega, revLI, H, E);
        for (auto &forest : loopForests)
            forest->addZeroLowerBounds(loopMap);
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
    /// 1. `llvm::SmallVectorImpl<llvm::Loop *> &forest`: the forest in which we
    /// are planting our tree.
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
    auto pushLoopTree(llvm::SmallVectorImpl<LoopTree *> &forest, llvm::Loop *L,
                      llvm::SmallVector<unsigned> &omega,
                      llvm::ArrayRef<llvm::Loop *> subLoops,
                      llvm::BasicBlock *H, llvm::BasicBlock *E) -> size_t {

        omega.push_back(0);
        if (size_t numSubLoops = subLoops.size()) {
            // branches of this tree;
            llvm::SmallVector<LoopTree *> branches;
            branches.reserve(numSubLoops);
            llvm::SmallVector<InstructionBlock *> branchBlocks;
            branchBlocks.reserve(numSubLoops + 1);
            for (size_t i = 0; i < numSubLoops; ++i) {
                llvm::Loop *subLoop = subLoops[i];
                if (size_t depth = pushLoopTree(
                        branches, subLoop, omega, subLoop->getSubLoops(),
                        subLoop->getHeader(), subLoop->getExitingBlock())) {
                    // pushLoopTree succeeded, and we have `depth` inner loops
                    // within `subLoop` (inclusive, i.e. `depth == 1` would
                    // indicate that `subLoop` doesn't have any subLoops itself,
                    // which we check with the following assertion:
                    assert((depth > 1) || (subLoop->getSubLoops().empty()));

                    // Now we check if we can create a direct path from `H` to
                    // `subLoop->getLoopPreheader();`
                    llvm::BasicBlock *subLoopPreheader =
                        subLoop->getLoopPreheader();
                    if (H == subLoopPreheader) {
                        // trivial fast path

                    } else if (InstructionBlock *iblck =
                                   pushInstructionBlock(H, subLoopPreheader)) {
                        branchBlocks.push_back(iblck);
                    } else {
                        // oops, no direct path, we split
                    }
                    // for the next loop, we'll want a path to its preheader
                    // from this loop's exit block.
                    H = subLoop->getExitBlock();
                } else {
                    // `depth == 0` indicates failure, therefore we need to
                    // split loops
                    //
                }
                ++omega.back();
            }
        } else {
            // we need `H` to have a direct path to `E`.
        }
        return 0;
    }

    /// try to construct a direct path from `llvm::BasicBlock *BBsrc` to
    /// `llvm::BasicBlock *BBdst`, so that we fuse it into a single
    /// `InstructionBlock*`.
    ///
    /// It assumes we have at least one block, in which case start == stop.
    /// That is, it loops, and checks `start++ == stop` to break.
    /// Note that this means that empty calls are not allowed, i.e. if it's
    /// undefined if stop < start (it'll probably iterate until it crashes).
    [[nodiscard]] auto pushInstructionBlock(llvm::BasicBlock *start,
                                            llvm::BasicBlock *stop)
        -> InstructionBlock * {
        if (start++ == stop) {
            auto *iblck =
                new (allocator) InstructionBlock(allocator, instrCache, start);
        }
        return nullptr;
    }
    /// returns index to the loop whose preheader we place it in.
    /// if it equals depth, then we must place it into the inner most loop
    /// header.
    static auto invariant(
        llvm::Value &V,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
            4> const &LPS) -> size_t {
        size_t depth = LPS.size();
        for (auto LP = LPS.rbegin(); LP != LPS.rend(); ++LP) {
            bool changed = false;
            bool invariant = LP->first->makeLoopInvariant(&V, changed);
            if (!(changed | invariant)) {
                return depth;
            }
            depth--;
        }
        return 0;
    }
    auto isLoopPreHeader(const llvm::BasicBlock *BB) const -> bool {
        if (const llvm::Instruction *term = BB->getTerminator())
            if (const auto *BI = llvm::dyn_cast<llvm::BranchInst>(term))
                if (!BI->isConditional())
                    return LI->isLoopHeader(BI->getSuccessor(0));
        return false;
    }
    inline static auto containsPeeled(const llvm::SCEV *S, size_t numPeeled)
        -> bool {
        return llvm::SCEVExprContains(S, [numPeeled](const llvm::SCEV *S) {
            if (auto r = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
                if (r->getLoop()->getLoopDepth() <= numPeeled)
                    return true;
            return false;
        });
    }
    static void addSymbolic(Vector<int64_t> &offsets,
                            llvm::SmallVector<const llvm::SCEV *, 3> &symbols,
                            const llvm::SCEV *S, int64_t x = 1) {
        if (size_t i = findSymbolicIndex(symbols, S)) {
            offsets[i] += x;
        } else {
            symbols.push_back(S);
            offsets.push_back(x);
        }
    }
    static auto blackListAllDependentLoops(const llvm::SCEV *S) -> uint64_t {
        uint64_t flag{0};
        if (const auto *x = llvm::dyn_cast<const llvm::SCEVNAryExpr>(S)) {
            if (const auto *y = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(x))
                flag |= uint64_t(1) << y->getLoop()->getLoopDepth();
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
        } else if (const auto *x =
                       llvm::dyn_cast<const llvm::SCEVCastExpr>(S)) {
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
            return flag;
        } else if (const auto *x =
                       llvm::dyn_cast<const llvm::SCEVUDivExpr>(S)) {
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
            return flag;
        }
        return flag;
    }
    static auto blackListAllDependentLoops(const llvm::SCEV *S,
                                           size_t numPeeled) -> uint64_t {
        return blackListAllDependentLoops(S) >> (numPeeled + 1);
    }
    // translates scev S into loops and symbols
    auto
    fillAffineIndices(MutPtrVector<int64_t> v, Vector<int64_t> &offsets,
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
                        v(end - loopInd) += *c;
                        return fillAffineIndices(v, offsets, symbolicOffsets,
                                                 x->getOperand(0), mlt,
                                                 numPeeled);
                    } else
                        blackList |= (uint64_t(1) << uint64_t(loopInd));
                }
                // we separate out the addition
                // the multiplication was either peeled or involved
                // non-const multiple
                blackList |=
                    fillAffineIndices(v, offsets, symbolicOffsets,
                                      x->getOperand(0), mlt, numPeeled);
                // and then add just the multiple here as a symbolic offset
                const llvm::SCEV *addRec = SE->getAddRecExpr(
                    SE->getZero(x->getOperand(0)->getType()), x->getOperand(1),
                    x->getLoop(), x->getNoWrapFlags());
                addSymbolic(offsets, symbolicOffsets, addRec, mlt);
                return blackList;
            } else if (loopInd >= 0)
                blackList |= (uint64_t(1) << uint64_t(loopInd));
        } else if (llvm::Optional<int64_t> c = getConstantInt(S)) {
            offsets[0] += *c;
            return 0;
        } else if (const auto *ex =
                       llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
            return fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(0), mlt, numPeeled) |
                   fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(1), mlt, numPeeled);
        } else if (const auto *ex =
                       llvm::dyn_cast<const llvm::SCEVMulExpr>(S)) {
            if (auto op = getConstantInt(ex->getOperand(0))) {
                return fillAffineIndices(v, offsets, symbolicOffsets,
                                         ex->getOperand(1), mlt * (*op),
                                         numPeeled);

            } else if (auto op = getConstantInt(ex->getOperand(1))) {
                return fillAffineIndices(v, offsets, symbolicOffsets,
                                         ex->getOperand(0), mlt * (*op),
                                         numPeeled);
            }
        } else if (const auto *ex = llvm::dyn_cast<llvm::SCEVCastExpr>(S))
            return fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(0), mlt, numPeeled);
        addSymbolic(offsets, symbolicOffsets, S, mlt);
        return blackList | blackListAllDependentLoops(S, numPeeled);
    }
    auto arrayRef(LoopTree &LT, llvm::Instruction *ptr,
                  llvm::Instruction *loadOrStore,
                  Predicate::PredicatesOld &pred, const llvm::SCEV *elSize)
        -> llvm::Optional<ArrayReference> {
        llvm::Loop *L = LT.loop;
        if (L)
            llvm::errs() << "arrayRef for " << *L << "\n";
        else
            llvm::errs() << "arrayRef for top-level\n";
        // const llvm::SCEV *scev = SE->getSCEV(ptr);
        // code modified from
        // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
        llvm::errs() << "ptr: " << *ptr << "\n";
        // llvm::Value *po = llvm::getPointerOperand(ptr);
        // if (!po)
        //     return {};
        // llvm::errs() << "ptr operand: " << *po << "\n";
        const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);

        llvm::errs() << "accessFn: " << *accessFn << "\n"
                     << "\nSE->getSCEV(ptr) = " << *(SE->getSCEV(ptr)) << "\n";

        const llvm::SCEV *pb = SE->getPointerBase(accessFn);
        llvm::errs() << "base pointer: " << *pb << "\n";
        const auto *basePointer = llvm::dyn_cast<llvm::SCEVUnknown>(pb);
        // Do not delinearize if we cannot find the base pointer.
        if (!basePointer)
            llvm::errs() << "ArrayReference failed because !basePointer\n";
        if (!basePointer) {
            conditionOnLoop(L);
            return {};
        }
        llvm::errs() << "base pointer SCEVUnknown: " << *basePointer << "\n";
        accessFn = SE->getMinusSCEV(accessFn, basePointer);
        llvm::errs() << "diff accessFn: " << *accessFn << "\n";
        llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
        llvm::delinearize(*SE, accessFn, subscripts, sizes, elSize);
        assert(subscripts.size() == sizes.size());
        // SHOWLN(sizes.size());
        AffineLoopNest<true> &aln = loopMap[L]->affineLoop;
        if (sizes.size() == 0)
            return ArrayReference(basePointer, &aln, loadOrStore,
                                  std::move(sizes), std::move(subscripts),
                                  pred);
        size_t numLoops{aln.getNumLoops()};
        // numLoops x arrayDim
        // IntMatrix R(numLoops, subscripts.size());
        size_t numPeeled = L->getLoopDepth() - numLoops;
        // numLoops x arrayDim
        IntMatrix Rt(subscripts.size(), numLoops);
        IntMatrix Bt;
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
        uint64_t blackList{0};
        llvm::errs() << "AccessFN: " << *accessFn << "\n";
        {
            Vector<int64_t> offsets;
            for (size_t i = 0; i < subscripts.size(); ++i) {
                llvm::errs()
                    << "subscripts[" << i << "] = " << *subscripts[i] << "\n";
                offsets.clear();
                offsets.pushBack(0);
                blackList |=
                    fillAffineIndices(Rt(i, _), offsets, symbolicOffsets,
                                      subscripts[i], 1, numPeeled);
                Bt.resize(subscripts.size(), offsets.size());
                llvm::errs() << "offsets = [";
                for (size_t i = 0; i < offsets.size(); ++i) {
                    if (i)
                        llvm::errs() << ", ";
                    llvm::errs() << offsets[i];
                }
                llvm::errs() << "]\n";
                Bt(i, _) = offsets;
            }
        }
        // SHOW(Bt.numCol());
        // CSHOW(offsets.size());
        // CSHOWLN(symbolicOffsets.size());
        if (blackList) {
            // blacklist: inner - outer
            uint64_t leadingZeros = std::countl_zero(blackList);
            uint64_t numExtraLoopsToPeel = 64 - leadingZeros;
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
            for (size_t i = 1; i < remainingLoops; ++i)
                P = P->getParentLoop();
            // remove
            conditionOnLoop(P->getParentLoop());
            for (size_t i = remainingLoops; i < numLoops; ++i) {
                P = P->getParentLoop();
                if (allZero(Rt(_, i)))
                    continue;
                // push the SCEV
                auto IntType = P->getInductionVariable(*SE)->getType();
                const llvm::SCEV *S =
                    SE->getAddRecExpr(SE->getZero(IntType), SE->getOne(IntType),
                                      P, llvm::SCEV::NoWrapMask);
                if (size_t j = findSymbolicIndex(symbolicOffsets, S)) {
                    Bt(_, j) += Rt(_, i);
                } else {
                    size_t N = Bt.numCol();
                    Bt.resizeCols(N + 1);
                    Bt(_, N) = Rt(_, i);
                    symbolicOffsets.push_back(S);
                }
            }
            Rt.truncateCols(numLoops - numExtraLoopsToPeel);
        }
        ArrayReference ref(basePointer, &aln, loadOrStore, std::move(sizes),
                           std::move(symbolicOffsets), pred);
        ref.resize(subscripts.size());
        ref.indexMatrix() = Rt.transpose();
        // SHOWLN(symbolicOffsets.size());
        // SHOW(ref.offsetMatrix().numRow());
        // CSHOWLN(ref.offsetMatrix().numCol());
        // SHOW(Bt.numRow());
        // CSHOWLN(Bt.numCol());
        SHOWLN(Rt);
        SHOWLN(Bt);
        ref.offsetMatrix() = Bt;
        // TODO: update schedule, array ref, and offsets when pruning failed
        // loops
        for (size_t i = 0; i < subscripts.size(); ++i) {
            llvm::errs() << "Array Dim " << i << ":\nSize: " << *ref.sizes[i]
                         << "\nSubscript: " << *subscripts[i] << "\n";
            // if (const llvm::SCEVUnknown *param =
            // llvm::dyn_cast<llvm::SCEVUnknown>(subscripts[i])) {
            if (llvm::isa<llvm::SCEVUnknown>(subscripts[i])) {
                llvm::errs() << "SCEVUnknown\n";
                // } else if (const llvm::SCEVNAryExpr *param =
                // llvm::dyn_cast<llvm::SCEVNAryExpr>(subscripts[i])) {
            } else if (llvm::isa<llvm::SCEVNAryExpr>(subscripts[i])) {
                llvm::errs() << "SCEVNAryExpr\n";
            }
        }
        return ref;
    }
    // LoopTree &getLoopTree(unsigned i) { return loopTrees[i]; }
    auto getLoopTree(llvm::Loop *L) -> LoopTree * { return loopMap[L]; }
    auto addLoad(LoopTree &LT, Predicate::PredicatesOld &pred,
                 llvm::LoadInst *I, llvm::SmallVector<unsigned> &omega)
        -> bool {
        llvm::Value *ptr = I->getPointerOperand();
        // llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        // TODO: support top level array refs
        if (LT.loop) {
            if (auto *iptr = llvm::dyn_cast<llvm::Instruction>(ptr)) {
                if (llvm::Optional<ArrayReference> re =
                        arrayRef(LT, iptr, I, pred, elSize)) {
                    SHOWLN(I);
                    SHOWLN(*I);
                    llvm::errs() << "omega = [" << omega.front();
                    for (size_t i = 1; i < omega.size(); ++i)
                        llvm::errs() << ", " << omega[i];
                    llvm::errs() << "]\n";
                    LT.memAccesses.emplace_back(std::move(*re), I, omega);
                    ++omega.back();
                    llvm::errs() << "Succesfully added load\n"
                                 << LT.memAccesses.back() << "\n";
                    return false;
                }
            }
            llvm::errs() << "Failed for load instruction: " << *I << "\n";
            return true;
        }
        return false;
    }
    auto addStore(LoopTree &LT, Predicate::PredicatesOld &pred,
                  llvm::StoreInst *I, llvm::SmallVector<unsigned> &omega)
        -> bool {
        llvm::Value *ptr = I->getPointerOperand();
        // llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        // TODO: support top level array refs
        if (LT.loop) {
            if (auto *iptr = llvm::dyn_cast<llvm::Instruction>(ptr)) {
                if (llvm::Optional<ArrayReference> re =
                        arrayRef(LT, iptr, I, pred, elSize)) {
                    SHOWLN(I);
                    SHOWLN(*I);
                    llvm::errs() << "omega = [" << omega.front();
                    for (size_t i = 1; i < omega.size(); ++i)
                        llvm::errs() << ", " << omega[i];
                    llvm::errs() << "]\n";
                    LT.memAccesses.emplace_back(std::move(*re), I, omega);
                    ++omega.back();
                    llvm::errs() << "Succesfully added store\n"
                                 << LT.memAccesses.back() << "\n";
                    return false;
                }
            }
            llvm::errs() << "Failed for store instruction: " << *I << "\n";
            return true;
        }
        return false;
    }

    void parseBB(LoopTree &LT, llvm::BasicBlock *BB,
                 Predicate::PredicatesOld &pred,
                 llvm::SmallVector<unsigned> &omega) {
        // omega.push_back(0);
        llvm::errs() << "\nParsing BB: " << BB << "\n"
                     << *BB << "\nNested in Loop: ";
        if (LT.loop)
            llvm::errs() << *LT.loop << "\n";
        else
            llvm::errs() << "toplevel\n";
        if (pred.size())
            SHOWLN(pred);
        llvm::errs() << "omega = [" << omega.front();
        for (size_t i = 1; i < omega.size(); ++i)
            llvm::errs() << ", " << omega[i];
        llvm::errs() << "]\n";
        for (llvm::Instruction &I : *BB) {
            llvm::errs() << "Parsing Instr: " << I << "\n";
            if (LT.loop)
                assert(LT.loop->contains(&I));
            if (I.mayReadFromMemory()) {
                if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I))
                    if (addLoad(LT, pred, LI, omega))
                        return;
            } else if (I.mayWriteToMemory())
                if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I))
                    if (addStore(LT, pred, SI, omega))
                        return;
        }
        // omega.pop_back();
    }
    // we fill omegas, we have loop pos only, not shifts
    // pR: 0
    // pL: 0
    // pL: 0
    //
    // [0, 0]
    void parseLoop(LoopTree &LT, llvm::SmallVector<unsigned> &omega) {
#ifndef NDEBUG
        size_t numOmega = omega.size();
        // FIXME:
        // two issues, currently:
        // 1. multiple parses produce the same omega
        // 2. we have the same BB showing up multiple times
        // for (auto &&path : LT.paths)
        //     for (auto PBB : path) {
        //         assert(!paths.contains(PBB.basicBlock));
        //         paths.insert(PBB.basicBlock);
        //     }
#endif
        llvm::SmallPtrSet<llvm::BasicBlock *, 32> paths;
        omega.push_back(0);
        assert(LT.subLoops.size() + 1 == LT.paths.size());
        // llvm::Loop *L = LT.loop;
        // now we walk blocks
        // auto &subLoops = L->getSubLoops();
        for (size_t i = 0; i < LT.subLoops.size(); ++i) {
            llvm::errs() << "Parsing loop, i = " << i;
            if (LT.loop)
                llvm::errs() << ": " << *LT.loop;
            llvm::errs() << "\n";
            for (auto &&PBB : LT.paths[i])
                parseBB(LT, PBB.basicBlock, PBB.predicates, omega);
            parseLoop(*LT.subLoops[i], omega);
            ++omega.back();
        }
        for (auto PBB : LT.paths.back())
            parseBB(LT, PBB.basicBlock, PBB.predicates, omega);
        omega.pop_back();
#ifndef NDEBUG
        assert(omega.size() == numOmega);
#endif
    }
    void parseNest() {
        llvm::SmallVector<unsigned> omega;
        for (auto forest : loopForests) {
            omega.clear();
            parseLoop(*forest, omega);
            // auto &forest = ;
            // for (size_t i = 0; i < forest.size(); ++i) {
            //     omega.front() = i;
            //     parseLoop(loopTrees[forest.subLoops[i]], omega);
            // }
        }
    }

    // bool parseLoop(llvm::Loop *L) {
    //     for (auto &BB : L->getBlocks()) {
    //         llvm::Loop *P = LI->getLoopFor(BB);
    //         if (parseBB(P, BB)) {
    //             conditionOnLoop(P);
    //             return true;
    //         }
    //     }
    //     return false;
    // }
    void peelOuterLoops(llvm::Loop *L, size_t numToPeel) {
        peelOuterLoops(*loopMap[L], numToPeel);
    }
    // peelOuterLoops is recursive inwards
    void peelOuterLoops(LoopTree &LT, size_t numToPeel) {
        for (auto SL : LT)
            peelOuterLoops(*SL, numToPeel);
        LT.affineLoop.removeOuterMost(numToPeel, LT.loop, *SE);
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
    void conditionOnLoop(LoopTree *LT) {
        if (LT->parentLoop == nullptr)
            return;
        LoopTree &PT = *LT->parentLoop;
        size_t numLoops = LT->getNumLoops();
        for (auto ST : *LT)
            peelOuterLoops(*ST, numLoops);

        LT->parentLoop = nullptr; // LT is now top of the tree
        loopForests.push_back(LT);
        llvm::SmallVector<LoopTree *> &friendLoops = PT.subLoops;
        // SHOW(LTID);
        for (auto id : friendLoops)
            llvm::errs() << ", " << id;
        llvm::errs() << "\n";
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
                llvm::SmallVector<LoopTree *> tmp;
                tmp.reserve(numFriendLoops - j);
                // for paths, we're dropping LT
                // thus, our paths are paths(_(0,j)), paths(_(j,end))
                llvm::SmallVector<PredicatedChain> paths;
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

    auto parseLoopPrint(auto B, auto E) -> bool {
        // Schedule sch(depth);
        size_t omega = 0;
        for (auto &&it = B; it != E; ++it, ++omega) {
            llvm::Loop *LP = *it;
            if (auto *inductOuter = LP->getInductionVariable(*SE)) {
                llvm::errs()
                    << "Outer InductionVariable: " << *inductOuter << "\n";
                if (const llvm::SCEV *backEdgeTaken =
                        getBackedgeTakenCount(*SE, LP)) {
                    llvm::errs() << "Back edge taken count: " << *backEdgeTaken
                                 << "\n\ttrip count: "
                                 << *(SE->getAddExpr(
                                        backEdgeTaken,
                                        SE->getOne(backEdgeTaken->getType())))
                                 << "\n";
                    continue;
                }
            }
            return true;
        }
        return false;
    }
    auto isLoopDependent(llvm::Value *v) const -> bool {
        for (auto &L : *LI)
            if (!L->isLoopInvariant(v))
                return true;
        return false;
    }
    auto mayReadOrWriteMemory(llvm::Value *v) const -> bool {
        if (auto inst = llvm::dyn_cast<llvm::Instruction>(v))
            if (inst->mayReadOrWriteMemory())
                return true;
        return false;
    }
    void fillLoopBlock(LoopTree &root) {
        for (auto &&mem : root.memAccesses)
            loopBlock.addMemory(mem.truncateSchedule());
        // loopBlock.memory.push_back(mem.truncateSchedule());
        for (size_t i = 0; i < root.subLoops.size(); ++i)
            fillLoopBlock(*root.subLoops[i]);
    }

    void buildInstructionGraph(LoopTree &root) {
        // predicates
    }
};
