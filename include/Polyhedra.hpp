#pragma once

#include "./Constraints.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./POSet.hpp"
#include "./Symbolics.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

// the AbstractPolyhedra defines methods we reuse across Polyhedra with known
// (`Int`) bounds, as well as with unknown (symbolic) bounds.
// In either case, we assume the matrix `A` consists of known integers.
template <class P, typename T> struct AbstractPolyhedra {

    IntMatrix A;
    llvm::SmallVector<T, 8> b;

    AbstractPolyhedra(const IntMatrix A, const llvm::SmallVector<T, 8> b)
        : A(std::move(A)), b(std::move(b)){};
    AbstractPolyhedra(size_t numIneq, size_t numVar)
        : A(numIneq, numVar), b(numIneq){};

    size_t getNumVar() const { return A.numCol(); }
    size_t getNumInequalityConstraints() const { return A.numRow(); }

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
        int64_t g = gcd(cu_base, cl_base);
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
                g = an ? gcd(g, an) : g;
            } else {
                g = an;
            }
        }
        g = g == 1 ? 1 : gcd(Polynomial::coefGCD(b), g);
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
                for (size_t r = 0; r < A.numCol(); ++r) {
                    allEqual &= (A(c, r) == A(C, r));
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
    static inline bool independentOfInner(llvm::ArrayRef<int64_t> a, size_t i) {
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
    bool eliminateVarForRCElim(IntMatrix &Adst, llvm::SmallVectorImpl<T> &bdst,
                               IntMatrix &E1, llvm::SmallVectorImpl<T> &q1,
                               IntMatrix &Asrc, llvm::SmallVectorImpl<T> &bsrc,
                               IntMatrix &E0, llvm::SmallVectorImpl<T> &q0,
                               const size_t i) const {
        std::cout << "Asrc0 =\n" << Asrc << std::endl;
        if (!substituteEquality(Asrc, bsrc, E0, q0, i)) {
            std::cout << "Asrc1 =\n" << Asrc << std::endl;
            const size_t numAuxVar = Asrc.numCol() - getNumVar();
            size_t c = Asrc.numRow();
            while (c-- > 0) {
                size_t s = 0;
                for (size_t j = 0; j < numAuxVar; ++j) {
                    s += (Asrc(c, j) != 0);
                }
                if (s > 1) {
                    eraseConstraint(Asrc, bsrc, c);
                }
            }
            if (E0.numRow() > 1) {
                NormalForm::simplifyEqualityConstraints(E0, q0);
            }
            return false;
        }
        // eliminate variable `i` according to original order
        auto [numExclude, c, numNonZero] =
            eliminateVarForRCElimCore(Adst, bdst, E0, q0, Asrc, bsrc, i);
        Adst.resize(c, Asrc.numCol());
        bdst.resize(c);
        auto [Ce, Re] = E0.size();
        size_t numReserve =
            Ce - numNonZero + ((numNonZero * (numNonZero - 1)) >> 1);
        E1.resizeForOverwrite(numReserve, Re);
        q1.resize_for_overwrite(numReserve);
        // auxInds are kept sorted
        // TODO: take advantage of the sorting to make iterating&checking more
        // efficient
        size_t k = 0;
        for (size_t u = 0; u < E0.numRow(); ++u) {
            auto Eu = E0.getRow(u);
            int64_t Eiu = Eu[i];
            if (Eiu == 0) {
                for (size_t v = 0; v < Re; ++v) {
                    E1(k, v) = Eu[v];
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
                auto El = E0.getRow(l);
                int64_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    auxMisMatch(auxInd, auxiliaryInd(El)))
                    continue;

                int64_t g = gcd(Eiu, Eil);
                int64_t Eiug = Eiu / g;
                int64_t Eilg = Eil / g;
                for (size_t v = 0; v < Re; ++v) {
                    E1(k, v) = Eiug * E0(l, v) - Eilg * E0(u, v);
                }
                q1[k] = Eiug * q0[l];
                Polynomial::fnmadd(q1[k], q0[u], Eilg);
                // q1(k) = Eiug * q0(l) - Eilg * q0(u);
                ++k;
            }
        }
        E1.resize(k, Re);
        q1.resize(k);
        NormalForm::simplifyEqualityConstraints(E1, q1);
        return true;
    }
    // method for when we do not match `Ex=q` constraints with themselves
    // e.g., for removeRedundantConstraints
    void eliminateVarForRCElim(IntMatrix &Adst, llvm::SmallVectorImpl<T> &bdst,
                               IntMatrix &E, llvm::SmallVectorImpl<T> &q,
                               PtrMatrix<int64_t> Asrc, llvm::ArrayRef<T> bsrc,
                               const size_t i) const {

        auto [numExclude, c, _] =
            eliminateVarForRCElimCore(Adst, bdst, E, q, Asrc, bsrc, i);
        for (size_t u = E.numRow(); u != 0;) {
            if (E(--u, i)) {
                eraseConstraint(E, q, u);
            }
        }
        if (Adst.numRow() != c) {
            Adst.resize(c, Asrc.numCol());
        }
        if (bdst.size() != c) {
            bdst.resize(c);
        }
    }
    std::tuple<size_t, size_t, size_t>
    eliminateVarForRCElimCore(IntMatrix &Adst, llvm::SmallVectorImpl<T> &bdst,
                              IntMatrix &E, llvm::SmallVectorImpl<T> &q,
                              PtrMatrix<int64_t> Asrc, llvm::ArrayRef<T> bsrc,
                              const size_t i) const {
        // eliminate variable `i` according to original order
        const auto [numCol, numVar] = Asrc.size();
        assert(bsrc.size() == numCol);
        size_t numNeg = 0;
        size_t numPos = 0;
        for (size_t j = 0; j < numCol; ++j) {
            int64_t Aij = Asrc(j, i);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        const size_t numECol = E.numRow();
        size_t numNonZero = 0;
        for (size_t j = 0; j < numECol; ++j) {
            numNonZero += E(j, i) != 0;
        }
        const size_t numExclude = numCol - numNeg - numPos;
        const size_t numColA = numNeg * numPos + numExclude +
                               numNonZero * (numNeg + numPos) +
                               ((numNonZero * (numNonZero - 1)) >> 1);
        Adst.resizeForOverwrite(numColA, numVar);
        bdst.resize(numColA);
        assert(Adst.numRow() == bdst.size());
        // assign to `A = Aold[:,exlcuded]`
        for (size_t j = 0, c = 0; c < numExclude; ++j) {
            if (Asrc(j, i)) {
                continue;
            }
            for (size_t k = 0; k < numVar; ++k) {
                Adst(c, k) = Asrc(j, k);
            }
            bdst[c++] = bsrc[j];
        }
        size_t c = numExclude;
        assert(numCol <= 500);
        // TODO: drop independentOfInner?
        for (size_t u = 0; u < numCol; ++u) {
            auto Au = Asrc.getRow(u);
            int64_t Aiu = Au[i];
            if (Aiu == 0)
                continue;
            int64_t auxInd = auxiliaryInd(Au);
            bool independentOfInnerU = independentOfInner(Au, i);
            for (size_t l = 0; l < u; ++l) {
                auto Al = Asrc.getRow(l);
                if ((Al[i] == 0) || ((Al[i] > 0) == (Aiu > 0)) ||
                    (independentOfInnerU && independentOfInner(Al, i)) ||
                    (auxMisMatch(auxInd, auxiliaryInd(Al))))
                    continue;
                if (setBounds(Adst.getRow(c), bdst[c], Al, bsrc[l], Au, bsrc[u],
                              i)) {
                    if (uniqueConstraint(Adst, bdst, c)) {
                        ++c;
                    }
                }
            }
            for (size_t l = 0; l < numECol; ++l) {
                auto El = E.getRow(l);
                int64_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    (auxMisMatch(auxInd, auxiliaryInd(El))))
                    continue;
                if ((Eil > 0) == (Aiu > 0)) {
                    // need to flip constraint in E
                    for (size_t v = 0; v < E.numCol(); ++v) {
                        negate(El[v]);
                    }
                    negate(q[l]);
                }
                if (setBounds(Adst.getRow(c), bdst[c], El, q[l], Au, bsrc[u],
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
    static std::pair<size_t, size_t>
    countNonZeroSign(PtrMatrix<const int64_t> A, size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        size_t numRow = A.numRow();
        for (size_t j = 0; j < numRow; ++j) {
            int64_t Aij = A(j, i);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    static void categorizeBounds(IntMatrix &lA, IntMatrix &uA,
                                 llvm::SmallVectorImpl<T> &lB,
                                 llvm::SmallVectorImpl<T> &uB,
                                 PtrMatrix<const int64_t> A,
                                 llvm::ArrayRef<T> b, size_t i) {
        auto [numConstraints, numLoops] = A.size();
        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        lA.resize(numNeg, numLoops);
        lB.resize(numNeg);
        uA.resize(numPos, numLoops);
        uB.resize(numPos);
        // fill bounds
        for (size_t j = 0, l = 0, u = 0; j < numConstraints; ++j) {
            int64_t Aij = A(j, i);
            if (Aij > 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    uA(u, k) = A(j, k);
                }
                uB[u++] = b[j];
            } else if (Aij < 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    lA(l, k) = A(j, k);
                }
                lB[l++] = b[j];
            }
        }
    }
    template <size_t CheckEmpty>
    bool appendBoundsSimple(const IntMatrix &lA, const IntMatrix &uA,
                            const llvm::SmallVectorImpl<T> &lB,
                            const llvm::SmallVectorImpl<T> &uB, IntMatrix &A,
                            llvm::SmallVectorImpl<T> &b, size_t i,
                            Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
        auto [numConstraints, numLoops] = A.size();
        A.reserve(numConstraints + numNeg * numPos, numLoops);
        b.reserve(numConstraints + numNeg * numPos);

        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(c + 1, numLoops);
                b.resize(c + 1);
                bool sb = setBounds(A.getRow(c), b[c], lA.getRow(l), lB[l],
                                    uA.getRow(u), uB[u], i);
                if (!sb) {
                    if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                        return true;
                    }
                }
                if ((!sb) || (!uniqueConstraint(A, b, c))) {
                    A.resize(c, numLoops);
                    b.resize(c);
                }
            }
        }
        return false;
    }

    template <size_t CheckEmpty>
    bool appendBounds(const IntMatrix &lA, const IntMatrix &uA,
                      const llvm::SmallVectorImpl<T> &lB,
                      const llvm::SmallVectorImpl<T> &uB, IntMatrix &Atmp0,
                      IntMatrix &Atmp1, IntMatrix &Etmp0, IntMatrix &Etmp1,
                      llvm::SmallVectorImpl<T> &btmp0,
                      llvm::SmallVectorImpl<T> &btmp1,
                      llvm::SmallVectorImpl<T> &qtmp0,
                      llvm::SmallVectorImpl<T> &qtmp1, IntMatrix &A,
                      llvm::SmallVectorImpl<T> &b, IntMatrix &E,
                      llvm::SmallVectorImpl<T> &q, size_t i,
                      Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
        auto [numConstraints, numLoops] = A.size();
        A.reserve(numConstraints + numNeg * numPos, numLoops);
        b.reserve(numConstraints + numNeg * numPos);

        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(c + 1, numLoops);
                b.resize(c + 1);
                bool sb = setBounds(A.getRow(c), b[c], lA.getRow(l), lB[l],
                                    uA.getRow(u), uB[u], i);
                if (!sb) {
                    if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                        return true;
                    }
                }
                if ((!sb) || (!uniqueConstraint(A, b, c))) {
                    A.resize(c, numLoops);
                    b.resize(c);
                }
            }
        }
        if (A.numRow()) {
            if (pruneBounds(Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                            qtmp1, A, b, E, q)) {
                return CheckEmpty;
            }
        }
        return false;
    }
    template <size_t CheckEmpty>
    bool appendBounds(const IntMatrix &lA, const IntMatrix &uA,
                      const llvm::SmallVectorImpl<T> &lB,
                      const llvm::SmallVectorImpl<T> &uB, IntMatrix &Atmp0,
                      IntMatrix &Atmp1, IntMatrix &Etmp,
                      llvm::SmallVectorImpl<T> &btmp0,
                      llvm::SmallVectorImpl<T> &btmp1,
                      llvm::SmallVectorImpl<T> &qtmp, IntMatrix &A,
                      llvm::SmallVectorImpl<T> &b, size_t i,
                      Polynomial::Val<CheckEmpty>) const {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
        auto [numConstraints, numLoops] = A.size();
        A.reserve(numConstraints + numNeg * numPos, numLoops);
        b.reserve(numConstraints + numNeg * numPos);

        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                size_t c = b.size();
                A.resize(c + 1, numLoops);
                b.resize(c + 1);
                bool sb = setBounds(A.getRow(c), b[c], lA.getRow(l), lB[l],
                                    uA.getRow(u), uB[u], i);
                if (!sb) {
                    if (CheckEmpty && knownLessEqualZero(b[c] + 1)) {
                        return true;
                    }
                }
                if ((!sb) || (!uniqueConstraint(A, b, c))) {
                    A.resize(c, numLoops);
                    b.resize(c);
                }
            }
        }
        if (A.numRow()) {
            pruneBounds(Atmp0, Atmp1, Etmp, btmp0, btmp1, qtmp, A, b);
        }
        return false;
    }
    void pruneBounds() { pruneBounds(A, b); }
    void pruneBounds(IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &E,
                     llvm::SmallVectorImpl<T> &btmp0,
                     llvm::SmallVectorImpl<T> &btmp1,
                     llvm::SmallVectorImpl<T> &q, IntMatrix &Aold,
                     llvm::SmallVectorImpl<T> &bold) const {

        for (size_t i = 0; i + 1 <= Aold.numRow(); ++i) {
            size_t c = Aold.numRow() - 1 - i;
            assert(Aold.numRow() == bold.size());
            if (removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                           Aold, bold, c)) {
                // drop `c`
                eraseConstraint(Aold, bold, c);
            }
        }
    }
    void pruneBounds(IntMatrix &Aold, llvm::SmallVectorImpl<T> &bold) const {

        IntMatrix Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        pruneBounds(Atmp0, Atmp1, E, btmp0, btmp1, q, Aold, bold);
    }
    static void moveEqualities(IntMatrix &Aold, llvm::SmallVectorImpl<T> &bold,
                               IntMatrix &Eold,
                               llvm::SmallVectorImpl<T> &qold) {

        const size_t numVar = Eold.numCol();
        assert(Aold.numCol() == numVar);
        if (Aold.numRow() > 1) {
            for (size_t o = Aold.numRow() - 1; o > 0;) {
                for (size_t i = o--; i < Aold.numRow(); ++i) {
                    bool isNeg = true;
                    for (size_t v = 0; v < numVar; ++v) {
                        if (Aold(i, v) != -Aold(o, v)) {
                            isNeg = false;
                            break;
                        }
                    }
                    if (isNeg && (bold[i] == -bold[o])) {
                        qold.push_back(bold[i]);
                        size_t e = Eold.numRow();
                        Eold.resize(qold.size(), numVar);
                        for (size_t v = 0; v < numVar; ++v) {
                            Eold(e, v) = Aold(i, v);
                        }
                        eraseConstraint(Aold, bold, i, o);
                        break;
                    }
                }
            }
        }
    }
    // returns `false` if not violated, `true` if violated
    bool pruneBounds(IntMatrix &Aold, llvm::SmallVectorImpl<T> &bold,
                     IntMatrix &Eold, llvm::SmallVectorImpl<T> &qold) const {

        IntMatrix Atmp0, Atmp1, Etmp0, Etmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1, qtmp0, qtmp1;
        return pruneBounds(Atmp0, Atmp1, Etmp0, Etmp1, btmp0, btmp1, qtmp0,
                           qtmp1, Aold, bold, Eold, qold);
    }
    bool pruneBounds(IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &Etmp0,
                     IntMatrix &Etmp1, llvm::SmallVectorImpl<T> &btmp0,
                     llvm::SmallVectorImpl<T> &btmp1,
                     llvm::SmallVectorImpl<T> &qtmp0,
                     llvm::SmallVectorImpl<T> &qtmp1, IntMatrix &Aold,
                     llvm::SmallVectorImpl<T> &bold, IntMatrix &Eold,
                     llvm::SmallVectorImpl<T> &qold) const {
        moveEqualities(Aold, bold, Eold, qold);
        NormalForm::simplifyEqualityConstraints(Eold, qold);
        // printConstraints(
        //     printConstraints(std::cout << "Constraints post-simplify:\n",
        //     Aold,
        //                      bold, true),
        //     Eold, qold, false)
        //     << std::endl;
        for (size_t i = 0; i < Eold.numRow(); ++i) {
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Eold.getRow(i), qold[i],
                                           Aold.numRow() + i, true)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                return true;
            }
            // flip
            for (size_t v = 0; v < Eold.numCol(); ++v) {
                Eold(i, v) *= -1;
            }
            qold[i] *= -1;
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Eold.getRow(i), qold[i],
                                           Aold.numRow() + i, true)) {
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                return true;
            }
        }
        assert(Aold.numRow() == bold.size());
        for (size_t i = 0; i + 1 <= Aold.numRow(); ++i) {
            size_t c = Aold.numRow() - 1 - i;
            assert(Aold.numRow() == bold.size());
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, btmp0,
                                           btmp1, qtmp0, qtmp1, Aold, bold,
                                           Eold, qold, Aold.getRow(c), bold[c],
                                           c, false)) {
                // drop `c`
                eraseConstraint(Aold, bold, c);
            }
        }
        return false;
    }
    bool removeRedundantConstraints(IntMatrix &Aold,
                                    llvm::SmallVectorImpl<T> &bold,
                                    const size_t c) const {
        IntMatrix Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, c);
    }
    bool removeRedundantConstraints(
        IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &E,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &q, IntMatrix &Aold,
        llvm::SmallVectorImpl<T> &bold, const size_t c) const {
        return removeRedundantConstraints(Atmp0, Atmp1, E, btmp0, btmp1, q,
                                          Aold, bold, Aold.getRow(c), bold[c],
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
    int64_t checkForTrivialRedundancies(
        llvm::SmallVector<unsigned, 32> &constraintsToErase,
        llvm::SmallVector<unsigned, 16> &boundDiffs, IntMatrix &Etmp,
        llvm::SmallVectorImpl<T> &qtmp, IntMatrix &Aold,
        llvm::SmallVectorImpl<T> &bold, llvm::ArrayRef<int64_t> a,
        const T &b) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numCol() - numVar;
        int64_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            size_t c = boundDiffs[i];
            int64_t dte = -1;
            for (size_t v = 0; v < numVar; ++v) {
                int64_t Evi = a[v] - Aold(c, v);
                Etmp(i, v + numAuxVar) = Evi;
                dte = (Evi) ? v + numAuxVar : dte;
            }
            qtmp[i] = b - bold[c];
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
                    constraintsToErase.push_back(i);
                }
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j) {
                Etmp(i, j) = (j == i);
            }
        }
        if (constraintsToErase.size()) {
            for (auto it = constraintsToErase.rbegin();
                 it != constraintsToErase.rend(); ++it) {
                size_t i = *it;
                size_t c = boundDiffs[i];
                boundDiffs.erase(boundDiffs.begin() + i);
                eraseConstraint(Aold, bold, c);
                eraseConstraint(Etmp, qtmp, i);
            }
            constraintsToErase.clear();
        }
        return dependencyToEliminate;
    }
    int64_t checkForTrivialRedundancies(
        llvm::SmallVector<unsigned, 32> &constraintsToErase,
        llvm::SmallVector<int, 16> &boundDiffs, IntMatrix &Etmp,
        llvm::SmallVectorImpl<T> &qtmp, IntMatrix &Aold,
        llvm::SmallVectorImpl<T> &bold, IntMatrix &Eold,
        llvm::SmallVectorImpl<T> &qold, llvm::ArrayRef<int64_t> a, const T &b,
        bool AbIsEq) const {
        const size_t numVar = getNumVar();
        const size_t numAuxVar = Etmp.numCol() - numVar;
        int64_t dependencyToEliminate = -1;
        for (size_t i = 0; i < numAuxVar; ++i) {
            int c = boundDiffs[i];
            int64_t dte = -1;
            T *bc;
            int64_t sign = (c > 0) ? 1 : -1;
            if ((0 <= c) && (size_t(c) < Aold.numRow())) {
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t Evi = a[v] - Aold(c, v);
                    Etmp(i, v + numAuxVar) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(bold[c]);
            } else {
                size_t cc = std::abs(c) - A.numRow();
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t Evi = a[v] - sign * Eold(cc, v);
                    Etmp(i, v + numAuxVar) = Evi;
                    dte = (Evi) ? v + numAuxVar : dte;
                }
                bc = &(qold[cc]);
            }
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
                    constraintsToErase.push_back(i);
                }
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j) {
                Etmp(i, j) = (j == i);
            }
            qtmp[i] = b;
            Polynomial::fnmadd(qtmp[i], *bc, sign);
            // qtmp[k] = b - (*bc)*sign;
        }
        if (constraintsToErase.size()) {
            for (auto it = constraintsToErase.rbegin();
                 it != constraintsToErase.rend(); ++it) {
                size_t i = *it;
                size_t c = std::abs(boundDiffs[i]);
                boundDiffs.erase(boundDiffs.begin() + i);
                if (c < Aold.numRow()) {
                    eraseConstraint(Aold, bold, c);
                }
                eraseConstraint(Etmp, qtmp, i);
            }
            constraintsToErase.clear();
        }
        return dependencyToEliminate;
    }
    // returns `true` if `a` and `b` should be eliminated as redundant,
    // otherwise it eliminates all variables from `Atmp0` and `btmp0` that `a`
    // and `b` render redundant.
    bool removeRedundantConstraints(
        IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &E,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &q, IntMatrix &Aold,
        llvm::SmallVectorImpl<T> &bold, llvm::ArrayRef<int64_t> a, const T &b,
        const size_t C) const {

        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<unsigned, 16> boundDiffs;
        for (size_t c = 0; c < C; ++c) {
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v];
                int64_t Avc = Aold(c, v);
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
        size_t AtmpCol = Aold.numRow() - (C < Aold.numRow());
        Atmp0.resizeForOverwrite(AtmpCol, numVarAugment);
        btmp0.resize_for_overwrite(AtmpCol);
        E.resizeForOverwrite(numAuxVar, numVarAugment);
        q.resize_for_overwrite(numAuxVar);
        for (size_t i = 0; i < Aold.numRow(); ++i) {
            if (i == C)
                continue;
            size_t j = i - (i > C);
            for (size_t v = 0; v < numAuxVar; ++v) {
                Atmp0(j, v) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Atmp0(j, v + numAuxVar) = Aold(i, v);
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
        assert(btmp0.size() == Atmp0.numRow());
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            assert(btmp0.size() == Atmp0.numRow());
            eliminateVarForRCElim(Atmp1, btmp1, E, q, Atmp0, btmp0,
                                  size_t(dependencyToEliminate));
            assert(btmp1.size() == Atmp1.numRow());
            std::swap(Atmp0, Atmp1);
            std::swap(btmp0, btmp1);
            assert(btmp0.size() == Atmp0.numRow());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can drop
            for (size_t c = 0; c < Atmp0.numRow(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getRow(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        int64_t Axc = Atmp0(c, auxInd);
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
            for (size_t c = 0; c < E.numRow(); ++c) {
                int64_t varInd = firstVarInd(E.getRow(c));
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
        IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &Etmp0, IntMatrix &Etmp1,
        llvm::SmallVectorImpl<T> &btmp0, llvm::SmallVectorImpl<T> &btmp1,
        llvm::SmallVectorImpl<T> &qtmp0, llvm::SmallVectorImpl<T> &qtmp1,
        IntMatrix &Aold, llvm::SmallVectorImpl<T> &bold, IntMatrix &Eold,
        llvm::SmallVectorImpl<T> &qold, llvm::ArrayRef<int64_t> a, const T &b,
        const size_t C, const bool AbIsEq) const {

        printConstraints(
            printConstraints(std::cout << "Constraints, eliminating C=" << C
                                       << ":\n",
                             Aold, bold, true),
            Eold, qold, false)
            << std::endl;
        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<int, 16> boundDiffs;
        for (size_t c = 0; c < Aold.numRow(); ++c) {
            if (c == C)
                continue;
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v];
                int64_t Avc = Aold(c, v);
                if (((av > 0) && (Avc > 0)) || ((av < 0) && (Avc < 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        if (!AbIsEq) {
            // if AbIsEq, would be eliminated via GaussianElimination
            for (int64_t c = 0; c < int64_t(Eold.numRow()); ++c) {
                int cc = c + Aold.numRow();
                if (cc == int(C))
                    continue;
                unsigned mask = 3;
                for (size_t v = 0; v < numVar; ++v) {
                    int64_t av = a[v];
                    int64_t Evc = Eold(c, v);
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
        bool CinA = C < Aold.numRow();
        size_t AtmpC = Aold.numRow() - CinA;
        size_t EtmpC = Eold.numRow() - (!CinA);
        Atmp0.resizeForOverwrite(AtmpC, numVarAugment);
        btmp0.resize_for_overwrite(AtmpC);
        Etmp0.reserve(EtmpC + numAuxVar, numVarAugment);
        qtmp0.reserve(EtmpC + numAuxVar);
        Etmp0.resizeForOverwrite(numAuxVar, numVarAugment);
        qtmp0.resize_for_overwrite(numAuxVar);
        for (size_t i = 0; i < Aold.numRow(); ++i) {
            if (i == C)
                continue;
            size_t j = i - (i > C);
            for (size_t v = 0; v < numAuxVar; ++v) {
                Atmp0(j, v) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Atmp0(j, v + numAuxVar) = Aold(i, v);
            }
            btmp0[j] = bold[i];
        }
        llvm::SmallVector<unsigned, 32> constraintsToErase;
        int64_t dependencyToEliminate = checkForTrivialRedundancies(
            constraintsToErase, boundDiffs, Etmp0, qtmp0, Aold, bold, Eold,
            qold, a, b, AbIsEq);
        if (dependencyToEliminate == -2) {
            return true;
        }
        size_t numEtmpAuxVar = Etmp0.numRow();
        Etmp0.resize(EtmpC + numEtmpAuxVar, numVarAugment);
        qtmp0.resize(EtmpC + numEtmpAuxVar);
        // fill Etmp0 with Eold
        for (size_t i = 0; i < Eold.numRow(); ++i) {
            if (i + Aold.numRow() == C)
                continue;
            size_t j = i - ((i + Aold.numRow() > C) && (C >= Aold.numRow())) +
                       numEtmpAuxVar;
            for (size_t v = 0; v < numAuxVar; ++v) {
                Etmp0(j, v) = 0;
            }
            for (size_t v = 0; v < numVar; ++v) {
                Etmp0(j, v + numAuxVar) = Eold(i, v);
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
        assert(btmp0.size() == Atmp0.numRow());
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            // std::cout << "Atmp0 (1) =\n" << Atmp0 << std::endl;
            assert(btmp0.size() == Atmp0.numRow());
            if (eliminateVarForRCElim(Atmp1, btmp1, Etmp1, qtmp1, Atmp0, btmp0,
                                      Etmp0, qtmp0,
                                      size_t(dependencyToEliminate))) {
                std::swap(Atmp0, Atmp1);
                std::swap(btmp0, btmp1);
                std::swap(Etmp0, Etmp1);
                std::swap(qtmp0, qtmp1);
            }
            // std::cout << "Atmp0 (2) =\n" << Atmp0 << std::endl;
            for (auto &a : Atmp0.mem) {
                assert(std::abs(a) < 100);
            }
            printConstraints(
                printConstraints(std::cout << "dependencyToEliminate = "
                                           << dependencyToEliminate
                                           << "; Temporary Constraints:\n",
                                 Atmp0, btmp0, true, numAuxVar),
                Etmp0, qtmp0, false, numAuxVar)
                << std::endl;
            // std::cout << "dependencyToEliminate = " << dependencyToEliminate
            // << std::endl;
            assert(btmp1.size() == Atmp1.numRow());
            // {
            //     int64_t lastAux = -1;
            //     for (size_t c = 0; c < Etmp0.numRow(); ++c) {
            //         int64_t auxInd = auxiliaryInd(Etmp0.getRow(c));
            //         assert(auxInd >= lastAux);
            //         lastAux = auxInd;
            //     }
            // }
            assert(btmp0.size() == Atmp0.numRow());
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can
            // drop
            for (size_t c = 0; c < Atmp0.numRow(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getRow(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    // FIXME: does knownLessEqualZero(btmp0[c]) always
                    // return `true` when `allZero(bold)`???
                    if ((auxInd != -1) && knownLessEqualZero(btmp0[c])) {
                        int64_t Axc = Atmp0(c, auxInd);
                        // Axc*delta <= b <= 0
                        // if (Axc > 0): (upper bound)
                        // delta <= b/Axc <= 0
                        // else if (Axc < 0): (lower bound)
                        // delta >= b/Axc >= 0
                        if (Axc > 0) {
                            // upper bound
                            size_t c = std::abs(boundDiffs[auxInd]);
                            if (c < Aold.numRow())
                                constraintsToErase.push_back(c);
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
            for (size_t c = 0; c < Etmp0.numRow(); ++c) {
                int64_t varInd = firstVarInd(Etmp0.getRow(c));
                if (varInd != -1) {
                    dependencyToEliminate = varInd;
                    break;
                }
            }
        }
        if (constraintsToErase.size()) {
            auto c = constraintsToErase.front();
            // std::cout << "Erasing Inequality Constraint c = " << c <<
            // std::endl;
            eraseConstraint(Aold, bold, c);
        }
        return false;
    }

    void deleteBounds(IntMatrix &A, llvm::SmallVectorImpl<T> &b,
                      size_t i) const {
        for (size_t j = b.size(); j != 0;) {
            if (A(--j, i)) {
                eraseConstraint(A, b, j);
            }
        }
    }
    // A'x <= b
    // removes variable `i` from system
    void removeVariable(IntMatrix &A, llvm::SmallVectorImpl<T> &b,
                        const size_t i) {

        IntMatrix lA;
        IntMatrix uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        removeVariable(lA, uA, lb, ub, A, b, i);
    }
    void removeVariable(IntMatrix &lA, IntMatrix &uA,
                        llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, IntMatrix &A,
                        llvm::SmallVectorImpl<T> &b, const size_t i) {

        IntMatrix Atmp0, Atmp1, E;
        llvm::SmallVector<T, 16> btmp0, btmp1, q;
        removeVariable(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1, q, A, b,
                       i);
    }
    void removeVariable(IntMatrix &lA, IntMatrix &uA,
                        llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, IntMatrix &Atmp0,
                        IntMatrix &Atmp1, IntMatrix &E,
                        llvm::SmallVectorImpl<T> &btmp0,
                        llvm::SmallVectorImpl<T> &btmp1,
                        llvm::SmallVectorImpl<T> &q, IntMatrix &A,
                        llvm::SmallVectorImpl<T> &b, const size_t i) {
        categorizeBounds(lA, uA, lb, ub, A, b, i);
        deleteBounds(A, b, i);
        appendBounds(lA, uA, lb, ub, Atmp0, Atmp1, E, btmp0, btmp1, q, A, b, i,
                     Polynomial::Val<false>());
    }

    // A'x <= b
    // E'x = q
    // removes variable `i` from system
    bool removeVariable(IntMatrix &A, llvm::SmallVectorImpl<T> &b, IntMatrix &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        if (substituteEquality(A, b, E, q, i)) {
            IntMatrix lA;
            IntMatrix uA;
            llvm::SmallVector<T, 8> lb;
            llvm::SmallVector<T, 8> ub;
            removeVariableCore(lA, uA, lb, ub, A, b, i);
        }
        if (E.numRow() > 1) {
            NormalForm::simplifyEqualityConstraints(E, q);
        }
        return pruneBounds(A, b, E, q);
    }
    bool removeVariable(IntMatrix &lA, IntMatrix &uA,
                        llvm::SmallVectorImpl<T> &lb,
                        llvm::SmallVectorImpl<T> &ub, IntMatrix &A,
                        llvm::SmallVectorImpl<T> &b, IntMatrix &E,
                        llvm::SmallVectorImpl<T> &q, const size_t i) {

        if (substituteEquality(A, b, E, q, i)) {
            removeVariableCore(lA, uA, lb, ub, A, b, i);
        }
        if (E.numRow() > 1) {
            NormalForm::simplifyEqualityConstraints(E, q);
        }
        return pruneBounds(A, b, E, q);
    }

    void removeVariableCore(IntMatrix &lA, IntMatrix &uA,
                            llvm::SmallVectorImpl<T> &lb,
                            llvm::SmallVectorImpl<T> &ub, IntMatrix &A,
                            llvm::SmallVectorImpl<T> &b, const size_t i) {

        IntMatrix Atmp0, Atmp1;
        llvm::SmallVector<T, 16> btmp0, btmp1;

        categorizeBounds(lA, uA, lb, ub, A, b, i);
        deleteBounds(A, b, i);
        appendBoundsSimple(lA, uA, lb, ub, A, b, i, Polynomial::Val<false>());
    }
    void removeVariable(const size_t i) { removeVariable(A, b, i); }
    static void erasePossibleNonUniqueElements(
        IntMatrix &A, llvm::SmallVectorImpl<T> &b,
        llvm::SmallVectorImpl<unsigned> &colsToErase) {
        // std::ranges::sort(colsToErase);
        std::sort(colsToErase.begin(), colsToErase.end());
        for (auto it = std::unique(colsToErase.begin(), colsToErase.end());
             it != colsToErase.begin();) {
            eraseConstraint(A, b, *(--it));
        }
    }

    void dropEmptyConstraints() {
	::dropEmptyConstraints(A, b);
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
        IntMatrix lA;
        IntMatrix uA;
        llvm::SmallVector<T, 8> lb;
        llvm::SmallVector<T, 8> ub;
        IntMatrix Atmp0, Atmp1, E;
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
        for (size_t c = 0; c < getNumInequalityConstraints(); ++c) {
            bc = b[c];
            for (size_t v = 0; v < numVar; ++v) {
                bc -= A(c, v) * x[v];
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
    IntegerPolyhedra(size_t numIneq, size_t numVar)
        : AbstractPolyhedra<IntegerPolyhedra, int64_t>(numIneq, numVar){};
    IntegerPolyhedra(IntMatrix A, llvm::SmallVector<int64_t, 8> b)
        : AbstractPolyhedra<IntegerPolyhedra, int64_t>(std::move(A),
                                                       std::move(b)){};
};
struct SymbolicPolyhedra : public AbstractPolyhedra<SymbolicPolyhedra, MPoly> {
    PartiallyOrderedSet poset;
    SymbolicPolyhedra(IntMatrix A, llvm::SmallVector<MPoly, 8> b,
                      PartiallyOrderedSet poset)
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
