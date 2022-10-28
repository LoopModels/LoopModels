#pragma once

#include "./ArrayReference.hpp"
#include "./IntegerMap.hpp"
#include "./LoopForest.hpp"
#include "./Loops.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "./UniqueIDMap.hpp"
#include <algorithm>
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
    std::vector<LoopForest> loopForests;
    llvm::DenseMap<llvm::Loop *, AffineLoopNest> loops;
    UniqueIDMap<const llvm::SCEVUnknown *> ptrToArrayIDMap;
    // Tree tree;
    // llvm::AssumptionCache *AC;
    const llvm::TargetLibraryInfo *TLI;
    const llvm::TargetTransformInfo *TTI;
    llvm::LoopInfo *LI;
    llvm::ScalarEvolution *SE;
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
        LoopForest forest;
        // NOTE: LoopInfo stores loops in reverse program order (opposite of
        // loops)
        for (auto &L : llvm::reverse(*LI))
            forest.pushBack(L, *SE, loopForests);
	if (forest.size())
	    loopForests.push_back(std::move(forest));
        for (auto &forest : loopForests)
            forest.addZeroLowerBounds();
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
