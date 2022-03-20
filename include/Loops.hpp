#pragma once

#include "./ArrayReference.hpp"
#include "./BitSets.hpp"
#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Permutation.hpp"
#include "./Symbolics.hpp"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <memory>
#include <utility>

//
// Loop nests
//
// typedef Matrix<Int, MAX_PROGRAM_VARIABLES, 0> RektM;
// typedef Vector<Int, MAX_PROGRAM_VARIABLES> Upperbound;
typedef llvm::SmallVector<MPoly, 3> UpperBounds;
// NOTE: UpperBounds assumes symbols in the monomial products are >= 0.
//       If a number is known to be negative, then it should receive a negative
//       coefficient.
//       This will be known for RectangularLoopNests, as the loop would
//          not iterate if this were false; thus our optimizations can rely on
//          it being true.
//
//       If it is not known for a triangular loop, this must be handled somehow.
//          Perhaps we can still confirm that the loop would not execute for
//          negative values. Otherwise, we require loop splitting.

struct RectangularLoopNest {
    UpperBounds data;

    RectangularLoopNest(size_t nloops) : data(UpperBounds(nloops)){};
    size_t getNumLoops() const { return data.size(); }
    MPoly &getUpperbound(size_t j) { return data[j]; }
    UpperBounds &getUpperbounds() { return data; }
};
// size_t length(RectangularLoopNest rekt) { return length(rekt.data); }

// Upperbound getUpperbound(RectangularLoopNest r, size_t j) {
//     return getCol(r.data, j);
// }

//  perm: og -> transform
// iperm: transform -> og
bool compatible(RectangularLoopNest &l1, RectangularLoopNest &l2,
                Permutation &perm1, Permutation &perm2, size_t _i1,
                size_t _i2) {
    return l1.getUpperbound(perm1(_i1)) == l2.getUpperbound(perm2(_i2));
}

typedef SquareMatrix<Int> TrictM;
// typedef Matrix<Int, 0, 0> TrictM;
// A*i < r
struct TriangularLoopNest {
    SquareMatrix<Int> A;
    RectangularLoopNest r;
    RectangularLoopNest u;
    TriangularLoopNest(size_t nloops)
        : A(SquareMatrix<Int>(nloops)), r(RectangularLoopNest(nloops)),
          u(RectangularLoopNest(nloops)){};

    size_t getNumLoops() const { return r.getNumLoops(); }
    RectangularLoopNest &getRekt() { return r; }
    SquareMatrix<Int> &getTrit() { return A; }
    UpperBounds &getUpperbounds() { return u.data; }

    void fillUpperBounds() {
        size_t nloops = getNumLoops();
        TrictM &A = getTrit();
        UpperBounds &upperBounds = getRekt().getUpperbounds();
        for (size_t i = 1; i < nloops; ++i) {
            for (size_t j = 0; j < i; ++j) {
                Int Aij = A(j, i);
                if (Aij) {
                    upperBounds[i] -= Aij * upperBounds[j];
                }
            }
        }
    }
};

bool otherwiseIndependent(TrictM &A, Int j, Int i) {
    for (Int k = 0; k < Int(A.size(0)); k++)
        if (!((k == i) | (k == j) | (A(k, j) == 0)))
            return false;
    return true;
}

bool zeroMinimum(TrictM &A, Int j, Int _j, Permutation &perm) {
    for (size_t k = j + 1; k < A.size(0); k++) {
        // if A(k, j) >= 0, then j is not lower bounded by k
        if (A(k, j) >= 0)
            continue;
        Int _k = perm.inv(k);
        // A[k,j] < 0 means that `k < C + j`, i.e. `j` has a lower bound of `k`
        bool k_in_perm = _k < _j;
        if (k_in_perm)
            return false;
        // if `k` not in perm, then if `k` has a zero minimum
        // `k` > j`, so it will skip
        if (!zeroMinimum(A, k, _k, perm))
            return false;
    }
    return true;
}
bool upperboundDominates(MPoly &ubi, MPoly &ubj) {
    MPoly delta = ubi - ubj;
    for (auto &term : delta) {
        if (term.coefficient < 0) {
            return false;
        }
    }
    return true;
}

bool zeroInnerIterationsAtMaximum(TrictM &A, MPoly &ub, RectangularLoopNest &r,
                                  Int i) {
    for (Int j = 0; j < i; j++) {
        Int Aij = A(i, j);
        if (Aij >= 0)
            continue;
        if (upperboundDominates(ub, r.getUpperbound(j)))
            return true;
    }
    for (size_t j = i + 1; j < A.size(0); j++) {
        Int Aij = A(i, j);
        if (Aij <= 0)
            continue;
        if (upperboundDominates(ub, r.getUpperbound(j)))
            return true;
    }
    return false;
}

// _i* are indices for the considered order
// perms map these to i*, indices in the original order.
bool compatible(TriangularLoopNest &l1, RectangularLoopNest &l2,
                Permutation &perm1, Permutation &perm2, Int _i1, Int _i2) {
    SquareMatrix<Int> &A = l1.getTrit();
    RectangularLoopNest &r = l1.getRekt();
    Int i = perm1(_i1);

    MPoly &ub1 = r.getUpperbound(i);
    MPoly &ub2 = l2.getUpperbound(perm2(_i2));
    MPoly delta_b = ub1 - ub2;
    // now need to add `A`'s contribution
    PtrVector<unsigned> iperm = perm1.inv();
    // the first loop adds variables that adjust `i`'s bounds
    for (size_t j = 0; j < size_t(i); j++) {
        Int Aij = A(j, i); // symmetric
        if (Aij == 0)
            continue;
        Int _j1 = iperm[j];
        // j1 < _i1 means it is included in the permutation, but rectangular
        // `l2` definitely does not depend on `j` loop!
        if (_j1 < _i1)
            return false;
        // we have i < C - Aᵢⱼ * j

        if (Aij < 0) { // i < C + j*abs(Aij)
            // TODO: relax restriction
            if (!otherwiseIndependent(A, j, i))
                return false;

            fnmadd(delta_b, r.getUpperbound(j), Aij);
            delta_b += Aij;
            // MPoly &ub_temp = ;
            // Vector<Int, MAX_PROGRAM_VARIABLES> ub_temp =
            // for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; k++)
            //     delta_b[k] -= Aij * ub_temp(k);
            // delta_b[0] += Aij;
        } else { // if Aij > 0, i < C - j abs(Aij)
            // Aij > 0 means that `j_lower_bounded_by_k` will be false when
            // `k=i`.
            if (!zeroMinimum(A, j, _j1, perm1))
                return false;
        }
    }
    // the second loop here defines additional bounds on `i`. If `j` below is in
    // the permutation, we can rule out compatibility with rectangular `l2`
    // loop. If it is not in the permutation, then the bound defined by the
    // first loop holds, so no checks/adjustments needed here.
    for (size_t j = i + 1; j < A.size(0); j++) {
        Int Aij = A(j, i);
        if (Aij == 0)
            continue;
        // j1 < _i1 means it is included in the permutation, but rectangular
        // `l2` definitely does not depend on `j` loop!
        if (iperm[j] < _i1)
            return false;
    }
    if (isZero(delta_b)) {
        return true;
    } else if ((delta_b.terms.size() == 1) &&
               (delta_b.leadingCoefficient() == -1)) {
        return zeroInnerIterationsAtMaximum(A, ub2, r, i);
    } else {
        return false;
    }
}

bool compatible(RectangularLoopNest &r, TriangularLoopNest &t,
                Permutation &perm2, Permutation &perm1, Int _i2, Int _i1) {
    return compatible(t, r, perm1, perm2, _i1, _i2);
}

bool updateBoundDifference(MPoly &delta_b, TriangularLoopNest &l1, TrictM &A2,
                           Permutation &perm1, Permutation &perm2, Int _i1,
                           Int i2, bool flip) {
    SquareMatrix<Int> &A1 = l1.getTrit();
    RectangularLoopNest &r1 = l1.getRekt();
    Int i1 = perm1(_i1);
    PtrVector<unsigned> iperm = perm1.inv();
    // the first loop adds variables that adjust `i`'s bounds
    // `j` and `i1` are in original domain.
    for (Int j = 0; j < i1; j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int _j1 = iperm[j];
        // if we're dependent on `j` (_j1 < _i1), we need terms to match
        if ((_j1 < _i1) & (A2(perm2(_j1), i2) != Aij))
            return false;
        if (Aij < 0) {
            if (!otherwiseIndependent(A1, j, i1))
                return false;
            Aij = flip ? -Aij : Aij;
            fnmadd(delta_b, r1.getUpperbound(j), Aij);
            delta_b += Aij;
        } else {
            if (!zeroMinimum(A1, j, _j1, perm1))
                return false;
        }
    }
    return true;
}

