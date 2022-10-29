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

// static llvm::Optional<int64_t> getConstantInt(llvm::Value *v) {
//     if (llvm::ConstantInt *c = llvm::dyn_cast<llvm::ConstantInt>(v))
//         if (c->getBitWidth() <= 64)
//             return c->getSExtValue();
//     return {};
// }
static llvm::Optional<int64_t> getConstantInt(const llvm::SCEV *v) {
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

// A' * i <= b
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest : SymbolicPolyhedra { //,
    // llvm::RefCountedBase<AffineLoopNest> {
    // struct AffineLoopNest : Polyhedra<EmptyMatrix<int64_t>,
    // SymbolicComparator> {
    llvm::SmallVector<const llvm::SCEV *> symbols{};
    // llvm::SmallVector<Polynomial::Monomial> symbols;
    size_t getNumSymbols() const { return 1 + symbols.size(); }
    size_t getNumLoops() const { return A.numCol() - getNumSymbols(); }

    size_t findIndex(const llvm::SCEV *v) {
        for (size_t i = 0; i < symbols.size();)
            if (symbols[i++] == v)
                return i;
        return 0;
    }
    // static llvm::Optional<
    //     std::tuple<const llvm::SCEV *, const llvm::Loop *, int64_t>>
    // decomposeAffineConstStride(const llvm::SCEV *v) {
    //     // ex->getStart() + ex->getLoop()*ex->getStepRecurrence(SE)
    //     // ex->getOperand(0) + ex->getLoop()*ex->getOperand(1)
    //     // we don't have SE here, so we're using the latter
    //     // NOTE: if we start passing a ScalarEvolution, switch to former
    //     if (const llvm::SCEVAddRecExpr *x =
    //             llvm::dyn_cast<const llvm::SCEVAddRecExpr>(v))
    //         if (x->isAffine())
    //             if (auto c = getConstantInt(x->getOperand(1)))
    //                 return std::make_tuple(x->getOperand(0), x->getLoop(),
    //                 *c);
    //     return {};
    // }
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
        }
        if (llvm::Optional<int64_t> c = getConstantInt(v)) {
            for (size_t j = l; j < u; ++j)
                A(j, 0) += mlt * (*c);
            return minDepth;
        } else if (const llvm::SCEVAddExpr *ex =
                       llvm::dyn_cast<const llvm::SCEVAddExpr>(v)) {
            size_t M = A.numRow();
            minDepth =
                addSymbol(B, L, ex->getOperand(0), SE, l, u, mlt, minDepth);
            if (M != A.numRow())
                minDepth = addSymbol(B, L, ex->getOperand(1), SE, M, A.numRow(),
                                     mlt, minDepth);
            return addSymbol(B, L, ex->getOperand(1), SE, l, u, mlt, minDepth);
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
            bool isMin = llvm::isa<llvm::SCEVSMinExpr>(ex) ||
                         llvm::isa<llvm::SCEVUMinExpr>(ex);
            if (isMin ^
                (mlt < 0)) { // we can represent this as additional constraints
                size_t M = A.numRow();
                A.resizeRows(M + u - l);
                B.resizeRows(M + u - l);
                size_t Mp = M + u - l;
                A(_(M, Mp), _) = A(_(l, u), _);
                B(_(M, Mp), _) = B(_(l, u), _);
                minDepth =
                    addSymbol(B, L, ex->getOperand(0), SE, l, u, mlt, minDepth);
                minDepth = addSymbol(B, L, ex->getOperand(1), SE, M, Mp, mlt,
                                     minDepth);
            }
        }
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
        symbols.push_back(v);
        A.resizeCols(A.numCol() + 1);
        // A.insertZeroColumn(symbols.size());
        for (size_t j = l; j < u; ++j)
            A(j, symbols.size()) = mlt;
        // llvm::errs() << "After adding sym A = " << A << "\n";
    }
    size_t addBackedgeTakenCount(IntMatrix &B, llvm::Loop *L,
                                 const llvm::SCEV *BT,
                                 llvm::ScalarEvolution &SE, size_t minDepth) {
        size_t M = A.numRow();
        A.resizeRows(M + 1);
        B.resizeRows(M + 1);
        minDepth = addSymbol(B, L, BT, SE, M, M + 1, 1, minDepth);
        assert(A.numRow() == B.numRow());
        size_t depth = L->getLoopDepth();
        for (size_t m = M; m < A.numRow(); ++m) {
            ++A(m, 0);                     // backedge taken -> trip count
            B(m, B.numCol() - depth) = -1; // indvar
        }
        // recurse, if possible to add an outer layer
        if (llvm::Loop *P = L->getParentLoop())
            if (areSymbolsLoopInvariant(P, SE))
                if (const llvm::SCEV *BTP = SE.getBackedgeTakenCount(P))
                    if (!llvm::isa<llvm::SCEVCouldNotCompute>(BTP))
                        return addBackedgeTakenCount(B, P, BTP, SE, minDepth);
        return std::max(depth - 1, minDepth);
    }
    bool areSymbolsLoopInvariant(llvm::Loop *L,
                                 llvm::ScalarEvolution &SE) const {
        for (size_t i = 0; i < symbols.size(); ++i)
            if ((!allZero(A(_, i + 1))) && (!SE.isLoopInvariant(symbols[i], L)))
                return false;
        return true;
    }
    static llvm::Optional<AffineLoopNest> construct(llvm::Loop *L,
                                                    llvm::ScalarEvolution &SE) {
        auto BT = SE.getBackedgeTakenCount(L);
        if (!BT || llvm::isa<llvm::SCEVCouldNotCompute>(BT))
            return {};
        return AffineLoopNest(L, BT, SE);
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
                                               llvm::SCEV::FlagNW),
                              i, i + 1, Bid);
                }
            }
        }
        size_t depth = maxDepth - minDepth;
        size_t N = A.numCol();
        A.resizeCols(N + depth);
        // copy the included loops from B into A
        A(_, _(N, N + depth)) = B(_, _(0, depth));
        // addZeroLowerBounds();
        // NOTE: pruneBounds() is not legal here if we wish to use
        // removeInnerMost later.
        // pruneBounds();
    }
    [[nodiscard]] AffineLoopNest removeInnerMost() const {
        size_t innermostLoopInd = getNumSymbols();
        IntMatrix B = A.deleteCol(innermostLoopInd);
        SHOWLN(A);
        SHOWLN(B);
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
        SHOWLN(B);
        return AffineLoopNest(B, symbols);
    }

    void addZeroLowerBounds() {
        size_t numLoops = getNumLoops();
        auto [M, N] = A.size();
        A.resizeRows(M + numLoops);
        A(_(M, M + numLoops), _) = 0;
        SHOW(M);
        CSHOW(N);
        CSHOWLN(A);
        for (size_t i = 0; i < numLoops; ++i)
            A(M + i, N - numLoops + i) = 1;
        C = LinearSymbolicComparator::construct(A);
        pruneBounds();
    }

    AffineLoopNest(IntMatrix A, llvm::SmallVector<const llvm::SCEV *> symbols)
        : SymbolicPolyhedra(std::move(A)), symbols(std::move(symbols)){};
    AffineLoopNest(IntMatrix A)
        : SymbolicPolyhedra(std::move(A)), symbols({}){};
    AffineLoopNest() = default;

    AffineLoopNest rotate(PtrMatrix<int64_t> R, size_t numPeeled = 0) const {
        SHOW(R.numCol());
        CSHOW(numPeeled);
        CSHOWLN(getNumLoops());
        assert(R.numCol() + numPeeled == getNumLoops());
        assert(R.numRow() + numPeeled == getNumLoops());
        assert(numPeeled < getNumLoops());
        const size_t numConst = getNumSymbols() + numPeeled;
        const auto [M, N] = A.size();
        AffineLoopNest ret;
        ret.symbols = symbols;
        IntMatrix &B = ret.A;
        B.resizeForOverwrite(M, N);
        B(_, _(begin, numConst)) = A(_, _(begin, numConst));
        B(_, _(numConst, end)) = A(_, _(numConst, end)) * R;
        ret.C = LinearSymbolicComparator::construct(B);
        llvm::errs() << "A = \n" << A << "\n";
        llvm::errs() << "R = \n" << R << "\n";
        llvm::errs() << "B = \n" << B << "\n";
        return ret;
    }

    PtrVector<int64_t> getProgVars(size_t j) const {
        return A(j, _(0, getNumSymbols()));
    }
    void removeLoopBang(size_t i) {
        SHOW(i);
        CSHOWLN(getNumSymbols());
        fourierMotzkin(A, i + getNumSymbols());
        pruneBounds();
    }
    [[nodiscard]] AffineLoopNest removeLoop(size_t i) const {
        AffineLoopNest L{*this};
        // AffineLoopNest L = *this;
        L.removeLoopBang(i);
        return L;
    }
    llvm::SmallVector<AffineLoopNest, 0> perm(PtrVector<unsigned> x) {
        llvm::SmallVector<AffineLoopNest, 0> ret;
        // llvm::SmallVector<AffineLoopNest, 0> ret;
        ret.resize_for_overwrite(x.size());
        ret.back() = *this;
        for (size_t i = x.size() - 1; i != 0;) {
            AffineLoopNest &prev = ret[i];
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
        AffineLoopNest tmp = *this;
        while (true) {
            size_t xi = x[--i];
            ret[i] = tmp.bounds(xi);
            if (i == 0)
                break;
            tmp.removeLoopBang(xi);
        }
        return ret;
    }
    // bool isEmpty(size_t numConst) const {
    //     return static_cast<const SymbolicPolyhedra *>(this)->isEmpty(
    //         getNumSymbols());
    // }
    // bool isEmptyBang(size_t numConst) {
    //     return static_cast<SymbolicPolyhedra *>(this)->isEmptyBang(
    //         getNumSymbols());
    // }
    bool zeroExtraIterationsUponExtending(size_t _i, bool extendLower) const {
        SymbolicPolyhedra tmp{*this};
        const size_t numPrevLoops = getNumLoops() - 1;
        // SHOW(getNumLoops());
        // SHOW(numPrevLoops);
        // SHOW(A.numRow());
        // SHOW(A.numCol());
        // for (size_t i = 0; i < numPrevLoops; ++i)
        // if (_i != i)
        // tmp.removeLoopBang(i);

        // for (size_t i = _i + 1; i < numPrevLoops; ++i)
        // tmp.removeLoopBang(i);
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
        SymbolicPolyhedra margi{tmp};
        margi.removeVariableAndPrune(numPrevLoops + getNumSymbols());
        SymbolicPolyhedra tmp2;
        llvm::errs() << "\nmargi="
                     << "\n";
        margi.dump();
        llvm::errs() << "\ntmp="
                     << "\n";
        tmp.dump();
        // margi contains extrema for `_i`
        // we can substitute extended for value of `_i`
        // in `tmp`
        int64_t sign = 2 * extendLower - 1; // extendLower ? 1 : -1
        for (size_t c = 0; c < margi.getNumInequalityConstraints(); ++c) {
            int64_t Aci = margi.A(c, _i + numConst);
            int64_t b = sign * Aci;
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
            llvm::errs() << "\nc=" << c << "; tmp2="
                         << "\n";
            tmp2.dump();
            if (!(tmp2.isEmpty()))
                return false;
        }
        return true;
    }

    void printSymbol(llvm::raw_ostream &os, PtrVector<int64_t> x,
                     int64_t mul) const {
        os << mul * x[0];
        for (size_t i = 1; i < x.size(); ++i)
            if (int64_t xi = x[i] * mul) {
                os << (xi > 0 ? " + " : " - ");
                int64_t absxi = std::abs(xi);
                if (absxi != 1)
                    os << absxi << " * ";
                os << *symbols[i - 1];
            }
    }

    // void printBound(llvm::raw_ostream &os, const IntMatrix &A, size_t i,
    void printBound(llvm::raw_ostream &os, size_t i, int64_t sign) const {
        const size_t numVar = getNumLoops();
        const size_t numConst = getNumSymbols();
        for (size_t j = 0; j < A.numRow(); ++j) {
            int64_t Aji = A(j, i + numConst) * sign;
            if (Aji <= 0)
                continue;
            if (A(j, i + numConst) != sign) {
                os << Aji << "*i_" << i << ((sign < 0) ? " <= " : " >= ");
            } else {
                os << "i_" << i << ((sign < 0) ? " <= " : " >= ");
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
                    os << "i_" << k;
                    printed = true;
                }
            }
            if (!printed)
                os << 0;
            os << "\n";
        }
    }
    void printLowerBound(llvm::raw_ostream &os, size_t i) const {
        printBound(os, i, 1);
    }
    void printUpperBound(llvm::raw_ostream &os, size_t i) const {
        printBound(os, i, -1);
    }
    // prints loops from inner most to outer most.
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const AffineLoopNest &alnb) {
        AffineLoopNest aln{alnb};
        size_t i = aln.getNumLoops();
        SHOWLN(alnb.getNumLoops());
        SHOWLN(aln.getNumLoops());
        SHOWLN(alnb.A);
        while (true) {
            os << "Loop " << --i << " lower bounds:\n";
            aln.printLowerBound(os, i);
            os << "Loop " << i << " upper bounds:\n";
            aln.printUpperBound(os, i);
            if (i == 0)
                break;
            aln.removeLoopBang(i);
        }
        return os;
    }
    void dump() const { llvm::errs() << *this; }
};
