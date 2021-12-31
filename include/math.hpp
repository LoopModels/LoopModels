#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

// enum SourceType { MEMORY, TERM, CONSTANT, LOOPINDUCTVAR, WTR, RTW };
//   Bits:            00               01                   10            11
enum SourceType {
    ConstantSource,
    LoopInductionVariable,
    MemorySource,
    TermSource
};

std::string toString(SourceType s) {
    switch (s) {
    case ConstantSource:
        return "Constant";
    case LoopInductionVariable:
        return "Induction Variable";
    case MemorySource:
        return "Memory";
    case TermSource:
        return "Term";
        // case WTR:
        //     return "Write then read";
        // case RTW: // dummy SourceType for indicating a relationship; not
        // lowered
        //     return "Read then write";
        // default:
        //     assert("Unreachable reached; invalid SourceType.");
        //     return "";
    }
}

struct Source {
    size_t id;
    SourceType typ;
    Source(size_t srcId, SourceType srcTyp) : id(srcId), typ(srcTyp) {}
    bool operator==(Source x) { return (id == x.id) & (typ == x.typ); }
    bool operator<(Source x) {
        return (typ < x.typ) | ((typ == x.typ) & (id < x.id));
    }
};

/*
struct SourceCount{
    size_t memory;
    size_t term;
    size_t constant;
    size_t loopInductVar;
    size_t total;
    SourceCount() : memory(0), term(0), constant(0), loopInductVar(0), total(0)
{} SourceCount& operator+=(SourceType s){ switch (s) { case ConstantSource:
            constant += 1;
            break;
        case LoopInductionVariable:
            loopInductVar += 1;
            break;
        case MemorySource:
            memory += 1;
            break;
        case TermSource:
            term += 1;
            break;
        }
        total += 1;
        return *this;
    }
    SourceCount& operator+=(Source &s){
        return *this += s.typ;
    }
    bool isAffine(){
        return (memory == 0) & (term == 0);
    }
};
*/

const size_t MAX_NUM_LOOPS = 16;
const size_t MAX_PROGRAM_VARIABLES = 32;
typedef intptr_t Int;

template <typename V> // generic function working with many stl containers, e.g.
                      // std::vector
inline size_t length(V &v) {
    return v.size();
}

template <typename T> T &last(std::vector<T> &x) { return x[x.size() - 1]; }
template <typename T> void show(std::vector<T> &x) {
    std::cout << "[";
    for (size_t i = 0; i < x.size() - 1; ++i) {
        std::cout << x[i] << ", ";
    }
    std::cout << last(x) << "]";
}
// `show` doesn't print a new line by convention.
template <typename T> void showln(T x) {
    show(x);
    std::printf("\n");
}

template <typename T0, typename T1> bool allMatch(T0 x0, T1 x1) {
    size_t N = length(x0);
    if (N != length(x1)) {
        return false;
    }
    bool m = true;
    for (size_t n = 0; n < N; ++n) {
        m &= (x0[n] == x1[n]);
    }
    return m;
}

//
// Vectors
//
template <typename T, size_t M> struct Vector {

    T *ptr;

    Vector(T *ptr) : ptr(ptr){};
    Vector(Vector<T, M> &a) : ptr(a.ptr){};

    T &operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < M));
#endif
        return ptr[i];
    }
    T &operator[](size_t i) { return ptr[i]; }
    bool operator==(Vector<T, M> x0) { return allMatch(*this, x0); }
};
template <typename T> struct Vector<T, 0> {
    T *ptr;
    size_t len;
    inline Vector<T, 0> &operator=(const Vector<T, 0> &a) {
        ptr = a.ptr;
        len = a.len;
    }
    inline Vector(T *ptr, size_t m) : ptr(ptr), len(m){};
    inline Vector(const Vector<T, 0> &a) : ptr(a.ptr), len(a.len){};
    inline Vector(Vector<T, 0> &a) : ptr(a.ptr), len(a.len){};
    inline Vector(std::vector<T> &x) : ptr(&x.front()), len(x.size()){};

    T &operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < len));
#endif
        return ptr[i];
    }
    T &operator[](size_t i) { return ptr[i]; }
    bool operator==(Vector<T, 0> x0) { return allMatch(*this, x0); }
};

template <typename T, size_t M> size_t length(Vector<T, M>) { return M; }
template <typename T> size_t length(Vector<T, 0> v) { return v.len; }

template <typename T, size_t M> void show(Vector<T, M> v) {
    for (size_t i = 0; i < length(v); i++) {
        std::cout << v(i) << ", ";
    }
    if (length(v)) {
        std::cout << v(length(v) - 1);
    }
}

