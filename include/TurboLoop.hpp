#pragma once

#include "./ArrayReference.hpp"
#include "./IntegerMap.hpp"
#include "./LoopBlock.hpp"
#include "./LoopForest.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "./UniqueIDMap.hpp"
#include "Predicate.hpp"
#include <algorithm>
#include <bit>
#include <bits/iterator_concepts.h>
#include <cassert>
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
    llvm::PreservedAnalyses run(llvm::Function &F,
                                llvm::FunctionAnalysisManager &AM);
    // llvm::SmallVector<AffineLoopNest, 0> affineLoopNests;
    // one reason to prefer SmallVector is because it bounds checks `ifndef
    // NDEBUG`
    llvm::SmallVector<LoopTree, 0> loopTrees;
    llvm::SmallVector<unsigned> loopForests;
    llvm::DenseMap<llvm::Loop *, unsigned> loopMap;
    // Tree tree;
    // llvm::AssumptionCache *AC;
    const llvm::TargetLibraryInfo *TLI;
    const llvm::TargetTransformInfo *TTI;
    llvm::LoopInfo *LI;
    llvm::ScalarEvolution *SE;
    LoopBlock loopBlock;
    // const llvm::DataLayout *DL;
    unsigned registerCount;

    // the process of building the LoopForest has the following steps:
    // 1. build initial forest of trees
    // 2. instantiate AffineLoopNests; any non-affine loops
    //    are pruned, and their inner loops added as new, separate forests.
    // 3. Existing forests are searched for indirect control flow between
    //    successive loops. In all such cases, the loops at that level are
    //    split into separate forests.
    void initializeLoopForest() {
        // count the number of loops, and then reserve enough memory to avoid
        // the need for reallocations
        size_t numLoops = 0;
        for (auto &L : *LI)
            numLoops += countNumLoopsPlusLeaves(L);
        loopTrees.reserve(numLoops);
        loopMap.reserve(numLoops);
        // affineLoopNests.reserve(numLoops);
        // thus, we should be able to reference these by pointer.
        llvm::SmallVector<unsigned> forest;
        // NOTE: LoopInfo stores loops in reverse program order (opposite of
        // loops)
        std::vector<llvm::Loop *> revLI{llvm::reverse(*LI).begin(),
                                        llvm::reverse(*LI).end()};
        if (revLI.empty())
            return;
        llvm::BasicBlock *E = revLI.back()->getExitingBlock();
        while (!E) {
            revLI.pop_back();
            if (revLI.empty())
                return;
            E = revLI.back()->getExitingBlock();
        }
        llvm::BasicBlock *H = revLI.front()->getLoopPreheader();
        while (!H) {
            revLI.erase(revLI.begin());
            if (revLI.empty())
                return;
            H = revLI.front()->getLoopPreheader();
        }

        LoopTree::pushBack(loopTrees, loopForests, forest, nullptr, *SE, revLI,
                           H, E, true);
        // for (auto &L : llvm::reverse(*LI))
        //     LoopTree::pushBack(loopTrees, loopForests, forest, L, *SE);
        // LoopTree::invalid(loopTrees, loopForests, forest);
        // for (auto &lt : loopTrees)
        // SHOWLN(lt.affineLoop.A);
        for (auto &forest : loopForests)
            loopTrees[forest].addZeroLowerBounds(
                loopTrees, loopMap, std::numeric_limits<unsigned>::max());
    }

    // returns index to the loop whose preheader we place it in.
    // if it equals depth, then we must place it into the inner most loop
    // header..
    static size_t invariant(
        llvm::Value &V,
        llvm::SmallVector<
            std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
            4> const &LPS) {
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
    bool isLoopPreHeader(const llvm::BasicBlock *BB) const {
        if (const llvm::Instruction *term = BB->getTerminator())
            if (const llvm::BranchInst *BI =
                    llvm::dyn_cast<llvm::BranchInst>(term))
                if (!BI->isConditional())
                    return LI->isLoopHeader(BI->getSuccessor(0));
        return false;
    }
    inline static bool containsPeeled(const llvm::SCEV *S, size_t numPeeled) {
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
    static uint64_t blackListAllDependentLoops(const llvm::SCEV *S) {
        uint64_t flag{0};
        if (const llvm::SCEVNAryExpr *x =
                llvm::dyn_cast<const llvm::SCEVNAryExpr>(S)) {
            if (const llvm::SCEVAddRecExpr *y =
                    llvm::dyn_cast<const llvm::SCEVAddRecExpr>(x))
                flag |= uint64_t(1) << y->getLoop()->getLoopDepth();
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
        } else if (const llvm::SCEVCastExpr *x =
                       llvm::dyn_cast<const llvm::SCEVCastExpr>(S)) {
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
            return flag;
        } else if (const llvm::SCEVUDivExpr *x =
                       llvm::dyn_cast<const llvm::SCEVUDivExpr>(S)) {
            for (size_t i = 0; i < x->getNumOperands(); ++i)
                flag |= blackListAllDependentLoops(x->getOperand(i));
            return flag;
        }
        return flag;
    }
    static uint64_t blackListAllDependentLoops(const llvm::SCEV *S,
                                               size_t numPeeled) {
        return blackListAllDependentLoops(S) >> (numPeeled + 1);
    }
    // translates scev S into loops and symbols
    uint64_t
    fillAffineIndices(MutPtrVector<int64_t> v, Vector<int64_t> &offsets,
                      llvm::SmallVector<const llvm::SCEV *, 3> &symbolicOffsets,
                      const llvm::SCEV *S, int64_t mlt, size_t numPeeled) {
        uint64_t blackList{0};
        if (const llvm::SCEVAddRecExpr *x =
                llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S)) {
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
                // the multiplication was either peeled or involved non-const
                // multiple
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
        } else if (const llvm::SCEVAddExpr *ex =
                       llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
            return fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(0), mlt, numPeeled) |
                   fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(1), mlt, numPeeled);
        } else if (const llvm::SCEVMulExpr *ex =
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
        } else if (const llvm::SCEVCastExpr *ex =
                       llvm::dyn_cast<llvm::SCEVCastExpr>(S))
            return fillAffineIndices(v, offsets, symbolicOffsets,
                                     ex->getOperand(0), mlt, numPeeled);
        addSymbolic(offsets, symbolicOffsets, S, mlt);
        return blackList | blackListAllDependentLoops(S, numPeeled);
    }
    // TODO: support top level array refs
    llvm::Optional<ArrayReference> arrayRef(LoopTree &LT,
                                            llvm::Instruction *ptr,
                                            Predicates &pred,
                                            const llvm::SCEV *elSize) {
        llvm::Loop *L = LT.loop;
        // const llvm::SCEV *scev = SE->getSCEV(ptr);
        // code modified from
        // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
        llvm::errs() << "ptr: " << *ptr << "\n";
        // llvm::Value *po = llvm::getPointerOperand(ptr);
        // if (!po)
        //     return {};
        // llvm::errs() << "ptr operand: " << *po << "\n";
        const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);

        llvm::errs() << "accessFn: " << *accessFn << "\n";
        const llvm::SCEV *pb = SE->getPointerBase(accessFn);
        llvm::errs() << "base pointer: " << *pb << "\n";
        const llvm::SCEVUnknown *basePointer =
            llvm::dyn_cast<llvm::SCEVUnknown>(pb);
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
        AffineLoopNest &aln = loopTrees[loopMap[L]].affineLoop;
        if (sizes.size() == 0)
            return ArrayReference(basePointer, &aln, std::move(sizes),
                                  std::move(subscripts), pred);
        size_t numLoops{aln.getNumLoops()};
        // numLoops x arrayDim
        // IntMatrix R(numLoops, subscripts.size());
        size_t numPeeled = L->getLoopDepth() - numLoops;
        // numLoops x arrayDim
        IntMatrix Rt(subscripts.size(), numLoops);
        IntMatrix Bt;
        Vector<int64_t> offsets(1);
        assert(offsets.size() == 1);
        assert(offsets[0] == 0);
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
        uint64_t blackList{0};
        for (size_t i = 0; i < subscripts.size(); ++i) {
            blackList |= fillAffineIndices(Rt(i, _), offsets, symbolicOffsets,
                                           subscripts[i], 1, numPeeled);
            Bt.resize(subscripts.size(), offsets.size());
            Bt(i, _) = offsets;
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
            // that is, we want to move Rt(_,_(end-numExtraLoopsToPeel,end)) to
            // would this code below actually be expected to boost performance?
            // if (Bt.numCol()+numExtraLoopsToPeel>Bt.rowStride())
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
        ArrayReference ref(basePointer, &aln, std::move(sizes),
                           std::move(symbolicOffsets), pred);
        ref.resize(subscripts.size());
        ref.indexMatrix() = Rt.transpose();
        // SHOWLN(symbolicOffsets.size());
        // SHOW(ref.offsetMatrix().numRow());
        // CSHOWLN(ref.offsetMatrix().numCol());
        // SHOW(Bt.numRow());
        // CSHOWLN(Bt.numCol());
        ref.offsetMatrix() = Bt;
        // TODO:
        //  1. set schedule
        //  2. update schedule, array ref, and offsets when pruning failed loops
        for (size_t i = 0; i < subscripts.size(); ++i) {
            llvm::errs() << "Array Dim " << i << ":\nSize: " << *ref.sizes[i]
                         << "\nSubscript: " << *subscripts[i] << "\n";
            if (const llvm::SCEVUnknown *param =
                    llvm::dyn_cast<llvm::SCEVUnknown>(subscripts[i])) {
                llvm::errs() << "SCEVUnknown\n";
            } else if (const llvm::SCEVNAryExpr *param =
                           llvm::dyn_cast<llvm::SCEVNAryExpr>(subscripts[i])) {
                llvm::errs() << "SCEVNAryExpr\n";
            }
        }
        return ref;
    }
    LoopTree &getLoopTree(unsigned i) { return loopTrees[i]; }
    LoopTree &getLoopTree(llvm::Loop *L) { return getLoopTree(loopMap[L]); }
    bool addLoad(LoopTree &LT, Predicates &pred, llvm::LoadInst *I,
                 llvm::SmallVector<unsigned> &omega) {
        llvm::Value *ptr = I->getPointerOperand();
        // llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        // TODO: support top level array refs
        if (LT.loop) {
            if (llvm::Instruction *iptr =
                    llvm::dyn_cast<llvm::Instruction>(ptr)) {
                if (llvm::Optional<ArrayReference> re =
		    arrayRef(LT, iptr, pred, elSize)) {
		    SHOWLN(I);
		    SHOWLN(*I);
                    LT.memAccesses.emplace_back(std::move(*re), I, omega, true);
                    // LT.memAccesses.emplace_back(std::move(*re), I, true);
		    SHOWLN(I);
		    SHOWLN(*I);
		    SHOWLN(LT.memAccesses.back().user);
		    SHOWLN(*LT.memAccesses.back().user);
		    incrementLast(omega);
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
    bool addStore(LoopTree &LT, Predicates &pred, llvm::StoreInst *I,
                  llvm::SmallVector<unsigned> &omega) {
        llvm::Value *ptr = I->getPointerOperand();
        // llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        // TODO: support top level array refs
        if (LT.loop) {
            if (llvm::Instruction *iptr =
                    llvm::dyn_cast<llvm::Instruction>(ptr)) {
                if (llvm::Optional<ArrayReference> re =
		    arrayRef(LT, iptr, pred, elSize)) {
		    SHOWLN(I);
		    SHOWLN(*I);
                    LT.memAccesses.emplace_back(std::move(*re), I, omega, false);
                    // LT.memAccesses.emplace_back(std::move(*re), I, false);
		    SHOWLN(I);
		    SHOWLN(*I);
		    SHOWLN(LT.memAccesses.back().user);
		    SHOWLN(*LT.memAccesses.back().user);
		    incrementLast(omega);
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

    void parseBB(LoopTree &LT, llvm::BasicBlock *BB, Predicates &pred,
                 llvm::SmallVector<unsigned> &omega) {
        omega.push_back(0);
        for (llvm::Instruction &I : *BB) {
            if (I.mayReadFromMemory()) {
                if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I))
                    if (addLoad(LT, pred, LI, omega))
                        return omega.pop_back();
            } else if (I.mayWriteToMemory())
                if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I))
                    if (addStore(LT, pred, SI, omega))
                        return omega.pop_back();
        }
        omega.pop_back();
    }
    static llvm::SmallVector<unsigned> &
    incrementLast(llvm::SmallVector<unsigned> &x) {
        ++x.back();
        return x;
    }
    // we fill omegas, we have loop pos only, not shifts
    // pR: 0
    // pL: 0
    // pL: 0
    //
    // [0, 0]
    void parseLoop(LoopTree &LT, llvm::SmallVector<unsigned> &omega) {
        omega.push_back(0);
        // llvm::Loop *L = LT.loop;
        // now we walk blocks
        // auto &subLoops = L->getSubLoops();
        for (size_t i = 0; i < LT.subLoops.size(); ++i) {
            LoopTree &SLT = loopTrees[LT.subLoops[i]];
            for (auto &&PBB : LT.paths[i]) {
                parseBB(SLT, PBB.basicBlock, PBB.predicates, omega);
                //
            }
            parseLoop(SLT, omega);
            incrementLast(omega);
        }
        for (auto PBB : LT.paths.back()) {
            parseBB(LT, PBB.basicBlock, PBB.predicates, omega);
            // incrementLast(omega);
        }
        omega.pop_back();
    }
    void parseNest() {
        llvm::SmallVector<unsigned> omega;
        for (auto forestID : loopForests) {
            omega.clear();
            parseLoop(loopTrees[forestID], omega);
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
        peelOuterLoops(loopTrees[loopMap[L]], numToPeel);
    }
    // peelOuterLoops is recursive inwards
    void peelOuterLoops(LoopTree &LT, size_t numToPeel) {
        for (auto SL : LT)
            peelOuterLoops(loopTrees[SL], numToPeel);
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
    // 3. remove D from B's subLoops; add prev and following loops as separate
    // new forests
    // 4. conditionOnLoop(B)
    //
    // approach: remove LoopIndex, and all loops that follow, unless it is first
    // in which case, just remove LoopIndex
    void conditionOnLoop(llvm::Loop *L) {
        unsigned LTID = loopMap[L];
        conditionOnLoop(loopTrees[LTID], LTID);
    }
    void conditionOnLoop(LoopTree &LT, unsigned LTID) {
        unsigned PTID = LT.parentLoop;
        if (PTID == std::numeric_limits<unsigned>::max())
            return;
        LoopTree &PT = loopTrees[PTID];
        size_t numLoops = LT.getNumLoops();
        for (auto ST : LT)
            peelOuterLoops(loopTrees[ST], numLoops);

        LT.parentLoop =
            std::numeric_limits<unsigned>::max(); // LT is now top of the tree
        loopForests.push_back(LTID);
        llvm::SmallVector<unsigned> &friendLoops = PT.subLoops;
        // SHOW(LTID);
        for (auto id : friendLoops)
            llvm::errs() << ", " << id;
        llvm::errs() << "\n";
        if (friendLoops.front() != LTID) {
            // we're cutting off the front
            size_t numFriendLoops = friendLoops.size();
            assert(numFriendLoops);
            size_t loopIndex = 0;
            for (size_t i = 1; i < numFriendLoops; ++i) {
                if (friendLoops[i] == LTID) {
                    loopIndex = i;
                    break;
                }
            }
            assert(loopIndex);
            size_t j = loopIndex + 1;
            if (j != numFriendLoops) {
                // we have some remaining paths we split off
                llvm::SmallVector<unsigned> tmp;
                tmp.reserve(numFriendLoops - j);
                // for paths, we're dropping LT
                // thus, our paths are paths(_(0,j)), paths(_(j,end))
                llvm::SmallVector<PredicatedChain> paths;
                paths.reserve(numFriendLoops - loopIndex);
                for (size_t i = j; i < numFriendLoops; ++i) {
                    peelOuterLoops(loopTrees[friendLoops[i]], numLoops - 1);
                    tmp.push_back(friendLoops[i]);
                    paths.push_back(std::move(PT.paths[i]));
                }
                paths.push_back(std::move(PT.paths[numFriendLoops]));
                loopForests.push_back(loopTrees.size());
                // TODO: split paths
                loopTrees.emplace_back(std::move(tmp), std::move(paths));
            }
            friendLoops.truncate(loopIndex);
            PT.paths.truncate(j);
        } else {
            friendLoops.erase(friendLoops.begin());
            PT.paths.erase(PT.paths.begin());
        }
        conditionOnLoop(PT, PTID);
    }

    bool parseLoopPrint(auto B, auto E) {
        // Schedule sch(depth);
        size_t omega = 0;
        for (auto &&it = B; it != E; ++it, ++omega) {
            llvm::Loop *LP = *it;
            if (auto *inductOuter = LP->getInductionVariable(*SE)) {
                llvm::errs()
                    << "Outer InductionVariable: " << *inductOuter << "\n";
                if (const llvm::SCEV *backEdgeTaken =
                        SE->getBackedgeTakenCount(LP)) {
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
    bool isLoopDependent(llvm::Value *v) const {
        for (auto &L : *LI)
            if (!L->isLoopInvariant(v))
                return true;
        return false;
    }
    bool mayReadOrWriteMemory(llvm::Value *v) const {
        if (auto inst = llvm::dyn_cast<llvm::Instruction>(v))
            if (inst->mayReadOrWriteMemory())
                return true;
        return false;
    }
};
