#pragma once

#include "./Constraints.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./POSet.hpp"
#include "./Symbolics.hpp"
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

    Matrix<int64_t, 0, 0, 0> A;
    llvm::SmallVector<T, 8> b;

    // AbstractPolyhedra(const Matrix<int64_t, 0, 0, 0> A,
    //                   const llvm::SmallVector<P, 8> b)
    //     : A(std::move(A)), b(std::move(b)), lowerA(A.size(0)),
    //       upperA(A.size(0)), lowerb(A.size(0)), upperb(A.size(0)){};
    AbstractPolyhedra(const Matrix<int64_t, 0, 0, 0> A,
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
    static bool setBounds(llvm::MutableArrayRef<int64_t> a, T &b,
                          llvm::ArrayRef<int64_t> la, const T &lb,
                          llvm::ArrayRef<int64_t> ua, const T &ub, size_t i) {
        int64_t cu_base = ua[i];
        int64_t cl_base = la[i];
        if ((cu_base < 0) && (cl_base > 0))
            // if cu_base < 0, then it is an lower bound, so swap
            return setBounds(a, b, ua, ub, la, lb, i);
        int64_t g = std::gcd(cu_base, cl_base);
        int64_t cu = cu_base / g;
        int64_t cl = cl_base / g;
        b = cu * lb;
        Polynomial::fnmadd(b, ub, cl);
        size_t N = la.size();
        for (size_t n = 0; n < N; ++n) {
            a[n] = cu * la[n] - cl * ua[n];
        }
        g = 0;
        for (size_t n = 0; n < N; ++n) {
            int64_t an = a[n];
            if (std::abs(an) == 1) {
                return true;
            }
            if (g) {
                g = an ? std::gcd(g, an) : g;
            } else {
                g = an;
            }
        }
        g = g == 1 ? 1 : std::gcd(Polynomial::coefGCD(b), g);
        if (g > 1) {
            for (size_t n = 0; n < N; ++n) {
                a[n] /= g;
            }
            b /= g;
            return true;
        } else {
            return g != 0;
        }
    }

    static bool uniqueConstraint(PtrMatrix<const int64_t> A,
                                 llvm::ArrayRef<T> b, size_t C) {
        for (size_t c = 0; c < C; ++c) {
            if (b[c] == b[C]) {
                bool allEqual = true;
                for (size_t r = 0; r < A.numRow(); ++r) {
                    allEqual &= (A(r, c) == A(r, C));
                }
                if (allEqual)
                    return false;
            }
        }
        return true;
    }
    // independentOfInner(a, i)
    // checks if any `a[j] != 0`, such that `j != i`.
    // I.e., if this vector defines a hyper plane otherwise independent of `i`.
    static inline bool independentOfInner(llvm::ArrayRef<int64_t> a,
                                          size_t i) {
        for (size_t j = 0; j < a.size(); ++j) {
            if ((a[j] != 0) & (i != j)) {
                return false;
            }
        }
        return true;
    }
    // -1 indicates no auxiliary variable
    int64_t auxiliaryInd(llvm::ArrayRef<int64_t> a) const {
        const size_t numAuxVar = a.size() - getNumVar();
        for (size_t i = 0; i < numAuxVar; ++i) {
            if (a[i])
                return i;
        }
        return -1;
    }
    static inline bool auxMisMatch(int64_t x, int64_t y) {
        return ((x >= 0) && (y >= 0)) && (x != y);
    }
    // static size_t

    // eliminateVarForRCElim(&Adst, &bdst, E1, q1, Asrc, bsrc, E0, q0, i)
    // For `Asrc' * x <= bsrc` and `E0'* x = q0`, eliminates `i`
    // using Fourier–Motzkin elimination, storing the updated equations in
    // `Adst`, `bdst`, `E1`, and `q1`.
    bool eliminateVarForRCElim(IntMatrix auto &Adst,
                               llvm::SmallVectorImpl<T> &bdst,
                               IntMatrix auto &E1, llvm::SmallVectorImpl<T> &q1,
                               IntMatrix auto &Asrc,
                               llvm::SmallVectorImpl<T> &bsrc,
                               IntMatrix auto &E0, llvm::SmallVectorImpl<T> &q0,
                               const size_t i) const {
        if (!substituteEquality(Asrc, bsrc, E0, q0, i)) {
            const size_t numAuxVar = Asrc.numRow() - getNumVar();
            size_t c = Asrc.numCol();
            while (c-- > 0) {
                size_t s = 0;
                for (size_t j = 0; j < numAuxVar; ++j) {
                    s += (Asrc(j, c) != 0);
                }
                if (s > 1) {
                    eraseConstraint(Asrc, bsrc, c);
                }
            }
            if (E0.numCol() > 1) {
                NormalForm::simplifyEqualityConstraints(E0, q0);
            }
            return false;
        }
        // eliminate variable `i` according to original order
        auto [numExclude, c, numNonZero] =
            eliminateVarForRCElimCore(Adst, bdst, E0, q0, Asrc, bsrc, i);
        Adst.resize(Asrc.numRow(), c);
        bdst.resize(c);
        auto [Re, Ce] = E0.size();
        size_t numReserve =
            Ce - numNonZero + ((numNonZero * (numNonZero - 1)) >> 1);
        E1.resizeForOverwrite(Re, numReserve);
        q1.resize_for_overwrite(numReserve);
        // auxInds are kept sorted
        // TODO: take advantage of the sorting to make iterating&checking more
        // efficient
        size_t k = 0;
        for (size_t u = 0; u < E0.numCol(); ++u) {
            auto Eu = E0.getCol(u);
            int64_t Eiu = Eu[i];
            if (Eiu == 0) {
                for (size_t v = 0; v < Re; ++v) {
                    E1(v, k) = Eu[v];
                }
                q1[k] = q0[u];
                ++k;
                continue;
            }
            if (u == 0)
                continue;
            int64_t auxInd = auxiliaryInd(Eu);
            bool independentOfInnerU = independentOfInner(Eu, i);
            for (size_t l = 0; l < u; ++l) {
                auto El = E0.getCol(l);
                int64_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    auxMisMatch(auxInd, auxiliaryInd(El)))
                    continue;

                int64_t g = std::gcd(Eiu, Eil);
                int64_t Eiug = Eiu / g;
                int64_t Eilg = Eil / g;
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
        NormalForm::simplifyEqualityConstraints(E1, q1);
        return true;
    }
    // method for when we do not match `Ex=q` constraints with themselves
    // e.g., for removeRedundantConstraints
    void eliminateVarForRCElim(IntMatrix auto &Adst,
                               llvm::SmallVectorImpl<T> &bdst,
                               IntMatrix auto &E, llvm::SmallVectorImpl<T> &q,
                               PtrMatrix<int64_t> Asrc, llvm::ArrayRef<T> bsrc,
                               const size_t i) const {

        auto [numExclude, c, _] =
            eliminateVarForRCElimCore(Adst, bdst, E, q, Asrc, bsrc, i);
        for (size_t u = E.numCol(); u != 0;) {
            if (E(i, --u)) {
                eraseConstraint(E, q, u);
            }
        }
        if (Adst.numCol() != c) {
            Adst.resize(Asrc.numRow(), c);
        }
        if (bdst.size() != c) {
            bdst.resize(c);
        }
    }
    std::tuple<size_t, size_t, size_t> eliminateVarForRCElimCore(
        IntMatrix auto &Adst, llvm::SmallVectorImpl<T> &bdst, IntMatrix auto &E,
        llvm::SmallVectorImpl<T> &q, PtrMatrix<int64_t> Asrc,
        llvm::ArrayRef<T> bsrc, const size_t i) const {
        // eliminate variable `i` according to original order
        const auto [numVar, numCol] = Asrc.size();
        assert(bsrc.size() == numCol);
        size_t numNeg = 0;
        size_t numPos = 0;
        for (size_t j = 0; j < numCol; ++j) {
            int64_t Aij = Asrc(i, j);
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
        assert(numCol <= 500);
        // TODO: drop independentOfInner?
        for (size_t u = 0; u < numCol; ++u) {
            auto Au = Asrc.getCol(u);
            int64_t Aiu = Au[i];
            if (Aiu == 0)
                continue;
            int64_t auxInd = auxiliaryInd(Au);
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
                int64_t Eil = El[i];
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
    static std::pair<size_t, size_t> countNonZeroSign(PtrMatrix<const int64_t> A,
                                                      size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        size_t numCol = A.size(1);
        for (size_t j = 0; j < numCol; ++j) {
            int64_t Aij = A(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    static void categorizeBounds(auto &lA, auto &uA,
                                 llvm::SmallVectorImpl<T> &lB,
                                 llvm::SmallVectorImpl<T> &uB,
                                 PtrMatrix<const int64_t> A, llvm::ArrayRef<T> b,
                                 size_t i) {
        auto [numLoops, numCol] = A.size();
        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        lA.resize(numLoops, numNeg);
        lB.resize(numNeg);
        uA.resize(numLoops, numPos);
        uB.resize(numPos);
        // fill bounds
        for (size_t j = 0, l = 0, u = 0; j < numCol; ++j) {
            int64_t Aij = A(i, j);
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
                            const llvm::SmallVectorImpl<T> &uB,
                            IntMatrix auto &A, llvm::SmallVectorImpl<T> &b,
                            size_t i, Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
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
        return false;
    }

    template <size_t CheckEmpty>
    bool appendBounds(const auto &lA, const auto &uA,
                      const llvm::SmallVectorImpl<T> &lB,
                      const llvm::SmallVectorImpl<T> &uB, auto &Atmp0,
                      auto &Atmp1, auto &Etmp0, auto &Etmp1,
                      llvm::SmallVectorImpl<T> &btmp0,
                      llvm::SmallVectorImpl<T> &btmp1,
                      llvm::SmallVectorImpl<T> &qtmp0,
                      llvm::SmallVectorImpl<T> &qtmp1, IntMatrix auto &A,
                      llvm::SmallVectorImpl<T> &b, IntMatrix auto &E,
                      llvm::SmallVectorImpl<T> &q, size_t i,
                      Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
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
        if (A.numCol()) {
            if (pruneBounds(Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                            qtmp1, A, b, E, q)) {
                return CheckEmpty;
            }
        }
        return false;
    }
    template <size_t CheckEmpty>
    bool appendBounds(const auto &lA, const auto &uA,
                      const llvm::SmallVectorImpl<T> &lB,
                      const llvm::SmallVectorImpl<T> &uB, auto &Atmp0,
                      auto &Atmp1, auto &Etmp, llvm::SmallVectorImpl<T> &btmp0,
                      llvm::SmallVectorImpl<T> &btmp1,
                      llvm::SmallVectorImpl<T> &qtmp, auto &A,
                      llvm::SmallVectorImpl<T> &b, size_t i,
                      Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
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
        if (A.numCol()) {
            pruneBounds(Atmp0, Atmp1, Etmp, btmp0, btmp1, qtmp, A, b);
        }
        return false;
    }
    void pruneBounds() { pruneBounds(A, b); }
    void pruneBounds(IntMatrix auto &Atmp0, IntMatrix auto &Atmp1,
                     IntMatrix auto &E, llvm::SmallVectorImpl<T> &btmp0,
                     llvm::SmallVectorImpl<T> &btmp1,
                     llvm::SmallVectorImpl<T> &q, IntMatrix auto &Aold,
                     llvm::SmallVectorImpl<T> &bold) const {

        for (size_t i = 0; i + 1 <= Aold.numCol(); ++i) {
            size_t c = Aold.numCol() - 1 - i;
            assert(Aold.numCol() == bold.size());
            if (removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                           Aold, bold, c)) {
                // drop `c`
                eraseConstraint(Aold, bold, c);
            }
        }
    }
    void pruneBounds(auto &Aold, llvm::SmallVectorImpl<T> &bold) const {

        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        pruneBounds(Atmp0, Atmp1, E, btmp0, btmp1, q, Aold, bold);
    }
    static void moveEqualities(auto &Aold, llvm::SmallVectorImpl<T> &bold,
                               auto &Eold, llvm::SmallVectorImpl<T> &qold) {

        if (Aold.numCol() > 1) {
            for (size_t o = Aold.numCol() - 1; o > 0;) {
                --o;
                for (size_t i = o + 1; i < Aold.numCol(); ++i) {
                    bool isNeg = true;
                    for (size_t v = 0; v < Aold.numRow(); ++v) {
                        if (Aold(v, i) != -Aold(v, o)) {
                            isNeg = false;
                            break;
                        }
                    }
                    if (isNeg && (bold[i] == -bold[o])) {
                        qold.push_back(bold[i]);
                        size_t e = Eold.numCol();
                        Eold.resize(Eold.numRow(), qold.size());
                        for (size_t v = 0; v < Eold.numRow(); ++v) {
                            Eold(v, e) = Aold(v, i);
                        }
                        eraseConstraint(Aold, bold, i, o);
                        break;
                    }
                }
            }
        }
    }
    // returns `false` if not violated, `true` if violated
    bool pruneBounds(auto &Aold, llvm::SmallVectorImpl<T> &bold, auto &Eold,
                     llvm::SmallVectorImpl<T> &qold) const {

        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1, Etmp0, Etmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1, qtmp0, qtmp1;
        return pruneBounds(Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                           qtmp1, Aold, bold, Eold, qold);
    }
    bool pruneBounds(IntMatrix auto &Atmp0, IntMatrix auto &Atmp1,
                     IntMatrix auto &Etmp0, IntMatrix auto &Etmp1,
                     llvm::SmallVectorImpl<T> &btmp0,
                     llvm::SmallVectorImpl<T> &btmp1,
                     llvm::SmallVectorImpl<T> &qtmp0,
                     llvm::SmallVectorImpl<T> &qtmp1, auto &Aold,
                     llvm::SmallVectorImpl<T> &bold, auto &Eold,
                     llvm::SmallVectorImpl<T> &qold) const {
        moveEqualities(Aold, bold, Eold, qold);
        NormalForm::simplifyEqualityConstraints(Eold, qold);
        for (size_t i = 0; i < Eold.numCol(); ++i) {
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Eold.getCol(i), qold[i],
                                           Aold.numCol() + i, true)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
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
                                           Aold.numCol() + i, true)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                return true;
            }
        }
        assert(Aold.numCol() == bold.size());
        for (size_t i = 0; i + 1 <= Aold.numCol(); ++i) {
            size_t c = Aold.numCol() - 1 - i;
            assert(Aold.numCol() == bold.size());
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Aold.getCol(c), bold[c],
                                           c, false)) {
                // drop `c`
                eraseConstraint(Aold, bold, c);
            }
        }
        return false;
    }
    bool removeRedundantConstraints(IntMatrix auto &Aold,
                                    llvm::SmallVectorImpl<T> &bold,
                                    const size_t c) const {
        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, c);
    }
    bool removeRedundantConstraints(
        IntMatrix auto &Atmp0, IntMatrix auto &Atmp1, IntMatrix auto &E,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &q, IntMatrix auto &Aold,
        llvm::SmallVectorImpl<T> &bold, const size_t c) const {
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, Aold.getCol(c), bold[c],
                                          c);
    }
    int64_t firstVarInd(llvm::ArrayRef<int64_t> a) const {
        const size_t numAuxVar = a.size() - getNumVar();
        for (size_t i = numAuxVar; i < a.size(); ++i) {
            if (a[i])
                return i;
        }
        return -1;
    }
    int64_t
    checkForTrivialRedundancies(llvm::SmallVector<unsigned, 32> &colsToErase,
                                llvm::SmallVector<unsigned, 16> &boundDiffs,
                                auto &Etmp, llvm::SmallVectorImpl<T> &qtmp,
                                auto &Aold, llvm::SmallVectorImpl<T> &bold,
                                llvm::ArrayRef<int64_t> a, const T &b) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numRow() - numVar;
        int64_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            size_t c = boundDiffs[i];
            int64_t dte = -1;
            for (size_t v = 0; v < numVar; ++v) {
                int64_t Evi = a[v] - Aold(v, c);
                Etmp(v + numAuxVar, i) = Evi;
                dte = (Evi) ? v + numAuxVar : dte;
            }
            qtmp[i] = b - bold[c];
            // std::cout << "dte = " << dte << std::endl;
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
                eraseConstraint(Aold, bold, c);
                eraseConstraint(Etmp, qtmp, i);
            }
            colsToErase.clear();
        }
        return dependencyToEliminate;
    }
    int64_t
    checkForTrivialRedundancies(llvm::SmallVector<unsigned, 32> &colsToErase,
                                llvm::SmallVector<int, 16> &boundDiffs,
                                auto &Etmp, llvm::SmallVectorImpl<T> &qtmp,
                                auto &Aold, llvm::SmallVectorImpl<T> &bold,
                                auto &Eold, llvm::SmallVectorImpl<T> &qold,
                                llvm::ArrayRef<int64_t> a, const T &b,
                                bool AbIsEq) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numRow() - numVar;
        int64_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            int c = boundDiffs[i];
            int64_t dte = -1;
            T *bc;
            int64_t sign = (c > 0) ? 1 : -1;
            if ((0 <= c) && (size_t(c) < Aold.numCol())) {
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t Evi = a[v] - Aold(v, c);
                    Etmp(v + numAuxVar, i) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(bold[c]);
            } else {
                size_t cc = std::abs(c) - A.numCol();
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t Evi = a[v] - sign * Eold(v, cc);
                    Etmp(v + numAuxVar, i) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(qold[cc]);
            }
            // std::cout << "dte = " << dte << std::endl;
            if (dte == -1) {
                T delta = (*bc) * sign - b;
                if (AbIsEq ? knownLessEqualZero(delta - 1)
                           : knownLessEqualZero(delta)) {
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
                if ((0 <= c) && (size_t(c) < Aold.numCol())) {
                    eraseConstraint(Aold, bold, c);
                }
                eraseConstraint(Etmp, qtmp, i);
            }
            colsToErase.clear();
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
                                    llvm::ArrayRef<int64_t> a, const T &b,
                                    const size_t C) const {

        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<unsigned, 16> boundDiffs;
        for (size_t c = 0; c < C; ++c) {
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v];
                int64_t Avc = Aold(v, c);
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
        int64_t dependencyToEliminate = checkForTrivialRedundancies(
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
            eliminateVarForRCElim(Atmp1, btmp1, E, q, Atmp0, btmp0,
                                  size_t(dependencyToEliminate));
            assert(btmp1.size() == Atmp1.numCol());
            std::swap(Atmp0, Atmp1);
            std::swap(btmp0, btmp1);
            assert(btmp0.size() == Atmp0.numCol());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can drop
            for (size_t c = 0; c < Atmp0.numCol(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getCol(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        int64_t Axc = Atmp0(auxInd, c);
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
                int64_t varInd = firstVarInd(E.getCol(c));
                if (varInd != -1) {
                    dependencyToEliminate = varInd;
                    break;
                }
            }
        }
        if (colsToErase.size()) {
            size_t c = colsToErase.front();
            eraseConstraint(Aold, bold, c);
        }
        return false;
    }
    bool removeRedundantConstraints(
        auto &Atmp0, auto &Atmp1, auto &Etmp0, auto &Etmp1,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &qtmp0, llvm::SmallVectorImpl<T> &qtmp1,
        auto &Aold, llvm::SmallVectorImpl<T> &bold, auto &Eold,
        llvm::SmallVectorImpl<T> &qold, llvm::ArrayRef<int64_t> a, const T &b,
        const size_t C, const bool AbIsEq) const {

        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<int, 16> boundDiffs;
        for (size_t c = 0; c < Aold.numCol(); ++c) {
            if (c == C)
                continue;
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v];
                int64_t Avc = Aold(v, c);
                if (((av > 0) && (Avc > 0)) || ((av < 0) && (Avc < 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        if (!AbIsEq) {
            // if AbIsEq, would be eliminated via GaussianElimination
            for (int64_t c = 0; c < int64_t(Eold.numCol()); ++c) {
                int cc = c + Aold.numCol();
                if (cc == int(C))
                    continue;
                unsigned mask = 3;
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t av = a[v];
                    int64_t Evc = Eold(v, c);
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
        int64_t dependencyToEliminate =
            checkForTrivialRedundancies(colsToErase, boundDiffs, Etmp0, qtmp0,
                                        Aold, bold, Eold, qold, a, b, AbIsEq);
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
        // fill Etmp0 with bound diffs
        // define variables as
        // (a-Aold) + delta = b - bold
        // delta = b - bold - (a-Aold) = (b - a) - (bold - Aold)
        // if we prove delta >= 0, then (b - a) >= (bold - Aold)
        // and thus (b - a) is the redundant constraint, and we return
        // `true`. else if we prove delta <= 0, then (b - a) <= (bold -
        // Aold) and thus (bold - Aold) is the redundant constraint, and we
        // eliminate the associated column.
        assert(btmp0.size() == Atmp0.numCol());
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            assert(btmp0.size() == Atmp0.numCol());
            if (eliminateVarForRCElim(Atmp1, btmp1, Etmp1, qtmp1, Atmp0, btmp0,
                                      Etmp0, qtmp0,
                                      size_t(dependencyToEliminate))) {
                std::swap(Atmp0, Atmp1);
                std::swap(btmp0, btmp1);
                std::swap(Etmp0, Etmp1);
                std::swap(qtmp0, qtmp1);
            }
            assert(btmp1.size() == Atmp1.numCol());
            // {
            //     int64_t lastAux = -1;
            //     for (size_t c = 0; c < Etmp0.numCol(); ++c) {
            //         int64_t auxInd = auxiliaryInd(Etmp0.getCol(c));
            //         assert(auxInd >= lastAux);
            //         lastAux = auxInd;
            //     }
            // }
            assert(btmp0.size() == Atmp0.numCol());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can
            // drop
            for (size_t c = 0; c < Atmp0.numCol(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getCol(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    // FIXME: does knownLessEqualZero(btmp0[c]) always
                    // return `true` when `allZero(bold)`???
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        int64_t Axc = Atmp0(auxInd, c);
                        // Axc*delta <= b <= 0
                        // if (Axc > 0): (upper bound)
                        // delta <= b/Axc <= 0
                        // else if (Axc < 0): (lower bound)
                        // delta >= b/Axc >= 0
                        if (Axc > 0) {
                            // upper bound
                            colsToErase.push_back(std::abs(boundDiffs[auxInd]));
                        } else if ((!AbIsEq) ||
                                   knownLessEqualZero(btmp0[c] - 1)) {
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
            for (size_t c = 0; c < Etmp0.numCol(); ++c) {
                int64_t varInd = firstVarInd(Etmp0.getCol(c));
                if (varInd != -1) {
                    dependencyToEliminate = varInd;
                    break;
                }
            }
        }
        if (colsToErase.size()) {
            size_t c = colsToErase.front();
            eraseConstraint(Aold, bold, c);
        }
        return false;
    }

    void deleteBounds(auto &A, llvm::SmallVectorImpl<T> &b, size_t i) const {
        for (size_t j = b.size(); j != 0;) {
            if (A(i, --j)) {
                eraseConstraint(A, b, j);
            }
        }
    }
    // A'x <= b
    // removes variable `i` from system
    void removeVariable(auto &A, llvm::SmallVectorImpl<T> &b, const size_t i) {

        Matrix<int64_t, 0, 0, 0> lA;
        Matrix<int64_t, 0, 0, 0> uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        removeVariable(lA, uA, lb, ub, A, b, i);
    }
    void removeVariable(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, auto &A,
                        llvm::SmallVectorImpl<T> &b, const size_t i) {

        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1, E;
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
    bool removeVariable(auto &A, llvm::SmallVectorImpl<T> &b, auto &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        if (substituteEquality(A, b, E, q, i)) {
            Matrix<int64_t, 0, 0, 0> lA;
            Matrix<int64_t, 0, 0, 0> uA;
            llvm::SmallVector<T, 8> lb;
            llvm::SmallVector<T, 8> ub;
            removeVariableCore(lA, uA, lb, ub, A, b, i);
        }
        if (E.numCol() > 1) {
            NormalForm::simplifyEqualityConstraints(E, q);
        }
        return pruneBounds(A, b, E, q);
    }
    bool removeVariable(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, auto &A,
                        llvm::SmallVectorImpl<T> &b, auto &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        if (substituteEquality(A, b, E, q, i)) {
            removeVariableCore(lA, uA, lb, ub, A, b, i);
        }
        if (E.numCol() > 1) {
            NormalForm::simplifyEqualityConstraints(E, q);
        }
        return pruneBounds(A, b, E, q);
    }

    void removeVariableCore(auto &lA, auto &uA, llvm::SmallVectorImpl<T> &lb,
                            llvm::SmallVectorImpl<T> &ub, auto &A,
                            llvm::SmallVectorImpl<T> &b, const size_t i) {

        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1;

        categorizeBounds(lA, uA, lb, ub, A, b, i);
        deleteBounds(A, b, i);
        appendBoundsSimple(lA, uA, lb, ub, A, b, i, Polynomial::Val<false>());
    }
    void removeVariable(const size_t i) { removeVariable(A, b, i); }
    static void erasePossibleNonUniqueElements(
        auto &A, llvm::SmallVectorImpl<T> &b,
        llvm::SmallVectorImpl<unsigned> &colsToErase) {
        std::ranges::sort(colsToErase);
        for (auto it = std::unique(colsToErase.begin(), colsToErase.end());
             it != colsToErase.begin();) {
            eraseConstraint(A, b, *(--it));
        }
    }
    void dropEmptyConstraints() {
        const size_t numConstraints = getNumConstraints();
        for (size_t c = numConstraints; c != 0;) {
            if (allZero(A.getCol(--c))) {
                eraseConstraint(A, b, c);
            }
        }
    }
    
    friend std::ostream &operator<<(std::ostream &os,
                                    const AbstractPolyhedra<P, T> &p) {
        return printConstraints(os, p.A, p.b);
    }
    void dump() const { std::cout << *this; }

    bool isEmpty() {
        // inefficient (compared to ILP + Farkas Lemma approach)
        //#ifndef NDEBUG
        //        std::cout << "calling isEmpty()" << std::endl;
        //#endif
        auto copy = *static_cast<const P *>(this);
        Matrix<int64_t, 0, 0, 0> lA;
        Matrix<int64_t, 0, 0, 0> uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        Matrix<int64_t, 0, 0, 128> Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        size_t i = getNumVar();
        while (i--) {
            copy.categorizeBounds(lA, uA, lb, ub, copy.A, copy.b, i);
            copy.deleteBounds(copy.A, copy.b, i);
            if (copy.appendBounds(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1,
                                  q, copy.A, copy.b, i,
                                  Polynomial::Val<true>())) {
                return true;
            }
        }
        return false;
    }
    bool knownSatisfied(llvm::ArrayRef<int64_t> x) const {
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

struct IntegerPolyhedra : public AbstractPolyhedra<IntegerPolyhedra, int64_t> {
    bool knownLessEqualZeroImpl(int64_t x) const { return x <= 0; }
    bool knownGreaterEqualZeroImpl(int64_t x) const { return x >= 0; }
    IntegerPolyhedra(Matrix<int64_t, 0, 0, 0> A,
                     llvm::SmallVector<int64_t, 8> b)
        : AbstractPolyhedra<IntegerPolyhedra, int64_t>(std::move(A),
                                                        std::move(b)){};
};
struct SymbolicPolyhedra : public AbstractPolyhedra<SymbolicPolyhedra, MPoly> {
    PartiallyOrderedSet poset;
    SymbolicPolyhedra(Matrix<int64_t, 0, 0, 0> A,
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