template <typename T> Vector<T, 0> toVector(std::vector<T> &x) {
    return Vector<T, 0>(x);
}

template <typename T> bool allzero(T a, size_t len) {
    for (size_t j = 0; j < len; j++)
        if (a[j] != 0)
            return false;
    return true;
}

template <typename T> inline Vector<T, 0> emptyVector() {
    return Vector<T, 0>(NULL, 0);
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
    T &operator[](size_t i) { return ptr[i]; }
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
    T &operator[](size_t i) { return ptr[i]; }
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
    T &operator[](size_t i) { return ptr[i]; }
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
    T &operator[](size_t i) { return ptr[i]; }
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

template <typename T, size_t M> T &last(Vector<T, M> x) {
    return x[length(x) - 1];
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
struct Permutation {
    PermutationData data;

    Permutation(Int *ptr, size_t nloops) : data(PermutationData(ptr, nloops)) {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    Int &operator()(size_t i) { return data(i, 0); }
    bool operator==(Permutation y) {
        Vector<Int, 0> x0 = getCol(data, 0);
        Vector<Int, 0> y0 = getCol(y.data, 0);
        return x0 == y0;
    }
};

size_t length(Permutation perm) { return length(perm.data); }

Vector<Int, 0> inv(Permutation p) { return getCol(p.data, 1); }

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

template <typename T> struct UnitRange {
    T operator()(size_t i) { return T(i); }
    bool operator==(UnitRange<T>) { return true; }
};
template <typename T> UnitRange<T> inv(UnitRange<T> r) { return r; }

struct PermutationVector {
    Int *ptr;
    size_t nloops;
    size_t nterms;
    Permutation operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < nterms));
#endif
        return Permutation(ptr + i * 2 * nloops, nloops);
    }
};

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

// a_1 * i + b_1 * j = b_0 - a_0
// to find all solutions where
// a_1 * i + a_0 == b_1 * j + b_0
// which is generally useful, whenever we have
// A[a_1 * i + a_0]
// A[b_1 * j + b_0]
// and we want to know, are the
//
// let b0 and a0 = 0, because if we're asking about a single intersection,
// then the constant part of where it occurs doesn't really matter?
// Well, constant parts are the same because it's the same array.
// e.g. A(i, j) -> a_1 * i + b_1 * j + c
//
// g, na, nb = extended_gcd(a, b)
// g == gcd(a, b)
// na * a + nb * b == g
//
// xi = na * ((b0 - a0) / g) + k * b1 / g
// xj = nb * ((b0 - a0) / g) + k * a1 / g
// they intersect for all integer values of `k`
//
//
// a_1 = M
// b_1 = M*N
// g = M
// xi = k * N
// xj = k * 1
//
// for (i = 0; i < N; ++i){
//   for (j = 0; j < i; ++j){
//     A(i, j) = A(j, i); // noalias: copying upper to lower triangle
//   }
// }
// here, let
// M, N = size(A);
// a: ... = A( M*i + j)
// b: A( i + M*j) = ...
//
// because M != N, we need to know that M >= N to prove non-aliasing
// TODO: how do we find/represent the relative values of these sym ids???
// I think in many cases, we can get the information. E.g., if there were
// bounds checks...
//
// What would be great is if we can prove the non-aliasing symbolically.
//
// a_0 = 0
// a_1 = M
// a_2 = 1
// b_0 = 0
// b_1 = 1
// b_2 = M
//
// solutions:
// M == 1 // if M >= N => N <= 1; if N <= 1 => 0 inner loop iterations
// or
// i == j == c0 // can tell from loop bounds this is impossible

// https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm
std::tuple<size_t, size_t, size_t> extended_gcd(size_t a, size_t b) {
    size_t old_r = a;
    size_t r = b;
    size_t old_s = 1;
    size_t s = 0;
    size_t old_t = 0;
    size_t t = 1;
    while (r != 0) {
        size_t quotient = old_r / r;
        size_t r_temp = r;
        size_t s_temp = s;
        size_t t_temp = t;
        r = old_r - quotient * r;
        s = old_s - quotient * s;
        t = old_t - quotient * t;
        old_r = r_temp;
        old_s = s_temp;
        old_t = t_temp;
    }
    // Solving for `t` at the end has 1 extra division, but lets us remove
    // the `t` updates in the loop:
    // size_t t = (b == 0) ? 0 : ((old_r - old_s * a) / b);
    // For now, I'll favor forgoing the division.
    return std::make_tuple(old_r, old_s, old_t);
}
