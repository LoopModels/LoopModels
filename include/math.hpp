#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <utility>
#include <vector>

const size_t MAX_NUM_LOOPS = 16;
const size_t MAX_PROGRAM_VARIABLES = 32;
typedef intptr_t Int;

template <typename V> // generic function working with many stl containers, e.g. std::vector
inline size_t length(V &v) { return v.size(); } 



// `show` doesn't print a new line by convention.
template <typename T> void showln(T x) {
    show(x);
    std::printf("\n");
}

//
// Vectors
//
template <typename T, size_t M> struct Vector {

    T *ptr;

    Vector(T *ptr) : ptr(ptr){};

    T &operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
#endif
        return ptr[i];
    }
};
template <typename T> struct Vector<T, 0> {
    T *ptr;
    const size_t len;
    Vector(T *ptr, size_t m) : ptr(ptr), len(m){};
    Vector(std::vector<T>& x) : ptr(&x.front()), len(x.size()){};

    T &operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < len));
#endif
        return ptr[i];
    }
};

template <typename T, size_t M> size_t length(Vector<T, M>) { return M; }
template <typename T> size_t length(Vector<T, 0> v) { return v.len; }

template <typename T, size_t M> void show(Vector<T, M> v) {
    for (size_t i = 0; i < length(v); i++) {
        std::cout << v(i) << ", ";
    }
    if (length(v)){
        std::cout << v(length(v) - 1);
    }
}

template <typename T> Vector<T, 0> toVector(std::vector<T>& x) {
    return Vector<T, 0>(x);
}

template <typename T> bool allzero(T a, size_t len) {
    for (size_t j = 0; j < len; j++)
        if (a[j] != 0)
            return false;
    return true;
}

//
// Matrix
//
template <typename T, size_t M, size_t N> struct Matrix {
    T *ptr;

    Matrix(T *ptr) : ptr(ptr){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
        assert((0 <= j) & (j < N));
#endif
        return ptr[i + j * M];
    }
};
template <typename T, size_t M> struct Matrix<T, M, 0> {
    T *ptr;

    size_t N;

    Matrix(T *ptr, size_t n) : ptr(ptr), N(n){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
        assert((0 <= j) & (j < N));
#endif
        return ptr[i + j * M];
    }
};
template <typename T, size_t N> struct Matrix<T, 0, N> {
    T *ptr;

    size_t M;

    Matrix(T *ptr, size_t m) : ptr(ptr), M(m){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
        assert((0 <= j) & (j < N));
#endif
        return ptr[i + j * M];
    }
};
template <typename T> struct Matrix<T, 0, 0> {
    T *ptr;

    size_t M;
    size_t N;

    Matrix(T *ptr, size_t m, size_t n) : ptr(ptr), M(m), N(n){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
        assert((0 <= j) & (j < N));
#endif
        return ptr[i + j * M];
    }
};

template <typename T, size_t M, size_t N>
size_t size(Matrix<T, M, N>, size_t i) {
    return i == 0 ? M : N;
}
template <typename T, size_t M> size_t size(Matrix<T, M, 0> A, size_t i) {
    return i == 0 ? M : A.N;
}
template <typename T, size_t N> size_t size(Matrix<T, 0, N> A, size_t i) {
    return i == 0 ? A.M : N;
}
template <typename T> size_t size(Matrix<T, 0, 0> A, size_t i) {
    return i == 0 ? A.M : A.N;
}

template <typename T, size_t M, size_t N> size_t length(Matrix<T, M, N> A) {
    return size(A, 0) * size(A, 1);
}

template <typename T, size_t M, size_t N>
Vector<T, M> getCol(Matrix<T, M, N> A, size_t i) {
    return Vector<T, M>(A.ptr + i * size(A, 0));
}
template <typename T, size_t N>
Vector<T, 0> getCol(Matrix<T, 0, N> A, size_t i) {
    auto s1 = size(A, 0);
    return Vector<T, 0>(A.ptr + i * s1, s1);
}

template <typename T, size_t M, size_t N> void show(Matrix<T, M, N> A) {
    for (size_t i = 0; i < size(A, 0); i++) {
        for (size_t j = 0; j < size(A, 1); j++) {
            std::printf("%17d", A(i, j));
        }
    }
}

template <typename T> struct StrideMatrix {
    T *ptr;
    size_t M;
    size_t N;
    size_t S;

    StrideMatrix(T *ptr, size_t M, size_t N, size_t S)
        : ptr(ptr), M(M), N(N), S(S){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
        assert((0 <= j) & (j < N));
#endif
        return ptr[i + j * S];
    }
};
template <typename T> size_t size(StrideMatrix<T> A, size_t i) {
    return i == 0 ? A.M : A.N;
}
template <typename T> size_t length(StrideMatrix<T> A) {
    return size(A, 0) * size(A, 1);
}
template <typename T> void show(StrideMatrix<T> A) {
    for (size_t i = 0; i < size(A, 0); i++) {
        for (size_t j = 0; j < size(A, 1); j++) {
            std::printf("%17d", A(i, j));
        }
    }
}
// template <typename T> size_t getstride1(Matrix<T> A) {return size(A, 0);}
// template <typename T> size_t getstride1(StrideMatrix<T> A) {return A.S;}
template <typename T>
StrideMatrix<T> subsetRows(StrideMatrix<T> A, size_t r0, size_t r1) {
    return StrideMatrix<T>(A.ptr + r0, r1 - r0, A.N, A.S);
}
template <typename T>
StrideMatrix<T> subsetCols(StrideMatrix<T> A, size_t c0, size_t c1) {
    return StrideMatrix<T>(A.ptr + c0 * A.S, A.M, c1 - c0, A.S);
}
template <typename T>
StrideMatrix<T> subset(StrideMatrix<T> A, size_t r0, size_t r1, size_t c0,
                       size_t c1) {
    return subsetRows(subsetCols(A, c0, c1), r0, r1);
}
template <typename T, size_t M, size_t N>
StrideMatrix<T> subsetRows(Matrix<T, M, N> A, size_t r0, size_t r1) {
    return StrideMatrix<T>(A.ptr + r0, r1 - r0, A.N, A.M);
}
template <typename T, size_t M, size_t N>
StrideMatrix<T> subsetCols(Matrix<T, M, N> A, size_t c0, size_t c1) {
    return StrideMatrix<T>(A.ptr + c0 * A.S, A.M, c1 - c0, A.M);
}
template <typename T, size_t M, size_t N>
StrideMatrix<T> subset(Matrix<T, M, N> A, size_t r0, size_t r1, size_t c0,
                       size_t c1) {
    return subsetRows(subsetCols(A, c0, c1), r0, r1);
}

