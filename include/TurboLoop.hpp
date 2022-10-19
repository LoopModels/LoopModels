#pragma once

#include "./ArrayReference.hpp"
#include "./IntegerMap.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./MemoryAccess.hpp"
#include "./POSet.hpp"
#include "./Schedule.hpp"
#include "./UniqueIDMap.hpp"
#include "Symbolics.hpp"
#include <cstdint>
#include <limits>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
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

[[maybe_unused]] static bool isKnownOne(llvm::Value *x) {
    if (llvm::ConstantInt *constInt = llvm::dyn_cast<llvm::ConstantInt>(x)) {
        return constInt->isOne();
    } else if (llvm::Constant *constVal = llvm::dyn_cast<llvm::Constant>(x)) {
        return constVal->isOneValue();
    }
    return false;
}

// requires `isRecursivelyLCSSAForm`
class TurboLoopPass : public llvm::PassInfoMixin<TurboLoopPass> {
  public:
    llvm::PreservedAnalyses run(llvm::Function &F,
                                llvm::FunctionAnalysisManager &AM);
    ValueToPosetMap valueToPosetMap;
    llvm::DenseMap<llvm::Loop *, AffineLoopNest> loops;
    // llvm::SmallVector<, 0> loops;
    PartiallyOrderedSet poset;
    UniqueIDMap<const llvm::SCEVUnknown *> ptrToArrayIDMap;
    // Tree tree;
    // llvm::AssumptionCache *AC;
    const llvm::TargetLibraryInfo *TLI;
    const llvm::TargetTransformInfo *TTI;
    llvm::LoopInfo *LI;
    llvm::ScalarEvolution *SE;
    // const llvm::DataLayout *DL;
    unsigned registerCount;

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
    static bool visit(llvm::SmallPtrSet<llvm::BasicBlock *, 32> &visitedBBs,
                      llvm::BasicBlock *BB) {
        if (visitedBBs.contains(BB))
            return true;
        visitedBBs.insert(BB);
        return false;
    }
    enum class Chain {
        split,
        unreachable,
        returned,
        visited,
        unknown,
        loopexit
    };
    std::pair<llvm::BasicBlock *, Chain>
    searchForFusileEnd(llvm::SmallPtrSet<llvm::BasicBlock *, 32> &visitedBBs,
                       llvm::BasicBlock *BB, llvm::Loop *L = nullptr) {

        if (visit(visitedBBs, BB))
            return std::make_pair(nullptr, Chain::visited);

        if (llvm::Instruction *term = BB->getTerminator()) {
            if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(term)) {
                if (!BI->isConditional())
                    return searchForFusileEnd(visitedBBs, BI->getSuccessor(0),
                                              L);
                // conditional means it has two successors
                // maybe BB is a new loop.
                if (llvm::Loop *BL = LI->getLoopFor(BB)) {
                    if (L != BL) {
                        llvm::SmallPtrSet<llvm::BasicBlock *, 32> oldBBs =
                            visitedBBs;
                        // BL is a new loop;
                        auto [LE, EC] = searchForFusileEnd(visitedBBs, BB, BL);
                        if (EC == Chain::loopexit)
                            return searchForFusileEnd(visitedBBs, LE, L);
                        // didn't work out, lets switch to backup so that
                        // we can still explore old BBs on a future call
                        std::swap(oldBBs, visitedBBs);
                    } else if (BB == BL->getExitingBlock()) {
                        if (llvm::BasicBlock *EB = BL->getExitBlock())
                            return std::make_pair(EB, Chain::loopexit);
                    }
                    return std::make_pair(nullptr, Chain::unknown);
                }
                llvm::SmallPtrSet<llvm::BasicBlock *, 32> oldBBs = visitedBBs;
                // not a loop, but two descendants
                std::pair<llvm::BasicBlock *, Chain> search0 =
                    searchForFusileEnd(visitedBBs, BI->getSuccessor(0), L);
                std::pair<llvm::BasicBlock *, Chain> search1 =
                    searchForFusileEnd(visitedBBs, BI->getSuccessor(1), L);
                if (search0.second == Chain::unreachable)
                    return search1;
                if (search1.second == Chain::unreachable)
                    return search0;
                std::swap(oldBBs, visitedBBs);
                return std::make_pair(BB, Chain::split);
            } else if (llvm::ReturnInst *RI =
                           llvm::dyn_cast<llvm::ReturnInst>(term)) {
                return std::make_pair(BB, Chain::returned);
            } else if (llvm::UnreachableInst *UI =
                           llvm::dyn_cast<llvm::UnreachableInst>(term)) {
                // TODO: add option to allow moving earlier?
                return std::make_pair(nullptr, Chain::unreachable);
            } else {
                // http://formalverification.cs.utah.edu/llvm_doxy/2.9/classllvm_1_1TerminatorInst.html
                // IndirectBrInst, InvokeInst, SwitchInst, UnwindInst
                // TODO: maybe something else?
                return std::make_pair(BB, Chain::unknown);
            }
        }
        return std::make_pair(nullptr, Chain::unknown);
    }
    llvm::Optional<ArrayReference>
    arrayRef(llvm::Loop *L, llvm::Instruction *ptr, const llvm::SCEV *elSize) {
        // const llvm::SCEV *scev = SE->getSCEV(ptr);
        // code modified from
        // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
        llvm::errs() << "ptr: " << *ptr << "\n";
        // llvm::Value *po = llvm::getPointerOperand(ptr);
        // if (!po)
        //     return {};
        // llvm::errs() << "ptr operand: " << *po << "\n";
        const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);
        ;
        llvm::errs() << "accessFn: " << *accessFn << "\n";
        const llvm::SCEV *pb = SE->getPointerBase(accessFn);
        llvm::errs() << "base pointer: " << *pb << "\n";
        const llvm::SCEVUnknown *basePointer =
            llvm::dyn_cast<llvm::SCEVUnknown>(pb);
        // Do not delinearize if we cannot find the base pointer.
        if (!basePointer)
            llvm::errs() << "!basePointer\n";
        if (!basePointer)
            return {};
        llvm::errs() << "base pointer SCEVUnknown: " << *basePointer << "\n";
        accessFn = SE->getMinusSCEV(accessFn, basePointer);
        llvm::errs() << "diff accessFn: " << *accessFn << "\n";
        llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
        llvm::delinearize(*SE, accessFn, subscripts, sizes, elSize);
        assert(subscripts.size() == sizes.size());
        if (sizes.size() == 0)
            return {};
        unsigned arrayID = ptrToArrayIDMap[basePointer];
        // ArrayReference ref(arrayID);
        for (size_t i = 0; i < subscripts.size(); ++i) {
            llvm::errs() << "Array Dim " << i << ":\nSize: " << *sizes[i]
                         << "\nSubscript: " << *subscripts[i] << "\n";
            if (const llvm::SCEVUnknown *param =
                    llvm::dyn_cast<llvm::SCEVUnknown>(subscripts[i])) {
                llvm::errs() << "SCEVUnknown\n";
            } else if (const llvm::SCEVNAryExpr *param =
                           llvm::dyn_cast<llvm::SCEVNAryExpr>(subscripts[i])) {
                llvm::errs() << "SCEVNAryExpr\n";
            }
        }
        // return ref;
        return {};
    }
    llvm::Optional<MemoryAccess> addLoad(llvm::Loop *L, llvm::LoadInst *I) {
        bool isLoad = true;
        llvm::Value *ptr = I->getPointerOperand();
        llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        if (L) {
            if (llvm::Instruction *iptr =
                    llvm::dyn_cast<llvm::Instruction>(ptr)) {
                llvm::Optional<ArrayReference> re = arrayRef(L, iptr, elSize);
                // } else {
                // MemoryAccess
            }
        }
        return {};
    }
    llvm::Optional<MemoryAccess> addStore(llvm::Loop *L, llvm::StoreInst *I) {
        bool isLoad = false;
        llvm::Value *ptr = I->getPointerOperand();
        llvm::Type *type = I->getPointerOperandType();
        const llvm::SCEV *elSize = SE->getElementSize(I);
        if (L) {
            if (llvm::Instruction *iptr =
                    llvm::dyn_cast<llvm::Instruction>(ptr)) {
                llvm::Optional<ArrayReference> re = arrayRef(L, iptr, elSize);
                // } else {
                // MemoryAccess
            }
        }
        return {};
    }

    bool parseBB(llvm::Loop *L, llvm::BasicBlock *BB) {
        for (llvm::Instruction &I : *BB) {
            if (I.mayReadFromMemory()) {
                if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
                    addLoad(L, LI);
                    continue;
                }
                return true;
            } else if (I.mayWriteToMemory()) {
                if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
                    addStore(L, SI);
                    continue;
                }
                return true;
            }
        }
        return false;
    }
    bool parseBB(llvm::BasicBlock *BB) {
        return parseBB(LI->getLoopFor(BB), BB);
    }

    bool parseLoopPrint(auto B, auto E, size_t depth) {
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
    // returns true on failure
    bool symbolify(MPoly &accum, llvm::Value *v, int64_t coef = 1) {
        if (auto c = llvm::dyn_cast<llvm::ConstantInt>(v)) {
            uint64_t val =
                c->getLimitedValue(std::numeric_limits<int64_t>::max());
            if (val == std::numeric_limits<int64_t>::max())
                return true;
            accum += int64_t(val) * coef;
            return false;
        } else if (auto binOp = llvm::dyn_cast<llvm::BinaryOperator>(v)) {
            int64_t c1 = coef;
            switch (binOp->getOpcode()) {
            case llvm::Instruction::BinaryOps::Sub:
                c1 = -c1;
            case llvm::Instruction::BinaryOps::Add:
                return (symbolify(accum, binOp->getOperand(0), coef) ||
                        symbolify(accum, binOp->getOperand(1), c1));
            case llvm::Instruction::BinaryOps::Mul:
                return mulUpdate(accum, binOp->getOperand(0),
                                 binOp->getOperand(1), coef);
            default:
                break;
            }
        }
        accum += Polynomial::Term{
            coef, Polynomial::Monomial{valueToPosetMap.getForward(v)}};
        return false;
    }
    bool mulUpdate(MPoly &accum, llvm::Value *a, llvm::Value *b, int64_t coef) {
        MPoly L, R;
        if (symbolify(R, b, 1) || symbolify(L, a, coef))
            return true;
        accum += L * R;
        return false;
    }
    llvm::Optional<MPoly> symbolify(llvm::Value *v, int64_t coef = 1) {
        MPoly accum;
        if (symbolify(accum, v, coef))
            return {};
        return accum;
    }

    // // // we need unit step size
    // // void canonicalizeStep(llvm::Value *initV,
    // //     llvm::Value *stepV, llvm::Value *finalV) {

    // // 	return;
    // // }
    // // this can result in unfavorable rotations in canonicalizing the
    // starting
    // // index to 0 so we rely on orthogonalizing indices later. Supporting
    // // orthogonalization is needed anyway, as loops may have originally been
    // // written in an unfavorable way.
    // // returns `true` if it failed.
    // void pushAffine(
    //     llvm::SmallVector<AffineCmp, 8> &affs, llvm::Value &initV,
    //     llvm::Value &finalV,
    //     llvm::SmallVector<
    //         std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
    //         4> const &outerLoops,
    //     llvm::Loop *LP) {

    //     size_t startInvariant = invariant(initV, outerLoops);
    //     size_t stopInvariant = invariant(finalV, outerLoops);

    //     llvm::SmallVector<int64_t, 4> aL(outerLoops.size() + 1, 0);
    //     llvm::SmallVector<int64_t, 4> aU(outerLoops.size() + 1, 0);
    //     MPoly bL;
    //     MPoly bU;
    //     /*
    //     if (llvm::ConstantInt *stepConst =
    //             llvm::dyn_cast<llvm::ConstantInt>(stepV)) {
    //         if (!(stepConst->isOne())) {
    //             // stepConst->getValue();
    //             // divide by const
    //             size_t defLevel = std::max(startInvariant, stopInvariant);
    //             llvm::BasicBlock *startStopPre;
    //             if (defLevel == outerLoops.size()) {
    //                 startStopPre = LP->getLoopPreheader();
    //             } else {
    //                 startStopPre =
    //                     outerLoops[defLevel].first->getLoopPreheader();
    //             }
    //             // auto stopSCEV = SE->getSCEV(finalV);
    //             // llvm::Value *len = rewriter.expandCodeFor(
    //             //     SE->getAddExpr(
    //             //         SE->getUDivExpr(SE->getMinusSCEV(stopSCEV,
    //             // SE->getSCEV(initV),
    //             // llvm::SCEV::FlagNUW),
    //             //                         SE->getSCEV(stepV)),
    //             //         SE->getOne(stopSCEV->getType())),
    //             //     stopSCEV->getType(), startStopPre->getTerminator());

    //             llvm::IRBuilder<> Builder(startStopPre);
    //             llvm::Value *len = Builder.CreateNSWAdd(
    //                 Builder.CreateSDiv(Builder.CreateNSWSub(finalV, initV),
    //                                    stepV),
    //                 llvm::ConstantInt::get(finalV->getType(), 1));
    //             // Now that we have the length
    //             // we must create a new phi initialized at 0
    //             // then insert a new break/latch, replacing old
    //             // then replace all uses of old phi with mul/step

    //             // initV),
    //         }
    //     } else {
    //     }
    //     */
    // 	if (llvm::ConstantInt *initConst =
    // llvm::dyn_cast<llvm::Constantint64_t>(&initV)){

    // 	} else {

    // 	}
    // 	if (llvm::ConstantInt *finalConst =
    // llvm::dyn_cast<llvm::Constantint64_t>(&finalV)){

    // 	} else {

    // 	}
    // }
    // void descend(
    //     Tree &tree,
    //     llvm::SmallVector<
    //         std::pair<llvm::Loop *, llvm::Optional<llvm::Loop::LoopBounds>>,
    //         4> &outerLoops,
    //     llvm::SmallVector<AffineCmp, 8> &affs, llvm::Loop *LP,
    //     llvm::DominatorTree &DT) {
    //     size_t numOuter = outerLoops.size();
    //     if (LP->isLCSSAForm(DT)) {
    //         // we check for LCSSA form as we'd like to assume it
    //         llvm::Optional<llvm::Loop::LoopBounds> boundsRoot =
    //             LP->getBounds(*SE);
    //         if (boundsRoot.hasValue()) {
    //             llvm::Loop::LoopBounds &bounds = boundsRoot.getValue();
    //             // TODO: write separate pass for canonicalizing steps to 1
    //             if (isKnownOne(bounds.getStepValue())) {
    //                 pushAffine(affs, bounds.getInitialIVValue(),
    //                            bounds.getFinalIVValue(),
    //                            outerLoops, LP);
    //                 llvm::Value *start = &bounds.getInitialIVValue();
    //                 llvm::Value *stop = &bounds.getFinalIVValue();

    //                 llvm::errs()
    //                     << "\nloop bounds: " << *start << " : " << *stop <<
    //                     "\n";
    //             }
    //         }
    //     }
    //     // TODO: check if reachable; if not we can safely ignore
    //     // TODO: insert unoptimizable op representing skipped loop?
    //     // The concern is we want some understanding of the dependencies
    //     // between the unoptimized block and optimized block, in case
    //     // we want to move loops around. Otherwise, this is basically
    //     // a volatile barrier.
    //     tree.emplace_back(LP, numOuter);
    //     return;
    //     // Alt TODO: insert a remark
    //     // return llvm::PreservedAnalyses::all();
    //     // llvm::LoopNest LN = llvm::LoopNest(*LP, SE);
    //     // size_t nestDepth = LN.getNestDepth();

    //     for (auto *B : LP->getBlocks()) {
    //         std::cout << "Basic block:\n";
    //         for (auto &I : *B) {
    //             llvm::errs() << I << "\n";
    //         }
    //     }
    // }
};
