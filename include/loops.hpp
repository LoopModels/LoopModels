#include "math.hpp"

//
// Loop nests
//
typedef Matrix<Int, MAX_PROGRAM_VARIABLES, 0> RektM;
typedef Vector<Int, MAX_PROGRAM_VARIABLES> Upperbound;

struct RectangularLoopNest {
    RektM data;

    RectangularLoopNest(Int *ptr, size_t nloops) : data(RektM(ptr, nloops)) {
        assert(nloops <= MAX_NUM_LOOPS);
    };
};

size_t length(RectangularLoopNest rekt) { return length(rekt.data); }

Upperbound getUpperbound(RectangularLoopNest r, size_t j) {
    return getCol(r.data, j);
}

//  perm: og -> transform
// iperm: transform -> og
bool compatible(RectangularLoopNest l1, RectangularLoopNest l2,
                Permutation perm1, Permutation perm2, Int _i1, Int _i2) {
    auto i1 = perm1(_i1);
    auto i2 = perm2(_i2);
    auto u1 = getUpperbound(l1, i1);
    auto u2 = getUpperbound(l2, i2);
    for (size_t i = 0; i < MAX_PROGRAM_VARIABLES; i++) {
        if (u1(i) != u2(i))
            return false;
    }
    return true;
}

typedef Matrix<Int, 0, 0> TrictM;
// A*i < r
struct TriangularLoopNest {
    Int *raw;
    size_t nloops;

    TriangularLoopNest(Int *ptr, size_t nloops) : raw(ptr), nloops(nloops) {
        assert(nloops <= MAX_NUM_LOOPS);
    };
};

RectangularLoopNest getRekt(TriangularLoopNest tri) {
    return RectangularLoopNest(tri.raw, tri.nloops);
}

TrictM getTrit(TriangularLoopNest tri) {
    TrictM A(tri.raw + length(getRekt(tri)), tri.nloops, tri.nloops);
    return A;
}

size_t length(TriangularLoopNest tri) {
    return length(getTrit(tri)) + 2 * length(getRekt(tri));
}

RektM getUpperbound(RectangularLoopNest r) { return r.data; }
RektM getUpperbound(TriangularLoopNest tri) {
    Int *ptr = tri.raw + length(getTrit(tri)) + length(getRekt(tri));
    return Matrix<Int, MAX_PROGRAM_VARIABLES, 0>(ptr, tri.nloops);
}

void fillUpperBounds(TriangularLoopNest tri) {
    size_t nloops = tri.nloops;
    RektM r = getRekt(tri).data;
    TrictM A = getTrit(tri);
    RektM upperBounds = getUpperbound(tri);
    for (size_t i = 0; i < length(r); ++i) {
        upperBounds[i] = r[i];
    }
    for (size_t i = 1; i < nloops; ++i) {
        for (size_t j = 0; j < i; ++j) {
            Int Aij = A(j, i);
            for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; ++k) {
                upperBounds(k, i) -= Aij * upperBounds(k, j);
            }
        }
    }
}

bool otherwiseIndependent(TrictM A, Int j, Int i) {
    for (Int k = 0; k < j; k++)
        if (A(k, j))
            return false; // A is symmetric
    for (size_t k = j + 1; k < size(A, 0); k++)
        if (!((k == size_t(i)) | (A(k, j) == 0)))
            return false;
    return true;
}

