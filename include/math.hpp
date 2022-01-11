#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

inline uint64_t trailingZeros(uint64_t x) { return __builtin_ctz(x); }
inline uint64_t leadingZeros(uint64_t x) { return __builtin_clz(x); }
inline uint64_t countOnes(uint64_t x) { return __builtin_popcount(x); }
std::pair<intptr_t, intptr_t> divgcd(intptr_t x, intptr_t y) {
    intptr_t g = std::gcd(x, y);
    return std::make_pair(x / g, y / g);
}

// template<typename T> T one(const T) { return T(1); }
struct One {
    operator intptr_t() { return 1; };
    operator size_t() { return 1; };
};
bool isOne(intptr_t x) { return x == 1; }
bool isOne(size_t x) { return x == 1; }

template <typename TRC> auto powBySquare(TRC &&x, size_t i) {
    // typedef typename std::remove_const<TRC>::type TR;
    // typedef typename std::remove_reference<TR>::type T;
    typedef typename std::remove_reference<TRC>::type TR;
    typedef typename std::remove_const<TR>::type T;
    switch (i) {
    case 0:
        return T(One());
    case 1:
        return T(x);
    case 2:
        return T(x * x);
    case 3:
        return T(x * x * x);
    default:
        break;
    }
    if (isOne(x)) {
        return T(One());
    }
    intptr_t t = trailingZeros(i) + 1;
    i >>= t;
    // T z(std::move(x));
    T z(std::forward<TRC>(x));
    T b;
    while (--t) {
        b = z;
        z *= b;
    }
    if (i == 0) {
        return z;
    }
    T y(z);
    while (i) {
        t = trailingZeros(i) + 1;
        i >>= t;
        while ((--t) >= 0) {
            b = z;
            z *= b;
        }
        y *= z;
    }
    return y;
}

template <typename T, typename S> void divExact(T &x, S const &y) {
    auto d = x / y;
    assert(d * y == x);
    x = d;
}

// enum SourceType { MEMORY, TERM, CONSTANT, LOOPINDUCTVAR, WTR, RTW };
//   Bits:            00               01                   10            11
enum class SourceType { Constant, LoopInductionVariable, Memory, Term };

struct Source {
    size_t id;
    SourceType typ;
    Source(size_t srcId, SourceType srcTyp) : id(srcId), typ(srcTyp) {}
    bool operator==(Source x) const { return (id == x.id) & (typ == x.typ); }
    bool operator<(Source x) const {
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
{} SourceCount& operator+=(SourceType s){ switch (s) { case
SourceType::Constant: constant += 1; break; case
SourceType::LoopInductionVariable: loopInductVar += 1; break; case
SourceType::Memory: memory += 1; break; case SourceType::Term: term += 1; break;
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

template <typename T0, typename T1> bool allMatch(T0 const &x0, T1 const &x1) {
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

    T &operator()(size_t i) const {
#ifndef DONOTBOUNDSCHECK
        assert((0 <= i) & (i < len));
#endif
        return ptr[i];
    }
    T &operator[](size_t i) const { return ptr[i]; }
    bool operator==(Vector<T, 0> x0) const { return allMatch(*this, x0); }
};

template <typename T, size_t M> size_t length(Vector<T, M>) { return M; }
template <typename T> size_t length(Vector<T, 0> v) { return v.len; }

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
// g, na, nb = gcdx(a, b)
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
std::tuple<size_t, size_t, size_t> gcdx(size_t a, size_t b) {
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