template <typename T, size_t M>
Vector<T, 0> subset(Vector<T, M> x, size_t i0, size_t i1) {
    return Vector<T, 0>(x.ptr + i0, i1 - i0);
}

template <typename T> struct Tensor3 {
    T *ptr;
    size_t M;
    size_t N;
    size_t O;
    Tensor3(T *ptr, size_t M, size_t N, size_t O)
        : ptr(ptr), M(M), N(N), O(O){};

    T &operator()(size_t m, size_t n, size_t o) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= m) & (m < M));
        assert((0 <= n) & (n < N));
        assert((0 <= o) & (o < O));
#endif
        return ptr[m + M * (n + N * o)];
    }
};
template <typename T> size_t size(Tensor3<T> A, size_t i) {
    return i == 0 ? A.M : (i == 1 ? A.N : A.O);
}
template <typename T> size_t length(Tensor3<T> A) { return A.M * A.N * A.O; }
template <typename T, size_t M, size_t N>
Matrix<T, M, N> subsetDim3(Tensor3<T> A, size_t d) {
    return Matrix<T, M, N>(A.ptr + A.M * A.N * d, A.M, A.N);
}

template <typename T> size_t getNLoops(T x) { return size(x.data, 0); }
//
// Permutations
//
typedef Matrix<Int, 0, 2> PermutationData;
typedef Vector<Int, 0> PermutationVector;
struct Permutation {
    PermutationData data;

    Permutation(Int *ptr, size_t nloops) : data(PermutationData(ptr, nloops)) {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    Int &operator()(size_t i) { return data(i, 0); }
};

size_t length(Permutation perm) { return length(perm.data); }

PermutationVector inv(Permutation p) { return getCol(p.data, 1); }

Int &inv(Permutation p, size_t j) { return p.data(j, 1); }

Permutation init(Permutation p) {
    Int numloops = getNLoops(p);
    for (Int n = 0; n < numloops; n++) {
        p(n) = n;
        inv(p, n) = n;
    }
    return p;
}

void show(Permutation perm) {
    auto numloop = getNLoops(perm);
    std::printf("perm: <");
    for (size_t j = 0; j < numloop - 1; j++)
        std::printf("%ld ", perm(j));
    std::printf("%ld>", perm(numloop - 1));
}

void swap(Permutation p, Int i, Int j) {
    Int xi = p(i);
    Int xj = p(j);
    p(i) = xj;
    p(j) = xi;
    inv(p, xj) = i;
    inv(p, xi) = j;
}

struct PermutationSubset {
    Permutation p;
    Int subset_size;
    Int num_interior;
};

struct PermutationLevelIterator {
    Permutation permobj;
    Int level;
    Int offset;

    PermutationLevelIterator(Permutation permobj, Int lv, Int num_interior)
        : permobj(permobj) {
        Int nloops = getNLoops(permobj);
        level = nloops - num_interior - lv;
        offset = nloops - num_interior;
    };

    PermutationLevelIterator(PermutationSubset ps) : permobj(ps.p) {
        auto lv = ps.subset_size + 1;
        Int num_exterior = getNLoops(ps.p) - ps.num_interior;
        Int num_interior = (lv >= num_exterior) ? 0 : ps.num_interior;
        level = getNLoops(ps.p) - num_interior - lv;
        offset = getNLoops(ps.p) - num_interior;
    }
};

PermutationSubset initialize_state(PermutationLevelIterator p) {
    Int num_interior = getNLoops(p.permobj) - p.offset;
    return PermutationSubset{.p = p.permobj,
                             .subset_size = p.offset - p.level,
                             .num_interior = num_interior};
}

std::pair<PermutationSubset, bool> advance_state(PermutationLevelIterator p,
                                                 Int i) {
    if (i == 0) {
        auto ps = initialize_state(p);
        return std::make_pair(ps, (i + 1) < p.level);
    } else {
        Int k = p.offset - (((p.level & 1) != 0) ? 1 : i);
        swap(p.permobj, p.offset - p.level, k);
        Int num_interior = getNLoops(p.permobj) - p.offset;
        auto ps = PermutationSubset{.p = p.permobj,
                                    .subset_size = p.offset - p.level,
                                    .num_interior = num_interior};
        return std::make_pair(ps, (i + 1) < p.level);
    }
}

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
    return length(getTrit(tri)) + length(getRekt(tri));
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
        auto _j1 = iperm(j);
        // j1 < _i1 means it is included in the permutation, but rectangular
        // `l2` definitely does not depend on `j` loop!
        if (_j1 < _i1)
            return false;
        // we have i < C - Aᵢⱼ * j

        if (Aij < 0) { // i < C + j*abs(Aij)
            // TODO: relax restriction
            if (!otherwiseIndependent(A, j, i))
                return false;
            auto ub_temp = getUpperbound(r, j);
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