bool checkRemainingBound(TriangularLoopNest &l1, TrictM &A2, Permutation &perm1,
                         Permutation &perm2, Int _i1, Int i2) {
    SquareMatrix<Int> &A1 = l1.getTrit();
    Int i1 = perm1(_i1);
    PtrVector<unsigned> iperm = perm1.inv();
    for (size_t j = i1 + 1; j < A1.size(0); j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int _j1 = iperm[j];
        // if we're dependent on `j1`, we require the same coefficient.
        if ((_j1 < _i1) & (A2(perm2(_j1), i2) != Aij))
            return false;
    }
    return true;
}

bool compatible(TriangularLoopNest &l1, TriangularLoopNest &l2,
                Permutation &perm1, Permutation &perm2, Int _i1, Int _i2) {
    SquareMatrix<Int> &A1 = l1.getTrit();
    SquareMatrix<Int> &A2 = l2.getTrit();
    RectangularLoopNest &r1 = l1.getRekt();
    RectangularLoopNest &r2 = l2.getRekt();
    Int i1 = perm1(_i1);
    Int i2 = perm2(_i2);
    MPoly &ub1 = r1.getUpperbound(i1);
    MPoly &ub2 = r2.getUpperbound(i2);
    MPoly delta_b = ub1 - ub2;
    // quick check if invalid
    if (!checkRemainingBound(l1, A2, perm1, perm2, _i1, i2)) {
        return false;
    }
    if (!checkRemainingBound(l2, A1, perm2, perm1, _i2, i1)) {
        return false;
    }
    // now need to add `A`'s contribution
    if (!updateBoundDifference(delta_b, l1, A2, perm1, perm2, _i1, i2, false)) {
        return false;
    }
    if (!updateBoundDifference(delta_b, l2, A1, perm2, perm1, _i2, i1, true)) {
        return false;
    }
    if (isZero(delta_b)) {
        return true;
    } else if (delta_b.terms.size() == 1) {
        Polynomial::Term<Int, Polynomial::Monomial> &lt = delta_b.leadingTerm();
        if (lt.degree()) {
            return false;
        } else if (lt.coefficient == -1) {
            return zeroInnerIterationsAtMaximum(A1, ub2, r1, i1);
        } else if (lt.coefficient == 1) {
            return zeroInnerIterationsAtMaximum(A2, ub1, r2, i2);
        } else {
            return false;
        }
    } else {
        return false;
    }
}

// a'i <= b
struct AffineLE {
    llvm::SmallVector<intptr_t, 4> a;
    MPoly b;

    // Factor out variable `i`, then set l <= u
    AffineLE(const AffineLE &l, const AffineLE &u, size_t i) {
        intptr_t cu = u.a[i];
        intptr_t cl = l.a[i];
        b = cu * l.b;
        Polynomial::fnmadd(b, u.b, cl);
        size_t N = l.a.size();
        a.resize_for_overwrite(N);
        for (size_t n = 0; n < N; ++n) {
            a[n] = cu * l.a[n] - cl * u.a[n];
        }
        a[i] = 0;
    }
    bool operator==(const AffineLE &x) const { return a == x.a && b == x.b; }
};

// c*j <= b - a * i
// For example, let
// c = 1, b = N - 1, a = [1, 0, -2]
// Then we have an upper bound:
// j <= N - 1 - i_0 + 2*i_2
// Or, say we have
// c = -1, b = N - 1, a = [1, 0, -2]
// Then we have a lower bound:
// -j <= N - 1 - i_0 + 2*i_2
// j >= 1 - N + i_0 - 2*i_2
struct AffineCmp {
    llvm::SmallVector<intptr_t, 4> a;
    MPoly b;
    intptr_t c;
    AffineCmp(MPoly m, intptr_t c) : b(m), c(c){};
    AffineCmp(llvm::SmallVector<intptr_t, 4> &&a, MPoly &&b, intptr_t c)
        : a(std::move(a)), b(std::move(b)), c(c) {}
    AffineCmp(llvm::SmallVector<intptr_t, 4> const &a, MPoly const &b,
              intptr_t c)
        : a(a), b(b), c(c) {}

    bool operator==(AffineCmp const &x) const {
        return c == x.c && a == x.a && b == x.b;
    }
    bool operator==(MPoly x) const { return allZero(a) && c * b == x; }
    bool operator==(int x) const {
        if (auto bc = b.getCompileTimeConstant()) {
            return ((c * bc.getValue()) == x) && allZero(a);
        } else {
            return false;
        }
    }

    bool isConstant() const { return (b.degree() == 0) && allZero(a); }
    void subtractUpdateAB(AffineCmp const &x, intptr_t c0, intptr_t c1) {
        b *= c0;
        fnmadd(b, x.b, c1); // y.b -= x.b * c1;
        for (size_t i = 0; i < a.size(); ++i) {
            a[i] = a[i] * c0 - c1 * x.a[i];
        }
        c *= c0;
    }
    void subtractUpdate(AffineCmp const &x, intptr_t a1) {
        intptr_t xc;
        if (x.c < 0) {
            xc = -x.c;
            a1 = -a1;
        } else {
            xc = x.c;
        }
        subtractUpdateAB(x, xc, a1);
    }
    // assumes we're subtracting off one of the bounds
    AffineCmp subtract(AffineCmp const &x, intptr_t a1) const {
        AffineCmp y = *this;
        y.subtractUpdate(x, a1);
        return y;
    }
    // assumes `j0 === j1`
    // aff1 -= aff0
    // yields c*j <= b - a*i
    // where
    // c = abs(c0) * c1
    // b = b1*abs(c0) - c1 * b0
    // a = a1*c0 - a0*c1
    AffineCmp &operator-=(AffineCmp const &x) {
        subtractUpdateAB(x, std::abs(x.c), std::abs(c));
        return *this;
    }
    // Let's say we have
    // aff_1 - aff_0
    // where
    // c_1 = 1, b_1 = U, a_1 = [1,0,-1]
    // c_0 = 1, b_1 = L, a_0 = [1,0, 1]
    // then we get
    // c = c_0*c_1
    // b = c_0*b_1 - c_1*b_0
    // a = c_0*a_1 - c_1*a_0
    // The basic idea is that if we have
    // sign(c)*j <= (b - a*i)/abs(c)
    // if we want to subtract two of the RHS, then we need to scale the denoms
    AffineCmp operator-(AffineCmp const &x) const {
        AffineCmp y = *this;
        y -= x;
        return y;
    }
    /*
    uint8_t superSub(Affine const &x) const {
        uint8_t supSub = 3; // 00000011
        for (size_t i = 0; i < a.size(); ++i){
            intptr_t ai = a[i];
            intptr_t xi = x.a[i];
            if (ai == xi){
                continue;
            } else {

            }
            if (ai | xi){
                // at least one not zero
                if (ai & xi){
                    // both not zero
                    continue;
                } else if (ai){

                } else {
                }
            }
        }
        return supSub;
    }
    */
    friend std::ostream &operator<<(std::ostream &os, const AffineCmp &x) {
        intptr_t sign = 1;
        if (x.c > 0) {
            if (x.c == 1) {
                os << "j <= ";
            } else {
                os << x.c << "j <= ";
            }
            os << x.b;
        } else {
            if (x.c == -1) {
                os << "j >= ";
            } else {
                os << -x.c << "j >= ";
            }
            auto xbn = x.b;
            xbn *= -1;
            os << xbn;
            sign = -1;
        }
        for (size_t i = 0; i < x.a.size(); ++i) {
            if (intptr_t ai = x.a[i] * sign) {
                if (ai > 0) {
                    if (ai == 1) {
                        os << " - i_" << i;
                    } else {
                        os << " - " << ai << " * i_" << i;
                    }
                } else if (ai == -1) {
                    os << " + i_" << i;
                } else {
                    os << " + " << ai * -1 << " * i_" << i;
                }
            }
        }
        return os;
    }
    void dump() const { std::cout << *this << std::endl; }
};

// A' * i <= r
// l are the lower bounds
// u are the upper bounds
// extrema are the extremes, in orig order
struct AffineLoopNest {
    Matrix<Int, 0, 0, 0> A; // somewhat triangular
    llvm::SmallVector<MPoly, 8> b;
    // llvm::SmallVector<unsigned, 8> origLoop;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> lExtrema;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> uExtrema;
    // uint32_t notAffine; // bitmask indicating non-affine loops
    size_t getNumLoops() const { return A.size(0); }
    // AffineLoopNest(Matrix<Int, 0, 0, 0> A, llvm::SmallVector<MPoly, 8> r)
    //     : A(A), b(r) {}
};

struct AffineLoopNestBounds {
    // TODO: Can the lifetimes of the AffineLoopNest and Partiallyorderedset
    // be guaranteed without resorting to shared_ptr?
    // note: bounds (lowerA, upperA, lowerB, upperB) are w/ respect to original
    // order
    std::shared_ptr<AffineLoopNest> aln;
    std::shared_ptr<PartiallyOrderedSet> poset;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> lowerA;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> upperA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 1>, 0> lowerB;
    llvm::SmallVector<llvm::SmallVector<MPoly, 1>, 0> upperB;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> remainingA;
    llvm::SmallVector<llvm::SmallVector<MPoly, 8>, 0> remainingB;
    Permutation perm; // maps current to orig

