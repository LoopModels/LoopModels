#pragma once
#include "math.hpp"
#include "symbolics.hpp"
#include <llvm/ADT/SmallVector.h>

//
// Loop nests
//
// typedef Matrix<Int, MAX_PROGRAM_VARIABLES, 0> RektM;
// typedef Vector<Int, MAX_PROGRAM_VARIABLES> Upperbound;

typedef Polynomial::Multivariate<intptr_t,Polynomial::Monomial> MPoly;
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

    RectangularLoopNest(size_t nloops) : data(UpperBounds(nloops)) {};
};
size_t getNumLoops(RectangularLoopNest const &data){ return data.data.size(); }
// size_t length(RectangularLoopNest rekt) { return length(rekt.data); }

// Upperbound getUpperbound(RectangularLoopNest r, size_t j) {
//     return getCol(r.data, j);
// }
MPoly &getUpperbound(RectangularLoopNest &r, size_t j) {
    return r.data[j];
}

//  perm: og -> transform
// iperm: transform -> og
bool compatible(RectangularLoopNest &l1, RectangularLoopNest &l2,
                Permutation &perm1, Permutation &perm2, size_t _i1, size_t _i2) {
    return getUpperbound(l1, perm1(_i1)) == getUpperbound(l2, perm2(_i2));
}

typedef SquareMatrix<Int> TrictM;
// typedef Matrix<Int, 0, 0> TrictM;
// A*i < r
struct TriangularLoopNest {
    SquareMatrix<Int> A;
    RectangularLoopNest r;
    RectangularLoopNest u;
    TriangularLoopNest(size_t nloops) : A(SquareMatrix<Int>(nloops)), r(RectangularLoopNest(nloops)), u(RectangularLoopNest(nloops)) {};
};

size_t getNumLoops(const TriangularLoopNest &t){ return getNumLoops(t.r); }
RectangularLoopNest &getRekt(TriangularLoopNest &tri) {
    return tri.r;
    // return RectangularLoopNest(tri.raw, tri.nloops);
}

SquareMatrix<Int> &getTrit(TriangularLoopNest &tri) {
    return tri.A;
    // TrictM A(tri.raw + length(getRekt(tri)), tri.nloops, tri.nloops);
    // return A;
}



UpperBounds &getUpperbounds(RectangularLoopNest &r) { return r.data; }
UpperBounds &getUpperbounds(TriangularLoopNest &tri) {
    return tri.u.data;
}

void fillUpperBounds(TriangularLoopNest &tri) {
    size_t nloops = getNumLoops(tri);
    TrictM &A = getTrit(tri);
    UpperBounds &upperBounds = getUpperbounds(getRekt(tri));
    for (size_t i = 1; i < nloops; ++i) {
        for (size_t j = 0; j < i; ++j) {
            Int Aij = A(j, i);
	    if (Aij){
		upperBounds[i] -= Aij * upperBounds[j];
	    }
        }
    }
}


bool otherwiseIndependent(TrictM &A, Int j, Int i) {
    for (Int k = 0; k < j; k++)
        if (A(k, j))
            return false; // A is symmetric
    for (size_t k = j + 1; k < size(A, 0); k++)
        if (!((k == size_t(i)) | (A(k, j) == 0)))
            return false;
    return true;
}

