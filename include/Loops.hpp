#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include "Comparators.hpp"
#include "Constraints.hpp"
#include "EmptyArrays.hpp"
#include "Macro.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>

// A' * i <= b
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest : SymbolicPolyhedra{//,
                        // llvm::RefCountedBase<AffineLoopNest> {
    // struct AffineLoopNest : Polyhedra<EmptyMatrix<int64_t>,
    // SymbolicComparator> {
    llvm::SmallVector<Polynomial::Monomial> symbols;
    size_t getNumSymbols() const { return 1 + symbols.size(); }
    size_t getNumLoops() const { return A.numCol() - getNumSymbols(); }

    // AffineLoopNest(AffineLoopNest &parent, MPoly &first, MPoly &step, MPoly &last){
    // }
    // AffineLoopNest() = default;
    // AffineLoopNest(IntMatrix A, llvm::SmallVector<Polynomial::Monomial> symbols) : SymbolicPolyhdra(A, symbols) {

    // }
    
    // static llvm::IntrusiveRefCntPtr<AffineLoopNest>
    // construct(IntMatrix A, llvm::SmallVector<Polynomial::Monomial> symbols) {
    //     llvm::IntrusiveRefCntPtr<AffineLoopNest> ret{
    //         llvm::makeIntrusiveRefCnt<AffineLoopNest>()};
    //     ret->A = std::move(A);
    //     ret->C = LinearSymbolicComparator::construct(ret->A);
    //     ret->symbols = std::move(symbols);
    //     return ret;
    // }
    // static llvm::IntrusiveRefCntPtr<AffineLoopNest>
    // construct(IntMatrix A, LinearSymbolicComparator C,
    //           llvm::SmallVector<Polynomial::Monomial> symbols) {
    //     llvm::IntrusiveRefCntPtr<AffineLoopNest> ret{
    //         llvm::makeIntrusiveRefCnt<AffineLoopNest>()};
    //     ret->A = std::move(A);
    //     ret->C = std::move(C);
    //     ret->symbols = std::move(symbols);
    //     return ret;
    // }

    llvm::IntrusiveRefCntPtr<AffineLoopNest>
    rotate(PtrMatrix<int64_t> R, size_t numPeeled = 0) const {
	SHOW(R.numCol());
	CSHOW(numPeeled);
	CSHOWLN(getNumLoops());
        assert(R.numCol() + numPeeled == getNumLoops());
        assert(R.numRow() + numPeeled == getNumLoops());
        assert(numPeeled < getNumLoops());
        const size_t numConst = getNumSymbols() + numPeeled;
        const auto [M, N] = A.size();
        auto ret = llvm::makeIntrusiveRefCnt<AffineLoopNest>();
        ret->symbols = symbols;
        IntMatrix &B = ret->A;
        B.resizeForOverwrite(M, N);
        B(_, _(begin, numConst)) = A(_, _(begin, numConst));
        B(_, _(numConst, end)) = A(_, _(numConst, end)) * R;
        ret->C = LinearSymbolicComparator::construct(B);
        std::cout << "A = \n" << A << std::endl;
        std::cout << "R = \n" << R << std::endl;
        std::cout << "B = \n" << B << std::endl;
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
    [[nodiscard]] llvm::IntrusiveRefCntPtr<AffineLoopNest>
    removeLoop(size_t i) const {
        auto L{llvm::makeIntrusiveRefCnt<AffineLoopNest>(*this)};
        // AffineLoopNest L = *this;
        L->removeLoopBang(i);
        return L;
    }
    llvm::SmallVector<llvm::IntrusiveRefCntPtr<AffineLoopNest>>
    perm(PtrVector<unsigned> x) {
        llvm::SmallVector<llvm::IntrusiveRefCntPtr<AffineLoopNest>> ret;
        // llvm::SmallVector<AffineLoopNest, 0> ret;
        ret.resize_for_overwrite(x.size());
        ret.back() = this;
        for (size_t i = x.size() - 1; i != 0;) {
            llvm::IntrusiveRefCntPtr<AffineLoopNest> prev = ret[i];
            // AffineLoopNest &prev = ret[i];
            size_t oldi = i;
            ret[--i] = prev->removeLoop(x[oldi]);
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
        std::cout << "\nmargi=" << std::endl;
        margi.dump();
        std::cout << "\ntmp=" << std::endl;
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
            std::cout << "\nc=" << c << "; tmp2=" << std::endl;
            tmp2.dump();
            if (!(tmp2.isEmpty()))
                return false;
        }
        return true;
    }

    // void printBound(std::ostream &os, const IntMatrix &A, size_t i,
    void printBound(std::ostream &os, size_t i, int64_t sign) const {
        const size_t numVar = getNumLoops();
        const size_t numConst = getNumSymbols();
        SHOW(numVar);
        CSHOW(numConst);
        CSHOWLN(A.numCol());
        // printVector(std::cout << "A.getRow(i) = ", A.getRow(i)) << std::endl;
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
                printSymbol(os, b, symbols, -sign);
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
            os << std::endl;
        }
    }
    void printLowerBound(std::ostream &os, size_t i) const {
        printBound(os, i, 1);
    }
    void printUpperBound(std::ostream &os, size_t i) const {
        printBound(os, i, -1);
    }
    // prints loops from inner most to outer most.
    friend std::ostream &operator<<(std::ostream &os,
                                    const AffineLoopNest &alnb) {
        AffineLoopNest aln{alnb};
        size_t i = aln.getNumLoops();
        SHOWLN(alnb.getNumLoops());
        SHOWLN(aln.getNumLoops());
        while (true) {
            os << "Loop " << --i << " lower bounds: " << std::endl;
            aln.printLowerBound(os, i);
            os << "Loop " << i << " upper bounds: " << std::endl;
            aln.printUpperBound(os, i);
            if (i == 0)
                break;
            aln.removeLoopBang(i);
        }
        return os;
    }
    void dump() const { std::cout << *this; }
};