    size_t getNumLoops() const { return perm.getNumLoops(); }
    AffineLoopNestBounds(std::shared_ptr<AffineLoopNest> aln,
                         std::shared_ptr<PartiallyOrderedSet> poset)
        : aln(aln), poset(poset), lowerA(aln->getNumLoops()),
          upperA(aln->getNumLoops()), lowerB(aln->getNumLoops()),
          upperB(aln->getNumLoops()), remainingA(aln->getNumLoops()),
          remainingB(aln->getNumLoops()), perm(aln->getNumLoops()) {
        size_t numLoops = getNumLoops();
        size_t i = numLoops;
        remainingA[i - 1] = aln->A;
        remainingB[i - 1] = aln->b;
        do {
            calculateBounds(--i);
        } while (i);
    }
    // setBounds(a, b, la, lb, ua, ub, i);
    // set `i` to zero after setting boundsfor lower bounds la and lb,
    // and upper bounds ua and ub.
    static bool setBounds(PtrVector<intptr_t> a, MPoly &b,
                          llvm::ArrayRef<intptr_t> la, const MPoly &lb,
                          llvm::ArrayRef<intptr_t> ua, const MPoly &ub,
                          size_t i) {
        intptr_t cu = ua[i];
        intptr_t cl = la[i];
        b = cu * lb;
        Polynomial::fnmadd(b, ub, cl);
        size_t N = la.size();
        for (size_t n = 0; n < N; ++n) {
            a[n] = cu * la[n] - cl * ua[n];
        }
        a[i] = 0;
        return std::ranges::any_of(a, [](intptr_t ai) { return ai != 0; });
    }