bool zeroMinimum(TrictM A, Int j, Int _j, Permutation perm) {
    for (size_t k = j + 1; k < size(A, 0); k++) {
        auto j_lower_bounded_by_k = A(k, j) < 0;
        if (!j_lower_bounded_by_k)
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

bool upperboundDominates(Upperbound ubi, Upperbound ubj) {
    bool all_le = true;
    for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; k++) {
        all_le &= (ubi(k) >= ubj(k));
    }
    return all_le;
}

bool zeroInnerIterationsAtMaximum(TrictM A, Upperbound ub,
                                  RectangularLoopNest r, Int i) {
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
bool compatible(TriangularLoopNest l1, RectangularLoopNest l2,
                Permutation perm1, Permutation perm2, Int _i1, Int _i2) {
    auto A = getTrit(l1);
    auto r = getRekt(l1);
    auto i = perm1(_i1);

    auto ub1 = getUpperbound(r, i);
    auto ub2 = getUpperbound(l2, perm2(_i2));
    Int delta_b[MAX_PROGRAM_VARIABLES];
    for (size_t j = 0; j < MAX_PROGRAM_VARIABLES; j++)
        delta_b[j] = ub1(j) - ub2(j);
    // now need to add `A`'s contribution
    auto iperm = inv(perm1);
    // the first loop adds variables that adjust `i`'s bounds
    for (size_t j = 0; j < size_t(i); j++) {
        auto Aij = A(j, i); // symmetric
        if (Aij == 0)
            continue;
        Int _j1 = iperm(j);
        // j1 < _i1 means it is included in the permutation, but rectangular
        // `l2` definitely does not depend on `j` loop!
        if (_j1 < _i1)
            return false;
        // we have i < C - Aᵢⱼ * j

        if (Aij < 0) { // i < C + j*abs(Aij)
            // TODO: relax restriction
            if (!otherwiseIndependent(A, j, i))
                return false;
            Vector<Int, MAX_PROGRAM_VARIABLES> ub_temp = getUpperbound(r, j);
            for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; k++)
                delta_b[k] -= Aij * ub_temp(k);
            delta_b[0] += Aij;
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
        if (iperm(j) < _i1)
            return false;
    }
    if (delta_b[0] == 0)
        return allzero(delta_b, MAX_PROGRAM_VARIABLES);
    if ((delta_b[0] == -1) && allzero(delta_b + 1, MAX_PROGRAM_VARIABLES - 1))
        return zeroInnerIterationsAtMaximum(A, ub2, r, i);
    return false;
}

bool compatible(RectangularLoopNest r, TriangularLoopNest t, Permutation perm2,
                Permutation perm1, Int _i2, Int _i1) {
    return compatible(t, r, perm1, perm2, _i1, _i2);
}

bool updateBoundDifference(Int delta_b[MAX_PROGRAM_VARIABLES],
                           TriangularLoopNest l1, TrictM A2, Permutation perm1,
                           Permutation perm2, Int _i1, Int i2, bool flip) {
    auto A1 = getTrit(l1);
    auto r1 = getRekt(l1);
    auto i1 = perm1(_i1);
    auto iperm = inv(perm1);
    // the first loop adds variables that adjust `i`'s bounds
    for (Int j = 0; j < i1; j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int _j1 = iperm(j);
        if ((_j1 < _i1) & (A2(perm2(_j1), i2) != Aij))
            return false;
        if (Aij < 0) {
            if (!otherwiseIndependent(A1, j, i1))
                return false;
            auto ub_temp = getUpperbound(r1, j);
            Aij = flip ? -Aij : Aij;
            for (size_t k = 0; k < MAX_PROGRAM_VARIABLES; k++)
                delta_b[k] -= Aij * ub_temp(k);
            delta_b[0] += Aij;
        } else {
            if (!zeroMinimum(A1, j, _j1, perm1))
                return false;
        }
    }
    return true;
}

bool checkRemainingBound(TriangularLoopNest l1, TrictM A2, Permutation perm1,
                         Permutation perm2, Int _i1, Int i2) {
    auto A1 = getTrit(l1);
    auto i1 = perm1(_i1);
    auto iperm = inv(perm1);
    for (size_t j = i1 + 1; j < size(A1, 0); j++) {
        Int Aij = A1(j, i1);
        if (Aij == 0)
            continue;
        Int j1 = iperm(j);
        if ((j1 < _i1) & (A2(perm2(j1), i2) != Aij))
            return false;
    }
    return true;
}

bool compatible(TriangularLoopNest l1, TriangularLoopNest l2, Permutation perm1,
                Permutation perm2, Int _i1, Int _i2) {
    auto A1 = getTrit(l1);
    auto r1 = getRekt(l1);
    auto A2 = getTrit(l2);
    auto r2 = getRekt(l2);
    auto i1 = perm1(_i1);
    auto i2 = perm2(_i2);
    auto ub1 = getUpperbound(r1, i1);
    auto ub2 = getUpperbound(r2, i2);
    Int delta_b[MAX_PROGRAM_VARIABLES];
    for (size_t j = 0; j < MAX_PROGRAM_VARIABLES; j++)
        delta_b[j] = ub1(j) - ub2(j);
    // now need to add `A`'s contribution
    if (!updateBoundDifference(delta_b, l1, A2, perm1, perm2, _i1, i2, false))
        return false;
    if (!updateBoundDifference(delta_b, l2, A1, perm2, perm1, _i2, i1, true))
        return false;

    if (!checkRemainingBound(l1, A2, perm1, perm2, _i1, i2))
        return false;
    if (!checkRemainingBound(l2, A1, perm2, perm1, _i2, i1))
        return false;

    auto delta_b_0 = delta_b[0];
    if (delta_b_0 == 0)
        return allzero(delta_b, MAX_PROGRAM_VARIABLES);
    else if (delta_b_0 == -1)
        return allzero(delta_b + 1, MAX_PROGRAM_VARIABLES - 1) &&
               zeroInnerIterationsAtMaximum(A1, ub2, r1, i1);
    else if (delta_b_0 == 1)
        return allzero(delta_b + 1, MAX_PROGRAM_VARIABLES - 1) &&
               zeroInnerIterationsAtMaximum(A2, ub1, r2, i2);
    else
        return false;
}

template <typename T, typename S>
bool compatible(T l1, S l2, PermutationSubset p1, PermutationSubset p2) {
    return compatible(l1, l2, p1.p, p2.p, p1.subset_size, p2.subset_size);
}

