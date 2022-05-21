#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Symbolics.hpp"
#include "NormalForm.hpp"
#include <algorithm>
#include <any>
#include <bits/ranges_algo.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

// the AbstractPolyhedra defines methods we reuse across Polyhedra with known
// (`Int`) bounds, as well as with unknown (symbolic) bounds.
// In either case, we assume the matrix `A` consists of known integers.
template <class P, typename T> struct AbstractPolyhedra {
    Matrix<intptr_t, 0, 0, 0> A;
    llvm::SmallVector<T, 8> b;

    // AbstractPolyhedra(const Matrix<intptr_t, 0, 0, 0> A,
    //                   const llvm::SmallVector<P, 8> b)
    //     : A(std::move(A)), b(std::move(b)), lowerA(A.size(0)),
    //       upperA(A.size(0)), lowerb(A.size(0)), upperb(A.size(0)){};
    AbstractPolyhedra(const Matrix<intptr_t, 0, 0, 0> A,
                      const llvm::SmallVector<T, 8> b)
        : A(std::move(A)), b(std::move(b)){};

    size_t getNumVar() const { return A.size(0); }
    size_t getNumConstraints() const { return A.size(1); }

    // methods required to support AbstractPolyhedra
    bool knownLessEqualZero(T x) const {
        return static_cast<const P *>(this)->knownLessEqualZeroImpl(
            std::move(x));
    }
    bool knownGreaterEqualZero(const T &x) const {
        return static_cast<const P *>(this)->knownGreaterEqualZeroImpl(x);
    }

    // setBounds(a, b, la, lb, ua, ub, i)
    // `la` and `lb` correspond to the lower bound of `i`
    // `ua` and `ub` correspond to the upper bound of `i`
    // Eliminate `i`, and set `a` and `b` appropriately.
    // Returns `true` if `a` still depends on another variable.
    static bool setBounds(PtrVector<intptr_t> a, T &b,
                          llvm::ArrayRef<intptr_t> la, const T &lb,
                          llvm::ArrayRef<intptr_t> ua, const T &ub, size_t i) {
        intptr_t cu_base = ua[i];
        intptr_t cl_base = la[i];
        if ((cu_base < 0) && (cl_base > 0))
            // if cu_base < 0, then it is an lower bound, so swap
            return setBounds(a, b, ua, ub, la, lb, i);
        intptr_t g = std::gcd(cu_base, cl_base);
        intptr_t cu = cu_base / g;
        intptr_t cl = cl_base / g;
        b = cu * lb;
        Polynomial::fnmadd(b, ub, cl);
        size_t N = la.size();
        for (size_t n = 0; n < N; ++n) {
            a[n] = cu * la[n] - cl * ua[n];
        }
        // bool anynonzero = std::ranges::any_of(a, [](intptr_t ai) { return ai
        // != 0; }); if (!anynonzero){
        //     std::cout << "All A[:,"<<i<<"] = 0; b["<<i<<"] = " << b <<
        //     std::endl;
        // }
        // return anynonzero;
        return std::ranges::any_of(a, [](intptr_t ai) { return ai != 0; });
    }
    static bool uniqueConstraint(auto &A, llvm::ArrayRef<T> b, size_t C) {
        for (size_t c = 0; c < C; ++c) {
            bool allEqual = b[c] == b[C];
            if (allEqual) {
                for (size_t r = 0; r < A.numRow(); ++r) {
                    allEqual &= (A(r, c) == A(r, C));
                }
            }
            if (allEqual)
                return false;
        }
        return true;
    }
    // independentOfInner(a, i)
    // checks if any `a[j] != 0`, such that `j != i`.
    // I.e., if this vector defines a hyper plane otherwise independent of `i`.
    static inline bool independentOfInner(llvm::ArrayRef<intptr_t> a,
                                          size_t i) {
        for (size_t j = 0; j < a.size(); ++j) {
            if ((a[j] != 0) & (i != j)) {
                return false;
            }
        }
        return true;
    }
    // -1 indicates no auxiliary variable
    intptr_t auxiliaryInd(llvm::ArrayRef<intptr_t> a) const {
        const size_t numAuxVar = a.size() - getNumVar();
        for (size_t i = 0; i < numAuxVar; ++i) {
            if (a[i])
                return i;
        }
        return -1;
    }
    static inline bool auxMisMatch(intptr_t x, intptr_t y) {
        return (x >= 0) && (y >= 0) && (x != y);
    }
    /*
    void zeroSupDiagonalAux(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                            size_t r, size_t c, size_t N) const {
        size_t M = A.numRow();
        intptr_t auxInd = auxiliaryInd(A.getCol(c));
        for (size_t j = c + 1; j < N; ++j) {
            if (auxMisMatch(auxInd, auxiliaryInd(A.getCol(j))))
                continue;
            intptr_t Aii = A(r, c);
            intptr_t Aij = A(r, j);
            auto [r, p, q] = gcdx(Aii, Aij);
            intptr_t Aiir = Aii / r;
            intptr_t Aijr = Aij / r;
            for (size_t k = 0; k < M; ++k) {
                intptr_t Aki = A(k, c);
                intptr_t Akj = A(k, j);
                A(k, c) = p * Aki + q * Akj;
                A(k, j) = Aiir * Akj - Aijr * Aki;
            }
            T bi = std::move(b[c]);
            T bj = std::move(b[j]);
            b[c] = p * bi + q * bj;
            b[j] = Aiir * bj - Aijr * bi;
        }
    }
    void reduceSubDiagonalAux(IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                              size_t r, size_t c, size_t s) const {
        const size_t M = A.numRow();
        intptr_t Akk = A(r, c);
        if (Akk < 0) {
            Akk = -Akk;
            for (size_t i = 0; i < M; ++i) {
                A(i, c) *= -1;
            }
            negate(b[c]);
        }
        intptr_t auxInd = auxiliaryInd(A.getCol(c));
        for (size_t z = s; z < c; ++z) {
            if (auxMisMatch(auxInd, auxiliaryInd(A.getCol(z))))
                continue;
            // try to eliminate `A(k,z)`
            intptr_t Akz = A(r, z);
            // if Akk == 1, then this zeros out Akz
            if (Akz) {
                // we want positive but smaller subdiagonals
                // e.g., `Akz = 5, Akk = 2`, then in the loop below when `i=k`,
                // we set A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
                //        =   5 - 2*2 = 1
                // or if `Akz = -5, Akk = 2`, then in the loop below we get
                // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
                //        =  -5 - (-2 - 1)*2 = = 6 - 5 = 1
                // if `Akk = 1`, then
                // A(k,z) = A(k,z) - (A(k,z)/Akk) * Akk
                //        = A(k,z) - A(k,z) = 0
                // or if `Akz = -7, Akk = 39`, then in the loop below we get
                // A(k,z) = A(k,z) - ((A(k,z)/Akk) - ((A(k,z) % Akk) != 0) * Akk
                //        =  -7 - ((-7/39) - 1)*39 = = 6 - 5 = 1
                intptr_t AkzOld = Akz;
                Akz /= Akk;
                if (AkzOld < 0) {
                    Akz -= (AkzOld != (Akz * Akk));
                }
            } else {
                continue;
            }
            for (size_t i = 0; i < M; ++i) {
                A(i, z) -= Akz * A(i, c);
            }
            Polynomial::fnmadd(b[z], b[c], Akz);
        }
    }
    bool pivotCols(IntMatrix auto &A, auto &K, size_t i, size_t piv,
                   size_t N) const {
        size_t j = piv;
        while (A(i, piv) == 0) {
            if (++piv == N) {
                return false;
            }
        }
        if (j != piv) {
            std::cout << "swapping columns j = " << j << " and piv = " << piv
                      << std::endl;
            swapCols(A, j, piv);
            swapCols(K, j, piv);
        }
        return true;
    }

    void simplifyAuxEqualityConstraints(auto &E,
                                        llvm::SmallVectorImpl<T> &q) const {
        {
            intptr_t lastAux = -1;
            for (size_t c = 0; c < E.numCol(); ++c) {
                intptr_t auxInd = auxiliaryInd(E.getCol(c));
                assert(auxInd >= lastAux);
                lastAux = auxInd;
            }
        }
        std::cout << "\nStarting simplifications:" << std::endl;
        printConstraints(std::cout, E, q, false);
        auto [M, N] = E.size();
        size_t m = 0, n = 0;
        intptr_t auxInd = -1;
        size_t Na = N;
        for (size_t c = 0; c < N; ++c) {
            if (auxiliaryInd(E.getCol(c)) != -1) {
                Na = c;
                break;
            }
        }
        std::cout << "Na = " << Na << std::endl;
        // first, we iterate over auxInd == -1
        while (true) {
            if ((m >= M) || (n >= Na))
                break;

            if (pivotCols(E, q, m, n, Na)) {
                // E(m, n) now contains non-zero
                // zero row `m` of every column to the right of `n`
                zeroSupDiagonalAux(E, q, m, n, Na);
                // now we reduce the sub diagonal
                reduceSubDiagonalAux(E, q, m, n, 0);
                ++n;
            }
            ++m;
        }
        std::cout << "\nAux = -1 simplifications:" << std::endl;
        printConstraints(std::cout, E, q, false);
        if ((m < M) && (n < N)) {
            // then auxInd != -1
            size_t ns = n;
            do {
                intptr_t nextAux = auxInd;
                size_t na = ns;
                while (++na < N) {
                    nextAux = auxiliaryInd(E.getCol(na));
                    if (nextAux != auxInd) {
                        break;
                    }
                }
                // now we simplify the hcat([0,n),[ns,na)) block.
                size_t mi = m, ni = ns;
                do {
                    if (pivotCols(E, q, mi, ni, na)) {
                        zeroSupDiagonalAux(E, q, mi, ni, na);
                        // now we reduce the sub diagonal
                        reduceSubDiagonalAux(E, q, mi, ni, ns);
                        ++ni;
                    }
                    ++mi;
                } while ((mi < M) && (ni < na));
                // while ((na != 0) && allZero(E.getCol(na - 1))) {
                //     E.eraseCol(--na);
                //     q.erase(q.begin() + na);
                //     --N;
                // }
                ns = na;
            } while (ns < N);
        }
        while (N != 0) {
            if (allZero(E.getCol(--N))) {
                E.eraseCol(N);
                q.erase(q.begin() + N);
            }
        }
        // std::cout << "getNumVar() = " << getNumVar() << std::endl;
        std::cout << "\nCompleted simplifications:" << std::endl;
        printConstraints(std::cout, E, q, false);
        {
            intptr_t lastAux = -1;
            for (size_t c = 0; c < E.numCol(); ++c) {
                intptr_t auxInd = auxiliaryInd(E.getCol(c));
                std::cout << "auxInd = " << auxInd << "; lastAux = " << lastAux
                          << std::endl;
                assert(auxInd >= lastAux);
                lastAux = auxInd;
            }
        }
    }
    */
    // eliminateVarForRCElim(&Adst, &bdst, E1, q1, Asrc, bsrc, E0, q0, i)
    // For `Asrc' * x <= bsrc` and `E0'* x = q0`, eliminates `i`
    // using Fourier–Motzkin elimination, storing the updated equations in
    // `Adst`, `bdst`, `E1`, and `q1`.
    size_t eliminateVarForRCElim(auto &Adst, llvm::SmallVectorImpl<T> &bdst,
                                 auto &E1, llvm::SmallVectorImpl<T> &q1,
                                 const auto &Asrc,
                                 const llvm::SmallVectorImpl<T> &bsrc, auto &E0,
                                 llvm::SmallVectorImpl<T> &q0,
                                 const size_t i) const {
        // eliminate variable `i` according to original order
        auto [numExclude, c, numNonZero] =
            eliminateVarForRCElimCore(Adst, bdst, E0, q0, Asrc, bsrc, i);
        Adst.resize(Asrc.numRow(), c);
        bdst.resize(c);
        auto [Re, Ce] = E0.size();
        // {
        //     intptr_t lastAux = -1;
        //     for (size_t c = 0; c < E0.numCol(); ++c) {
        //         intptr_t auxInd = auxiliaryInd(E0.getCol(c));
        //         assert(auxInd >= lastAux);
        //         lastAux = auxInd;
        //     }
        // }
        size_t numReserve =
            Ce - numNonZero + ((numNonZero * (numNonZero - 1)) >> 1);
        E1.resizeForOverwrite(Re, numReserve);
        q1.resize_for_overwrite(numReserve);
        const size_t numAuxVar = Asrc.numRow() - getNumVar();
        // auxInds are kept sorted
        // TODO: take advantage of the sorting to make iterating&checking more
        // efficient
        // std::cout << "Constraints prior to eliminateVarForRCElim" <<
        // std::endl; printConstraints(std::cout, E0, q0, false, numAuxVar);
        // const size_t numAuxVar = Asrc.numRow() - getNumVar();
        size_t k = 0;
        for (size_t u = 0; u < E0.numCol(); ++u) {
            auto Eu = E0.getCol(u);
            intptr_t Eiu = Eu[i];
            if (Eiu == 0) {
                for (size_t v = 0; v < Re; ++v) {
                    E1(v, k) = Eu(v);
                }
                q1[k] = q0[u];
                ++k;
                continue;
            }
            if (u == 0)
                continue;
            intptr_t auxInd = auxiliaryInd(Eu);
            bool independentOfInnerU = independentOfInner(Eu, i);
            for (size_t l = 0; l < u; ++l) {
                auto El = E0.getCol(l);
                intptr_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    auxMisMatch(auxInd, auxiliaryInd(El)))
                    continue;

                intptr_t g = std::gcd(Eiu, Eil);
                intptr_t Eiug = Eiu / g;
                intptr_t Eilg = Eil / g;
                for (size_t v = 0; v < Re; ++v) {
                    E1(v, k) = Eiug * E0(v, l) - Eilg * E0(v, u);
                }
                q1[k] = Eiug * q0[l];
                Polynomial::fnmadd(q1[k], q0[u], Eilg);
                // q1(k) = Eiug * q0(l) - Eilg * q0(u);
                ++k;
            }
        }
        E1.resize(Re, k);
        q1.resize(k);
        // std::cout << "Constraints after eliminateVarForRCElim" << std::endl;
        // printConstraints(std::cout, E1, q1, false, numAuxVar);
        // {
        //     intptr_t lastAux = -1;
        //     for (size_t c = 0; c < E1.numCol(); ++c) {
        //         intptr_t auxInd = auxiliaryInd(E1.getCol(c));
        //         assert(auxInd >= lastAux);
        //         lastAux = auxInd;
        //     }
        // }
        // simplifyAuxEqualityConstraints(E1, q1);
        NormalForm::simplifyEqualityConstraints(E1, q1);
        std::cout << "Eliminated variable i = " << i - numAuxVar << std::endl;
        printConstraints(
            printConstraints(std::cout, Adst, bdst, true, numAuxVar), E1, q1,
            false, numAuxVar);
        return numExclude;
    }
    // method for when we do not match `Ex=q` constraints with themselves
    // e.g., for removeRedundantConstraints
    size_t eliminateVarForRCElim(auto &Adst, llvm::SmallVectorImpl<T> &bdst,
                                 auto &E, llvm::SmallVectorImpl<T> &q,
                                 const auto &Asrc,
                                 const llvm::SmallVectorImpl<T> &bsrc,
                                 const size_t i) const {

        auto [numExclude, c, _] =
            eliminateVarForRCElimCore(Adst, bdst, E, q, Asrc, bsrc, i);
        for (size_t u = E.numCol(); u != 0;) {
            if (E(i, --u)) {
                E.eraseCol(u);
                q.erase(q.begin() + u);
            }
        }
        if (Adst.numCol() != c) {
            Adst.resize(Asrc.numRow(), c);
        }
        if (bdst.size() != c) {
            bdst.resize(c);
        }
        return numExclude;
    }
    std::tuple<size_t, size_t, size_t> eliminateVarForRCElimCore(
        auto &Adst, llvm::SmallVectorImpl<T> &bdst, auto &E,
        llvm::SmallVectorImpl<T> &q, const auto &Asrc,
        const llvm::SmallVectorImpl<T> &bsrc, const size_t i) const {
        // eliminate variable `i` according to original order
        const auto [numVar, numCol] = Asrc.size();
        assert(bsrc.size() == numCol);
        size_t numNeg = 0;
        size_t numPos = 0;
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = Asrc(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        const size_t numECol = E.numCol();
        size_t numNonZero = 0;
        for (size_t j = 0; j < numECol; ++j) {
            numNonZero += E(i, j) != 0;
        }
        const size_t numExclude = numCol - numNeg - numPos;
        const size_t numColA = numNeg * numPos + numExclude +
                               numNonZero * (numNeg + numPos) +
                               ((numNonZero * (numNonZero - 1)) >> 1);
        Adst.resizeForOverwrite(numVar, numColA);
        bdst.resize(numColA);
        assert(Adst.numCol() == bdst.size());
        // assign to `A = Aold[:,exlcuded]`
        for (size_t j = 0, c = 0; c < numExclude; ++j) {
            if (Asrc(i, j)) {
                continue;
            }
            for (size_t k = 0; k < numVar; ++k) {
                Adst(k, c) = Asrc(k, j);
            }
            bdst[c++] = bsrc[j];
        }
        size_t c = numExclude;
        std::cout << "Eliminating: " << i + getNumVar() - Asrc.numRow()
                  << "; Asrc.numCol() = " << numCol
                  << "; bsrc.size() = " << bsrc.size()
                  << "; E.numCol() = " << E.numCol() << std::endl;
        assert(numCol <= 500);
        // std::cout << "A = \n" << A << "\n bold =
        // TODO: drop independentOfInner?
        for (size_t u = 0; u < numCol; ++u) {
            auto Au = Asrc.getCol(u);
            intptr_t Aiu = Au[i];
            if (Aiu == 0)
                continue;
            intptr_t auxInd = auxiliaryInd(Au);
            bool independentOfInnerU = independentOfInner(Au, i);
            for (size_t l = 0; l < u; ++l) {
                auto Al = Asrc.getCol(l);
                if ((Al[i] == 0) || ((Al[i] > 0) == (Aiu > 0)) ||
                    (independentOfInnerU && independentOfInner(Al, i)) ||
                    (auxMisMatch(auxInd, auxiliaryInd(Al))))
                    continue;
                if (setBounds(Adst.getCol(c), bdst[c], Al, bsrc[l], Au, bsrc[u],
                              i)) {
                    if (uniqueConstraint(Adst, bdst, c)) {
                        ++c;
                    }
                }
            }
            for (size_t l = 0; l < numECol; ++l) {
                auto El = E.getCol(l);
                intptr_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    (auxMisMatch(auxInd, auxiliaryInd(El))))
                    continue;
                if ((Eil > 0) == (Aiu > 0)) {
                    // need to flip constraint in E
                    for (size_t v = 0; v < E.numRow(); ++v) {
                        negate(El[v]);
                    }
                    negate(q[l]);
                }
                if (setBounds(Adst.getCol(c), bdst[c], El, q[l], Au, bsrc[u],
                              i)) {
                    if (uniqueConstraint(Adst, bdst, c)) {
                        ++c;
                    }
                }
            }
        }
        return std::make_tuple(numExclude, c, numNonZero);
    }
    // returns std::make_pair(numNeg, numPos);
    // countNonZeroSign(Matrix A, i)
    // counts how many negative and positive elements there are in row `i`.
    // A row corresponds to a particular variable in `A'x <= b`.
    static std::pair<size_t, size_t> countNonZeroSign(const auto &A, size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        size_t numCol = A.size(1);
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = A(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    static void categorizeBounds(auto &lA, auto &uA,
                                 llvm::SmallVectorImpl<T> &lB,
                                 llvm::SmallVectorImpl<T> &uB, const auto &A,
                                 const llvm::SmallVectorImpl<T> &b, size_t i) {
        auto [numLoops, numCol] = A.size();
        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        lA.resize(numLoops, numNeg);
        lB.resize(numNeg);
        uA.resize(numLoops, numPos);
        uB.resize(numPos);
        // fill bounds
        for (size_t j = 0, l = 0, u = 0; j < numCol; ++j) {
            intptr_t Aij = A(i, j);
            if (Aij > 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    uA(k, u) = A(k, j);
                }
                uB[u++] = b[j];
            } else if (Aij < 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    lA(k, l) = A(k, j);
                }
                lB[l++] = b[j];
            }
        }
    }
    template <size_t CheckEmpty>
    bool appendBoundsSimple(const auto &lA, const auto &uA,
                            const llvm::SmallVectorImpl<T> &lB,
                            const llvm::SmallVectorImpl<T> &uB, auto &A,
                            llvm::SmallVectorImpl<T> &b, size_t i,
                            Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
#ifdef EXPENSIVEASSERTS
        for (auto &lb : lB) {
            if (auto c = lb.getCompileTimeConstant()) {
                if (c.getValue() == 0) {
                    assert(lb.terms.size() == 0);
                }
            }
        }
#endif
        auto [numLoops, numCol] = A.size();
        A.reserve(numLoops, numCol + numNeg * numPos);
        b.reserve(numCol + numNeg * numPos);

        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(numLoops, c + 1);
                b.resize(c + 1);
                bool sb = setBounds(A.getCol(c), b[c], lA.getCol(l), lB[l],
                                    uA.getCol(u), uB[u], i);
                if (!sb) {
                    if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                        return true;
                    }
                }
                if ((!sb) || (!uniqueConstraint(A, b, c))) {
                    A.resize(numLoops, c);
                    b.resize(c);
                }
            }
        }
        std::cout << "\n in AppendBounds, about to pruneBounds" << std::endl;
        printConstraints(std::cout, A, b, true, A.numRow() - getNumVar());
        return false;
    }

    template <size_t CheckEmpty>
    bool appendBounds(const auto &lA, const auto &uA,
                      const llvm::SmallVectorImpl<T> &lB,
                      const llvm::SmallVectorImpl<T> &uB, auto &Atmp0,
                      auto &Atmp1, auto &E, llvm::SmallVectorImpl<T> &btmp0,
                      llvm::SmallVectorImpl<T> &btmp1,
                      llvm::SmallVectorImpl<T> &q, auto &A,
                      llvm::SmallVectorImpl<T> &b, size_t i,
                      Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
#ifdef EXPENSIVEASSERTS
        for (auto &lb : lB) {
            if (auto c = lb.getCompileTimeConstant()) {
                if (c.getValue() == 0) {
                    assert(lb.terms.size() == 0);
                }
            }
        }
#endif
        auto [numLoops, numCol] = A.size();
        A.reserve(numLoops, numCol + numNeg * numPos);
        b.reserve(numCol + numNeg * numPos);

        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(numLoops, c + 1);
                b.resize(c + 1);
                bool sb = setBounds(A.getCol(c), b[c], lA.getCol(l), lB[l],
                                    uA.getCol(u), uB[u], i);
                if (!sb) {
                    if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                        return true;
                    }
                }
                if ((!sb) || (!uniqueConstraint(A, b, c))) {
                    A.resize(numLoops, c);
                    b.resize(c);
                }
            }
        }
        std::cout << "\n in AppendBounds, about to pruneBounds" << std::endl;
        printConstraints(std::cout, A, b, true, A.numRow() - getNumVar());
        printConstraints(std::cout, E, q, false, A.numRow() - getNumVar());
        if (A.numCol()) {
            std::cout << "CheckEmpty = " << CheckEmpty
                      << "; A.numCol() = " << A.numCol() << std::endl;
            pruneBounds(A, b, E, q);
        }
        /*if (removeRedundantConstraints(Atmp0, Atmp1, E, btmp0,
                                       btmp1, q, A, b, c)) {
            // removeRedundantConstraints returns `true` if we are
            // to drop the bounds `c`
            std::cout << "c = " << c
                      << " is a redundant constraint!" << std::endl;
            A.resize(numLoops, c);
            b.resize(c);
        } else {
            std::cout
                << "c = " << c
                << " may have removed constraints; now we have: "
                << A.numCol() << std::endl;
        }*/
        return false;
    }
    template <size_t CheckEmpty>
    bool appendBounds(
        const auto &lA, const auto &uA, const llvm::SmallVectorImpl<T> &lB,
        const llvm::SmallVectorImpl<T> &uB, auto &Atmp0, auto &Atmp1,
        auto &Etmp0, auto &Etmp1, llvm::SmallVectorImpl<T> &btmp0,
        llvm::SmallVectorImpl<T> &btmp1, llvm::SmallVectorImpl<T> &qtmp0,
        llvm::SmallVectorImpl<T> &qtmp1, auto &A, llvm::SmallVectorImpl<T> &b,
        auto &E, llvm::SmallVectorImpl<T> &q, size_t i,
        Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
#ifdef EXPENSIVEASSERTS
        for (auto &lb : lB) {
            if (auto c = lb.getCompileTimeConstant()) {
                if (c.getValue() == 0) {
                    assert(lb.terms.size() == 0);
                }
            }
        }
#endif
        auto [numLoops, numCol] = A.size();
        A.reserve(numLoops, numCol + numNeg * numPos);
        b.reserve(numCol + numNeg * numPos);
        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(numLoops, c + 1);
                b.resize(c + 1);
                if (setBounds(A.getCol(c), b[c], lA.getCol(l), lB[l],
                              uA.getCol(u), uB[u], i)) {
                    if (removeRedundantConstraints(
                            Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                            qtmp1, A, b, E, q, A.getCol(c), b[c], c)) {
                        // removeRedundantConstraints returns `true` if we are
                        // to drop the bounds `c`
                        std::cout << "c = " << c
                                  << " is a redundant constraint!" << std::endl;
                        A.resize(numLoops, c);
                        b.resize(c);
                    } else {
                        std::cout
                            << "c = " << c
                            << " may have removed constraints; now we have: "
                            << A.numCol() << std::endl;
                    }
                } else if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                    return true;
                } else {
                    A.resize(numLoops, c);
                    b.resize(c);
                }
            }
        }
        return false;
    }

    void pruneBounds() { pruneBounds(A, b); }
    void pruneBounds(auto &Atmp0, auto &Atmp1, auto &E,
                     llvm::SmallVectorImpl<T> &btmp0,
                     llvm::SmallVectorImpl<T> &btmp1,
                     llvm::SmallVectorImpl<T> &q, auto &Aold,
                     llvm::SmallVectorImpl<T> &bold) const {

        for (size_t i = 0; i + 1 < Aold.numCol(); ++i) {
            size_t c = Aold.numCol() - 1 - i;
            // std::cout << "i = " << i << "; Aold.numCol() = " << Aold.numCol()
            //           << "; c = " << c << std::endl;
            assert(Aold.numCol() == bold.size());
            if (removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                           Aold, bold, c)) {
                // drop `c`
                Aold.eraseCol(c);
                bold.erase(bold.begin() + c);
                std::cout << "Dropping column c = " << c << "." << std::endl;
            } else {
                std::cout << "Keeping column c = " << c << "." << std::endl;
            }
        }
    }
    void pruneBounds(auto &Aold, llvm::SmallVectorImpl<T> &bold) const {

        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        pruneBounds(Atmp0, Atmp1, E, btmp0, btmp1, q, Aold, bold);
    }
    // returns `false` if not violated, `true` if violated
    bool pruneBounds(auto &Aold, llvm::SmallVectorImpl<T> &bold, auto &Eold,
                     llvm::SmallVectorImpl<T> &qold) const {

        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, Etmp0, Etmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1, qtmp0, qtmp1;
        NormalForm::simplifyEqualityConstraints(Eold, qold);
        for (size_t i = 0; i < Eold.numCol(); ++i) {
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Eold.getCol(i), qold[i],
                                           Aold.numCol() + i)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                std::cout << "Oops!!!" << std::endl;
                return true;
            }
            // flip
            for (size_t v = 0; v < Eold.numRow(); ++v) {
                Eold(v, i) *= -1;
            }
            qold[i] *= -1;
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Eold.getCol(i), qold[i],
                                           Aold.numCol() + i)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                std::cout << "Oops!!!" << std::endl;

                return true;
            }
            std::cout << "E-Elim" << std::endl;
            printConstraints(std::cout, Aold, bold, true);
            printConstraints(std::cout, Eold, qold, false);
        }
        assert(Aold.numCol() == bold.size());
        for (size_t i = 0; Aold.numCol() > i + 1; ++i) {
            size_t c = Aold.numCol() - 1 - i;
            assert(Aold.numCol() == bold.size());
            std::cout << "Aold.numCol() = " << Aold.numCol()
                      << "; bold.size() = " << bold.size() << "; c = " << c
                      << std::endl;

            if (removeRedundantConstraints(
                    Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0, qtmp1,
                    Aold, bold, Eold, qold, Aold.getCol(c), bold[c], c)) {
                // drop `c`
                Aold.eraseCol(c);
                bold.erase(bold.begin() + c);
            }
            std::cout << "\nAold = " << std::endl;
            printConstraints(std::cout, Aold, bold, true);
        }
        return false;
    }
    bool removeRedundantConstraints(auto &Aold, llvm::SmallVectorImpl<T> &bold,
                                    const size_t c) const {
        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, c);
    }
    bool removeRedundantConstraints(auto &Atmp0, auto &Atmp1, auto &E,
                                    llvm::SmallVectorImpl<T> &btmp0,
                                    llvm::SmallVectorImpl<T> &btmp1,
                                    llvm::SmallVectorImpl<T> &q, auto &Aold,
                                    llvm::SmallVectorImpl<T> &bold,
                                    const size_t c) const {
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, Aold.getCol(c), bold[c],
                                          c);
    }
    intptr_t firstVarInd(llvm::ArrayRef<intptr_t> a) const {
        const size_t numAuxVar = a.size() - getNumVar();
        for (size_t i = numAuxVar; i < a.size(); ++i) {
            if (a[i])
                return i;
        }
        return -1;
    }
    intptr_t
    checkForTrivialRedundancies(llvm::SmallVector<unsigned, 32> &colsToErase,
                                llvm::SmallVector<unsigned, 16> &boundDiffs,
                                auto &Etmp, llvm::SmallVectorImpl<T> &qtmp,
                                auto &Aold, llvm::SmallVectorImpl<T> &bold,
                                const PtrVector<intptr_t, 0> &a,
                                const T &b) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numRow() - numVar;
        intptr_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            size_t c = boundDiffs[i];
            intptr_t dte = -1;
            for (size_t v = 0; v < numVar; ++v) {
                intptr_t Evi = a[v] - Aold(v, c);
                Etmp(v + numAuxVar, i) = Evi;
                dte = (Evi) ? v + numAuxVar : dte;
            }
            qtmp[i] = b - bold[c];
            std::cout << "dte = " << dte << std::endl;
            if (dte == -1) {
                T delta = bold[c] - b;
                if (knownLessEqualZero(delta)) {
                    // bold[c] - b <= 0
                    // bold[c] <= b
                    // thus, bound `c` will always trigger before `b`
                    return -2;
                } else if (knownGreaterEqualZero(delta)) {
                    // bound `b` triggers first
                    // normally we insert `c`, but if inserting here
                    // we also want to erase elements from boundDiffs
                    colsToErase.push_back(i);
                }
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j) {
                Etmp(j, i) = (j == i);
            }
        }
        if (colsToErase.size()) {
            for (auto it = colsToErase.rbegin(); it != colsToErase.rend();
                 ++it) {
                size_t i = *it;
                size_t c = boundDiffs[i];
                boundDiffs.erase(boundDiffs.begin() + i);
                *it = c; // redefine to `c`
                Etmp.eraseCol(i);
                qtmp.erase(qtmp.begin() + i);
            }
        }
        return dependencyToEliminate;
    }
    intptr_t
    checkForTrivialRedundancies(llvm::SmallVector<unsigned, 32> &colsToErase,
                                llvm::SmallVector<int, 16> &boundDiffs,
                                auto &Etmp, llvm::SmallVectorImpl<T> &qtmp,
                                auto &Aold, llvm::SmallVectorImpl<T> &bold,
                                auto &Eold, llvm::SmallVectorImpl<T> &qold,
                                const PtrVector<intptr_t, 0> &a,
                                const T &b) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numRow() - numVar;
        intptr_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            int c = boundDiffs[i];
            intptr_t dte = -1;
            T *bc;
            intptr_t sign = (c > 0) ? 1 : -1;
            if ((0 <= c) && (size_t(c) < Aold.numCol())) {
                for (size_t v = 0; v < numVar; ++v) {
                    intptr_t Evi = a[v] - Aold(v, c);
                    Etmp(v + numAuxVar, i) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(bold[c]);
            } else {
                size_t cc = std::abs(c) - A.numCol();
                for (size_t v = 0; v < numVar; ++v) {
                    intptr_t Evi = a[v] - sign * Eold(v, cc);
                    Etmp(v + numAuxVar, i) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(qold[cc]);
            }
            std::cout << "dte = " << dte << std::endl;
            if (dte == -1) {
                T delta = (*bc) * sign - b;
                if (knownLessEqualZero(delta)) {
                    // bold[c] - b <= 0
                    // bold[c] <= b
                    // thus, bound `c` will always trigger before `b`
                    return -2;
                } else if (knownGreaterEqualZero(delta)) {
                    // bound `b` triggers first
                    // normally we insert `c`, but if inserting here
                    // we also want to erase elements from boundDiffs
                    colsToErase.push_back(i);
                }
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j) {
                Etmp(j, i) = (j == i);
            }
            qtmp[i] = b;
            Polynomial::fnmadd(qtmp[i], *bc, sign);
            // qtmp[k] = b - (*bc)*sign;
        }
        if (colsToErase.size()) {
            for (auto it = colsToErase.rbegin(); it != colsToErase.rend();
                 ++it) {
                size_t i = *it;
                size_t c = std::abs(boundDiffs[i]);
                boundDiffs.erase(boundDiffs.begin() + i);
                *it = c; // redefine to `c`
                Etmp.eraseCol(i);
                qtmp.erase(qtmp.begin() + i);
            }
        }
        return dependencyToEliminate;
    }
    // returns `true` if `a` and `b` should be eliminated as redundant,
    // otherwise it eliminates all variables from `Atmp0` and `btmp0` that `a`
    // and `b` render redundant.
    bool removeRedundantConstraints(auto &Atmp0, auto &Atmp1, auto &E,
                                    llvm::SmallVectorImpl<T> &btmp0,
                                    llvm::SmallVectorImpl<T> &btmp1,
                                    llvm::SmallVectorImpl<T> &q, auto &Aold,
                                    llvm::SmallVectorImpl<T> &bold,
                                    const PtrVector<intptr_t, 0> &a, const T &b,
                                    const size_t C) const {

        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<unsigned, 16> boundDiffs;
        for (size_t c = 0; c < C; ++c) {
            for (size_t v = 0; v < numVar; ++v) {
                intptr_t av = a(v);
                intptr_t Avc = Aold(v, c);
                if (((av > 0) && (Avc > 0)) || ((av < 0) && (Avc < 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        const size_t numAuxVar = boundDiffs.size();
        if (numAuxVar == 0) {
            return false;
        }
        const size_t numVarAugment = numVar + numAuxVar;
        size_t AtmpCol = Aold.numCol() - (C < Aold.numCol());
        Atmp0.resizeForOverwrite(numVarAugment, AtmpCol);
        btmp0.resize_for_overwrite(AtmpCol);
        E.resizeForOverwrite(numVarAugment, numAuxVar);
        q.resize_for_overwrite(numAuxVar);
        for (size_t i = 0; i < Aold.numCol(); ++i) {
            if (i == C)
                continue;
            size_t j = i - (i > C);
            for (size_t v = 0; v < numAuxVar; ++v) {
                Atmp0(v, j) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Atmp0(v + numAuxVar, j) = Aold(v, i);
            }
            btmp0[j] = bold[i];
        }
        llvm::SmallVector<unsigned, 32> colsToErase;
        intptr_t dependencyToEliminate = checkForTrivialRedundancies(
            colsToErase, boundDiffs, E, q, Aold, bold, a, b);
        if (dependencyToEliminate == -2) {
            return true;
        }
        // define variables as
        // (a-Aold) + delta = b - bold
        // delta = b - bold - (a-Aold) = (b - a) - (bold - Aold)
        // if we prove delta >= 0, then (b - a) >= (bold - Aold)
        // and thus (b - a) is the redundant constraint, and we return `true`.
        // else if we prove delta <= 0, then (b - a) <= (bold - Aold)
        // and thus (bold - Aold) is the redundant constraint, and we eliminate
        // the associated column.
        assert(btmp0.size() == Atmp0.numCol());
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            assert(btmp0.size() == Atmp0.numCol());
            size_t numExcluded =
                eliminateVarForRCElim(Atmp1, btmp1, E, q, Atmp0, btmp0,
                                      size_t(dependencyToEliminate));
            assert(btmp1.size() == Atmp1.numCol());
            std::swap(Atmp0, Atmp1);
            std::swap(btmp0, btmp1);
            assert(btmp0.size() == Atmp0.numCol());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can drop
            for (size_t c = 0; c < Atmp0.numCol(); ++c) {
                PtrVector<intptr_t, 0> Ac = Atmp0.getCol(c);
                intptr_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    intptr_t auxInd = auxiliaryInd(Ac);
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        intptr_t Axc = Atmp0(auxInd, c);
                        // -Axc*delta <= b <= 0
                        // if (Axc > 0): (upper bound)
                        // delta <= b/Axc <= 0
                        // else if (Axc < 0): (lower bound)
                        // delta >= -b/Axc >= 0
                        if (Axc > 0) {
                            // upper bound
                            colsToErase.push_back(boundDiffs[auxInd]);
                        } else {
                            // lower bound
                            return true;
                        }
                    }
                } else {
                    dependencyToEliminate = varInd;
                }
            }
            if (dependencyToEliminate >= 0)
                continue;
            for (size_t c = 0; c < E.numCol(); ++c) {
                intptr_t varInd = firstVarInd(E.getCol(c));
                if (varInd != -1) {
                    dependencyToEliminate = varInd;
                    break;
                }
            }
            // if (dependencyToEliminate >= 0)
            //     continue;
            // for (size_t c = 0; c < numExcluded; ++c) {
            //     intptr_t varInd = firstVarInd(Atmp0.getCol(c));
            //     if (varInd != -1) {
            //         dependencyToEliminate = varInd;
            //         break;
            //     }
            // }
        }
        const size_t CHECK = C;
        if (C == CHECK) {
            std::cout << "### CHECKING ### C = " << CHECK
                      << " ###\nboundDiffs = [ ";
            for (auto &c : boundDiffs) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "colsToErase = [ ";
            for (auto &c : colsToErase) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "a = [ ";
            for (auto &c : a) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "b = " << b << std::endl;
            printConstraints(std::cout, Aold, bold, true);
        }
        if (colsToErase.size()) {
            size_t c = colsToErase.front();
            Aold.eraseCol(c);
            bold.erase(bold.begin() + c);
            // erasePossibleNonUniqueElements(Aold, bold, colsToErase);
        }
        return false;
    }
    bool removeRedundantConstraints(
        auto &Atmp0, auto &Atmp1, auto &Etmp0, auto &Etmp1,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &qtmp0, llvm::SmallVectorImpl<T> &qtmp1,
        auto &Aold, llvm::SmallVectorImpl<T> &bold, auto &Eold,
        llvm::SmallVectorImpl<T> &qold, const PtrVector<intptr_t, 0> &a,
        const T &b, const size_t C) const {

        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        std::cout << "a = [";
        for (auto &ai : a) {
            std::cout << ai << ", ";
        }
        std::cout << "]; b = " << b << std::endl;
        llvm::SmallVector<int, 16> boundDiffs;
        for (size_t c = 0; c < Aold.numCol(); ++c) {
            if (c == C)
                continue;
            for (size_t v = 0; v < numVar; ++v) {
                intptr_t av = a(v);
                intptr_t Avc = Aold(v, c);
                if (((av > 0) && (Avc > 0)) || ((av < 0) && (Avc < 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        for (intptr_t c = 0; c < intptr_t(Eold.numCol()); ++c) {
            int cc = c + Aold.numCol();
            if (cc == int(C))
                continue;
            unsigned mask = 3;
            for (size_t v = 0; v < numVar; ++v) {
                intptr_t av = a(v);
                intptr_t Evc = Eold(v, c);
                if ((av != 0) & (Evc != 0)) {
                    if (((av > 0) == (Evc > 0)) && (mask & 1)) {
                        boundDiffs.push_back(cc);
                        mask &= 2;
                    } else if (mask & 2) {
                        boundDiffs.push_back(-cc);
                        mask &= 1;
                    }
                    if (mask == 0) {
                        break;
                    }
                }
            }
        }
        const size_t numAuxVar = boundDiffs.size();
        const size_t numVarAugment = numVar + numAuxVar;
        bool CinA = C < Aold.numCol();
        size_t AtmpCol = Aold.numCol() - CinA;
        size_t EtmpCol = Eold.numCol() - (!CinA);
        Atmp0.resizeForOverwrite(numVarAugment, AtmpCol);
        btmp0.resize_for_overwrite(AtmpCol);
        Etmp0.reserve(numVarAugment, EtmpCol + numAuxVar);
        qtmp0.reserve(EtmpCol + numAuxVar);
        Etmp0.resizeForOverwrite(numVarAugment, numAuxVar);
        qtmp0.resize_for_overwrite(numAuxVar);
        // fill Atmp0 with Aold
        for (size_t i = 0; i < Aold.numCol(); ++i) {
            if (i == C)
                continue;
            size_t j = i - (i > C);
            for (size_t v = 0; v < numAuxVar; ++v) {
                Atmp0(v, j) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Atmp0(v + numAuxVar, j) = Aold(v, i);
            }
            btmp0[j] = bold[i];
        }
        llvm::SmallVector<unsigned, 32> colsToErase;
        intptr_t dependencyToEliminate =
            checkForTrivialRedundancies(colsToErase, boundDiffs, Etmp0, qtmp0,
                                        Aold, bold, Eold, qold, a, b);
        if (dependencyToEliminate == -2) {
            return true;
        }
        size_t numEtmpAuxVar = Etmp0.numCol();
        Etmp0.resize(numVarAugment, EtmpCol + numEtmpAuxVar);
        qtmp0.resize(EtmpCol + numEtmpAuxVar);
        // fill Etmp0 with Eold
        for (size_t i = 0; i < Eold.numCol(); ++i) {
            if (i + Aold.numCol() == C)
                continue;
            size_t j = i - ((i + Aold.numCol() > C) && (C >= Aold.numCol())) +
                       numEtmpAuxVar;
            for (size_t v = 0; v < numAuxVar; ++v) {
                Etmp0(v, j) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Etmp0(v + numAuxVar, j) = Eold(v, i);
            }
            qtmp0[j] = qold[i];
        }
        std::cout << "Removing Redundant Constraints ### C = " << C
                  << " ###\nboundDiffs = [ ";
        for (auto &c : boundDiffs) {
            std::cout << c << ", ";
        }
        std::cout << "]" << std::endl;
        printConstraints(
            printConstraints(std::cout, Atmp0, btmp0, true, numAuxVar), Etmp0,
            qtmp0, false, numAuxVar);

        // intptr_t dependencyToEliminate = -1;
        // fill Etmp0 with bound diffs
        // define variables as
        // (a-Aold) + delta = b - bold
        // delta = b - bold - (a-Aold) = (b - a) - (bold - Aold)
        // if we prove delta >= 0, then (b - a) >= (bold - Aold)
        // and thus (b - a) is the redundant constraint, and we return
        // `true`. else if we prove delta <= 0, then (b - a) <= (bold -
        // Aold) and thus (bold - Aold) is the redundant constraint, and we
        // eliminate the associated column. std::cout << "Entering
        // loop\nAtmp0 = \n" << Atmp0 << "\nbtmp0 = " << std::endl; for
        // (auto &bi : btmp0){
        //     std::cout << bi << ", ";
        // }
        // std::cout << "\nEtmp0 = \n" << Etmp0 << "\nqtmp0 = " <<
        // std::endl; for (auto &qi : qtmp0){
        //     std::cout << qi << ", ";
        // }
        // std::cout << std::endl;

        assert(btmp0.size() == Atmp0.numCol());
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            assert(btmp0.size() == Atmp0.numCol());
            // {
            //     intptr_t lastAux = -1;
            //     for (size_t c = 0; c < Etmp0.numCol(); ++c) {
            //         intptr_t auxInd = auxiliaryInd(Etmp0.getCol(c));
            //         assert(auxInd >= lastAux);
            //         lastAux = auxInd;
            //     }
            // }
            size_t numExcluded = eliminateVarForRCElim(
                Atmp1, btmp1, Etmp1, qtmp1, Atmp0, btmp0, Etmp0, qtmp0,
                size_t(dependencyToEliminate));
            assert(btmp1.size() == Atmp1.numCol());

            std::swap(Atmp0, Atmp1);
            std::swap(btmp0, btmp1);
            std::swap(Etmp0, Etmp1);
            std::swap(qtmp0, qtmp1);
            // {
            //     intptr_t lastAux = -1;
            //     for (size_t c = 0; c < Etmp0.numCol(); ++c) {
            //         intptr_t auxInd = auxiliaryInd(Etmp0.getCol(c));
            //         assert(auxInd >= lastAux);
            //         lastAux = auxInd;
            //     }
            // }
            assert(btmp0.size() == Atmp0.numCol());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can
            // drop
            for (size_t c = 0; c < Atmp0.numCol(); ++c) {
                PtrVector<intptr_t, 0> Ac = Atmp0.getCol(c);
                intptr_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    intptr_t auxInd = auxiliaryInd(Ac);
                    std::cout
                        << "auxInd = " << auxInd << "; knownLessEqualZero("
                        << btmp0[c] << ") = "
                        << (knownLessEqualZero(btmp0[c]) ? "true" : "false")
                        << std::endl;
                    // FIXME: does knownLessEqualZero(btmp0[c]) always
                    // return `true` when `allZero(bold)`???
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        intptr_t Axc = Atmp0(auxInd, c);
                        // Axc*delta <= b <= 0
                        // if (Axc > 0): (upper bound)
                        // delta <= b/Axc <= 0
                        // else if (Axc < 0): (lower bound)
                        // delta >= b/Axc >= 0
                        if (Axc > 0) {
                            // upper bound
                            colsToErase.push_back(std::abs(boundDiffs[auxInd]));
                        } else {
                            // lower bound
                            std::cout
                                << "Col to erase, C = " << C
                                << "; obsoleted by: " << boundDiffs[auxInd]
                                << std::endl;
                            return true;
                        }
                    }
                } else {
                    dependencyToEliminate = varInd;
                }
            }
            if (dependencyToEliminate >= 0)
                continue;
            for (size_t c = 0; c < Etmp0.numCol(); ++c) {
                intptr_t varInd = firstVarInd(Etmp0.getCol(c));
                if (varInd != -1) {
                    dependencyToEliminate = varInd;
                    break;
                }
            }
            // if (dependencyToEliminate >= 0)
            //     continue;
            // for (size_t c = 0; c < numExcluded; ++c) {
            //     intptr_t varInd = firstVarInd(Atmp0.getCol(c));
            //     if (varInd != -1) {
            //         dependencyToEliminate = varInd;
            //         break;
            //     }
            // }
        }
        const size_t CHECK = C;
        if (C == CHECK) {
            std::cout << "### CHECKING ### C = " << CHECK
                      << " ###\nboundDiffs = [ ";
            for (auto &c : boundDiffs) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "colsToErase = [ ";
            for (auto &c : colsToErase) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "a = [ ";
            for (auto &c : a) {
                std::cout << c << ", ";
            }
            std::cout << "]" << std::endl;
            std::cout << "b = " << b << std::endl;
            printConstraints(std::cout, Aold, bold, true);
            printConstraints(std::cout, Eold, qold, false);
        }
        if (colsToErase.size()) {
            size_t c = colsToErase.front();

            Aold.eraseCol(c);
            bold.erase(bold.begin() + c);
        }
        // erasePossibleNonUniqueElements(Aold, bold, colsToErase);
        return false;
    }

    void deleteBounds(auto &A, llvm::SmallVectorImpl<T> &b, size_t i) const {
        for (size_t j = b.size(); j != 0;) {
            --j;
            if (A(i, j)) {
                A.eraseCol(j);
                b.erase(b.begin() + j);
            }
        }
    }
    // A'x <= b
    // removes variable `i` from system
    void removeVariable(auto &A, llvm::SmallVectorImpl<T> &b, const size_t i) {

        Matrix<intptr_t, 0, 0, 0> lA;
        Matrix<intptr_t, 0, 0, 0> uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        removeVariable(lA, uA, lb, ub, A, b, i);
    }
    void removeVariable(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, auto &A,
                        llvm::SmallVectorImpl<T> &b, const size_t i) {

        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        removeVariable(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1, q, A, b,
                       i);
    }
    void removeVariable(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, auto &Atmp0, auto &Atmp1,
                        auto &E, llvm::SmallVectorImpl<T> &btmp0,
                        llvm::SmallVectorImpl<T> &btmp1,
                        llvm::SmallVectorImpl<T> &q, auto &A,
                        llvm::SmallVectorImpl<T> &b, const size_t i) {
        categorizeBounds(lA, uA, lb, ub, A, b, i);
        deleteBounds(A, b, i);
        appendBounds(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1, q, A, b, i,
                     Polynomial::Val<false>());
    }
    // A'x <= b
    // E'x = q
    // removes variable `i` from system
    void removeVariable(auto &A, llvm::SmallVectorImpl<T> &b, auto &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        Matrix<intptr_t, 0, 0, 0> lA;
        Matrix<intptr_t, 0, 0, 0> uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        removeVariable(lA, uA, lb, ub, A, b, E, q, i);
    }
    void removeVariable(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, auto &A,
                        llvm::SmallVectorImpl<T> &b, auto &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        std::cout << "Removing variable: " << i << std::endl;
        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, Etmp0, Etmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1, qtmp0, qtmp1;
        categorizeBounds(lA, uA, lb, ub, A, b, i);
        deleteBounds(A, b, i);
        appendBoundsSimple(lA, uA, lb, ub, A, b, i, Polynomial::Val<false>());
        // lA and uA updated
        // now, add E and q
        llvm::SmallVector<unsigned, 32> nonZero;
        size_t eCol = E.numCol();
        for (size_t j = 0; j < eCol; ++j) {
            if (E(i, j))
                nonZero.push_back(j);
        }
        const size_t numNonZero = nonZero.size();
        if (numNonZero == 0)
            return;
        size_t C = A.numCol();
        assert(E.numCol() == q.size());
        size_t reserveIneqCount = C + numNonZero * (lA.numCol() + uA.numCol());
        A.reserve(A.numRow(), reserveIneqCount);
        b.reserve(reserveIneqCount);
        size_t reserveEqCount = eCol + ((numNonZero * (numNonZero - 1)) >> 1);
        E.reserve(E.numRow(), reserveEqCount);
        q.reserve(reserveEqCount);
        // {
        //     intptr_t lastAux = -1;
        //     for (size_t c = 0; c < E.numCol(); ++c) {
        //         intptr_t auxInd = auxiliaryInd(E.getCol(c));
        //         assert(auxInd >= lastAux);
        //         lastAux = auxInd;
        //     }
        // }
        // assert(E.numCol() == q.size());
        auto ito = nonZero.end();
        auto itb = nonZero.begin();
        do {
            --ito;
            size_t n = *ito;
            PtrVector<intptr_t, 0> En = E.getCol(n);
            // compare E(:,n) with A
            intptr_t Ein = En(i);
            intptr_t sign = (Ein > 0) * 2 - 1;
            for (size_t k = 0; k < En.size(); ++k) {
                En[k] *= sign;
            }
            q[n] *= sign;
            for (size_t k = 0; k < lA.numCol(); ++k) {
                size_t c = b.size();
                A.resize(A.numRow(), c + 1);
                b.resize(c + 1);
                if (setBounds(A.getCol(c), b[c], lA.getCol(k), lb[k], En, q[n],
                              i) &&
                    uniqueConstraint(A, b, c)) {
                    /*if (removeRedundantConstraints(
                            Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                            qtmp1, A, b, E, q, A.getCol(c), b[c], c)) {
                        A.resize(A.numRow(), c);
                        b.resize(c);
                    } else {
                        std::cout << "Did not remove any redundant "
                                     "constraints. c = "
                                  << c << std::endl;
                        printConstraints(std::cout, A, b);
                    }*/
                } else {
                    A.resize(A.numRow(), c);
                    b.resize(c);
                }
            }
            sign *= -1;
            for (size_t k = 0; k < En.size(); ++k) {
                En[k] *= sign;
            }
            q[n] *= sign;
            for (size_t k = 0; k < uA.numCol(); ++k) {
                size_t c = b.size();
                A.resize(A.numRow(), c + 1);
                b.resize(c + 1);
                if (setBounds(A.getCol(c), b[c], En, q[n], uA.getCol(k), ub[k],
                              i) &&
                    uniqueConstraint(A, b, c)) {
                    /*if (removeRedundantConstraints(
                            Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                            qtmp1, A, b, E, q, A.getCol(c), b[c], c)) {
                        A.resize(A.numRow(), c);
                        b.resize(c);
                    }*/
                } else {
                    A.resize(A.numRow(), c);
                    b.resize(c);
                }
            }
            assert(E.numCol() == q.size());
            // here we essentially do Gaussian elimination on `E`
            if (ito != itb) {
                // compare E(:,*ito) with earlier (yet unvisited) columns
                auto iti = itb;
                assert(E.numCol() == q.size());
                while (true) {
                    size_t m = *iti;
                    PtrVector<intptr_t, 0> Em = E.getCol(m);
                    ++iti;
                    // it is for the current iteration
                    // if iti == ito, this is the last iteration
                    size_t d;
                    if (iti == ito) {
                        // on the last iteration, we overwrite
                        d = *ito;
                    } else {
                        // earlier iterations, we add a new column
                        d = eCol;
                        E.resize(E.numRow(), ++eCol);
                        q.resize(eCol);
                    }
                    std::cout << "d = " << d << "; *ito = " << *ito
                              << "; C = " << C << std::endl;
                    assert(d < E.numCol());
                    PtrVector<intptr_t, 0> Ed = E.getCol(d);
                    intptr_t g = std::gcd(En[i], Em[i]);
                    intptr_t Eng = En[i] / g;
                    intptr_t Emg = Em[i] / g;
                    for (size_t k = 0; k < E.numRow(); ++k) {
                        Ed[k] = Emg * En[k] - Eng * Em[k];
                    }
                    std::cout << "q.size() = " << q.size() << "; d = " << d
                              << "; n = " << n << "; E.size() = (" << E.numRow()
                              << ", " << E.numCol() << ")" << std::endl;
                    q[d] = Emg * q[n];
                    Polynomial::fnmadd(q[d], q[m], Eng);
                    // q[d] = Emg * q[n] - Eng * q[m];
                    if (iti == ito) {
                        break;
                    } else if (std::ranges::all_of(
                                   Ed, [](intptr_t ai) { return ai == 0; })) {
                        // we get rid of the column
                        E.resize(E.numRow(), --eCol);
                        q.resize(eCol);
                    }
                };
            } else {
                E.eraseCol(n);
                q.erase(q.begin() + n);
                break;
            }
        } while (ito != itb);
        // {
        //     intptr_t lastAux = -1;
        //     for (size_t c = 0; c < E.numCol(); ++c) {
        //         intptr_t auxInd = auxiliaryInd(E.getCol(c));
        //         assert(auxInd >= lastAux);
        //         lastAux = auxInd;
        //     }
        // }
        if (numNonZero > 1) {
            NormalForm::simplifyEqualityConstraints(E, q);
        }
        pruneBounds(A, b, E, q);
    }
    void removeVariable(const size_t i) { removeVariable(A, b, i); }
    static void erasePossibleNonUniqueElements(
        auto &A, llvm::SmallVectorImpl<T> &b,
        llvm::SmallVectorImpl<unsigned> &colsToErase) {
        std::ranges::sort(colsToErase);
        for (auto it = std::unique(colsToErase.begin(), colsToErase.end());
             it != colsToErase.begin();) {
            --it;
            A.eraseCol(*it);
            b.erase(b.begin() + (*it));
        }
    }
    void dropEmptyConstraints() {
        const size_t numConstraints = getNumConstraints();
        for (size_t c = numConstraints; c != 0;) {
            --c;
            if (allZero(A.getCol(c))) {
                A.eraseCol(c);
                b.erase(b.begin() + c);
            }
        }
    }
    // prints in current permutation order.
    // TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
    // in which case, we have to remove `currentToOriginalPerm`,
    // which menas either change printing, or move prints `<<` into
    // the derived classes.
    static std::ostream &printConstraints(std::ostream &os, const auto &A,
                                          const llvm::ArrayRef<T> b,
                                          bool inequality = true,
                                          size_t numAuxVar = 0) {
        const auto [numVar, numConstraints] = A.size();
        for (size_t c = 0; c < numConstraints; ++c) {
            bool hasPrinted = false;
            for (size_t v = 0; v < numVar; ++v) {
                if (intptr_t Avc = A(v, c)) {
                    if (hasPrinted) {
                        if (Avc > 0) {
                            os << " + ";
                        } else {
                            os << " - ";
                            Avc *= -1;
                        }
                    }
                    if (Avc != 1) {
                        if (Avc == -1) {
                            os << "-";
                        } else {
                            os << Avc;
                        }
                    }
                    if (v >= numAuxVar) {
                        os << "v_" << v - numAuxVar;
                    } else {
                        os << "d_" << v;
                    }
                    hasPrinted = true;
                }
            }
            if (inequality) {
                os << " <= ";
            } else {
                os << " == ";
            }
            os << b[c] << std::endl;
        }
        return os;
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const AbstractPolyhedra<P, T> &p) {
        return p.printConstraints(os, p.A, p.b);
    }
    void dump() const { std::cout << *this; }

    bool isEmpty() {
        // inefficient (compared to ILP + Farkas Lemma approach)
        std::cout << "calling isEmpty()" << std::endl;
        auto copy = *static_cast<const P *>(this);
        Matrix<intptr_t, 0, 0, 0> lA;
        Matrix<intptr_t, 0, 0, 0> uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        Matrix<intptr_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        size_t i = 0;
        while (true) {
            copy.categorizeBounds(lA, uA, lb, ub, copy.A, copy.b, i);
            copy.deleteBounds(copy.A, copy.b, i);
            if (copy.appendBounds(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1,
                                  q, copy.A, copy.b, i,
                                  Polynomial::Val<true>())) {
                return true;
            }
            if (i + 1 == getNumVar())
                break;
            copy.removeVariable(lA, uA, lb, ub, copy.A, copy.b, i++);
            // std::cout << "i = " << i << std::endl;
            // copy.dump();
            // std::cout << "\n" << std::endl;
        }
        return false;
    }
    bool knownSatisfied(llvm::ArrayRef<intptr_t> x) const {
        T bc;
        size_t numVar = std::min(x.size(), getNumVar());
        for (size_t c = 0; c < getNumConstraints(); ++c) {
            bc = b[c];
            for (size_t v = 0; v < numVar; ++v) {
                bc -= A(v, c) * x[v];
            }
            if (!knownGreaterEqualZero(bc)) {
                return false;
            }
        }
        return true;
    }
};

struct IntegerPolyhedra : public AbstractPolyhedra<IntegerPolyhedra, intptr_t> {
    bool knownLessEqualZeroImpl(intptr_t x) const { return x <= 0; }
    bool knownGreaterEqualZeroImpl(intptr_t x) const { return x >= 0; }
    IntegerPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                     llvm::SmallVector<intptr_t, 8> b)
        : AbstractPolyhedra<IntegerPolyhedra, intptr_t>(std::move(A),
                                                        std::move(b)){};
};
struct SymbolicPolyhedra : public AbstractPolyhedra<SymbolicPolyhedra, MPoly> {
    PartiallyOrderedSet poset;
    SymbolicPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                      llvm::SmallVector<MPoly, 8> b, PartiallyOrderedSet poset)
        : AbstractPolyhedra<SymbolicPolyhedra, MPoly>(std::move(A),
                                                      std::move(b)),
          poset(std::move(poset)){};

    bool knownLessEqualZeroImpl(MPoly x) const {
        return poset.knownLessEqualZero(std::move(x));
    }
    bool knownGreaterEqualZeroImpl(const MPoly &x) const {
        return poset.knownGreaterEqualZero(x);
    }
};

struct IntegerEqPolyhedra : public IntegerPolyhedra {
    Matrix<intptr_t, 0, 0, 0> E;
    llvm::SmallVector<intptr_t, 8> q;

    IntegerEqPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                       llvm::SmallVector<intptr_t, 8> b,
                       Matrix<intptr_t, 0, 0, 0> E,
                       llvm::SmallVector<intptr_t, 8> q)
        : IntegerPolyhedra(std::move(A), std::move(b)), E(std::move(E)),
          q(std::move(q)){};
    void pruneBounds() { IntegerPolyhedra::pruneBounds(A, b, E, q); }
    void removeVariable(const size_t i) {
        IntegerPolyhedra::removeVariable(A, b, E, q, i);
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    const IntegerEqPolyhedra &p) {
        return p.printConstraints(p.printConstraints(os, p.A, p.b, true), p.E,
                                  p.q, false);
    }
};
struct SymbolicEqPolyhedra : public SymbolicPolyhedra {
    Matrix<intptr_t, 0, 0, 0> E;
    llvm::SmallVector<MPoly, 8> q;

    SymbolicEqPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                        llvm::SmallVector<MPoly, 8> b,
                        Matrix<intptr_t, 0, 0, 0> E,
                        llvm::SmallVector<MPoly, 8> q,
                        PartiallyOrderedSet poset)
        : SymbolicPolyhedra(std::move(A), std::move(b), std::move(poset)),
          E(std::move(E)), q(std::move(q)){};
    void pruneBounds() { SymbolicPolyhedra::pruneBounds(A, b, E, q); }
    void removeVariable(const size_t i) {
        SymbolicPolyhedra::removeVariable(A, b, E, q, i);
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    const SymbolicEqPolyhedra &p) {
        return p.printConstraints(p.printConstraints(os, p.A, p.b, true), p.E,
                                  p.q, false);
    }
};
