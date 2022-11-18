#pragma once

#include "./Comparators.hpp"
#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./Polyhedra.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>

static inline bool isKnownOne(llvm::ScalarEvolution &SE, llvm::Value *v) {
    return v && SE.getSCEV(v)->isOne();
}

[[nodiscard]] [[maybe_unused]] static const llvm::SCEV *
getBackedgeTakenCount(llvm::ScalarEvolution &SE, llvm::Loop *L) {
    auto b = L->getBounds(SE);
    if (!b || (!isKnownOne(SE, b->getStepValue())))
        return SE.getBackedgeTakenCount(L);
    const llvm::SCEV *LB = SE.getSCEV(&b->getInitialIVValue());
    const llvm::SCEV *UB = SE.getSCEV(&b->getFinalIVValue());
    SHOWLN(*LB);
    SHOWLN(*UB);
    if (auto umm = llvm::dyn_cast<llvm::SCEVUMaxExpr>(UB)) {
        const llvm::SCEV *m0 = SE.getMinusSCEV(
            umm->getOperand(0), LB, llvm::SCEV::NoWrapFlags::FlagNUW);
        const llvm::SCEV *m1 = SE.getMinusSCEV(
            umm->getOperand(1), LB, llvm::SCEV::NoWrapFlags::FlagNUW);
        // Does checking known negative make sense if we have NUW?
        SHOWLN(*UB);
        SHOWLN(*m0);
        SHOWLN(*m1);
        if (SE.isKnownNegative(m0))
            return m1;
        if (SE.isKnownNegative(m1))
            return m0;
    } else if (auto smm = llvm::dyn_cast<llvm::SCEVSMaxExpr>(UB)) {
        const llvm::SCEV *m0 = SE.getMinusSCEV(
            smm->getOperand(0), LB, llvm::SCEV::NoWrapFlags::FlagNSW);
        const llvm::SCEV *m1 = SE.getMinusSCEV(
            smm->getOperand(1), LB, llvm::SCEV::NoWrapFlags::FlagNSW);
        SHOWLN(*m0);
        SHOWLN(*m1);
        if (SE.isKnownNegative(m0))
            return m1;
        if (SE.isKnownNegative(m1))
            return m0;
    }
    return SE.getMinusSCEV(UB, LB, llvm::SCEV::NoWrapMask);
}

[[nodiscard]] [[maybe_unused]] static const llvm::SCEV *
noWrapSCEV(llvm::ScalarEvolution &SE, const llvm::SCEV *S) {
    if (const llvm::SCEVAddExpr *ex =
            llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
        return SE.getAddExpr(noWrapSCEV(SE, ex->getOperand(0)),
                             noWrapSCEV(SE, ex->getOperand(1)),
                             llvm::SCEV::NoWrapMask);
    } else if (const llvm::SCEVMulExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVMulExpr>(S)) {
        return SE.getMulExpr(noWrapSCEV(SE, ex->getOperand(0)),
                             noWrapSCEV(SE, ex->getOperand(1)),
                             llvm::SCEV::NoWrapMask);
    } else if (const llvm::SCEVUMaxExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVUMaxExpr>(S)) {
        return SE.getUMaxExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    } else if (const llvm::SCEVUMaxExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVUMaxExpr>(S)) {
        return SE.getUMaxExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    } else if (const llvm::SCEVUMinExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVUMinExpr>(S)) {
        return SE.getUMinExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    } else if (const llvm::SCEVSMaxExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVSMaxExpr>(S)) {
        return SE.getSMaxExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));

    } else if (const llvm::SCEVSMinExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVSMinExpr>(S)) {
        return SE.getSMinExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    } else if (const llvm::SCEVUDivExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVUDivExpr>(S)) {
        return SE.getUDivExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    } else if (const llvm::SCEVPtrToIntExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVPtrToIntExpr>(S)) {
        return SE.getPtrToIntExpr(noWrapSCEV(SE, ex->getOperand(0)),
                                  ex->getType());
    } else if (const llvm::SCEVSignExtendExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVSignExtendExpr>(S)) {
        return SE.getSignExtendExpr(noWrapSCEV(SE, ex->getOperand(0)),
                                    ex->getType());
    } else if (const llvm::SCEVZeroExtendExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVZeroExtendExpr>(S)) {
        return SE.getZeroExtendExpr(noWrapSCEV(SE, ex->getOperand(0)),
                                    ex->getType());
    } else if (const llvm::SCEVTruncateExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVTruncateExpr>(S)) {
        return SE.getTruncateExpr(noWrapSCEV(SE, ex->getOperand(0)),
                                  ex->getType());
    } else if (const llvm::SCEVSMinExpr *ex =
                   llvm::dyn_cast<const llvm::SCEVSMinExpr>(S)) {
        return SE.getSMinExpr(noWrapSCEV(SE, ex->getOperand(0)),
                              noWrapSCEV(SE, ex->getOperand(1)));
    }
    return S;
}

