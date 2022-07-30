
#pragma once

#include "./Comparators.hpp"
#include "./Constraints.hpp"
#include "./EmptyArrays.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include "./POSet.hpp"
#include "./Simplex.hpp"
#include "./Symbolics.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <sys/types.h>
#include <type_traits>

// Can we represent Polyhedra using slack variables + equalities?
// What must we do with Polyhedra?
// 1) A*x >= 0 && c'x >= 0 <-> l_0 + l'Ax == c'x && l >= 0 && l_0 >= 0
// 2) pruning bounds

// For "1)", we'd need to recover inequalities from slack vars.
// How does moving through solutions work with a mix of non-negative and
// unbounded variables?
// i <= j - 1
// j <= J - 1
// i <= J - 1
//
// for fun, lower bounds are -2
// i >= -2
// j >= -2
// and we have symbolic J
//  c  J  i  j s0 s1 s2 s3 s4
// -1  0  1 -1  1  0  0  0  0
// -1  1  0  1  0  1  0  0  0
// -1  1  1  0  0  0  1  0  0
// -2  0  1  0  0  0  0 -1  0
// -2  0  0  1  0  0  0  0 -1
// How confident can we be about arbitrary combinations of variables vs 0 for
// comparisons?

// A*x <= b
// representation is
// A[:,0] + A[:,1:s.size()]*s + A[:,1+s.size():end]*x >= 0
// E[:,0] + E[:,1:s.size()]*s + E[:,1+s.size():end]*x == 0
// where `s` is the vector of symbolic variables.
// These are treated as constants, and clearly separated from the dynamically
// varying values `x`.
// We have `A.numRow()` inequality constraints and `E.numRow()` equality
// constraints.
//
template <MaybeMatrix<int64_t> I64Matrix, Comparator CmptrType>
struct Polyhedra {
    // order of vars:
    // constants, loop vars, symbolic vars
    // this is because of hnf prioritizing diagonalizing leading rows
    IntMatrix A;
    I64Matrix E;
    CmptrType C;

    // Polyhedra(const IntMatrix A, I64Matrix E) : A(std::move(A)), E(E){};
    // static Polyhedra empty(size_t numIneq, size_t numVar) {
    // A(numIneq, numVar + 1)
    // 	};

    size_t getNumVar() const { return A.numCol() - C.getNumConstTerms(); }
    size_t getNumInequalityConstraints() const { return A.numRow(); }
    size_t getNumEqualityConstraints() const { return E.numRow(); }

    static constexpr bool hasEqualities =
        !std::is_same_v<I64Matrix, EmptyMatrix<int64_t>>;

