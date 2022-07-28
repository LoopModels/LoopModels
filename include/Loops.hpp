#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Permutation.hpp"
#include "./Polyhedra.hpp"
#include "./Symbolics.hpp"
#include "Comparators.hpp"
#include "Constraints.hpp"
#include "EmptyArrays.hpp"
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
struct AffineLoopNest : SymbolicPolyhedra,
                        llvm::RefCountedBase<AffineLoopNest> {
    // struct AffineLoopNest : Polyhedra<EmptyMatrix<int64_t>,
    // SymbolicComparator> {

    static llvm::IntrusiveRefCntPtr<AffineLoopNest>
    construct(const IntMatrix &A, llvm::ArrayRef<MPoly> b,
              PartiallyOrderedSet poset) {
        assert(b.size() == A.numRow());
        // IntMatrix B;
        llvm::SmallVector<Polynomial::Monomial> monomials;
        llvm::DenseMap<Polynomial::Monomial, unsigned> map;

        for (auto &p : b)
            for (auto &t : p)
                if (!t.isCompileTimeConstant())
                    if (map.insert(std::make_pair(t.exponent, map.size()))
                            .second)
                        monomials.push_back(t.exponent);
        const size_t numMonomials = monomials.size();
        auto ret = llvm::makeIntrusiveRefCnt<AffineLoopNest>();
	auto &B = ret->A;
        B.resize(A.numRow(), A.numCol() + 1 + map.size());
        for (size_t r = 0; r < A.numRow(); ++r) {
            for (auto &t : b[r]) {
                size_t c = t.isCompileTimeConstant() ? 0 : map[t.exponent] + 1;
                B(r, c) = t.coefficient;
            }
            for (size_t c = 0; c < A.numCol(); ++c)
                B(r, c + 1 + numMonomials) = A(r, c);
        }
	ret->C = SymbolicComparator::construct(b, std::move(poset));
	return ret;
        // return llvm::makeIntrusiveRefCnt<AffineLoopNest>(
        //     std::move(B), EmptyMatrix<int64_t>{},
        //     SymbolicComparator::construct(b, std::move(poset)));
    }

    inline size_t getNumLoops() const { return getNumVar(); }

    llvm::IntrusiveRefCntPtr<AffineLoopNest>
    rotate(PtrMatrix<const int64_t> R) const {
        assert(R.numCol() == getNumLoops());
        assert(R.numRow() == getNumLoops());
        const size_t numConst = C.getNumConstTerms();
        const auto [M, N] = A.size();
        assert(numConst + getNumLoops() == N);
        auto ret = llvm::makeIntrusiveRefCnt<AffineLoopNest>();
	ret->C =C;
        IntMatrix &B = ret->A;
        B.resizeForOverwrite(M, N);
        for (size_t m = 0; m < M; ++m) {
            for (size_t n = 0; n < numConst; ++n)
                B(m, n) = A(m, n);
            for (size_t n = numConst; n < N; ++n)
                B(m, n) = 0;
        }
        matmul(B.view(0, M, numConst, N), A.view(0, M, numConst, N), R);
        std::cout << "A = \n" << A << std::endl;
        std::cout << "R = \n" << R << std::endl;
        std::cout << "B = \n" << B << std::endl;
	return ret;
        // return llvm::makeIntrusiveRefCnt<AffineLoopNest>(
            // std::move(B), EmptyMatrix<int64_t>(), C);
    }
    // AffineLoopNest(const IntMatrix &Ain, llvm::ArrayRef<MPoly> b,
    //                PartiallyOrderedSet posetin)
    //     : Polyhedra{symbolicPolyhedra(Ain, b, std::move(posetin))} {
    //     pruneBounds();
    // }
    // AffineLoopNest(IntMatrix Ain, SymbolicComparator C)
    //     : Polyhedra<EmptyMatrix<int64_t>, SymbolicComparator>{
    //           .A = std::move(Ain),
    //           .E = EmptyMatrix<int64_t>{},
    //           .C = std::move(C)} {
    //     pruneBounds();
    // }
    void removeLoopBang(size_t i) {
        // for (size_t i = 0; i < A.numRow(); ++i)
        // assert(!allZero(A.getRow(i)));
        // std::cout << "removing i = " << i << "; A=\n" << A << std::endl;
        fourierMotzkin(A, i + C.getNumConstTerms());
        // std::cout << "removed i = " << i << "; A=\n" << A << std::endl;
        // for (size_t i = 0; i < A.numRow(); ++i)
        // assert(!allZero(A.getRow(i)));
        pruneBounds();
        // // std::cout << "After prune bounds i = " << i << "; A =\n"<< A <<
        // // std::endl;
        // for (size_t i = 0; i < A.numRow(); ++i)
        //     assert(!allZero(A.getRow(i)));
        // assert(allZero(A.getCol(i + C.getNumConstTerms())));
    }
    llvm::IntrusiveRefCntPtr<AffineLoopNest> removeLoop(size_t i) {
        auto L{llvm::makeIntrusiveRefCnt<AffineLoopNest>(*this)};
        // AffineLoopNest L = *this;
        L->removeLoopBang(i);
        return L;
    }
    llvm::SmallVector<llvm::IntrusiveRefCntPtr<AffineLoopNest>>
    perm(llvm::ArrayRef<unsigned> x) {
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
    std::pair<IntMatrix, IntMatrix> bounds(size_t i) {
        const auto [numNeg, numPos] = countSigns(A, i);
        std::pair<IntMatrix, IntMatrix> ret;
        ret.first.resizeForOverwrite(numNeg, A.numCol());
        ret.second.resizeForOverwrite(numPos, A.numCol());
        size_t negCount = 0;
        size_t posCount = 0;
        for (size_t j = 0; j < A.numRow(); ++j) {
            if (int64_t Aji = A(j, i))
                (Aji < 0 ? ret.first : ret.second)
                    .copyRow(A.getRow(j), (Aji < 0 ? negCount++ : posCount++));
        }
        return ret;
    }
    llvm::SmallVector<std::pair<IntMatrix, IntMatrix>, 0>
    getBounds(llvm::ArrayRef<unsigned> x) {
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
    bool zeroExtraIterationsUponExtending(size_t _i, bool extendLower) const {
        SymbolicPolyhedra tmp{*this};
        const size_t numPrevLoops = getNumLoops() - 1;
        // for (size_t i = 0; i < numPrevLoops; ++i)
        // if (_i != i)
        // tmp.removeLoopBang(i);

        // for (size_t i = _i + 1; i < numPrevLoops; ++i)
        // tmp.removeLoopBang(i);
        for (size_t i = 0; i < numPrevLoops; ++i)
            if (i != _i)
                tmp.removeVariableAndPrune(i + C.getNumConstTerms());
        bool indep = true;
        const size_t numConst = C.getNumConstTerms();
        for (size_t n = 0; n < tmp.A.numRow(); ++n)
            if ((tmp.A(n, _i + numConst) != 0) &&
                (tmp.A(n, numPrevLoops + numConst) != 0))
                indep = false;
        if (indep)
            return false;
        SymbolicPolyhedra margi{tmp};
        margi.removeVariableAndPrune(numPrevLoops + C.getNumConstTerms());
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
        const size_t numVar = getNumVar();
        const size_t numConst = C.getNumConstTerms();
        // printVector(std::cout << "A.getRow(i) = ", A.getRow(i)) << std::endl;
        for (size_t j = 0; j < A.numRow(); ++j) {
            int64_t Aji = A(j, i + numConst) * sign;
            if (Aji <= 0)
                continue;
            if (A(j, i + numConst) == sign) {
                if (sign < 0) {
                    os << "i_" << i << " <= ";
                } else {
                    os << "i_" << i << " >= ";
                }
            } else {
                os << Aji << "*i_" << i << ((sign < 0) ? " <= " : " >= ");
            }
            llvm::ArrayRef<int64_t> b = getSymbol(A, j);
            bool printed = !allZero(b);
            if (printed)
                C.printSymbol(os, b, -sign);
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
        size_t i = aln.getNumVar();
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