bool zeroMinimum(TrictM &A, Int j, Int _j, Permutation &perm) {
    for (size_t k = j + 1; k < size(A, 0); k++) {
	// if A(k, j) >= 0, then j is not lower bounded by k
        if (A(k, j) >= 0)
            continue;
        auto _k = inv(perm, k);
        // A[k,j] < 0 means that `k < C + j`, i.e. `j` has a lower bound of `k`
        auto k_in_perm = _k < _j;
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
    for (auto &term : delta){
	if (term.coefficient < 0){
	    return false;
	}
    }
    return true;
}


bool zeroInnerIterationsAtMaximum(TrictM &A, MPoly &ub,
                                  RectangularLoopNest &r, Int i) {
    for (auto j = 0; j < i; j++) {
        auto Aij = A(i, j);
        if (Aij >= 0)
            continue;
        if (upperboundDominates(ub, getUpperbound(r, j)))
            return true;
    }
    for (size_t j = i + 1; j < size(A, 0); j++) {
        auto Aij = A(i, j);
        if (Aij <= 0)
            continue;
        if (upperboundDominates(ub, getUpperbound(r, j)))
            return true;
    }
    return false;
}

// _i* are indices for the considered order
// perms map these to i*, indices in the original order.
bool compatible(TriangularLoopNest &l1, RectangularLoopNest &l2,
                Permutation &perm1, Permutation &perm2, Int _i1, Int _i2) {
    SquareMatrix<Int>& A = getTrit(l1);
    RectangularLoopNest& r = getRekt(l1);
    auto i = perm1(_i1);
    
    MPoly& ub1 = getUpperbound(r, i);
    MPoly& ub2 = getUpperbound(l2, perm2(_i2));
    auto delta_b = ub1 - ub2;
    // now need to add `A`'s contribution
    auto iperm = inv(perm1);
    // the first loop adds variables that adjust `i`'s bounds
    for (size_t j = 0; j < size_t(i); j++) {
        auto Aij = A(j, i); // symmetric
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
	    
	    fnmadd(delta_b, getUpperbound(r, j), Aij);
	    delta_b += Aij;
	    // MPoly &ub_temp = ;
            // Vector<Int, MAX_PROGRAM_VARIABLES> ub_temp = 
            // for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; k++)
            //     delta_b[k] -= Aij * ub_temp(k);
            // delta_b[0] += Aij;
        } else { // if Aij > 0 i < C - j abs(Aij)
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
    for (size_t j = i + 1; j < size(A, 0); j++) {
        auto Aij = A(j, i);
        if (Aij == 0)
            continue;
        // j1 < _i1 means it is included in the permutation, but rectangular
        // `l2` definitely does not depend on `j` loop!
        if (iperm[j] < _i1)
            return false;
    }
    if (isZero(delta_b)){
	return true;
    } else if ((delta_b.terms.size() == 1) && (delta_b.leadingCoefficient() == -1)){
	return zeroInnerIterationsAtMaximum(A, ub2, r, i);
    } else {
	return false;
    }
}

bool compatible(RectangularLoopNest &r, TriangularLoopNest &t, Permutation &perm2,
                Permutation &perm1, Int _i2, Int _i1) {
    return compatible(t, r, perm1, perm2, _i1, _i2);
}

bool updateBoundDifference(MPoly &delta_b,
                           TriangularLoopNest &l1, TrictM &A2, Permutation &perm1,
                           Permutation &perm2, Int _i1, Int i2, bool flip) {
    SquareMatrix<Int>& A1 = getTrit(l1);
    RectangularLoopNest& r1 = getRekt(l1);
    auto i1 = perm1(_i1);
    auto iperm = inv(perm1);
    // the first loop adds variables that adjust `i`'s bounds
    for (Int j = 0; j < i1; j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int _j1 = iperm[j];
        if ((_j1 < _i1) & (A2(perm2(_j1), i2) != Aij))
            return false;
        if (Aij < 0) {
            if (!otherwiseIndependent(A1, j, i1))
                return false;
            Aij = flip ? -Aij : Aij;
	    fnmadd(delta_b, getUpperbound(r1, j), Aij);
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
    auto A1 = getTrit(l1);
    auto i1 = perm1(_i1);
    auto iperm = inv(perm1);
    for (size_t j = i1 + 1; j < size(A1, 0); j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int j1 = iperm[j];
        if ((j1 < _i1) & (A2(perm2(j1), i2) != Aij))
            return false;
    }
    return true;
}

bool compatible(TriangularLoopNest &l1, TriangularLoopNest &l2, Permutation &perm1,
                Permutation &perm2, Int _i1, Int _i2) {
    SquareMatrix<Int> &A1 = getTrit(l1);
    RectangularLoopNest &r1 = getRekt(l1);
    SquareMatrix<Int> &A2 = getTrit(l2);
    RectangularLoopNest &r2 = getRekt(l2);
    Int i1 = perm1(_i1);
    Int i2 = perm2(_i2);
    MPoly &ub1 = getUpperbound(r1, i1);
    MPoly &ub2 = getUpperbound(r2, i2);
    MPoly delta_b = ub1 - ub2;
    // now need to add `A`'s contribution
    if (!updateBoundDifference(delta_b, l1, A2, perm1, perm2, _i1, i2, false)){
        return false;
    }
    if (!updateBoundDifference(delta_b, l2, A1, perm2, perm1, _i2, i1, true)){
        return false;
    }
    if (!checkRemainingBound(l1, A2, perm1, perm2, _i1, i2)){
        return false;
    }
    if (!checkRemainingBound(l2, A1, perm2, perm1, _i2, i1)){
        return false;
    }
    if (isZero(delta_b)){
	return true;
    } else if (delta_b.terms.size() == 1){
	Polynomial::Term<Int,Polynomial::Monomial> &lt = delta_b.leadingTerm();
	if (lt.degree()){
	    return false;
	} else if (lt.coefficient == -1){
	    return zeroInnerIterationsAtMaximum(A1, ub2, r1, i1);
	} else if (lt.coefficient == 1){
	    return zeroInnerIterationsAtMaximum(A2, ub1, r2, i2);
	} else {
	    return false;
	}
    } else {
	return false;
    }
}

// A * i < r
// l are the lower bounds
// u are the upper bounds
struct AffineLoopNest {
    Matrix<Int,0,0> A;
    RectangularLoopNest r;
    RectangularLoopNest l;
    RectangularLoopNest u;
};
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