    bool lessZero(const IntMatrix &A, const size_t r) const {
        return C.less(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool lessEqualZero(const IntMatrix &A, const size_t r) const {
        return C.lessEqual(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool greaterZero(const IntMatrix &A, const size_t r) const {
        return C.greater(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool greaterEqualZero(const IntMatrix &A, const size_t r) const {
        return C.greaterEqual(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool lessZero(const size_t r) const {
        return C.less(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool lessEqualZero(const size_t r) const {
        return C.lessEqual(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool greaterZero(const size_t r) const {
        return C.greater(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    bool greaterEqualZero(const size_t r) const {
        return C.greaterEqual(view(A.getRow(r), 0, C.getNumConstTerms()));
    }
    llvm::ArrayRef<int64_t> getSymbol(PtrMatrix<const int64_t> A,
                                      size_t i) const {
        return view(A.getRow(i), 0, C.getNumConstTerms());
    }
    llvm::ArrayRef<int64_t> getNonSymbol(PtrMatrix<const int64_t> A,
                                         size_t i) const {
        return view(A.getRow(i), C.getNumConstTerms(), A.numCol());
    }
    bool equalNegative(const size_t i, const size_t j) const {
        return C.equalNegative(getSymbol(A, i), getSymbol(A, j));
    }
    bool equalNegative(const IntMatrix &A, const size_t i,
                       const size_t j) const {
        return C.equalNegative(getSymbol(A, i), getSymbol(A, j));
    }

    // setBounds(a, b, la, lb, ua, ub, i)
    // `la` and `lb` correspond to the lower bound of `i`
    // `ua` and `ub` correspond to the upper bound of `i`
    // Eliminate `i`, and set `a` and `b` appropriately.
    // Returns `true` if `a` still depends on another variable.
    static bool setBounds(llvm::MutableArrayRef<int64_t> a,
                          llvm::ArrayRef<int64_t> la,
                          llvm::ArrayRef<int64_t> ua, size_t i) {
        int64_t cu_base = ua[i];
        int64_t cl_base = la[i];
        if ((cu_base > 0) && (cl_base < 0))
            // if cu_base > 0, then it is an lower bound, so swap
            return setBounds(a, ua, la, i);
        int64_t g = gcd(cu_base, cl_base);
        int64_t cu = cu_base / g;
        int64_t cl = cl_base / g;
        size_t N = la.size();
        for (size_t n = 0; n < N; ++n)
            a[n] = cu * la[n] - cl * ua[n];
        g = 0;
        for (size_t n = 0; n < N; ++n) {
            int64_t an = a[n];
            if (std::abs(an) == 1)
                return true;
            g = g ? (an ? gcd(g, an) : g) : an;
        }
        if (g <= 0)
            return g != 0;
        for (size_t n = 0; n < N; ++n)
            a[n] /= g;
        return true;
    }

    static bool uniqueConstraint(PtrMatrix<const int64_t> A, size_t C) {
        for (size_t c = 0; c < C; ++c) {
            bool allEqual = true;
            for (size_t r = 0; r < A.numCol(); ++r)
                allEqual &= (A(c, r) == A(C, r));
            if (allEqual)
                return false;
        }
        return true;
    }
    // independentOfInner(a, i)
    // checks if any `a[j] != 0`, such that `j != i`.
    // I.e., if this vector defines a hyper plane otherwise independent of `i`.
    static inline bool independentOfInner(llvm::ArrayRef<int64_t> a, size_t i) {
        for (size_t j = 0; j < a.size(); ++j)
            if ((a[j] != 0) & (i != j))
                return false;
        return true;
    }
    // -1 indicates no auxiliary variable
    inline int64_t auxiliaryInd(llvm::ArrayRef<int64_t> a) const {
        for (size_t i = A.numCol(); i < a.size(); ++i)
            if (a[i])
                return i;
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
    bool eliminateVarForRCElim(IntMatrix &Adst, IntMatrix &E1, IntMatrix &Asrc,
                               IntMatrix &E0, const size_t i) const {
        const size_t numConst = C.getNumConstTerms();
        const size_t numCol = A.numCol();
        // std::cout << "Asrc0 =\n" << Asrc << std::endl;
        if (!substituteEquality(Asrc, E0, i)) {
            // std::cout << "Asrc1 =\n" << Asrc << std::endl;
            // const size_t numAuxVar = Asrc.numCol() - getNumVar();
            size_t c = Asrc.numRow();
            while (c--) {
                size_t s = 0;
                for (size_t j = numCol; j < Asrc.numCol(); ++j)
                    s += (Asrc(c, j) != 0);
                if (s > 1)
                    eraseConstraint(Asrc, c);
            }
            if (E0.numRow() > 1)
                NormalForm::simplifySystem(E0, numConst);
            return false;
        }
        // eliminate variable `i` according to original order
        auto [numExclude, c, numNonZero] =
            eliminateVarForRCElimCore(Adst, E0, Asrc, i);
        Adst.resize(c, Asrc.numCol());
        auto [Ce, Re] = E0.size();
        size_t numReserve =
            Ce - numNonZero + ((numNonZero * (numNonZero - 1)) >> 1);
        E1.resizeForOverwrite(numReserve, Re);
        // auxInds are kept sorted
        // TODO: take advantage of the sorting to make iterating&checking more
        // efficient
        size_t k = 0;
        for (size_t u = 0; u < E0.numRow(); ++u) {
            auto Eu = E0.getRow(u);
            int64_t Eiu = Eu[i];
            if (Eiu == 0) {
                for (size_t v = 0; v < Re; ++v)
                    E1(k, v) = Eu[v];
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
                for (size_t v = 0; v < Re; ++v)
                    E1(k, v) = Eiug * E0(l, v) - Eilg * E0(u, v);
                ++k;
            }
        }
        E1.resizeRows(k);
        NormalForm::simplifySystem(E1, numConst);
        return true;
    }
    // method for when we do not match `Ex=q` constraints with themselves
    // e.g., for removeRedundantConstraints
    void eliminateVarForRCElim(IntMatrix &Adst, IntMatrix &E,
                               PtrMatrix<int64_t> Asrc, const size_t i) const {

        auto [numExclude, c, _] = eliminateVarForRCElimCore(Adst, E, Asrc, i);
        for (size_t u = E.numRow(); u != 0;)
            if (E(--u, i))
                eraseConstraint(E, u);
        if (Adst.numRow() != c)
            Adst.resize(c, Asrc.numCol());
    }
    static std::pair<size_t, size_t> countSigns(PtrMatrix<const int64_t> A,
                                                size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        for (size_t j = 0; j < A.numRow(); ++j) {
            int64_t Aij = A(j, i);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    std::tuple<size_t, size_t, size_t>
    eliminateVarForRCElimCore(IntMatrix &Adst, IntMatrix &E,
                              PtrMatrix<int64_t> Asrc, const size_t i) const {
        // eliminate variable `i` according to original order
        const auto [numCon, numVar] = Asrc.size();
        const auto [numNeg, numPos] = countSigns(Asrc, i);
        const size_t numECol = E.numRow();
        size_t numNonZero = 0;
        for (size_t j = 0; j < numECol; ++j)
            numNonZero += E(j, i) != 0;
        const size_t numExclude = numCon - numNeg - numPos;
        const size_t numColA = numNeg * numPos + numExclude +
                               numNonZero * (numNeg + numPos) +
                               ((numNonZero * (numNonZero - 1)) >> 1);
        Adst.resizeForOverwrite(numColA, numVar);
        // assign to `A = Aold[:,exlcuded]`
        for (size_t j = 0, c = 0; c < numExclude; ++j) {
            if (Asrc(j, i))
                continue;
            for (size_t k = 0; k < numVar; ++k)
                Adst(c, k) = Asrc(j, k);
            ++c;
        }
        size_t c = numExclude;
        assert(numCon <= 500);
        // TODO: drop independentOfInner?
        for (size_t u = 0; u < numCon; ++u) {
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
                if (setBounds(Adst.getRow(c), Al, Au, i))
                    if (uniqueConstraint(Adst, c))
                        ++c;
            }
            for (size_t l = 0; l < numECol; ++l) {
                auto El = E.getRow(l);
                int64_t Eil = El[i];
                if ((Eil == 0) ||
                    (independentOfInnerU && independentOfInner(El, i)) ||
                    (auxMisMatch(auxInd, auxiliaryInd(El))))
                    continue;
                if ((Eil > 0) == (Aiu > 0))
                    // need to flip constraint in E
                    for (size_t v = 0; v < E.numCol(); ++v)
                        negate(El[v]);
                if (setBounds(Adst.getRow(c), El, Au, i))
                    if (uniqueConstraint(Adst, c))
                        ++c;
            }
        }
        return std::make_tuple(numExclude, c, numNonZero);
    }
    void moveEqualities(IntMatrix &Aold, I64Matrix &Eold) const {
        ::moveEqualities(Aold, Eold, C);
    }
    // returns std::make_pair(numNeg, numPos);
    // countNonZeroSign(Matrix A, i)
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    // static void categorizeBounds(IntMatrix &lA, IntMatrix &uA,
    //                              PtrMatrix<const int64_t> A, size_t i) {
    //     auto [numConstraints, numLoops] = A.size();
    //     const auto [numNeg, numPos] = countNonZeroSign(A, i);
    //     lA.resize(numNeg, numLoops);
    //     uA.resize(numPos, numLoops);
    //     // fill bounds
    //     for (size_t j = 0, l = 0, u = 0; j < numConstraints; ++j) {
    //         if (int64_t Aij = A(j, i)) {
    //             if (Aij > 0) {
    //                 for (size_t k = 0; k < numLoops; ++k)
    //                     uA(u, k) = A(j, k);
    //             } else {
    //                 for (size_t k = 0; k < numLoops; ++k)
    //                     lA(l, k) = A(j, k);
    //             }
    //         }
    //     }
    // }
    void pruneBounds() { pruneBounds(A); }
    void pruneBounds(IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &E,
                     IntMatrix &Aold) const {

        // for (size_t i = 0; i + 1 <= Aold.numRow(); ++i) {
        // size_t c = Aold.numRow() - 1 - i;
        for (size_t c = Aold.numRow(); c;) {
            // std::cout << "trying to prune c = "<<c<<std::endl;
            if (removeRedundantConstraints(Atmp0, Atmp1, E, Aold, --c)) {
                // drop `c`
                std::cout << "ERASING c = " << c << std::endl;
                eraseConstraint(Aold, c);
            }
        }
    }
    void pruneBounds(IntMatrix &Aold) const {

        IntMatrix Atmp0, Atmp1, E;
        pruneBounds(Atmp0, Atmp1, E, Aold);
    }
    inline static bool equalsNegative(llvm::ArrayRef<int64_t> x,
                                      llvm::ArrayRef<int64_t> y) {
        assert(x.size() == y.size());
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] + y[i])
                return false;
        return true;
    }
    // returns `false` if not violated, `true` if violated
    bool pruneBounds(IntMatrix &Aold, I64Matrix &Eold) const {
        IntMatrix Atmp0, Atmp1, Etmp0;
        if constexpr (!hasEqualities) {
            pruneBounds(Atmp0, Atmp1, Etmp0, Aold);
            return false;
        }
        IntMatrix Etmp1;
        return pruneBounds(Atmp0, Atmp1, Etmp0, Etmp1, Aold, Eold);
    }
    bool pruneBounds(IntMatrix &Atmp0, IntMatrix &Atmp1, IntMatrix &Etmp0,
                     IntMatrix &Etmp1, IntMatrix &Aold, IntMatrix &Eold) const {
        moveEqualities(Aold, Eold);
        NormalForm::simplifySystem(Eold);
        // printConstraints(
        //     printConstraints(std::cout << "Constraints post-simplify:\n",
        //     Aold,
        //                      bold, true),
        //     Eold, qold, false)
        //     << std::endl;
        for (size_t i = 0; i < Eold.numRow(); ++i) {
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, Aold,
                                           Eold, Eold.getRow(i),
                                           Aold.numRow() + i, true))
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                return true;
            // flip
            for (auto &&e : Eold.getRow(i))
                e *= -1;
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, Aold,
                                           Eold, Eold.getRow(i),
                                           Aold.numRow() + i, true))
                // if Eold's constraint is redundant, that means there was a
                // stricter one, and the constraint is violated
                return true;
        }
        for (size_t i = 0; i + 1 <= Aold.numRow(); ++i) {
            size_t c = Aold.numRow() - 1 - i;
            if (removeRedundantConstraints(Atmp0, Atmp1, Etmp0, Etmp1, Aold,
                                           Eold, Aold.getRow(c), c, false))
                eraseConstraint(Aold, c); // drop `c`
        }
        return false;
    }
    bool removeRedundantConstraints(IntMatrix &A, const size_t c) const {
        IntMatrix A0, A1, E;
        return removeRedundantConstraints(A0, A1, E, A, c);
    }
    bool removeRedundantConstraints(IntMatrix &A0, IntMatrix &A1, IntMatrix &E,
                                    IntMatrix &A, const size_t c) const {
        return removeRedundantConstraints(A0, A1, E, A, A.getRow(c), c);
    }
    int64_t firstVarInd(llvm::ArrayRef<int64_t> a) const {
        for (size_t i = 0; i < A.numCol(); ++i)
            if (a[i])
                return i;
        return -1;
    }
    int64_t checkForTrivialRedundancies(
        llvm::SmallVector<unsigned, 32> &constraintsToErase,
        llvm::SmallVector<unsigned, 16> &boundDiffs, IntMatrix &Etmp,
        IntMatrix &Aold, llvm::ArrayRef<int64_t> a) const {
        const size_t numConst = C.getNumConstTerms();
        const size_t numCol = Aold.numCol();
        const size_t numAuxVar = Etmp.numCol() - numCol;
        const size_t numVar = getNumVar();
        assert(numConst + numVar == numCol);
        int64_t dependencyToEliminate = -1;
        llvm::SmallVector<int64_t> delta;
        delta.resize_for_overwrite(C.getNumConstTerms());
        for (size_t i = 0; i < numAuxVar; ++i) {
            size_t c = boundDiffs[i];
            int64_t dte = -1;
            for (size_t v = 0; v < numCol; ++v) {
                int64_t Evi = a[v] + Aold(c, v);
                Etmp(i, v) = Evi;
                dte = (Evi & (v >= numConst)) ? v : dte;
            }
            if (dte == -1) {
                for (size_t j = 0; j < delta.size(); ++j)
                    delta[j] = Aold(c, j) - a[j];
                printVector(std::cout << "delta = ", delta) << std::endl;
                if (C.lessEqual(delta)) {
                    // bold[c] - b <= 0
                    // bold[c] <= b
                    // thus, bound `c` will always trigger before `b`
                    return -2;
                } else if (C.greaterEqual(delta)) {
                    // bound `b` triggers first
                    // normally we insert `c`, but if inserting here
                    // we also want to erase elements from boundDiffs
                    constraintsToErase.push_back(i);
                }
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j)
                Etmp(i, j + numCol) = (j == i);
        }
        if (constraintsToErase.size()) {
            for (auto it = constraintsToErase.rbegin();
                 it != constraintsToErase.rend(); ++it) {
                size_t i = *it;
                size_t c = boundDiffs[i];
                boundDiffs.erase(boundDiffs.begin() + i);
                eraseConstraint(Aold, c);
                eraseConstraint(Etmp, i);
            }
            constraintsToErase.clear();
        }
        return dependencyToEliminate;
    }
    int64_t checkForTrivialRedundancies(
        llvm::SmallVector<unsigned, 32> &constraintsToErase,
        llvm::SmallVector<int, 16> &boundDiffs, IntMatrix &Etmp,
        IntMatrix &Aold, I64Matrix &Eold, llvm::ArrayRef<int64_t> a,
        bool AbIsEq) const {
        const size_t numCol = Aold.numCol();
        const size_t numConst = C.getNumConstTerms();
        const size_t numAuxVar = Etmp.numCol() - numCol;
        int64_t dependencyToEliminate = -1;
        llvm::SmallVector<int64_t> delta;
        delta.resize_for_overwrite(C.getNumConstTerms());
        for (size_t i = 0; i < numAuxVar; ++i) {
            int c = boundDiffs[i];
            int64_t dte = -1;
            llvm::ArrayRef<int64_t> bc;
            int64_t sign = (c > 0) ? 1 : -1;
            if ((0 <= c) && (size_t(c) < Aold.numRow())) {
                for (size_t v = 0; v < numCol; ++v) {
                    int64_t Evi = a[v] + Aold(c, v);
                    Etmp(i, v) = Evi;
                    dte = (Evi & (v >= numConst)) ? v : dte;
                }
                bc = getSymbol(Aold, c);
            } else {
                size_t cc = std::abs(c) - A.numRow();
                for (size_t v = 0; v < numCol; ++v) {
                    int64_t Evi = a[v] - sign * Eold(cc, v);
                    Etmp(i, v) = Evi;
                    dte = (Evi & (v >= numConst)) ? v : dte;
                }
                bc = getSymbol(Eold, cc);
            }
            if (dte == -1) {
                for (size_t j = 0; j < delta.size(); ++j)
                    delta[j] = bc[j] * sign - a[j];
                if (C.lessEqual(delta, AbIsEq))
                    // bold[c] - b <= 0
                    // bold[c] <= b
                    // thus, bound `c` will always trigger before `b`
                    return -2;
                if (C.greaterEqual(delta))
                    // bound `b` triggers first
                    // normally we insert `c`, but if inserting here
                    // we also want to erase elements from boundDiffs
                    constraintsToErase.push_back(i);
            } else {
                dependencyToEliminate = dte;
            }
            for (size_t j = 0; j < numAuxVar; ++j)
                Etmp(i, j + numCol) = (j == i);
        }
        if (constraintsToErase.size()) {
            for (auto it = constraintsToErase.rbegin();
                 it != constraintsToErase.rend(); ++it) {
                size_t i = *it;
                size_t c = std::abs(boundDiffs[i]);
                boundDiffs.erase(boundDiffs.begin() + i);
                if (c < Aold.numRow())
                    eraseConstraint(Aold, c);
                eraseConstraint(Etmp, i);
            }
            constraintsToErase.clear();
        }
        return dependencyToEliminate;
    }
    // returns `true` if `a` and `b` should be eliminated as redundant,
    // otherwise it eliminates all variables from `Atmp0` and `btmp0` that `a`
    // and `b` render redundant.
    bool removeRedundantConstraints(IntMatrix &Atmp0, IntMatrix &Atmp1,
                                    IntMatrix &E, IntMatrix &Aold,
                                    llvm::ArrayRef<int64_t> a,
                                    const size_t CC) const {

        const size_t numCol = A.numCol();
        const size_t numVar = getNumVar();
        const size_t numConst = C.getNumConstTerms();
        assert(numConst + numVar == numCol);
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<unsigned, 16> boundDiffs;
        for (size_t c = 0; c < CC; ++c) {
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v + numConst];
                int64_t Avc = Aold(c, v + numConst);
                if (((av > 0) == (Avc > 0)) && ((av != 0) & (Avc != 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        // std::cout << "A =\n" << A << std::endl;
        // printVector(std::cout << "a = ", a) << std::endl;
        // printVector(std::cout << "boundDiffs = ", boundDiffs) << std::endl;
        const size_t numAuxVar = boundDiffs.size();
        // std::cout << "numAuxVar = " << numAuxVar << std::endl;
        if (numAuxVar == 0)
            return false;
        const size_t numColAugment = numCol + numAuxVar;
        Atmp0.resizeForOverwrite(Aold.numRow() - (CC < Aold.numRow()),
                                 numColAugment);
        E.resizeForOverwrite(numAuxVar, numColAugment);
        for (size_t i = 0; i < Aold.numRow(); ++i) {
            if (i == CC)
                continue;
            size_t j = i - (i > CC);
            for (size_t v = 0; v < numCol; ++v)
                Atmp0(j, v) = Aold(i, v);
            for (size_t v = numCol; v < numColAugment; ++v)
                Atmp0(j, v) = 0;
        }
        llvm::SmallVector<unsigned, 32> colsToErase;
        int64_t dependencyToEliminate =
            checkForTrivialRedundancies(colsToErase, boundDiffs, E, Aold, a);
        std::cout << "Aold =\n" << Aold << std::endl;
        printVector(std::cout << "CC = " << CC << "; a = ", a) << std::endl;
        printVector(std::cout << "boundDiffs = ", boundDiffs) << std::endl;
        std::cout << "Initial eliminating: " << dependencyToEliminate
                  << std::endl;
        if (dependencyToEliminate == -2)
            return true;
        std::cout << "Atmp0 =\n" << Atmp0 << std::endl;
        std::cout << "E =\n" << E << std::endl;
        // define variables as
        // (a-Aold) + delta = b - bold
        // delta = b - bold - (a-Aold) = (b - a) - (bold - Aold)
        // if we prove delta >= 0, then (b - a) >= (bold - Aold)
        // and thus (b - a) is the redundant constraint, and we return `true`.
        // else if we prove delta <= 0, then (b - a) <= (bold - Aold)
        // and thus (bold - Aold) is the redundant constraint, and we eliminate
        // the associated column.
        while (dependencyToEliminate >= 0) {
            std::cout << "eliminating: " << dependencyToEliminate << std::endl;
            // eliminate dependencyToEliminate
            eliminateVarForRCElim(Atmp1, E, Atmp0,
                                  size_t(dependencyToEliminate));
            std::swap(Atmp0, Atmp1);
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can drop
            std::cout << "Atmp0 = \n" << Atmp0 << "\nE =\n" << E << std::endl;
            for (size_t c = 0; c < Atmp0.numRow(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getRow(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    printVector(std::cout << "sym = ", getSymbol(Atmp0, c))
                        << std::endl;
                    std::cout << "C.lessEqual(getSymbol(Atmp0, c)) = "
                              << C.lessEqual(getSymbol(Atmp0, c)) << std::endl;
                    if ((auxInd != -1) && (C.lessEqual(getSymbol(Atmp0, c)))) {
                        int64_t Axc = Atmp0(c, auxInd);
                        // -Axc*delta <= b <= 0
                        // if (Axc > 0): (upper bound)
                        // delta <= b/Axc <= 0
                        // else if (Axc < 0): (lower bound)
                        // delta >= -b/Axc >= 0
                        if (Axc <= 0)
                            // lower bound
                            return true;
                        // upper bound
                        colsToErase.push_back(boundDiffs[auxInd - numCol]);
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
        if (colsToErase.size())
            eraseConstraint(Aold, colsToErase.front());
        return false;
    }
    bool removeRedundantConstraints(IntMatrix &Atmp0, IntMatrix &Atmp1,
                                    IntMatrix &Etmp0, IntMatrix &Etmp1,
                                    IntMatrix &Aold, I64Matrix &Eold,
                                    llvm::ArrayRef<int64_t> a, const size_t CC,
                                    const bool AbIsEq) const {

        printConstraints(
            printConstraints(std::cout << "Constraints, eliminating C=" << CC
                                       << ":\n",
                             Aold, C.getNumConstTerms(), true),
            Eold, C.getNumConstTerms(), false)
            << std::endl;
        const size_t numVar = getNumVar();
        // simple mapping of `k` to particular bounds
        // we'll have C - other bound
        llvm::SmallVector<int, 16> boundDiffs;
        for (size_t c = 0; c < Aold.numRow(); ++c) {
            if (c == CC)
                continue;
            for (size_t v = 0; v < numVar; ++v) {
                int64_t av = a[v];
                int64_t Avc = Aold(c, v);
                if (((av > 0) == (Avc > 0)) && ((av != 0) & (Avc != 0))) {
                    boundDiffs.push_back(c);
                    break;
                }
            }
        }
        if (!AbIsEq) {
            // if AbIsEq, would be eliminated via GaussianElimination
            for (int64_t c = 0; c < int64_t(Eold.numRow()); ++c) {
                int cc = c + Aold.numRow();
                if (cc == int(CC))
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
                        if (mask == 0)
                            break;
                    }
                }
            }
        }
        const size_t numAuxVar = boundDiffs.size();
        const size_t numVarAugment = numVar + numAuxVar;
        bool CinA = CC < Aold.numRow();
        size_t AtmpC = Aold.numRow() - CinA;
        size_t EtmpC = Eold.numRow() - (!CinA);
        Atmp0.resizeForOverwrite(AtmpC, numVarAugment);
        Etmp0.reserve(EtmpC + numAuxVar, numVarAugment);
        Etmp0.resizeForOverwrite(numAuxVar, numVarAugment);
        for (size_t i = 0; i < Aold.numRow(); ++i) {
            if (i == CC)
                continue;
            size_t j = i - (i > CC);
            for (size_t v = 0; v < numAuxVar; ++v)
                Atmp0(j, v) = 0;
            for (size_t v = 0; v < numVar; ++v)
                Atmp0(j, v + numAuxVar) = Aold(i, v);
        }
        llvm::SmallVector<unsigned, 32> constraintsToErase;
        int64_t dependencyToEliminate = checkForTrivialRedundancies(
            constraintsToErase, boundDiffs, Etmp0, Aold, Eold, a, AbIsEq);
        if (dependencyToEliminate == -2)
            return true;
        size_t numEtmpAuxVar = Etmp0.numRow();
        Etmp0.resize(EtmpC + numEtmpAuxVar, numVarAugment);
        // fill Etmp0 with Eold
        for (size_t i = 0; i < Eold.numRow(); ++i) {
            if (i + Aold.numRow() == CC)
                continue;
            size_t j = i - ((i + Aold.numRow() > CC) && (CC >= Aold.numRow())) +
                       numEtmpAuxVar;
            for (size_t v = 0; v < numAuxVar; ++v)
                Etmp0(j, v) = 0;
            for (size_t v = 0; v < numVar; ++v)
                Etmp0(j, v + numAuxVar) = Eold(i, v);
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
        while (dependencyToEliminate >= 0) {
            // eliminate dependencyToEliminate
            // std::cout << "Atmp0 (1) =\n" << Atmp0 << std::endl;
            if (eliminateVarForRCElim(Atmp1, Etmp1, Atmp0, Etmp0,
                                      size_t(dependencyToEliminate))) {
                std::swap(Atmp0, Atmp1);
                std::swap(Etmp0, Etmp1);
            }
            // std::cout << "Atmp0 (2) =\n" << Atmp0 << std::endl;
            for (auto &a : Atmp0.mem) {
                assert(std::abs(a) < 100);
            }
            printConstraints(
                printConstraints(std::cout << "dependencyToEliminate = "
                                           << dependencyToEliminate
                                           << "; Temporary Constraints:\n",
                                 Atmp0, C.getNumConstTerms(), true, numAuxVar),
                Etmp0, C.getNumConstTerms(), false, numAuxVar)
                << std::endl;
            // std::cout << "dependencyToEliminate = " << dependencyToEliminate
            // << std::endl;
            // {
            //     int64_t lastAux = -1;
            //     for (size_t c = 0; c < Etmp0.numRow(); ++c) {
            //         int64_t auxInd = auxiliaryInd(Etmp0.getRow(c));
            //         assert(auxInd >= lastAux);
            //         lastAux = auxInd;
            //     }
            // }
            dependencyToEliminate = -1;
            // iterate over the new bounds, search for constraints we can
            // drop
            for (size_t c = 0; c < Atmp0.numRow(); ++c) {
                llvm::ArrayRef<int64_t> Ac = Atmp0.getRow(c);
                int64_t varInd = firstVarInd(Ac);
                if (varInd == -1) {
                    int64_t auxInd = auxiliaryInd(Ac);
                    // FIXME: does (btmp0[c]<=0) always
                    // return `true` when `allZero(bold)`???
                    if ((auxInd != -1) && (C.lessEqual(getSymbol(Atmp0, c)))) {
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
                                   (C.lessEqual(getSymbol(Atmp0, c), 1))) {
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
        if (constraintsToErase.size())
            // std::cout << "Erasing Inequality Constraint c = " << c <<
            // std::endl;
            eraseConstraint(Aold, constraintsToErase.front());
        return false;
    }

    void deleteBounds(IntMatrix &A, size_t i) const {
        for (size_t j = A.numRow(); j != 0;)
            if (A(--j, i))
                eraseConstraint(A, j);
    }
    // returns `true` if empty, `false` otherwise
    bool checkZeros() {
        for (size_t c = A.numRow(); c != 0;)
            if (allZero(getNonSymbol(A, --c))) {
                // std::cout << "checkZeros c = " << c << std::endl;
                // dump();
                if (lessZero(c))
                    return true;
                eraseConstraint(A, c);
            }
        return false;
    }
    // A'x <= b
    // removes variable `i` from system
    bool removeVariable(IntMatrix &A, const size_t i) {
        fourierMotzkin(A, i);
        // std::cout << "removed i = " << i << "\nA=\n"<<A <<std::endl;
        // dump();
        return checkZeros();
    }
    // A'x <= b
    // E'x = q
    // removes variable `i` from system
    bool removeVariable(IntMatrix &A, IntMatrix &E, const size_t i) {
        if (substituteEquality(A, E, i))
            fourierMotzkin(A, i);
        if (E.numRow() > 1)
            NormalForm::simplifySystem(E);
        if (checkZeros())
            return true;
        pruneBounds(A, E);
        return false;
    }

    bool removeVariable(const size_t i) {
        if constexpr (hasEqualities)
            return removeVariable(A, E, i);
        return removeVariable(A, i);
    }
    void removeVariableAndPrune(const size_t i) {
        if constexpr (hasEqualities){
	    removeVariable(A, E, i);
	} else {
	    removeVariable(A, i);
	}
	pruneBounds();
    }
    
    static void erasePossibleNonUniqueElements(
        IntMatrix &A, llvm::SmallVectorImpl<unsigned> &colsToErase) {
        // std::ranges::sort(colsToErase);
        std::sort(colsToErase.begin(), colsToErase.end());
        for (auto it = std::unique(colsToErase.begin(), colsToErase.end());
             it != colsToErase.begin();) {
            eraseConstraint(A, *(--it));
        }
    }
    void dropEmptyConstraints(IntMatrix &A) const {
        for (size_t c = A.numRow(); c != 0;)
            if (allZero(getNonSymbol(A, --c)))
                eraseConstraint(A, c);
    }
    void dropEmptyConstraints() {
        dropEmptyConstraints(A);
        if constexpr (hasEqualities)
            dropEmptyConstraints(E);
    }

    friend std::ostream &operator<<(std::ostream &os, const Polyhedra &p) {
        auto &&os2 = printConstraints(os, p.A, p.C.getNumConstTerms());
        if constexpr (hasEqualities)
            return printConstraints(os2, p.E, p.C.getNumConstTerms());
        return os2;
    }
    void dump() const { std::cout << *this; }

    bool isEmptyBang() {
        const size_t numVar = getNumVar();
        const size_t numConst = C.getNumConstTerms();
        for (size_t i = 0; i < numVar; ++i)
            if (!allZero(A.getCol(numConst + i)))
                if (removeVariable(numConst + i))
                    return true;
        return false;
    }
    bool isEmpty() const {
        auto B{*this};
        return B.isEmptyBang();
    }
    // A*x <= b
    bool knownSatisfied(llvm::ArrayRef<int64_t> x) const {
        int64_t bc;
        size_t numVar = std::min(x.size(), getNumVar());
        for (size_t c = 0; c < getNumInequalityConstraints(); ++c) {
            bc = A(c, 0);
            for (size_t v = 0; v < numVar; ++v)
                bc -= A(c, v + 1) * x[v];
            if (bc < 0)
                return false;
        }
        return true;
    }
};

typedef Polyhedra<EmptyMatrix<int64_t>, SymbolicComparator> SymbolicPolyhedra;

/*
template <MaybeMatrix I64Matrix>
struct SymbolicPolyhedra : public Polyhedra<I64Matrix> {
    llvm::SmallVector<Polynomial::Monomial> monomials;
    SymbolicPolyhedra(IntMatrix A, I64Matrix E, llvm::ArrayRef<MPoly> b,
                      const PartiallyOrderedSet &poset)
        : Polyhedra<I64Matrix>(std::move(A), std::move(E)), monomials({}) {
        // use poset
        // monomials[{unsigned}_1] >= monomials[{unsigned}_2] + {int64_t}_1
        llvm::SmallVector<std::tuple<int64_t, unsigned, unsigned>> cmps;
        for (auto &bi : b) {
            for (auto &t : bi) {
                if (t.isCompileTimeConstant())
                    continue;
                bool dontPushBack = false;
                const Polynomial::Monomial &tm = t.exponent;
                for (auto &m : monomials) {
                    if (m == tm) {
                        dontPushBack = true;
                        break;
                    }
                }
                if (dontPushBack)
                    continue;
                for (size_t i = 0; i < monomials.size(); ++i) {
                    Polynomial::Monomial &m = monomials[i];
                    // loop inner triangle
                    if ((m.degree() == 1) && (t.degree() == 1)) {
                        // we can do a direct lookup
                        // interval on t - m
                        const Interval itv = poset(m.prodIDs.front().getID(),
                                                   tm.prodIDs.front().getID());
                        if ((itv.lowerBound >
                             (std::numeric_limits<int64_t>::min() >> 2)) &&
                            (itv.lowerBound <
                             (std::numeric_limits<int64_t>::max() >> 2))) {
                            // t - m >= lowerBound
                            // t >= m + lowerBound
                            cmps.emplace_back(itv.lowerBound, monomials.size(),
                                              i);
                        }
                        if ((itv.upperBound >
                             (std::numeric_limits<int64_t>::min() >> 2)) &&
                            (itv.upperBound <
                             (std::numeric_limits<int64_t>::max() >> 2))) {
                            // t - m <= upperBound
                            // m >= t + -upperBound
                            cmps.emplace_back(-itv.upperBound, i,
                                              monomials.size());
                        }
                    } else {
                        auto [itvt, itvm] = poset.unmatchedIntervals(tm, m);
                        // TODO: tighten by using matched to
                        // get less conservative bounds
                        if (itvt.knownGreaterEqual(itvm)) {
                            cmps.emplace_back(0, monomials.size(), i);
                        } else if (itvm.knownGreaterEqual(itvt)) {
                            cmps.emplace_back(0, i, monomials.size());
                        }
                    }
                }
                monomials.push_back(tm);
            }
        }
        // order of vars:
        // constants, loop vars, symbolic vars
        // this is because of hnf prioritizing diagonalizing leading rows
        const size_t origNumRow = A.numRow();
        const size_t origNumCol = A.numCol();
        const size_t numSymbolicVars = monomials.size();
        A.resize(origNumRow + cmps.size(), origNumCol + numSymbolicVars + 1);
    };
};
*/