// static llvm::Optional<int64_t> getConstantInt(llvm::Value *v) {
//     if (llvm::ConstantInt *c = llvm::dyn_cast<llvm::ConstantInt>(v))
//         if (c->getBitWidth() <= 64)
//             return c->getSExtValue();
//     return {};
// }
[[maybe_unused]] static llvm::Optional<int64_t>
getConstantInt(const llvm::SCEV *v) {
    if (const llvm::SCEVConstant *sc =
            llvm::dyn_cast<const llvm::SCEVConstant>(v)) {
        llvm::ConstantInt *c = sc->getValue();
        // we need bit width of 64, for sake of negative numbers
        if (c->getBitWidth() <= 64)
            return c->getSExtValue();
    }
    return {};
}

template <typename T>
[[maybe_unused]] static size_t findFirst(llvm::ArrayRef<T> v, const T &x) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == x)
            return i;
    return std::numeric_limits<size_t>::max();
}

// returns 1-based index, to match the pattern we use where index 0 refers to a
// constant offset this function returns 0 if S not found in `symbols`.
[[maybe_unused]] [[nodiscard]] static size_t
findSymbolicIndex(llvm::ArrayRef<const llvm::SCEV *> symbols,
                  const llvm::SCEV *S) {
    for (size_t i = 0; i < symbols.size();)
        if (symbols[i++] == S)
            return i;
    return 0;
}

[[maybe_unused]] [[nodiscard]] static std::pair<const llvm::SCEV *,
                                                const llvm::SCEV *>
getMinMaxValueSCEV(llvm::ScalarEvolution &SE, const llvm::SCEVAddRecExpr *S) {
    // if (!SE.containsAddRecurrence(S))
    // 	return S;
    if ((!S) || (!(S->isAffine())))
        return std::make_pair(S, S);
    auto opStart = S->getStart();
    auto opStep = S->getStepRecurrence(SE);
    auto opFinal = SE.getSCEVAtScope(S, nullptr);
    // auto opFinal = SE.getSCEVAtScope(S, S->getLoop()->getParentLoop());
    // FIXME: what if there are more AddRecs nested inside?
    if (SE.isKnownNonNegative(opStep))
        return std::make_pair(opStart, opFinal);
    else if (SE.isKnownNonPositive(opStep))
        return std::make_pair(opFinal, opStart);
    return std::make_pair(S, S);
}
// TODO: strengthen through recursion
[[maybe_unused]] [[nodiscard]] static std::pair<const llvm::SCEV *,
                                                const llvm::SCEV *>
getMinMaxValueSCEV(llvm::ScalarEvolution &SE, const llvm::SCEV *S) {
    if (const llvm::SCEVAddRecExpr *T = llvm::dyn_cast<llvm::SCEVAddRecExpr>(S))
        return getMinMaxValueSCEV(SE, T);
    return std::make_pair(S, S);
}
[[maybe_unused]] [[nodiscard]] static const llvm::SCEV *
simplifyMinMax(llvm::ScalarEvolution &SE, const llvm::SCEVMinMaxExpr *S) {
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
    if (SE.isKnownPredicate(GE, LB0, UB1)) {
        // op0 >= op1
        return isMin ? op1 : op0;
    } else if (SE.isKnownPredicate(GE, LB1, UB0)) {
        // op1 >= op0
        return isMin ? op0 : op1;
    }
    return S;
}
[[maybe_unused]] [[nodiscard]] static const llvm::SCEV *
simplifyMinMax(llvm::ScalarEvolution &SE, const llvm::SCEV *S) {
    if (const llvm::SCEVMinMaxExpr *MM =
            llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(S))
        return simplifyMinMax(SE, MM);
    return S;
}