    void swap(size_t _i, size_t _j) {
        if (_i == _j) {
            return;
        }
        perm.swap(_i, _j);
        for (intptr_t i = std::max(_i, _j); i >= intptr_t(std::min(_i, _j));
             --i) {
            calculateBounds(i);
        }
    }
    static inline bool independentOfInner(llvm::ArrayRef<intptr_t> a,
                                          size_t i) {
        for (size_t j = 0; j < a.size(); ++j) {
            if ((a[j] != 0) & (i != j)) {
                return false;
            }
        }
        return true;
    }
    void eliminateVariable(Matrix<intptr_t, 0, 0, 128> &A,
                           llvm::SmallVectorImpl<MPoly> &b,
                           const Matrix<intptr_t, 0, 0, 128> &AOld,
                           const llvm::SmallVectorImpl<MPoly> &bOld,
                           const size_t i) const {
        // eliminate variable `i` according to original order
        const size_t numLoops = getNumLoops();
        size_t numNeg = 0;
        size_t numPos = 0;
        const auto [numVar, numCol] = AOld.size();
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = AOld(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        const size_t numExclude = numCol - numNeg - numPos;
        const size_t numColA = numNeg * numPos + numExclude;
        std::cout << "numCol=" << numCol << "; numNeg=" << numNeg
                  << "; numPos=" << numPos << std::endl;
        A.resizeForOverwrite(numVar, numColA);
        b.resize(numColA);

        // assign to `A = Aold[:,exlcuded]`
        for (size_t j = 0, c = 0; c < numExclude; ++j) {
            if (AOld(i, j)) {
                continue;
            }
            for (size_t k = 0; k < numVar; ++k) {
                A(k, c) = AOld(k, j);
            }
            b[c++] = bOld[j];
        }
        size_t c = numExclude;
        for (size_t u = 0; u < numCol; ++u) {
            if (AOld(i, u) > 0) {
                bool independentOfInnerU =
                    independentOfInner(AOld.getCol(u), i);
                for (size_t l = 0; l < numCol; ++l) {
                    if (AOld(i, l) < 0) {
                        if ((independentOfInnerU &&
                             independentOfInner(AOld.getCol(l), i)) ||
                            (differentAuxiliaries(AOld, l, u, numLoops))) {
                            // if we've eliminated everything, then this bound
                            // does not provide any useful information.
                            //
                            // We also check for differentAuxiliaries, as we
                            // don't care about any bounds comparing them, we
                            // only care about them in isolation.
                            continue;
                        }
                        c += setBounds(A.getCol(c), b[c], AOld.getCol(l),
                                       bOld[l], AOld.getCol(u), bOld[u], i);
                    }
                }
            }
        }
        A.resize(numVar, c);
        b.resize(c);
    }
    static bool differentAuxiliaries(const Matrix<intptr_t, 0, 0, 128> &A,
                                     size_t l, size_t u, size_t start) {
        size_t numVar = A.size(0);
        size_t lFound = false, uFound = false;
        for (size_t i = start; i < numVar; ++i) {
            bool Ail = A(i, l) != 0;
            bool Aiu = A(i, u) != 0;
            // if the opposite was found on a previous iteration, then that must
            // mean both have different auxiliary variables.
            if ((Ail & uFound) | (Aiu & lFound)) {
                return true;
            }
            uFound |= Aiu;
            lFound |= Ail;
        }
        return false;
    }
    // returns std::make_pair(numNeg, numPos);
    static std::pair<size_t, size_t>
    countNonZeroSign(const Matrix<intptr_t, 0, 0, 0> &A, size_t i) {
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
    static void fillBounds(Matrix<intptr_t, 0, 0, 0> &lA,
                           Matrix<intptr_t, 0, 0, 0> &uA,
                           llvm::SmallVectorImpl<MPoly> &lB,
                           llvm::SmallVectorImpl<MPoly> &uB,
                           const Matrix<intptr_t, 0, 0, 0> &A,
                           const llvm::SmallVectorImpl<MPoly> &b, size_t i) {
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

    void fillBounds(const Matrix<intptr_t, 0, 0, 0> &A,
                    const llvm::SmallVectorImpl<MPoly> &b, size_t i) {

        fillBounds(lowerA[i], upperA[i], lowerB[i], upperB[i], A, b, i);
    }
    static void appendBounds(const Matrix<intptr_t, 0, 0, 0> &lA,
                             const Matrix<intptr_t, 0, 0, 0> &uA,
                             const llvm::SmallVectorImpl<MPoly> &lB,
                             const llvm::SmallVectorImpl<MPoly> &uB,
                             Matrix<intptr_t, 0, 0, 0> &A,
                             llvm::SmallVectorImpl<MPoly> &b, size_t i) {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
        auto [numLoops, numCol] = A.size();
        A.resize(numLoops, numCol + numNeg * numPos);
        b.resize_for_overwrite(numCol + numNeg * numPos);
        size_t c = numCol;
        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                c += setBounds(A.getCol(c), b[c], lA.getCol(l), lB[l],
                               uA.getCol(u), uB[u], i);
            }
        }
        A.resize(numLoops, c);
        b.resize(c);
    }
    void appendBounds(Matrix<intptr_t, 0, 0, 0> &A,
                      llvm::SmallVectorImpl<MPoly> &b, size_t i) {
        appendBounds(lowerA[i], upperA[i], lowerB[i], upperB[i], A, b, i);
    }
    void pruneBounds(Matrix<intptr_t, 0, 0, 0> &A,
                     llvm::SmallVector<MPoly, 8> &b, const size_t i) const {

        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        if ((numNeg > 1) | (numPos > 1)) {
            pruneBounds(A, b, i, numNeg, numPos);
        }
    }
    void pruneBounds(Matrix<intptr_t, 0, 0, 0> &Aold,
                     llvm::SmallVector<MPoly, 8> &bold, const size_t i,
                     const size_t numNeg, const size_t numPos) const {
        const size_t numLoops = getNumLoops();
        const size_t numCol = Aold.size(1);
        // if we have multiple bounds, we try to prune
        // First, do we have dependencies in these bounds that must be
        // eliminated?
        intptr_t dependencyToEliminate = -1;
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = Aold(i, j);
            if (Aij) {
                for (size_t k = 0; k < numLoops; ++k) {
                    intptr_t Akj = Aold(k, j);
                    bool depends = ((Akj != 0) & (k != i));
                    dependencyToEliminate =
                        (depends ? k : dependencyToEliminate);
                }
                if (dependencyToEliminate != -1) {
                    break;
                }
            }
        }
        if (dependencyToEliminate >= 0) {
            // hopefully stack allocate scratch space
            const size_t numAuxiliaryVar = bin2(numNeg) + bin2(numPos);
            const size_t numVar = numLoops + numAuxiliaryVar;
            const size_t totalCol = numCol + 2 * numAuxiliaryVar;
            Matrix<intptr_t, 0, 0, 128> AOld(numVar, totalCol);
            Matrix<intptr_t, 0, 0, 128> ANew;
            llvm::SmallVector<MPoly, 16> bOld(totalCol);
            llvm::SmallVector<MPoly, 16> bNew;
            // simple mapping of `k` to particular bounds
            llvm::SmallVector<std::pair<unsigned, unsigned>, 16> boundDiffPairs;
            boundDiffPairs.reserve(numAuxiliaryVar);
            size_t c = numCol;
            for (size_t j = 0; j < numCol; ++j) {
                bOld[j] = bold[j];
                for (size_t k = 0; k < numLoops; ++k) {
                    AOld(k, j) = Aold(k, j);
                }
                bOld[j] = bold[j];
                if (intptr_t Aij = AOld(i, j)) {
                    bool positive = Aij > 0;
                    intptr_t absAij = std::abs(Aij);
                    for (size_t d = j + 1; d < numCol; ++d) {
                        // index Aold as we haven't yet copied d > j to
                        // AOld
                        intptr_t Aid = Aold(i, d);
                        if ((Aid != 0) & ((Aid > 0) == positive)) {
                            intptr_t absAid = std::abs(Aid);
                            // Aij * i <= b_j - a_j*k
                            // Aid * i <= b_d - a_d*k
                            // Aij*abs(Aid) * i <=
                            //        abs(Aid)*b_j - abs(Aid)*a_j*k
                            // abs(Aij)*Aid * i <=
                            //        abs(Aij)*b_d - abs(Aij)*a_d*k
                            //
                            // So, we define bound difference:
                            // bd_jd = (abs(Aid)*b_j - abs(Aid)*a_j*k)
                            //       - (abs(Aij)*b_d - abs(Aij)*a_d*k)
                            //
                            // we now introduce bd_jd as a variable to
                            // our inequalities, by defining both (0)
                            // bd_jd <= (abs(Aid)*b_j - abs(Aid)*a_j*k)
                            //            - (abs(Aij)*b_d -
                            //            abs(Aij)*a_d*k)
                            // (1) bd_jd >= (abs(Aid)*b_j -
                            // abs(Aid)*a_j*k)
                            //            - (abs(Aij)*b_d -
                            //            abs(Aij)*a_d*k)
                            //
                            // These can be rewritten as
                            // (0) bd_jd + abs(Aid)*a_j*k -
                            // abs(Aij)*a_d*k
                            //       <= abs(Aid)*b_j - abs(Aij)*b_d
                            // (1) -bd_jd - abs(Aid)*a_j*k +
                            // abs(Aij)*a_d*k
                            //       <= -abs(Aid)*b_j + abs(Aij)*b_d
                            //
                            // Then, we try to prove that either
                            // bd_jd <= 0, or that bd_jd >= 0
                            //
                            // Note that for Aij>0 (i.e. they're upper
                            // bounds), we want to keep the smaller
                            // bound, as the other will never come into
                            // play. For Aij<0 (i.e., they're lower
                            // bounds), we want the larger bound for the
                            // same reason. However, as solving for `i`
                            // means dividing by a negative number, this
                            // means we're actually looking for the
                            // smaller of `abs(Aid)*b_j -
                            // abs(Aid)*a_j*k` and `abs(Aij)*b_d -
                            // abs(Aij)*a_d*k`. This was the reason for
                            // multiplying by `abs(...)` rather than raw
                            // values, so that we could treat both paths
                            // the same.
                            //
                            // Thus, if bd_jd <= 0, we keep the `j`
                            // bound else if bd_jd >= 0, we keep the `d`
                            // bound else, we must keep both.
                            for (size_t l = 0; l < numLoops; ++l) {
                                intptr_t Alc =
                                    absAid * AOld(l, j) - absAij * Aold(l, d);
                                AOld(l, c) = Alc;
                                AOld(l, c + 1) = -Alc;
                            }
                            MPoly delta = absAij * bold[d];
                            Polynomial::fnmadd(delta, bOld[j], absAid);
                            bOld[c] = -delta;
                            bOld[c + 1] = std::move(delta);
                            size_t newVarId = numLoops + boundDiffPairs.size();
                            AOld(newVarId, c++) = 1;
                            AOld(newVarId, c++) = -1;
                            std::cout << "boundDiffPairs["
                                      << boundDiffPairs.size() << "] = (" << j
                                      << ", " << d << ")" << std::endl;
                            boundDiffPairs.emplace_back(j, d);
                        }
                    }
                }
            }
            llvm::SmallVector<int8_t, 32> provenBoundsDeltas(numAuxiliaryVar,
                                                             0);
            llvm::SmallVector<unsigned, 32> rowsToErase;
            std::cout << "A_pre_elim =\n" << AOld << std::endl;
            std::cout << "b_pre_elim =\n";
            printVector(std::cout, bOld) << std::endl;
            do {
                std::cout << "dependencyToEliminate = " << dependencyToEliminate
                          << std::endl;
                eliminateVariable(ANew, bNew, AOld, bOld,
                                  size_t(dependencyToEliminate));
                std::swap(ANew, AOld);
                std::swap(bNew, bOld);
                std::cout << "A_post_elim =\n" << AOld << std::endl;
                std::cout << "b_post_elim =\n";
                printVector(std::cout, bOld) << std::endl;
                dependencyToEliminate = -1;
                for (size_t j = 0; j < AOld.size(1); ++j) {
                    for (size_t k = numLoops; k < numVar; ++k) {
                        if (intptr_t Akj = AOld(k, j)) {
                            bool dependsOnOthers = false;
                            // note that we don't add equations for
                            // comparing the different bounds diffs,
                            // therefore `AOld(l, j) == 0` for all `l !=
                            // k
                            // && l >= numLoops`
                            for (size_t l = 0; l < numLoops; ++l) {
                                // if ((AOld(l, j) != 0) & (l != i)) {
                                if (AOld(l, j)) {
                                    dependencyToEliminate = l;
                                    dependsOnOthers = true;
                                    break;
                                }
                            }
                            if (!dependsOnOthers) {
                                // we can eliminate this equation, but
                                // check if we can eliminate the bound
                                // if (AOld(i,j) == 0){
                                if (poset->knownGreaterEqualZero(bOld[j])) {
                                    // Akj * k >= 0
                                    std::cout << "bOld[j=" << j
                                              << "] >= 0: " << bOld[j]
                                              << std::endl;
                                    provenBoundsDeltas[k - numLoops] = 1;
                                } else if (poset->knownLessEqualZero(bOld[j])) {
                                    // Akj * k <= 0
                                    std::cout << "bOld[j=" << j
                                              << "] <= 0: " << bOld[j]
                                              << std::endl;
                                    provenBoundsDeltas[k - numLoops] = -1;
                                }
                                // }
                                if ((rowsToErase.size() == 0) ||
                                    (rowsToErase.back() != j)) {
                                    rowsToErase.push_back(j);
                                }
                            }
                        }
                    }
                }
                for (auto it = rowsToErase.rbegin(); it != rowsToErase.rend();
                     ++it) {
                    AOld.eraseRow(*it);
                    bOld.erase(bOld.begin() + (*it));
                }
                rowsToErase.clear();
            } while (dependencyToEliminate >= 0);
            for (size_t l = 0; l < provenBoundsDeltas.size(); ++l) {
                size_t k = provenBoundsDeltas[l];
                // eliminate bound difference k
                // bound difference is j - d
                if (k) {
                    auto [j, d] = boundDiffPairs[l];
                    // if Akj * k >= 0, discard j, else discard d
                    rowsToErase.push_back(k == 1 ? j : d);
                }
            }
            std::cout << "rowsToErase (dominated) = ";
            printVector(std::cout, rowsToErase) << std::endl;
            erasePossibleNonUniqueElements(Aold, bold, rowsToErase);

        } else {
            // no dependencyToEliminate
            llvm::SmallVector<unsigned, 32> rowsToErase;
            rowsToErase.reserve(numNeg * numPos);
            for (size_t j = 0; j < numCol - 1; ++j) {
                if (intptr_t Aij = Aold(i, j)) {
                    for (size_t k = j + 1; k < numCol; ++k) {
                        intptr_t Aik = Aold(i, k);
                        if ((Aik != 0) & ((Aik > 0) == (Aij > 0))) {
                            MPoly delta = bold[k] * std::abs(Aij);
                            Polynomial::fnmadd(delta, bold[j], std::abs(Aik));
                            // delta = k - j
                            if (poset->knownGreaterEqualZero(delta)) {
                                rowsToErase.push_back(k);
                            } else if (poset->knownLessEqualZero(
                                           std::move(delta))) {
                                rowsToErase.push_back(j);
                            }
                        }
                    }
                }
            }
            std::cout << "rowsToErase (dominated) = ";
            printVector(std::cout, rowsToErase) << std::endl;
            erasePossibleNonUniqueElements(Aold, bold, rowsToErase);
        }
    }
    void calculateBounds0() {
        const size_t i = perm(0);
        const auto [numNeg, numPos] = countNonZeroSign(remainingA[0], i);
        if ((numNeg > 1) | (numPos > 1)) {
            Matrix<intptr_t, 0, 0, 0> Aold = remainingA[0];
            llvm::SmallVector<MPoly, 8> bold = remainingB[0];
            pruneBounds(Aold, bold, i, numNeg, numPos);
            fillBounds(Aold, bold, i);
        } else {
            fillBounds(remainingA[0], remainingB[0], i);
            return;
        }
    }
    // `_i` is w/ respect to current order, `i` for original order.
    void calculateBounds(const size_t _i) {
        if (_i == 0) {
            return calculateBounds0();
        }
        const size_t i = perm(_i);
        // const size_t iNext = perm(_i - 1);
        remainingA[_i - 1] = remainingA[_i];
        auto &Aold = remainingA[_i - 1];
        remainingB[_i - 1] = remainingB[_i];
        auto &bold = remainingB[_i - 1];
        std::cout << "init: A(" << i << ") =\n" << Aold << std::endl;
        pruneBounds(Aold, bold, i);
        std::cout << "pruned: A(" << i << ") =\n" << Aold << std::endl;
        fillBounds(Aold, bold, i);
        deleteBounds(Aold, bold, i);
        std::cout << "deleted: A(" << i << ") =\n" << Aold << std::endl;
        appendBounds(Aold, bold, i);
        std::cout << "appended: A(" << i << ") =\n" << Aold << std::endl;
    }
    void deleteBounds(Matrix<intptr_t, 0, 0, 0> &A,
                      llvm::SmallVectorImpl<MPoly> &b, size_t i) {
        llvm::SmallVector<unsigned, 16> deleteBounds;
        for (size_t j = 0; j < b.size(); ++j) {
            if (A(i, j)) {
                // bool doDelete = true;
                // for (size_t k = 0; k < A.size(0); ++k){
                //     if ((k != i) & (A(k,j) != 0)){
                // 	doDelete = false;
                // 	break;
                //     }
                // }
                // if (doDelete){
                //     deleteBounds.push_back(j);
                // } else {
                //     A(i,j) = 0;
                // }
                deleteBounds.push_back(j);
            }
        }
        for (auto it = deleteBounds.rbegin(); it != deleteBounds.rend(); ++it) {
            A.eraseRow(*it);
            b.erase(b.begin() + (*it));
        }
    }
    static void erasePossibleNonUniqueElements(
        Matrix<intptr_t, 0, 0, 0> &A, llvm::SmallVectorImpl<MPoly> &b,
        llvm::SmallVectorImpl<unsigned> &rowsToErase) {
        std::ranges::sort(rowsToErase);
        for (auto it = std::unique(rowsToErase.begin(), rowsToErase.end());
             it != rowsToErase.begin();) {
            --it;
            A.eraseRow(*it);
            b.erase(b.begin() + (*it));
        }
    }
    // returns true if extending (extendLower ? lower : upper) bound of `_i`th
    // loop by `extend` doesn't result in the innermost loop experiencing any
    // extra iterations.
    // if `extendLower`, `min(i) - extend`
    // else `max(i) + extend`
    bool zeroExtraIterationsUponExtending(size_t _i, bool extendLower) const {
        size_t _j = _i + 1;
        const size_t numLoops = getNumLoops();
        if (_j == numLoops) {
            return false;
        }
        // eliminate variables 0..._j
        auto A = remainingA.back();
        auto b = remainingB.back();
        Matrix<intptr_t, 0, 0, 0> lwrA;
        Matrix<intptr_t, 0, 0, 0> uprA;
        llvm::SmallVector<MPoly, 16> lwrB;
        llvm::SmallVector<MPoly, 16> uprB;
        for (size_t _k = 0; _k < _j; ++_k) {
            if (_k != _i) {
                size_t k = perm(_k);
                pruneBounds(A, b, k);
                fillBounds(lwrA, uprA, lwrB, uprB, A, b, k);
                appendBounds(lwrA, uprA, lwrB, uprB, A, b, k);
            }
        }
        Matrix<intptr_t, 0, 0, 0> Anew;
        llvm::SmallVector<MPoly, 8> bnew;
        size_t i = perm(_i);
        do {
            // `A` and `b` contain representation independent of `0..._j`,
            // except for `_i`
            size_t j = perm(_j);
            Anew = A;
            bnew = b;
            for (size_t _k = _i + 1; _k < numLoops; ++_k) {
                if (_k != _j) {
                    size_t k = perm(_k);
                    // eliminate
                    pruneBounds(Anew, bnew, k);
                    fillBounds(lwrA, uprA, lwrB, uprB, Anew, bnew, k);
                    appendBounds(lwrA, uprA, lwrB, uprB, Anew, bnew, k);
                }
            }
            // now depends only on `j` and `i`
            // check if we have zero iterations on loop `j`
            pruneBounds(Anew, bnew, j);
            size_t numCols = Anew.size(1);
            for (size_t l = 0; l < numCols; ++l) {
                intptr_t Ajl = Anew(j, l);
                if (Ajl >= 0) {
                    // then it is not a lower bound
                    continue;
                }
                intptr_t Ail = Anew(i, l);
                for (size_t u = 0; u < numCols; ++u) {
                    intptr_t Aju = Anew(j, u);
                    if (Aju <= 0) {
                        // then it is not an upper bound
                        continue;
                    }
                    intptr_t Aiu = Anew(i, u);
                    intptr_t c = Ajl * Aiu - Aju * Ail;
                    auto delta = bnew[l] * Aju;
                    Polynomial::fnmadd(delta, bnew[u], Ajl);
                    // delta + c * i >= 0 -> iterates at least once
                    if (extendLower) {
                        if (c > 0) {
                            bool doesNotIterate = true;
                            // we're adding to the lower bound
                            for (size_t il = 0; il < numCols; ++il) {
                                intptr_t ail = Anew(i, il);
                                if ((ail >= 0) | (Anew(j, il) != 0)) {
                                    // (ail >= 0) means not a lower bound
                                    // (Anew(j, il) != 0) means the lower bound
                                    // is a function of `j` if we're adding
                                    // beyond what `j` defines as the bound
                                    // here, then `j` won't undergo extra
                                    // iterations, due to being sandwiched
                                    // between this bound, and whatever bound it
                                    // was that defines the extrema we're adding
                                    // to here.
                                    continue;
                                }
                                // recall: ail < 0
                                //
                                // ail * i <= bnew[il]
                                // i >= bnew[il] / ail
                                //
                                // ail * (i - e + e) <= bnew[il]
                                // ail * (i - e) <= bnew[il] - ail*e
                                // (i - e) >= (bnew[il] - ail*e) / ail
                                ///
                                // we want to check
                                // delta + c * (i - e) >= 0
                                // ail * (delta + c * (i - e)) <= 0
                                // ail*delta + c*(ail * (i - e)) <= 0
                                //
                                // let c = c, for brevity
                                //
                                // c*ail*(i-e) <= c*(bnew[il] - ail*e)
                                // we also wish to check
                                // ail*delta + s*c*(ail*(i-e)) <= 0
                                //
                                // ail*delta + c*(ail*(i-e)) <=
                                // ail*delta + c*(bnew[il] - ail*e)
                                // thus, if
                                // ail*delta + c*(bnew[il] - ail*e) <= 0
                                // the loop iterates at least once
                                // we'll check if it is known that this is
                                // false, i.e. if
                                //
                                // ail*delta + c*(bnew[il] - ail*e) - 1 >= 0
                                //
                                // then the following must be
                                // false:
                                // ail*delta + c*(bnew[il] - ail*e) - 1 <= -1
                                auto idelta = ail * delta;
                                Polynomial::fnmadd(idelta, bnew[il], -c);
                                // let e = 1
                                idelta -= c * ail + 1;
                                if (poset->knownGreaterEqualZero(idelta)) {
                                    return true;
                                } else {
                                    doesNotIterate = false;
                                }
                            }
                            if (doesNotIterate) {
                                return true;
                            }
                        } else {
                            continue;
                        }

                    } else {
                        // extend upper
                        if (c < 0) {
                            bool doesNotIterate = true;
                            // does `imax + e` iterate at least once?
                            for (size_t il = 0; il < numCols; ++il) {
                                intptr_t ail = Anew(i, il);
                                if ((ail <= 0) | (Anew(j, il) != 0)) {
                                    // not an upper bound
                                    continue;
                                }
                                // ail > 0, c < 0
                                // ail * i <= ubi
                                // c*ail*i >= c*ubi
                                // c*ail*(i+e-e) >= c*ubi
                                // c*ail*(i+e) >= c*ubi + c*ail*e
                                //
                                // iterates at least once if:
                                // delta + c * (i+e) >= 0
                                // ail*(delta + c * (i+e)) >= 0
                                // ail*delta + ail*c*(i+e) >= 0
                                // note
                                // ail*delta + ail*c*(i+e) >=
                                // ail*delta + c*ubi + c*ail+e
                                // so proving
                                // ail*delta + c*ubi + c*ail+e >= 0
                                // proves the loop iterates at least once
                                // we check if this is known to be false, i.e.
                                // if this is known to be true:
                                // - ail*delta - c*ubi - c*ail+e -1 >= 0
                                auto idelta = (-ail) * delta;
                                Polynomial::fnmadd(idelta, bnew[il], c);
                                idelta -= c * ail - 1;
                                if (poset->knownGreaterEqualZero(idelta)) {
                                    return true;
                                } else {
                                    doesNotIterate = false;
                                }
                                /*
                                auto idelta = ail * delta;
                                Polynomial::fnmadd(idelta, bnew[il], -c);
                                idelta += c * ail + 1;
                                if (poset->knownLessThanZero(
                                        std::move(idelta))) {
                                    return true;
                                } else {
                                    doesNotIterate = false;
                                }
                                */
                            }
                            if (doesNotIterate) {
                                return true;
                            }
                        } else {
                            continue;
                        }
                    }
                }
            }
            ++_j;
        } while (_j != numLoops);
        return false;
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const AffineLoopNestBounds &alnb) {
        const size_t numLoops = alnb.getNumLoops();
        for (size_t _i = 0; _i < numLoops; ++_i) {
            os << "Loop " << _i << " lower bounds: " << std::endl;
            size_t i = alnb.perm(_i);
            // size_t _i = alnb.perm(Permutation::Original{i});
            auto &lb = alnb.lowerB[i];
            auto &lA = alnb.lowerA[i];
            for (size_t j = 0; j < lb.size(); ++j) {
                if (lA(i, j) == -1) {
                    os << "i_" << i << " >= ";
                } else {
                    os << -lA(i, j) << "*i_" << i << " >= ";
                }
                os << -lb[j];
                for (size_t k = 0; k < numLoops; ++k) {
                    if (k == i) {
                        continue;
                    }
                    if (intptr_t lakj = lA(k, j)) {
                        if (lakj > 0) {
                            os << " + ";
                        } else {
                            os << " - ";
                        }
                        lakj = std::abs(lakj);
                        if (lakj != 1) {
                            os << lakj << "*";
                        }
                        os << "i_" << k;
                    }
                }
                os << std::endl;
            }
            os << "Loop " << _i << " upper bounds: " << std::endl;
            auto &ub = alnb.upperB[i];
            auto &uA = alnb.upperA[i];
            for (size_t j = 0; j < ub.size(); ++j) {
                if (uA(i, j) == 1) {
                    os << "i_" << i << " <= ";
                } else {
                    os << uA(i, j) << "*i_" << i << " <= ";
                }
                os << ub[j];
                for (size_t k = 0; k < numLoops; ++k) {
                    if (k == i) {
                        continue;
                    }
                    if (intptr_t uakj = uA(k, j)) {
                        if (uakj > 0) {
                            os << " - ";
                        } else {
                            os << " + ";
                        }
                        uakj = std::abs(uakj);
                        if (uakj != 1) {
                            os << uakj << "*";
                        }
                        os << "i_" << k;
                    }
                }
                os << std::endl;
            }
        }
        return os;
    }
    void dump() const { std::cout << *this; }
};
/*
// Affine structs `a` are w/ respect original `A`;
// inds will need to go through `perm.inv(...)`.
//
// stores loops under current perm, so lc/uc are
// under the current permutation
// extrema are original
struct AffineLoopNestPerm {
    std::shared_ptr<AffineLoopNest> aln;
    llvm::SmallVector<llvm::SmallVector<AffineCmp, 2>, 4> lc;
    llvm::SmallVector<llvm::SmallVector<AffineCmp, 2>, 4> uc;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> maxIters;
    Permutation perm;   // maps current to orig
    uint32_t notAffine; // bitmask indicating non-affine loops
    // may be smaller than aln->getNumLoops();
    size_t getNumLoops() const { return perm.getNumLoops(); }
    AffineLoopNestPerm(std::shared_ptr<AffineLoopNest> a)
        : aln(a), perm(a->getNumLoops()) {
        // perm.init();
        lc.resize(getNumLoops());
        uc.resize(getNumLoops());
        for (size_t i = getNumLoops(); i > 0; --i) {
            cacheBounds(i - 1);
        }
        if (aln->lExtrema.size() == 0) {
            for (size_t i = 0; i < getNumLoops(); ++i) {
                calcLowerExtrema(i);
                calcUpperExtrema(i);
            }
        }
    }
    void swap(PartiallyOrderedSet const &poset, intptr_t i, intptr_t j) {
        perm.swap(i, j);
        for (intptr_t k = std::max(i, j); k >= std::min(i, j); --k) {
            lc[k].clear();
            uc[k].clear();
            cacheBounds(k);
            pruneBounds(k, poset);
        }
    }
    // Comments on pruning legality...
    // for m in 0:M-1 {
    //   for n in m+1:M-1 {
    //      // m != M-1
    //   }
    // }
    // 0 <= m <= M-1
    // m+1 <= n <= M-1
    // permute
    // 1 <= m+1 <= n <= M-1 // when we depend on an inner loop, take extreme
    // 0 <= m <= M-1
    //      m <= n-1
    // prune first upper bound on `M`, because n-1<=M-2, yielding
    // 1 <= n <= M-1
    // 0 <= m <= n-1
    // and now we never have `m = M-1`, but we did before...
    // if we had
    // for m in 0:M-1 {
    //   x[m] = 1
    //   for n in m+1:M-1 {
    //     A[m,n] = 1
    //   }
    // }
    // for n in 0:M-1 {
    //   x[n] = 1
    //   for m in 0:n-1 {
    //      A[m,n] = 1
    //   }
    // }
    intptr_t pruneBound(llvm::SmallVectorImpl<AffineCmp> &a,
                        PartiallyOrderedSet const &poset, intptr_t o) {
        for (size_t i = o; i < a.size() - 1; ++i) {
            for (size_t j = i + 1; j < a.size(); ++j) {
                llvm::SmallVector<MPoly, 2> bounds;
                AffineCmp delta = a[i] - a[j];
                bounds.push_back(std::move(delta.b));
                // 0 <= b - a'i;
                // a'i <= b;
                auto &lExtrema = aln->lExtrema;
                auto &uExtrema = aln->uExtrema;
                for (size_t k = 0; k < delta.a.size(); ++k) {
                    if (intptr_t ak = delta.a[k]) {
                        size_t added = bounds.size();
                        llvm::SmallVector<MPoly, 2> &lExt = lExtrema[k];
                        llvm::SmallVector<MPoly, 2> &uExt = uExtrema[k];
                        bounds.reserve(added * (lExt.size() + uExt.size()));

                        for (size_t l = 0; l < added; ++l) {
                            MPoly &bl = bounds[l];
                            // add all extrema
                            for (size_t ll = 0; ll < lExt.size(); ++ll) {
                                MPoly bu = bl;
                                fnmadd(bu, lExt[ll], ak);
                                bounds.push_back(std::move(bu));
                            }
                            for (size_t uu = 0; uu < uExt.size() - 1; ++uu) {
                                MPoly bu = bl;
                                fnmadd(bu, uExt[uu], ak);
                                bounds.push_back(std::move(bu));
                            }
                            fnmadd(bl, uExt[uExt.size() - 1], ak);
                        }
                    }
                }
                uint8_t mask = 3;
                for (auto &b : bounds) {
                    if (!isZero(b)) {
                        if (poset.knownGreaterEqualZero(b)) {
                            mask &= 1;
                        } else {
                            // TODO: write knownCmpZero to do both known >=
                            // 0 and <= 0 (as well as maybe == 0, < 0, and >
                            // 0
                            b *= -1;
                            if (poset.knownGreaterEqualZero(b)) {
                                mask &= 2;
                            } else {
                                mask = 0;
                                break;
                            }
                        }
                        if (mask == 0) {
                            break;
                        }
                    } else {
                        mask = 0;
                        break;
                    }
                }
                if (mask & 1) {
                    // positive (or equal)
                    a.erase(a.begin() + i);
                    return i;
                } else if (mask & 2) {
                    // negative
                    a.erase(a.begin() + j);
                    return i;
                }
                // if lower bounds, then the intersection is the largest, so
                // prune `j` if delta >= 0 at both extrema `i` if delta <= 0
                // at both extrema if upper bounds, then the intersection is
                // the smallest, so prune `j` if delta >= 0 at both extrema
                // `i` if delta <= 0 at both extrema
            }
        }
        return -1;
    }
    void pruneABound(llvm::SmallVectorImpl<AffineCmp> &a,
                     PartiallyOrderedSet const &poset) {
        intptr_t o = 0;
        if (a.size() > 1) {
            do {
                o = pruneBound(a, poset, o);
            } while (o >= 0);
        }
    }
    void pruneBounds(intptr_t k, PartiallyOrderedSet const &poset) {
        pruneABound(lc[k], poset);
        pruneABound(uc[k], poset);
    }
    void pruneBounds(PartiallyOrderedSet const &poset) {
        for (size_t k = 0; k < lc.size(); ++k) {
            pruneABound(lc[k], poset);
            pruneABound(uc[k], poset);
        }
    }
    static bool pruneDiffs(llvm::SmallVector<MPoly, 2> &bv, intptr_t sign) {
        for (auto it = bv.begin(); it != bv.end() - 1; ++it) {
            for (auto ii = it + 1; ii != bv.end(); ++ii) {
                auto delta = *it - *ii;
                if (isZero(delta)) {
                    bv.erase(ii);
                    return true;
                } else if (delta.degree() == 0) {
                    if (delta.leadingTerm().coefficient * sign > 0) {
                        bv.erase(it);
                    } else {
                        bv.erase(ii);
                    }
                    return true;
                }
            }
        }
        return false;
    }
    // prunes larger values, leaves minimum
    static bool pruneMin(llvm::SmallVector<MPoly, 2> &bv) {
        return pruneDiffs(bv, 1);
    }
    // prunes smaller values, leaves maximum
    static bool pruneMax(llvm::SmallVector<MPoly, 2> &bv) {
        return pruneDiffs(bv, -1);
    }
    static void extremaUpdate(llvm::SmallVector<MPoly, 2> &bv,
                              llvm::SmallVector<MPoly, 2> const &jBounds,
                              intptr_t aba, size_t bvStart) {
        size_t bvStop = bv.size();
        size_t jbl = jBounds.size() - 1;
        // in case of multiple extrema, calc/compare all of them
        for (size_t k = bvStart; k < bvStop; ++k) {
            for (size_t l = 0; l < jbl; ++l) {
                MPoly bvk = bv[k]; // copy
                fnmadd(bvk, jBounds[l], aba);
                bv.push_back(std::move(bvk));
            }
            // modify original
            fnmadd(bv[k], jBounds[jbl], aba);
        }
    }
    static void extremaUpdate(llvm::SmallVector<MPoly, 2> &bv,
                              llvm::SmallVector<MPoly, 2> const &jBounds,
                              intptr_t aba, size_t bvStart,
                              MPoly const &offset) {
        size_t bvStop = bv.size();
        size_t jbl = jBounds.size() - 1;
        // in case of multiple extrema, calc/compare all of them
        for (size_t k = bvStart; k < bvStop; ++k) {
            for (size_t l = 0; l < jbl; ++l) {
                MPoly bvk = bv[k]; // copy
                Polynomial::Terms<intptr_t, Polynomial::Monomial> jBound =
                    jBounds[l] + offset;
                fnmadd(bvk, jBound, aba);
                bv.push_back(std::move(bvk));
            }
            // modify original
            Polynomial::Terms<intptr_t, Polynomial::Monomial> jBound =
                jBounds[jbl] + offset;
            fnmadd(bv[k], jBound, aba);
        }
    }
    static void extremaUpdate(llvm::SmallVector<MPoly, 2> &bv,
                              llvm::SmallVector<MPoly, 2> const &jBounds,
                              intptr_t aba, size_t bvStart, intptr_t offset) {
        size_t bvStop = bv.size();
        size_t jbl = jBounds.size() - 1;
        // in case of multiple extrema, calc/compare all of them
        for (size_t k = bvStart; k < bvStop; ++k) {
            for (size_t l = 0; l < jbl; ++l) {
                MPoly bvk = bv[k]; // copy
                Polynomial::Terms<intptr_t, Polynomial::Monomial> jBound =
                    jBounds[k];
                jBound += offset;
                fnmadd(bvk, jBound, aba);
                bv.push_back(std::move(bvk));
            }
            Polynomial::Terms<intptr_t, Polynomial::Monomial> jBound =
                jBounds[jbl];
            jBound += offset;
            // modify original
            fnmadd(bv[k], jBound, aba);
        }
    }
    // For example:
    // j <= N - i + k
    // for the extrema, you need the minimum value of `i` and the maximum of
    // `k`
    void calcExtrema(llvm::SmallVector<MPoly, 2> &bv, AffineCmp const &ab) {
        size_t bvStart = bv.size();
        bv.push_back(ab.b);
        auto &lExtrema = aln->lExtrema;
        auto &uExtrema = aln->uExtrema;
        // TODO: maybe don't use extrema.
        // For example, in `j <= N - i + k`, what if we also have loop `l`
        // and `i in l:N+l`, `k in l:M+l`, and `l in a:b`.
        // Then, by taking the minimum of `i` and maximum of `k`, we get
        // j <= N - a + M+b = N - a + M + b
        // when we should instead, by keeping the `l` symbol, we'd have
        // j <= N - l + M + l = N + M
        // The extrema are a cache that already substituted everything.
        // So, instead of using this cache, we should just do the full
        // symbolic calculation here.
        for (size_t _j = 0; _j < ab.a.size(); ++_j) {
            if (intptr_t aba = -ab.a[_j]) {
                // need the largest (a*i - b) [ c is negative ]
                if (aba > 0) {
                    extremaUpdate(bv, uExtrema[_j], aba, bvStart);
                } else {
                    extremaUpdate(bv, lExtrema[_j], aba, bvStart);
                }
            }
        }
    }
    void calcExtremaMin(llvm::SmallVector<MPoly, 2> &bv, AffineCmp const &ab,
                        MPoly const &extend, size_t extendInd,
                        bool extendLower) {
        size_t bvStart = bv.size();
        bv.push_back(ab.b);
        auto &lExtrema = aln->lExtrema;
        auto &uExtrema = aln->uExtrema;
        for (size_t _j = 0; _j < ab.a.size(); ++_j) {
            if (_j == extendInd) {
                // this means we're extending in `ex
                if (intptr_t aba = ab.a[_j]) {
                    if (aba > 0) {
                        // uExtrema is most problematic, therefore if
                        // extending lower we extend by minimum amount (-1)
                        if (extendLower) {
                            extremaUpdate(bv, lExtrema[_j], aba, bvStart, -1);
                        } else {
                            extremaUpdate(bv, uExtrema[_j], aba, bvStart,
                                          extend);
                        }
                    } else {
                        // lExtrema is most problematic, therefore if
                        // extending upper we extend by minimum amount (1)
                        if (extendLower) {
                            extremaUpdate(bv, lExtrema[_j], aba, bvStart,
                                          extend);
                        } else {
                            extremaUpdate(bv, uExtrema[_j], aba, bvStart, 1);
                        }
                    }
                }
            } else {
                if (intptr_t aba = -ab.a[_j]) {
                    // need the largest (a*i - b) [ c is negative ]
                    if (aba < 0) {
                        extremaUpdate(bv, uExtrema[_j], aba, bvStart);
                    } else {
                        extremaUpdate(bv, lExtrema[_j], aba, bvStart);
                    }
                }
            }
        }
    }
    void calcLowerExtrema(size_t i) {
        llvm::SmallVector<MPoly, 2> bv;
        // c*j <= b - a'i
        for (auto &ab : lc[i]) {
            calcExtrema(bv, ab);
        }
        while (pruneMax(bv)) {
        }
        aln->lExtrema.push_back(std::move(bv));
    }
    void calcUpperExtrema(size_t i) {
        llvm::SmallVector<MPoly, 2> bv;
        // c*j <= b - a'i
        for (auto &ab : uc[i]) {
            std::cout << ab << std::endl;
            calcExtrema(bv, ab);
        }
        while (pruneMin(bv)) {
        }
        aln->uExtrema.push_back(std::move(bv));
    }
    // zeroIterationsUponExtending(size_t i, bool lower);
    // i is the loop we're trying to pad with extra iterations below the
    // minimum (if lower = true) or above the maximum (upper = true) for the
    // sake of making it compatible with other loops. It returns `true` if
    // upon adding extra iterations, the inner most loop does not iterate.
    // This would be because, for any of the loops interior to `i`, the
    // lower bound exceeds the upper bound.
    bool zeroIterations(PartiallyOrderedSet &poset, AffineCmp &upper,
                        AffineCmp &lower, MPoly const &extend, size_t extendInd,
                        bool extendLower) {
        AffineCmp delta = upper;
        delta.subtractUpdateAB(lower, lower.c, upper.c);
        delta.b -= 1;
        llvm::SmallVector<MPoly, 2> bv;
        // must minimize subtracted `i`s.
        calcExtremaMin(bv, delta, extend, extendInd, extendLower);
        for (auto &b : bv) {
            if (!poset.knownGreaterEqualZero(b)) {
                return false;
            }
        }
        return true;
    }
    bool zeroExtraIterationsUponExtending(PartiallyOrderedSet &poset,
                                          MPoly const &extend, bool lower,
                                          size_t _i, size_t j) {

        llvm::SmallVector<AffineCmp, 2> &ucj = uc[j];
        llvm::SmallVector<AffineCmp, 2> &lcj = lc[j];
        for (auto &ucjk : ucj) {
            for (auto &lcjk : lcj) {
                if (!zeroIterations(poset, ucjk, lcjk, extend, _i, lower)) {
                    return false;
                }
            }
        }
        return true;
    }
    // `i` is the current loop
    bool zeroExtraIterationsUponExtending(PartiallyOrderedSet &poset, size_t i,
                                          const MPoly &extend, bool lower) {
        // if `i` is the inner most loop, then padding it must add loops
        size_t _i = perm(i);
        for (size_t j = i + 1; j < getNumLoops(); ++j) {
            if (zeroExtraIterationsUponExtending(poset, extend, lower, _i, j)) {
                return true;
            }
        }
        return false;
    }
    // i is current order
    void cacheBounds(size_t i) {
        const auto &A = aln->A;
        const auto &r = aln->r;
        const auto &origLoop = aln->origLoop;
        auto [numLoops, numEquations] = A.size();
        llvm::SmallVector<AffineCmp, 2> &lowerBoundsAff = lc[i];
        llvm::SmallVector<AffineCmp, 2> &upperBoundsAff = uc[i];
        size_t _i = perm(i);
        for (size_t j = 0; j < numEquations; ++j) {
            // if original loop equation `j` was bound to was external
            // and it is still external under this permutation, we
            // can ignore this equation(?)
            // NOTE: need to ensure that an operation occuring at some
            // partial level of a nest is executed the correct number of
            // times after reordering. Also, if nothing is occuring at this
            // level, perhaps we can trim some iterations from this level,
            // if those interior loops do not iterate for some values of
            // this outer loop.
            if (perm.inv(origLoop[j]) > i) {
                continue;
            }
            if (Int Aij = A(_i, j)) {
                // we have found a bound
                llvm::SmallVector<AffineCmp, 2> *bounds;
                if (Aij > 0) {
                    bounds = &upperBoundsAff;
                } else {
                    bounds = &lowerBoundsAff;
                }
                size_t init = bounds->size();
                bounds->emplace_back(
                    llvm::SmallVector<intptr_t, 4>(numLoops, intptr_t(0)), r[j],
                    Aij);
                for (size_t _k = 0; _k < numLoops; ++_k) {
                    if (_k == _i) {
                        continue;
                    }
                    if (Int Akj = A(_k, j)) {
                        size_t k = perm.inv(_k);
                        if (k > i) {
                            // k external to i
                            llvm::SmallVector<AffineCmp, 2> *kAff;
                            // NOTE: this means we have to cache innermost
                            // loops first.
                            if (Akj > 0) {
                                kAff = &lc[k];
                            } else {
                                kAff = &uc[k];
                            }
                            subtractGroup(bounds, kAff, Akj, init);
                            // size_t nkb = kAff->size();
                            // assert(nkb > 0);
                            // size_t S = bounds->size();
                            // bounds->reserve(S + (S - init) * nkb);
                            // for (size_t id = init; id < S; ++id) {
                            //     Affine &b0 = (*bounds)[id];
                            //     // TODO: what is Akj's relation here?
                            //     for (size_t idk = 0; idk < nkb - 1;
                            //     ++idk) {
                            //         bounds->push_back(
                            //             b0.subtract((*kAff)[idk], Akj));
                            //     }
                            //     b0.subtractUpdate((*kAff)[nkb - 1], Akj);
                            // }
                        } else {
                            // k internal to i
                            for (size_t e = init; e < bounds->size(); ++e) {
                                (*bounds)[e].a[_k] = Akj;
                            }
                        }
                    }
                }
            }
        }
        for (auto &b : lowerBoundsAff) {
            for (size_t _j = 0; _j < b.a.size(); ++_j) {
                if (perm.inv(_j) >= i) {
                    b.a[_j] = 0;
                }
            }
        }
        for (auto &b : upperBoundsAff) {
            for (size_t _j = 0; _j < b.a.size(); ++_j) {
                if (perm.inv(_j) >= i) {
                    b.a[_j] = 0;
                }
            }
        }
        // TODO: prune dominated bounds. Need to check that the pruned
        // bounds are always dominated.
    }
    void subtractGroup(llvm::SmallVector<AffineCmp, 2> *bounds,
                       llvm::SmallVector<AffineCmp, 2> *kAff, intptr_t Akj,
                       size_t init) {
        size_t nkb = kAff->size();
        assert(nkb > 0);
        size_t S = bounds->size();
        bounds->reserve(S + (S - init) * nkb);
        for (size_t id = init; id < S; ++id) {
            AffineCmp &b0 = (*bounds)[id];
            // TODO: what is Akj's relation here?
            for (size_t idk = 0; idk < nkb - 1; ++idk) {
                bounds->push_back(b0.subtract((*kAff)[idk], Akj));
            }
            b0.subtractUpdate((*kAff)[nkb - 1], Akj);
        }
    }
    // returns optional; contains orthogonalized if orthogonalization
    // succeeded.

    friend std::ostream &operator<<(std::ostream &os,
                                    const AffineLoopNestPerm &alnp) {
        for (size_t i = 0; i < alnp.getNumLoops(); ++i) {
            os << "Loop " << i << " lower bounds: " << std::endl;
            for (auto &b : alnp.lc[i]) {
                os << b << std::endl;
            }
            os << "Loop " << i << " upper bounds: " << std::endl;
            for (auto &b : alnp.uc[i]) {
                os << b << std::endl;
            }
        }
        return os;
    }
    void dump() const { std::cout << *this; }
};
*/
// returns unsigned integer as bitfield.
// `1` indicates loop does iterate, `0` does not.
// uint32_t zeroInnerIterationsAtMaximum(AffineLoopNest &aln, size_t i){

