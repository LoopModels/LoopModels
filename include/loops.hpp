#pragma once
#include "bitsets.hpp"
#include "ir.hpp"
#include "math.hpp"
#include "poset.hpp"
#include "symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
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
    llvm::ArrayRef<unsigned> iperm = perm1.inv();
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
    llvm::ArrayRef<unsigned> iperm = perm1.inv();
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
    llvm::ArrayRef<unsigned> iperm = perm1.inv();
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
struct Affine {
    llvm::SmallVector<intptr_t, 4> a;
    MPoly b;
    intptr_t c;
    Affine(MPoly m, intptr_t c) : b(m), c(c){};
    Affine(llvm::SmallVector<intptr_t, 4> &&a, MPoly &&b, intptr_t c)
        : a(std::move(a)), b(std::move(b)), c(c) {}
    Affine(llvm::SmallVector<intptr_t, 4> const &a, MPoly const &b, intptr_t c)
        : a(a), b(b), c(c) {}

    bool operator==(Affine const &x) const {
        return c == x.c && a == x.a && b == x.b;
    }
    bool operator==(MPoly x) const { return allZero(a) && c * b == x; }
    bool operator==(int x) const {
        return (b.degree() == 0) && (b.leadingCoefficient() == x) && allZero(a);
    }
    bool isConstant() const { return (b.degree() == 0) && allZero(a); }
    void subtractUpdateAB(Affine const &x, intptr_t c0, intptr_t c1) {
        b *= c0;
        fnmadd(b, x.b, c1); // y.b -= x.b * c1;
        for (size_t i = 0; i < a.size(); ++i) {
            a[i] = a[i] * c0 - c1 * x.a[i];
        }
        c *= c0;
    }
    void subtractUpdate(Affine const &x, intptr_t a1) {
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
    Affine subtract(Affine const &x, intptr_t a1) const {
        Affine y = *this;
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
    Affine &operator-=(Affine const &x) {
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
    Affine operator-(Affine const &x) const {
        Affine y = *this;
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
    friend std::ostream &operator<<(std::ostream &os, const Affine &x) {
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
    Matrix<Int, 0, 0> A; // somewhat triangular
    llvm::SmallVector<MPoly, 8> r;
    llvm::SmallVector<unsigned, 8> origLoop;
    llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> lExtrema;
    llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> uExtrema;
    uint32_t notAffine; // bitmask indicating non-affine loops
    size_t getNumLoops() const { return A.size(0); }
    AffineLoopNest(Matrix<Int, 0, 0> A, llvm::SmallVector<MPoly, 8> r)
        : A(A), r(r) {
        assert(A.size(0) * 2 == A.size(1));
        origLoop.reserve(A.size(0) * 2);
        for (unsigned i = 0; i < A.size(0); ++i) {
            origLoop.push_back(i);
            origLoop.push_back(i);
        }
    }
    AffineLoopNest(Matrix<Int, 0, 0> A, llvm::SmallVector<MPoly, 8> r,
                   llvm::SmallVector<unsigned, 8> origLoop)
        : A(A), r(r), origLoop(origLoop) {}
};

// Affine structs `a` are w/ respect original `A`;
// inds will need to go through `perm.inv(...)`.
//
// stores loops under current perm, so lc/uc are
// under the current permutation
// extrema are original
struct AffineLoopNestPerm {
    std::shared_ptr<AffineLoopNest> aln;
    llvm::SmallVector<llvm::SmallVector<Affine, 2>, 4> lc;
    llvm::SmallVector<llvm::SmallVector<Affine, 2>, 4> uc;
    // llvm::SmallVector<llvm::SmallVector<MPoly, 2>, 4> maxIters;
    Permutation perm;   // maps current to orig
    uint32_t notAffine; // bitmask indicating non-affine loops
    // may be smaller than aln->getNumLoops();
    size_t getNumLoops() const { return perm.getNumLoops(); }
    AffineLoopNestPerm(std::shared_ptr<AffineLoopNest> a)
        : aln(a), perm(a->getNumLoops()) {
        init();
    }
    void init() {
        perm.init();
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
    intptr_t pruneBound(llvm::SmallVectorImpl<Affine> &a,
                        PartiallyOrderedSet const &poset, intptr_t o) {
        for (size_t i = o; i < a.size() - 1; ++i) {
            for (size_t j = i + 1; j < a.size(); ++j) {
                llvm::SmallVector<MPoly, 2> bounds;
                Affine delta = a[i] - a[j];
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
                            // TODO: write knownCmpZero to do both known >= 0
                            // and <= 0 (as well as maybe == 0, < 0, and > 0
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
                // prune `j` if delta >= 0 at both extrema `i` if delta <= 0 at
                // both extrema if upper bounds, then the intersection is the
                // smallest, so prune `j` if delta >= 0 at both extrema `i` if
                // delta <= 0 at both extrema
            }
        }
        return -1;
    }
    void pruneABound(llvm::SmallVectorImpl<Affine> &a,
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
    static bool pruneMin(llvm::SmallVector<MPoly, 2> &bv) {
        return pruneDiffs(bv, 1);
    }
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
    // for the extrema, you need the minimum value of `i` and the maximum of `k`
    void calcExtrema(llvm::SmallVector<MPoly, 2> &bv, Affine const &ab) {
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
    void calcExtremaMin(llvm::SmallVector<MPoly, 2> &bv, Affine const &ab,
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
            calcExtrema(bv, ab);
        }
        while (pruneMin(bv)) {
        }
        aln->uExtrema.push_back(std::move(bv));
    }
    // zeroIterationsUponExtending(size_t i, bool lower);
    // i is the loop we're trying to pad with extra iterations below the
    // minimum (if lower = true) or above the maximum (upper = true) for the
    // sake of making it compatible with other loops. It returns `true` if upon
    // adding extra iterations, the inner most loop does not iterate. This would
    // be because, for any of the loops interior to `i`, the lower bound exceeds
    // the upper bound.
    bool zeroIterations(PartiallyOrderedSet &poset, Affine &upper,
                        Affine &lower, MPoly const &extend, size_t extendInd,
                        bool extendLower) {
        Affine delta = upper;
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

        llvm::SmallVector<Affine, 2> &ucj = uc[j];
        llvm::SmallVector<Affine, 2> &lcj = lc[j];
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
        llvm::SmallVector<Affine, 2> &lowerBoundsAff = lc[i];
        llvm::SmallVector<Affine, 2> &upperBoundsAff = uc[i];
        size_t _i = perm(i);
        for (size_t j = 0; j < numEquations; ++j) {
            // if original loop equation `j` was bound to was external
            // and it is still external under this permutation, we
            // can ignore this equation(?)
            // NOTE: need to ensure that an operation occuring at some partial
            // level of a nest is executed the correct number of times after
            // reordering.
            // Also, if nothing is occuring at this level, perhaps we can
            // trim some iterations from this level, if those interior loops
            // do not iterate for some values of this outer loop.
            if (perm.inv(origLoop[j]) > i) {
                continue;
            }
            if (Int Aij = A(_i, j)) {
                // we have found a bound
                llvm::SmallVector<Affine, 2> *bounds;
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
                            llvm::SmallVector<Affine, 2> *kAff;
                            // NOTE: this means we have to cache innermost loops
                            // first.
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
                            //     for (size_t idk = 0; idk < nkb - 1; ++idk) {
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
        // TODO: prune dominated bounds. Need to check that the pruned bounds
        // are always dominated.
    }
    void subtractGroup(llvm::SmallVector<Affine, 2> *bounds,
                       llvm::SmallVector<Affine, 2> *kAff, intptr_t Akj,
                       size_t init) {
        size_t nkb = kAff->size();
        assert(nkb > 0);
        size_t S = bounds->size();
        bounds->reserve(S + (S - init) * nkb);
        for (size_t id = init; id < S; ++id) {
            Affine &b0 = (*bounds)[id];
            // TODO: what is Akj's relation here?
            for (size_t idk = 0; idk < nkb - 1; ++idk) {
                bounds->push_back(b0.subtract((*kAff)[idk], Akj));
            }
            b0.subtractUpdate((*kAff)[nkb - 1], Akj);
        }
    }
    // returns `true` if orthogonalization succeeded.
    bool orthogonalize(ArrayRef& ai, size_t loop0, size_t loop1){
	// need to construct matrix `A` of relationship
	// B*L = I
	// where L are the loop induct variables, and I are the array indices
	// e.g., if we have `C[i + j, j]`, then
	// B = [1 1; 0 1]
	// additionally, the loop is defined by the bounds
	// A*L = A*(B\^-1 * I) <= r
	// assuming that `B` is an invertible integer matrix,
	// which we can check via `lufact(B)`, and confirming that
	// the determinant == 1 or determinant == -1.
	// If so, we can then use the lufactorizationm for computing
	// A/B, to get loop bounds in terms of the indexes.
	return false;
    }
    
    friend std::ostream &operator<<(std::ostream &os,
                                    const AffineLoopNestPerm &alnp) {
        for (size_t i = 0; i < alnp.getNumLoops(); ++i) {
            auto lbs = alnp.lc[i];
            auto ubs = alnp.uc[i];
            os << "Loop " << i << " lower bounds: " << std::endl;
            for (auto &b : lbs) {
                os << b << std::endl;
            }
            os << "Loop " << i << " upper bounds: " << std::endl;
            for (auto &b : ubs) {
                os << b << std::endl;
            }
        }
        return os;
    }
    void dump() const { std::cout << *this; }
};

// returns unsigned integer as bitfield.
// `1` indicates loop does iterate, `0` does not.
// uint32_t zeroInnerIterationsAtMaximum(AffineLoopNest &aln, size_t i){

//     return 0;
// }

// TODO: it would be most useful if `compatible` could return some indicator of
// to what degree the loops are compatible. For example, perhaps it is worth
// masking one loop off?
// Perhaps simple an indicator of if the difference in iterations is statically
// known, and then additionally whether masking inner levels of the nest is
// needed (if those inner levels have zero iterations for the added iterations
// of an outer loop, they don't need to be masked off).
/*
bool compatible(PartiallyOrderedSet &poset, AffineLoopNestPerm &aln1,
                AffineLoopNestPerm &aln2, Int _i1, Int _i2) {
    auto &l1 = aln1.lc[_i1];
    auto &u1 = aln1.uc[_i1];
    auto &l2 = aln2.lc[_i2];
    auto &u2 = aln2.uc[_i2];
    // these bounds have already been pruned, so if lengths > 1...
    // we don't necessarily want bipartite matching, as it is possible for one
    // loop to match multiple. E.g., if inner loops iterate zero times for
    // certain values of the iteration variable, we may be able to extend the
    // loop. Or we could mask a loop off for a small number of iterations.

    // How to proceed here? Match all permutations of the former with all
    // permutations of the latter?
    // Perhaps only consider lengths?
    // llvm::SmallVector<Affine,1> delta1;
    // for (auto &al1 : l1){
    // 	for (auto &au1 : u1){
    // 	    delta1.push_back(au1 - al1);
    // 	}
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
    if (!updateBoundDifference(delta_b, l1, A2, perm1, perm2, _i1, i2, false)) {
        return false;
    }
    if (!updateBoundDifference(delta_b, l2, A1, perm2, perm1, _i2, i1, true)) {
        return false;
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
//     return compatible(l1, l2, p1.p, p2.p, p1.subset_size, p2.subset_size);
// }