// A * x >= 0
// if constexpr(NonNegative)
//   x >= 0
template <bool NonNegative>
struct AffineLoopNest
    : Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                llvm::SmallVector<const llvm::SCEV *>, NonNegative> {

    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>,
                    NonNegative>::getNumDynamic;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>,
                    NonNegative>::getNumSymbols;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>,
                    NonNegative>::pruneBounds;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>,
                    NonNegative>::initializeComparator;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>,
                    NonNegative>::isEmpty;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>, NonNegative>::A;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>, NonNegative>::C;
    using Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>, NonNegative>::S;

    constexpr size_t getNumLoops() const { return getNumDynamic(); }

    size_t findIndex(const llvm::SCEV *v) const {
        return findSymbolicIndex(S, v);
    }
    AffineLoopNest<false> rotate(PtrMatrix<int64_t> R) const {
        size_t numExtraVar = 0;
        if constexpr (NonNegative)
            numExtraVar = getNumLoops();
        assert(R.numCol() == numExtraVar);
        assert(R.numRow() == numExtraVar);
        const size_t numConst = getNumSymbols();
        const auto [M, N] = A.size();
        AffineLoopNest<false> ret;
        ret.S = S;
        IntMatrix &B = ret.A;
        B.resizeForOverwrite(M + numExtraVar, N);
        B(_(0, M), _(begin, numConst)) = A(_, _(begin, numConst));
        B(_(0, M), _(numConst, end)) = A(_, _(numConst, end)) * R;
        if constexpr (NonNegative) {
            B(_(M, end), _(0, numConst)) = 0;
            B(_(M, end), _(numConst, end)) = R;
        }
        ret.initializeComparator();
        ret.pruneBounds();
        // llvm::errs() << "A = \n" << A << "\n";
        // llvm::errs() << "R = \n" << R << "\n";
        // llvm::errs() << "B = \n" << B << "\n";
        return ret;
    }

    // add a symbol to row `r` of A
    // we try to break down value `v`, so that adding
    // N, N - 1, N - 3 only adds the variable `N`, and adds the constant offsets
    [[nodiscard]] size_t addSymbol(IntMatrix &B, llvm::Loop *L,
                                   const llvm::SCEV *v,
                                   llvm::ScalarEvolution &SE, const size_t l,
                                   const size_t u, int64_t mlt,
                                   size_t minDepth) {
        assert(u > l);
        // first, we check if `v` in `Symbols`
        if (size_t i = findIndex(v)) {
            for (size_t j = l; j < u; ++j)
                A(j, i) += mlt;
            return minDepth;
        } else if (llvm::Optional<int64_t> c = getConstantInt(v)) {
            for (size_t j = l; j < u; ++j)
                A(j, 0) += mlt * (*c);
            return minDepth;
        } else if (const llvm::SCEVAddExpr *ex =
                       llvm::dyn_cast<const llvm::SCEVAddExpr>(v)) {
            const llvm::SCEV *op0 = ex->getOperand(0);
            const llvm::SCEV *op1 = ex->getOperand(1);
            // // check if either op is a SCEVMinMaxExpr of the wrong kind
            // // if so, check if we can simplify by moving the add inside.
            // if (const llvm::SCEVAddRecExpr *ar0 =
            //         llvm::dyn_cast<llvm::SCEVAddRecExpr>(op0)) {
            //     if (const llvm::SCEVMinMaxExpr *mm1 =
            //             llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(op1)) {
            //         llvm::errs() << "for SCEV:" << *ex << "\nwe
            //         distribute:\n"
            //                      << *SE.getAddExpr(ar0, mm1->getOperand(0),
            //                                        llvm::SCEV::NoWrapMask)
            //                      << "\n"
            //                      << *SE.getAddExpr(ar0, mm1->getOperand(1),
            //                                        llvm::SCEV::NoWrapMask)
            //                      << "\n";
            //     }
            // } else if (const llvm::SCEVMinMaxExpr *mm0 =
            //                llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(op0)) {
            //     if (const llvm::SCEVAddRecExpr *ar1 =
            //             llvm::dyn_cast<llvm::SCEVAddRecExpr>(op1)) {
            //     }
            // }

            size_t M = A.numRow();
            minDepth = addSymbol(B, L, op0, SE, l, u, mlt, minDepth);
            if (M != A.numRow())
                minDepth =
                    addSymbol(B, L, op1, SE, M, A.numRow(), mlt, minDepth);
            return addSymbol(B, L, op1, SE, l, u, mlt, minDepth);
        } else if (const llvm::SCEVMulExpr *ex =
                       llvm::dyn_cast<const llvm::SCEVMulExpr>(v)) {
            if (auto op = getConstantInt(ex->getOperand(0))) {
                return addSymbol(B, L, ex->getOperand(1), SE, l, u, mlt * (*op),
                                 minDepth);
            } else if (auto op = getConstantInt(ex->getOperand(1))) {
                return addSymbol(B, L, ex->getOperand(0), SE, l, u, mlt * (*op),
                                 minDepth);
            }
        } else if (const llvm::SCEVAddRecExpr *x =
                       llvm::dyn_cast<const llvm::SCEVAddRecExpr>(v)) {
            size_t recDepth = x->getLoop()->getLoopDepth();
            if (x->isAffine()) {
                minDepth =
                    addSymbol(B, L, x->getOperand(0), SE, l, u, mlt, minDepth);
                if (auto c = getConstantInt(x->getOperand(1))) {
                    // swap order vs recDepth to go inner<->outer
                    B(l, B.numCol() - recDepth) = mlt * (*c);
                    return minDepth;
                }
                v = SE.getAddRecExpr(SE.getZero(x->getOperand(0)->getType()),
                                     x->getOperand(1), x->getLoop(),
                                     x->getNoWrapFlags());
            }
            // we only support affine SCEVAddRecExpr with constant steps
            // we use a flag "minSupported", which defaults to 0
            // 0 means we support all loops, as the outer most depth is 1
            // Depth of 0 means toplevel.
            minDepth = std::max(minDepth, recDepth);
        } else if (const llvm::SCEVMinMaxExpr *ex =
                       llvm::dyn_cast<const llvm::SCEVMinMaxExpr>(v)) {
            auto S = simplifyMinMax(SE, ex);
            if (S != v)
                return addSymbol(B, L, S, SE, l, u, mlt, minDepth);
            bool isMin = llvm::isa<llvm::SCEVSMinExpr>(ex) ||
                         llvm::isa<llvm::SCEVUMinExpr>(ex);
            llvm::errs() << "llvm::SCEVMinMaxExpr: " << *ex
                         << "\nisMin = " << isMin << "; mlt = " << mlt << "\n";
            const llvm::SCEV *op0 = ex->getOperand(0);
            const llvm::SCEV *op1 = ex->getOperand(1);
            if (isMin ^
                (mlt < 0)) { // we can represent this as additional constraints
                size_t M = A.numRow();
                A.resizeRows(M + u - l);
                B.resizeRows(M + u - l);
                size_t Mp = M + u - l;
                A(_(M, Mp), _) = A(_(l, u), _);
                B(_(M, Mp), _) = B(_(l, u), _);
                minDepth = addSymbol(B, L, op0, SE, l, u, mlt, minDepth);
                minDepth = addSymbol(B, L, op1, SE, M, Mp, mlt, minDepth);
            } else if (addRecMatchesLoop(op0, L)) {
                return addSymbol(B, L, op1, SE, l, u, mlt, minDepth);
            } else if (addRecMatchesLoop(op1, L)) {
                return addSymbol(B, L, op0, SE, l, u, mlt, minDepth);
                // } else {
                //     // auto S = simplifyMinMax(SE, ex);
                //     // if (S != v)
                //     //     return addSymbol(B,L,S,SE,l,u,mlt,minDepth);
                //     // llvm::errs() << "Failing on llvm::SCEVMinMaxExpr = "
                //     << *ex
                //     //              << "<<\n*L =" << *L << "\n";
                //     // SHOWLN(*op0);
                //     // SHOWLN(*op1);
                //     // TODO: don't only consider final value
                //     // this assumes the final value is the maximum, which is
                //     not
                //     // necessarilly true
                //     if (auto op0ar =
                //     llvm::dyn_cast<llvm::SCEVAddRecExpr>(op0)) {
                //         // auto op0final = SE.getSCEVAtScope(
                //         //     op0ar, op0ar->getLoop()->getParentLoop());
                //         auto op0final = SE.getSCEVAtScope(op0ar, nullptr);
                //         SHOWLN(*op0final);
                //         auto op0FinalMinusOp1 = SE.getMinusSCEV(op0final,
                //         op1);
                //         SHOWLN(SE.isKnownNonNegative(op0FinalMinusOp1));
                //         SHOWLN(SE.isKnownNonPositive(op0FinalMinusOp1));
                //         auto op0init = op0ar->getOperand(0);
                //         auto op0InitMinusOp1 = SE.getMinusSCEV(op0init, op1);
                //         SHOWLN(SE.isKnownNonNegative(op0InitMinusOp1));
                //         SHOWLN(SE.isKnownNonPositive(op0InitMinusOp1));
                //         auto op0step = op0ar->getOperand(0);
                //         SHOWLN(SE.isKnownNonNegative(op0step));
                //         SHOWLN(SE.isKnownNonPositive(op0step));
                //     }
                //     if (auto op1ar =
                //     llvm::dyn_cast<llvm::SCEVAddRecExpr>(op1)) {
                //         SHOWLN(*SE.getSCEVAtScope(
                //             op1ar, op1ar->getLoop()->getParentLoop()));
                //     }
                //     auto op0MinusOp1 = SE.getMinusSCEV(op0, op1);
                //     // SHOWLN(SE.isKnownNonNegative(op0MinusOp1));
                //     // SHOWLN(SE.isKnownNonPositive(op0MinusOp1));

                //     if (auto b = L->getBounds(SE))
                //         llvm::errs()
                //             << "Loop Bounds:\nInitial: " <<
                //             b->getInitialIVValue()
                //             << "\nStep: " << *b->getStepValue()
                //             << "\nFinal: " << b->getFinalIVValue() << "\n";
                //     assert(false);
            }
        } else if (const llvm::SCEVCastExpr *ex =
                       llvm::dyn_cast<llvm::SCEVCastExpr>(v))
            return addSymbol(B, L, ex->getOperand(0), SE, l, u, mlt, minDepth);
        // } else if (const llvm::SCEVUDivExpr *ex = llvm::dyn_cast<const
        // llvm::SCEVUDivExpr>(v)) {

        // } else if (const llvm::SCEVUnknown *ex = llvm::dyn_cast<const
        // llvm::SCEVUnknown>(v)) {
        addSymbol(v, l, u, mlt);
        return minDepth;
    }
    void addSymbol(const llvm::SCEV *v, size_t l, size_t u, int64_t mlt) {
        assert(u > l);
        // llvm::errs() << "Before adding sym A = " << A << "\n";
        S.push_back(v);
        A.resizeCols(A.numCol() + 1);
        // A.insertZeroColumn(symbols.size());
        for (size_t j = l; j < u; ++j)
            A(j, S.size()) = mlt;
        // llvm::errs() << "After adding sym A = " << A << "\n";
    }
    static bool addRecMatchesLoop(const llvm::SCEV *S, llvm::Loop *L) {
        if (const llvm::SCEVAddRecExpr *x =
                llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S))
            return x->getLoop() == L;
        return false;
    }
    size_t addBackedgeTakenCount(IntMatrix &B, llvm::Loop *L,
                                 const llvm::SCEV *BT,
                                 llvm::ScalarEvolution &SE, size_t minDepth) {
        size_t M = A.numRow();
        A.resizeRows(M + 1);
        B.resizeRows(M + 1);
        llvm::errs() << "BT = " << *BT
                     << "\naddBackedgeTakenCount pre addSym; M = " << M
                     << "; A = " << A << "\n";
        minDepth = addSymbol(B, L, BT, SE, M, M + 1, 1, minDepth);
        llvm::errs() << "addBackedgeTakenCount post addSym; M = " << M
                     << "; A = " << A << "\n";
        assert(A.numRow() == B.numRow());
        size_t depth = L->getLoopDepth();
        for (size_t m = M; m < A.numRow(); ++m)
            B(m, B.numCol() - depth) = -1; // indvar
        // recurse, if possible to add an outer layer
        if (llvm::Loop *P = L->getParentLoop()) {
            if (areSymbolsLoopInvariant(P, SE)) {
                // llvm::SmallVector<const llvm::SCEVPredicate *, 4> predicates;
                // auto *BTI = SE.getPredicatedBackedgeTakenCount(L,
                // predicates);
                if (const llvm::SCEV *BTP = getBackedgeTakenCount(SE, P)) {
                    llvm::errs() << "BackedgeTakenCount: " << *BTP << "\n";
                    if (!llvm::isa<llvm::SCEVCouldNotCompute>(BTP)) {
                        return addBackedgeTakenCount(B, P, BTP, SE, minDepth);
                    } else {
                        llvm::errs()
                            << "SCEVCouldNotCompute from loop: " << *P << "\n";
                    }
                }
            } else {
                llvm::errs()
                    << "Fail because symbols are not loop invariant in loop:\n"
                    << *P << "\n";
                if (auto b = L->getBounds(SE))
                    llvm::errs()
                        << "Loop Bounds:\nInitial: " << b->getInitialIVValue()
                        << "\nStep: " << *b->getStepValue()
                        << "\nFinal: " << b->getFinalIVValue() << "\n";
                for (auto s : S)
                    llvm::errs() << *s << "\n";
            }
        }
        return std::max(depth - 1, minDepth);
    }
    bool areSymbolsLoopInvariant(llvm::Loop *L,
                                 llvm::ScalarEvolution &SE) const {
        for (size_t i = 0; i < S.size(); ++i)
            if ((!allZero(A(_, i + 1))) && (!SE.isLoopInvariant(S[i], L)))
                return false;
        return true;
    }
    static llvm::Optional<AffineLoopNest<NonNegative>>
    construct(llvm::Loop *L, llvm::ScalarEvolution &SE) {
        auto BT = getBackedgeTakenCount(SE, L);
        if (!BT || llvm::isa<llvm::SCEVCouldNotCompute>(BT))
            return {};
        return AffineLoopNest<NonNegative>(L, BT, SE);
    }
    AffineLoopNest(llvm::Loop *L, const llvm::SCEV *BT,
                   llvm::ScalarEvolution &SE) {
        IntMatrix B;
        // once we're done assembling these, we'll concatenate A and B
        size_t maxDepth = L->getLoopDepth();
        // size_t maxNumSymbols = BT->getExpressionSize();
        A.resize(0, 1, 1 + BT->getExpressionSize());
        B.resize(0, maxDepth, maxDepth);
        size_t minDepth = addBackedgeTakenCount(B, L, BT, SE, 0);
        // We first check for loops in B that are shallower than minDepth
        // we include all loops such that L->getLoopDepth() > minDepth
        // note that the outer-most loop has a depth of 1.
        // We turn these loops into `getAddRecExprs`s, so that we can
        // add them as variables to `A`.
        for (size_t d = 0; d < minDepth; ++d) {
            // loop at depth d+1
            llvm::Loop *P = nullptr;
            // search B(_,end-d) for references
            for (size_t i = 0; i < B.numRow(); ++i) {
                if (int64_t Bid = B(i, end - d)) {
                    if (!P) {
                        // find P
                        P = L;
                        for (size_t r = d + 1; r < maxDepth; ++r)
                            P = P->getParentLoop();
                    }
                    // TODO: find a more efficient way to get IntTyp
                    llvm::Type *IntTyp = P->getInductionVariable(SE)->getType();
                    addSymbol(SE.getAddRecExpr(SE.getZero(IntTyp),
                                               SE.getOne(IntTyp), P,
                                               llvm::SCEV::NoWrapMask),
                              i, i + 1, Bid);
                    llvm::errs() << "UnboundedAffineLoopNest iter i = " << i
                                 << "A = " << A << "\n";
                }
            }
        }
        size_t depth = maxDepth - minDepth;
        size_t N = A.numCol();
        A.resizeCols(N + depth);
        // copy the included loops from B into A
        A(_, _(N, N + depth)) = B(_, _(0, depth));
        initializeComparator();
        // addZeroLowerBounds();
        // NOTE: pruneBounds() is not legal here if we wish to use
        // removeInnerMost later.
        // pruneBounds();
    }
    [[nodiscard]] AffineLoopNest<NonNegative> removeInnerMost() const {
        size_t innermostLoopInd = getNumSymbols();
        IntMatrix B = A.deleteCol(innermostLoopInd);
        // no loop may be conditioned on the innermost loop
        // so we should be able to safely remove all constraints that reference
        // it
        for (size_t m = B.numRow(); m-- > 0;) {
            if (A(m, innermostLoopInd)) {
                // B(_(m,end-1),_) = B(_(m+1,end),_);
                // make sure we're explicit about the order we copy rows
                size_t M = B.numRow() - 1;
                for (size_t r = m; r < M; ++r)
                    B(r, _) = B(r + 1, _);
                B.resizeRows(M);
            }
        }
        return AffineLoopNest<NonNegative>(B, S);
    }
    void clear() {
        A.resize(0, 1); // 0 x 1 so that getNumLoops() == 0
        S.truncate(0);
    }
    void removeOuterMost(size_t numToRemove, llvm::Loop *L,
                         llvm::ScalarEvolution &SE) {
        // basically, we move the outermost loops to the symbols section,
        // and add the appropriate addressees
        size_t oldNumLoops = getNumLoops();
        if (numToRemove >= oldNumLoops)
            return clear();
        size_t innermostLoopInd = getNumSymbols();
        size_t numRemainingLoops = oldNumLoops - numToRemove;
        auto [M, N] = A.size();
        if (numRemainingLoops != numToRemove) {
            Vector<int64_t> tmp;
            if (numRemainingLoops > numToRemove) {
                tmp.resizeForOverwrite(numToRemove);
                for (size_t m = 0; m < M; ++m) {
                    // fill tmp
                    tmp = A(m, _(innermostLoopInd + numRemainingLoops, N));
                    for (size_t i = innermostLoopInd;
                         i < numRemainingLoops + innermostLoopInd; ++i)
                        A(m, i + numToRemove) = A(m, i);
                    A(m, _(numToRemove + innermostLoopInd, N)) = tmp;
                }
            } else {
                tmp.resizeForOverwrite(numRemainingLoops);
                for (size_t m = 0; m < M; ++m) {
                    // fill tmp
                    tmp = A(m, _(innermostLoopInd,
                                 innermostLoopInd + numRemainingLoops));
                    for (size_t i = innermostLoopInd;
                         i < numToRemove + innermostLoopInd; ++i)
                        A(m, i) = A(m, i + numRemainingLoops);
                    A(m, _(numToRemove + innermostLoopInd, N)) = tmp;
                }
            }
        } else
            for (size_t m = 0; m < M; ++m)
                for (size_t i = 0; i < numToRemove; ++i)
                    std::swap(A(m, innermostLoopInd + i),
                              A(m, innermostLoopInd + i + numToRemove));

        for (size_t i = 0; i < numRemainingLoops; ++i)
            L = L->getParentLoop();
        // L is now inner most loop getting removed
        for (size_t i = 0; i < numToRemove; ++i) {
            llvm::Type *IntType = L->getInductionVariable(SE)->getType();
            S.push_back(SE.getAddRecExpr(SE.getZero(IntType),
                                         SE.getOne(IntType), L,
                                         llvm::SCEV::NoWrapMask));
        }
        initializeComparator();
    }
    void addZeroLowerBounds() {
        if (isEmpty())
            return;
        if constexpr (NonNegative)
            return pruneBounds();
        // return initializeComparator();
        auto [M, N] = A.size();
        if (!N)
            return;
        size_t numLoops = getNumLoops();
        A.resizeRows(M + numLoops);
        A(_(M, M + numLoops), _) = 0;
        for (size_t i = 0; i < numLoops; ++i)
            A(M + i, N - numLoops + i) = 1;
        initializeComparator();
        pruneBounds();
    }

    AffineLoopNest(IntMatrix A, llvm::SmallVector<const llvm::SCEV *> symbols)
        : Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>, NonNegative>(
              std::move(A), std::move(symbols)){};
    AffineLoopNest(IntMatrix A)
        : Polyhedra<EmptyMatrix<int64_t>, LinearSymbolicComparator,
                    llvm::SmallVector<const llvm::SCEV *>, NonNegative>(
              std::move(A)){};
    AffineLoopNest() = default;

    PtrVector<int64_t> getProgVars(size_t j) const {
        return A(j, _(0, getNumSymbols()));
    }
    void removeLoopBang(size_t i) {
        if constexpr (NonNegative)
            fourierMotzkinNonNegative(A, i + getNumSymbols());
        else
            fourierMotzkin(A, i + getNumSymbols());
        pruneBounds();
    }
    [[nodiscard]] AffineLoopNest<NonNegative> removeLoop(size_t i) const {
        AffineLoopNest<NonNegative> L{*this};
        // UnboundedAffineLoopNest L = *this;
        L.removeLoopBang(i);
        return L;
    }
    llvm::SmallVector<AffineLoopNest<NonNegative>, 0>
    perm(PtrVector<unsigned> x) {
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
    std::pair<IntMatrix, IntMatrix> bounds(size_t i) const {
        const auto [numNeg, numPos] = countSigns(A, i);
        std::pair<IntMatrix, IntMatrix> ret;
        ret.first.resizeForOverwrite(numNeg, A.numCol());
        ret.second.resizeForOverwrite(numPos, A.numCol());
        size_t negCount = 0;
        size_t posCount = 0;
        for (size_t j = 0; j < A.numRow(); ++j) {
            if (int64_t Aji = A(j, i))
                (Aji < 0 ? ret.first : ret.second)(
                    Aji < 0 ? negCount++ : posCount++, _) = A(j, _);
        }
        return ret;
    }
    llvm::SmallVector<std::pair<IntMatrix, IntMatrix>, 0>
    getBounds(PtrVector<unsigned> x) {
        llvm::SmallVector<std::pair<IntMatrix, IntMatrix>, 0> ret;
        size_t i = x.size();
        ret.resize_for_overwrite(i);
        AffineLoopNest<NonNegative> tmp = *this;
        while (true) {
            size_t xi = x[--i];
            ret[i] = tmp.bounds(xi);
            if (i == 0)
                break;
            tmp.removeLoopBang(xi);
        }
        return ret;
    }
    bool zeroExtraIterationsUponExtending(size_t _i, bool extendLower) const {
        AffineLoopNest<NonNegative> tmp{*this};
        const size_t numPrevLoops = getNumLoops() - 1;
        for (size_t i = 0; i < numPrevLoops; ++i)
            if (i != _i)
                tmp.removeVariableAndPrune(i + getNumSymbols());
        bool indep = true;
        const size_t numConst = getNumSymbols();
        for (size_t n = 0; n < tmp.A.numRow(); ++n)
            if ((tmp.A(n, _i + numConst) != 0) &&
                (tmp.A(n, numPrevLoops + numConst) != 0))
                indep = false;
        if (indep)
            return false;
        AffineLoopNest<NonNegative> margi{tmp};
        margi.removeVariableAndPrune(numPrevLoops + getNumSymbols());
        AffineLoopNest<NonNegative> tmp2;
        // margi contains extrema for `_i`
        // we can substitute extended for value of `_i`
        // in `tmp`
        int64_t sign = 2 * extendLower - 1; // extendLower ? 1 : -1
        for (size_t c = 0; c < margi.getNumInequalityConstraints(); ++c) {
            int64_t b = sign * margi.A(c, _i + numConst);
            if (b <= 0)
                continue;
            tmp2 = tmp;
            // increment to increase bound
            // this is correct for both extending lower and extending upper
            // lower: a'x + i + b >= 0 -> i >= -a'x - b
            // upper: a'x - i + b >= 0 -> i <=  a'x + b
            // to decrease the lower bound or increase the upper, we increment
            // `b`
            ++margi.A(c, 0);
            // our approach here is to set `_i` equal to the extended bound
            // and then check if the resulting polyhedra is empty.
            // if not, then we may have >0 iterations.
            for (size_t cc = 0; cc < tmp2.A.numRow(); ++cc) {
                int64_t d = tmp2.A(cc, _i + numConst);
                if (d == 0)
                    continue;
                d *= sign;
                for (size_t v = 0; v < tmp2.A.numCol(); ++v)
                    tmp2.A(cc, v) = b * tmp2.A(cc, v) - d * margi.A(c, v);
            }
            for (size_t cc = tmp2.A.numRow(); cc != 0;)
                if (tmp2.A(--cc, numPrevLoops + numConst) == 0)
                    eraseConstraint(tmp2.A, cc);
            tmp2.initializeComparator();
            if (!(tmp2.calcIsEmpty()))
                return false;
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
                for (size_t cc = 0; cc < tmp.A.numRow(); ++cc) {
                    if (int64_t d = tmp.A(cc, _i + numConst)) {
                        // lower bound is i >= 0
                        // so setting equal to the extended lower bound now
                        // means that i = -1 so we decrement `d` from the column
                        tmp.A(cc, 0) -= d;
                        tmp.A(cc, _i + numConst) = 0;
                    }
                }
                for (size_t cc = tmp.A.numRow(); cc != 0;)
                    if (tmp.A(--cc, numPrevLoops + numConst) == 0)
                        eraseConstraint(tmp.A, cc);
                tmp.initializeComparator();
                if (!(tmp.calcIsEmpty()))
                    return false;
            }
        }
        return true;
    }

    void printSymbol(llvm::raw_ostream &os, PtrVector<int64_t> x,
                     int64_t mul) const {
        bool printed = x[0] != 0;
        if (printed)
            os << mul * x[0];
        for (size_t i = 1; i < x.size(); ++i)
            if (int64_t xi = x[i] * mul) {
                if (printed)
                    os << (xi > 0 ? " + " : " - ");
                printed = true;
                int64_t absxi = std::abs(xi);
                if (absxi != 1)
                    os << absxi << " * ";
                os << *S[i - 1];
            }
    }

    // void printBound(llvm::raw_ostream &os, const IntMatrix &A, size_t i,
    void printBound(llvm::raw_ostream &os, size_t i, int64_t sign) const {
        const size_t numVar = getNumLoops();
        const size_t numVarMinus1 = numVar - 1;
        const size_t numConst = getNumSymbols();
        for (size_t j = 0; j < A.numRow(); ++j) {
            int64_t Aji = A(j, i + numConst) * sign;
            if (Aji <= 0)
                continue;
            if (A(j, i + numConst) != sign) {
                os << Aji << "*i_" << numVarMinus1 - i
                   << ((sign < 0) ? " <= " : " >= ");
            } else {
                os << "i_" << numVarMinus1 - i
                   << ((sign < 0) ? " <= " : " >= ");
            }
            PtrVector<int64_t> b = getProgVars(j);
            bool printed = !allZero(b);
            if (printed)
                printSymbol(os, b, -sign);
            for (size_t k = 0; k < numVar; ++k) {
                if (k == i)
                    continue;
                if (int64_t lakj = A(j, k + numConst)) {
                    if (lakj * sign > 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    lakj = std::abs(lakj);
                    if (lakj != 1)
                        os << lakj << "*";
                    os << "i_" << numVarMinus1 - k;
                    printed = true;
                }
            }
            if (!printed)
                os << 0;
            os << "\n";
        }
    }
    void printLowerBound(llvm::raw_ostream &os, size_t i) const {
        if constexpr (NonNegative)
            os << "i_" << getNumLoops() - 1 - i << " >= 0\n";
        printBound(os, i, 1);
    }
    void printUpperBound(llvm::raw_ostream &os, size_t i) const {
        printBound(os, i, -1);
    }
    // prints loops from inner most to outer most.
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const AffineLoopNest &alnb) {
        AffineLoopNest<NonNegative> aln{alnb};
        size_t numLoopsMinus1 = aln.getNumLoops() - 1;
        SHOWLN(alnb.getNumLoops());
        SHOWLN(aln.getNumLoops());
        SHOWLN(alnb.A);
        size_t i = 0;
        while (true) {
            os << "Loop " << numLoopsMinus1 - i << " lower bounds:\n";
            aln.printLowerBound(os, i);
            os << "Loop " << numLoopsMinus1 - i << " upper bounds:\n";
            aln.printUpperBound(os, i);
            if (i == numLoopsMinus1)
                break;
            aln.removeLoopBang(i++);
        }
        return os;
    }
    void dump() const { llvm::errs() << *this; }
};