//     return 0;
// }

// TODO: it would be most useful if `compatible` could return some indicator
// of to what degree the loops are compatible. For example, perhaps it is
// worth masking one loop off? Perhaps simple an indicator of if the
// difference in iterations is statically known, and then additionally
// whether masking inner levels of the nest is needed (if those inner levels
// have zero iterations for the added iterations of an outer loop, they
// don't need to be masked off).
/*
bool compatible(PartiallyOrderedSet &poset, AffineLoopNestPerm &aln1,
                AffineLoopNestPerm &aln2, Int _i1, Int _i2) {
    auto &l1 = aln1.lc[_i1];
    auto &u1 = aln1.uc[_i1];
    auto &l2 = aln2.lc[_i2];
    auto &u2 = aln2.uc[_i2];
    // these bounds have already been pruned, so if lengths > 1...
    // we don't necessarily want bipartite matching, as it is possible for
one
    // loop to match multiple. E.g., if inner loops iterate zero times for
    // certain values of the iteration variable, we may be able to extend
the
    // loop. Or we could mask a loop off for a small number of iterations.

    // How to proceed here? Match all permutations of the former with all
    // permutations of the latter?
    // Perhaps only consider lengths?
    // llvm::SmallVector<Affine,1> delta1;
    // for (auto &al1 : l1){
    //	for (auto &au1 : u1){
    //	    delta1.push_back(au1 - al1);
    //	}
    // }
    llvm::SmallVector<Affine, 1> delta2;
    for (auto &al2 : l2) {
        for (auto &au2 : u2) {
            delta2.push_back(au2 - al2);
        }
    }
    llvm::SmallVector<Affine, 1> deltadelta;
    // for (auto &d1 : delta1){
    for (auto &al1 : l1) {
        for (auto &au1 : u1) {
            auto d1 = au1 - al1;
            for (auto &d2 : delta2) {
                auto dd = d2 - d1;
            }
        }
    }
    return false;
}
*/
/*
bool compatible(AffineLoopNest &l1, AffineLoopNest &l2, Permutation &perm1,
                Permutation &perm2, Int _i1, Int _i2) {
    Matrix<Int,0,0> &A1 = l1.A;
    Matrix<Int,0,0> &A2 = l2.A;
    RectangularLoopNest &r1 = l1.r;
    RectangularLoopNest &r2 = l2.r;
    Int i1 = perm1(_i1);
    Int i2 = perm2(_i2);
    MPoly &ub1 = r1.getUpperbound(i1);
    MPoly &ub2 = r2.getUpperbound(i2);
    MPoly delta_b = ub1 - ub2;
    // now need to add `A`'s contribution
    if (!updateBoundDifference(delta_b, l1, A2, perm1, perm2, _i1, i2,
false)) { return false;
    }
    if (!updateBoundDifference(delta_b, l2, A1, perm2, perm1, _i2, i1,
true)) { return false;
    }
    if (!checkRemainingBound(l1, A2, perm1, perm2, _i1, i2)) {
        return false;
    }
    if (!checkRemainingBound(l2, A1, perm2, perm1, _i2, i1)) {
        return false;
    }
    if (isZero(delta_b)) {
        return true;
    } else if (delta_b.terms.size() == 1) {
        Polynomial::Term<Int, Polynomial::Monomial> &lt =
delta_b.leadingTerm(); if (lt.degree()) { return false; } else if
(lt.coefficient == -1) { return zeroInnerIterationsAtMaximum(A1, ub2, r1,
i1); } else if (lt.coefficient == 1) { return
zeroInnerIterationsAtMaximum(A2, ub1, r2, i2); } else { return false;
        }
    } else {
        return false;
    }
}
*/
// Not necessarily convertible to `TriangularLoopNest`, e.g.
// [-1  0 ] [ m   < [ 1
//   1  0     n       M
//   0 -1             1
//   0  1             N ]
//   0  1             N ]

// struct GenericLoopNest{
//     llvm::SmallVector<MPoly, 4> l;
//     llvm::SmallVector<MPoly, 4> u;

// };

//
// template <typename T, typename S>
// bool compatible(T l1, S l2, PermutationSubset p1, PermutationSubset p2) {
//     return compatible(l1, l2, p1.p, p2.p, p1.subset_size,
//     p2.subset_size);
// }
