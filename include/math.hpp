#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

template <class T>
concept Integral = std::is_integral<T>::value;

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
    // typedef typename std::remove_reference<TRC>::type TR;
    // typedef typename std::remove_const<TR>::type T;
    typedef typename std::remove_cvref<TRC>::type T;
    switch (i) {
    case 0:
        return T(One());
    case 1:
        return T(std::forward<TRC>(x));
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
    intptr_t t = std::countr_zero(i) + 1;
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
        t = std::countr_zero(i) + 1;
        i >>= t;
        while ((--t) >= 0) {
            b = z;
            z *= b;
        }
        y *= z;
    }
    return y;
}

template <typename T>
concept HasMul = requires(T t) {
    t.mul(t, t);
};

// a and b are temporary, z stores the final results.
template <HasMul T> void powBySquare(T &z, T &a, T &b, T const &x, size_t i) {
    switch (i) {
    case 0:
        z = One();
        return;
    case 1:
        z = x;
        return;
    case 2:
        z.mul(x, x);
        return;
    case 3:
        b.mul(x, x);
        z.mul(b, x);
        return;
    default:
        break;
    }
    if (isOne(x)) {
        z = x;
        return;
    }
    intptr_t t = std::countr_zero(i) + 1;
    i >>= t;
    z = x;
    while (--t) {
        b.mul(z, z);
        std::swap(b, z);
    }
    if (i == 0) {
        return;
    }
    a = z;
    while (i) {
        t = std::countr_zero(i) + 1;
        i >>= t;
        while ((--t) >= 0) {
            b.mul(a, a);
            std::swap(b, a);
        }
        b.mul(a, z);
        std::swap(b, z);
    }
    return;
}
template <HasMul TRC> auto powBySquare(TRC &&x, size_t i) {
    // typedef typename std::remove_const<TRC>::type TR;
    // typedef typename std::remove_reference<TR>::type T;
    // typedef typename std::remove_reference<TRC>::type TR;
    // typedef typename std::remove_const<TR>::type T;
    typedef typename std::remove_cvref<TRC>::type T;
    switch (i) {
    case 0:
        return T(One());
    case 1:
        return T(std::forward<TRC>(x));
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
    intptr_t t = std::countr_zero(i) + 1;
    i >>= t;
    // T z(std::move(x));
    T z(std::forward<TRC>(x));
    T b;
    while (--t) {
        b.mul(z, z);
        std::swap(b, z);
    }
    if (i == 0) {
        return z;
    }
    T y(z);
    while (i) {
        t = std::countr_zero(i) + 1;
        i >>= t;
        while ((--t) >= 0) {
            b.mul(z, z);
            std::swap(b, z);
        }
        b.mul(y, z);
        std::swap(b, y);
    }
    return y;
}

template <typename T, typename S> void divExact(T &x, S const &y) {
    auto d = x / y;
    assert(d * y == x);
    x = d;
}

enum class VarType : uint32_t {
    Constant = 0x0,
    LoopInductionVariable = 0x1,
    Memory = 0x2,
    Term = 0x3
};
std::ostream &operator<<(std::ostream &os, VarType s) {
    switch (s) {
    case VarType::Constant:
        os << "Constant";
        break;
    case VarType::LoopInductionVariable:
        os << "Induction Variable";
        break;
    case VarType::Memory:
        os << "Memory";
        break;
    case VarType::Term:
        os << "Term";
        break;
    }
    return os;
}

typedef uint32_t IDType;
struct VarID {
    IDType id;
    VarID(IDType id) : id(id) {}
    VarID(IDType i, VarType typ) : id((static_cast<IDType>(typ) << 30) | i) {}
    bool operator<(VarID x) const { return id < x.id; }
    bool operator<=(VarID x) const { return id <= x.id; }
    bool operator==(VarID x) const { return id == x.id; }
    bool operator>(VarID x) const { return id > x.id; }
    bool operator>=(VarID x) const { return id >= x.id; }
    std::strong_ordering operator<=>(VarID x) { return id <=> x.id; }
    IDType getID() const { return id & 0x3fffffff; }
    // IDType getID() const { return id & 0x3fff; }
    VarType getType() const { return static_cast<VarType>(id >> 30); }
};
std::ostream &operator<<(std::ostream &os, VarID s) {
    return os << s.getType() << ": " << s.getID();
}

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
typedef intptr_t Int;

template <typename V> // generic function working with many stl containers, e.g.
                      // std::vector
inline size_t length(V &v) {
    return v.size();
}

template <typename T>
concept HasEnd = requires(T t) {
    t.end();
};

template <HasEnd T> auto &last(T &x) { return *(x.end() - 1); }
template <HasEnd T> auto &last(T const &x) { return *(x.end() - 1); }

template <typename T0, typename T1> bool allMatch(T0 const &x0, T1 const &x1) {
    size_t N = length(x0);
    if (N != length(x1)) {
        return false;
    }
    for (size_t n = 0; n < N; ++n) {
        if (x0[n] != x1[n])
            return false;
    }
    return true;
}

//
// Vectors
//
template <typename T, size_t M> struct Vector {
    T data[M];

    // Vector(T *ptr) : ptr(ptr){};
    // Vector(Vector<T, M> &a) : data(a.data){};

    T &operator()(size_t i) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
#endif
        return data[i];
    }
    T &operator[](size_t i) { return data[i]; }
    const T &operator[](size_t i) const { return data[i]; }
    T *begin() { return data; }
    T *end() { return begin() + M; }
    const T *begin() const { return data; }
    const T *end() const { return begin() + M; }
};
template <typename T, size_t M> struct PtrVector {
    T *ptr;

    PtrVector(T *ptr) : ptr(ptr){};
    // Vector(Vector<T, M> &a) : ptr(a.ptr){};

    T &operator()(size_t i) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
#endif
        return ptr[i];
    }
    T &operator[](size_t i) { return ptr[i]; }
    const T &operator[](size_t i) const { return ptr[i]; }
    T *begin() { return ptr; }
    T *end() { return ptr + M; }
    const T *begin() const { return ptr; }
    const T *end() const { return ptr + M; }
};

template <typename T> struct Vector<T, 0> {
    llvm::SmallVector<T> data;
    Vector(size_t N) : data(llvm::SmallVector<T>(N)){};

    T &operator()(size_t i) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < data.size());
#endif
        return data[i];
    }
    T &operator[](size_t i) { return data[i]; }
    const T &operator[](size_t i) const { return data[i]; }
    // bool operator==(Vector<T, 0> x0) const { return allMatch(*this, x0); }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
};
template <typename T, size_t M>
bool operator==(Vector<T, M> const &x0, Vector<T, M> const &x1) {
    return allMatch(x0, x1);
}

template <typename T, size_t M> size_t length(Vector<T, M>) { return M; }
template <typename T> size_t length(Vector<T, 0> v) { return v.data.size(); }

template <typename T, size_t M>
std::ostream &operator<<(std::ostream &os, Vector<T, M> const &v) {
    // std::ostream &operator<<(std::ostream &os, Vector<T, M> const &v) {
    os << "[ ";
    for (size_t i = 0; i < length(v) - 1; i++) {
        os << v(i) << ", ";
    }
    if (length(v)) {
        os << v(length(v) - 1);
    }
    os << " ]";
    return os;
}

template <typename T> Vector<T, 0> toVector(llvm::SmallVectorImpl<T> const &x) {
    Vector<T, 0> y;
    y.data.reserve(x.size());
    for (auto &i : x) {
        y.push_back(i);
    }
    return y;
}

template <typename T> bool allZero(llvm::SmallVectorImpl<T> const &x) {
    for (auto &a : x)
        if (a != 0)
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
    // static_assert(M*N != L, "if specifying non-zero M and N, we should have
    // M*N == L");
    T data[M * N];
    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T operator()(size_t i, size_t j) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T &operator[](size_t i) { return data[i]; }
    T operator[](size_t i) const { return data[i]; }
    T *begin() { return data; }
    T *end() { return begin() + M; }
    const T *begin() const { return data; }
    const T *end() const { return begin() + M; }

    size_t size(size_t i) const { return i == 0 ? M : N; }
    std::pair<size_t, size_t> size() const { return std::make_pair(M, N); }
    size_t length() const { return M * N; }

    PtrVector<T, M> getCol(size_t i) { return PtrVector<T, M>(data + i * M); }
};

template <typename T, size_t M> struct Matrix<T, M, 0> {
    static constexpr size_t M3 = M * 3;
    llvm::SmallVector<T, M3> data;
    size_t N;

    Matrix(size_t n) : data(llvm::SmallVector<T>(M * n)), N(n){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T &operator[](size_t i) { return data[i]; }
    const T &operator[](size_t i) const { return data[i]; }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }

    size_t size(size_t i) const { return i == 0 ? M : N; }
    std::pair<size_t, size_t> size() const { return std::make_pair(M, N); }
    size_t length() const { return data.size(); }

    PtrVector<T, M> getCol(size_t i) { return PtrVector<T, M>(data + i * M); }
};
template <typename T, size_t N> struct Matrix<T, 0, N> {
    static constexpr size_t N3 = N * 3;
    llvm::SmallVector<T, N3> data;
    size_t M;

    Matrix(size_t m) : data(llvm::SmallVector<T>(m * N)), M(m){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T operator()(size_t i, size_t j) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T &operator[](size_t i) { return data[i]; }
    T operator[](size_t i) const { return data[i]; }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }

    size_t size(size_t i) const { return i == 0 ? M : N; }
    std::pair<size_t, size_t> size() const { return std::make_pair(M, N); }
    size_t length() const { return data.size(); }

    llvm::ArrayRef<T> getCol(size_t i) {
        T *p = data.data() + i * M;
        return llvm::ArrayRef<T>(p, M);
    }
};
template <typename T> struct Matrix<T, 0, 0> {
    llvm::SmallVector<T> data;

    size_t M;
    size_t N;

    Matrix(size_t m, size_t n)
        : data(llvm::SmallVector<T>(m * n)), M(m), N(n){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T operator()(size_t i, size_t j) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < N);
#endif
        return data[i + j * M];
    }
    T &operator[](size_t i) { return data[i]; }
    T operator[](size_t i) const { return data[i]; }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    size_t size(size_t i) const { return i == 0 ? M : N; }
    std::pair<size_t, size_t> size() const { return std::make_pair(M, N); }
    size_t length() const { return data.size(); }
    llvm::ArrayRef<T> getCol(size_t i) {
        T *p = data.data() + i * M;
        return llvm::ArrayRef<T>(p, M);
    }
};

template <typename T, size_t M, size_t N>
std::pair<size_t, size_t> size(Matrix<T, M, N> const &A) {
    return std::make_pair(size(A, 0), size(A, 1));
}

template <typename T> struct SquareMatrix {
    llvm::SmallVector<T, 9> data;
    size_t M;

    SquareMatrix(size_t m) : data(llvm::SmallVector<T>(m * m)), M(m){};

    T &operator()(size_t i, size_t j) {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < M);
#endif
        return data[i + j * M];
    }
    T operator()(size_t i, size_t j) const {
#ifndef DONOTBOUNDSCHECK
        assert(i < M);
        assert(j < M);
#endif
        return data[i + j * M];
    }
    T &operator[](size_t i) { return data[i]; }
    T operator[](size_t i) const { return data[i]; }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    std::pair<size_t, size_t> size() const { return std::make_pair<M, M>; }
    size_t size(size_t) const { return M; }
    size_t length() const { return data.size(); }

    llvm::ArrayRef<T> getCol(size_t i) {
        T *p = data.data() + i * M;
        return llvm::ArrayRef<T>(p, M);
    }
};

// template <typename T> struct StrideMatrix {
//     T *ptr;
//     size_t M;
//     size_t N;
//     size_t S;

//     StrideMatrix(T *ptr, size_t M, size_t N, size_t S)
//         : ptr(ptr), M(M), N(N), S(S){};

//     T &operator()(size_t i, size_t j) {
// #ifndef DONOTBOUNDSCHECK
//         assert(i < M);
//         assert(j < N);
// #endif
//         return ptr[i + j * S];
//     }
// };
// template <typename T> size_t size(StrideMatrix<T> A, size_t i) {
//     return i == 0 ? A.M : A.N;
// }
// template <typename T> std::pair<size_t, size_t> size(StrideMatrix<T> A) {
//     return std::make_pair(A.M, A.N);
// }
// template <typename T> size_t length(StrideMatrix<T> A) {
//     return size(A, 0) * size(A, 1);
// }
// // template <typename T> size_t getstride1(Matrix<T> A) {return size(A, 0);}
// // template <typename T> size_t getstride1(StrideMatrix<T> A) {return A.S;}
// template <typename T>
// StrideMatrix<T> subsetRows(StrideMatrix<T> A, size_t r0, size_t r1) {
//     return StrideMatrix<T>(A.ptr + r0, r1 - r0, A.N, A.S);
// }
// template <typename T>
// StrideMatrix<T> subsetCols(StrideMatrix<T> A, size_t c0, size_t c1) {
//     return StrideMatrix<T>(A.ptr + c0 * A.S, A.M, c1 - c0, A.S);
// }
// template <typename T>
// StrideMatrix<T> subset(StrideMatrix<T> A, size_t r0, size_t r1, size_t c0,
//                        size_t c1) {
//     return subsetRows(subsetCols(A, c0, c1), r0, r1);
// }
// template <typename T, size_t M, size_t N>
// StrideMatrix<T> subsetRows(Matrix<T, M, N> A, size_t r0, size_t r1) {
//     return StrideMatrix<T>(A.ptr + r0, r1 - r0, A.N, A.M);
// }
// template <typename T, size_t M, size_t N>
// StrideMatrix<T> subsetCols(Matrix<T, M, N> A, size_t c0, size_t c1) {
//     return StrideMatrix<T>(A.ptr + c0 * A.S, A.M, c1 - c0, A.M);
// }
// template <typename T, size_t M, size_t N>
// StrideMatrix<T> subset(Matrix<T, M, N> A, size_t r0, size_t r1, size_t c0,
//                        size_t c1) {
//     return subsetRows(subsetCols(A, c0, c1), r0, r1);
// }

// template <typename T, size_t M>
// Vector<T, 0> subset(Vector<T, M> x, size_t i0, size_t i1) {
//     return Vector<T, 0>(x.ptr + i0, i1 - i0);
// }

// template <typename T, size_t M> T &last(Vector<T, M> x) {
//     return x[length(x) - 1];
// }

template <typename T> std::ostream &printMatrix(std::ostream &os, T const &A) {
    // std::ostream &printMatrix(std::ostream &os, T const &A) {
    os << "[ ";
    auto [m, n] = A.size();
    for (size_t i = 0; i < m - 1; i++) {
        for (size_t j = 0; j < n - 1; j++) {
            os << A(i, j) << ", ";
        }
        if (n) {
            os << A(i, n - 1);
        }
        os << std::endl;
    }
    if (m) {
        for (size_t j = 0; j < n - 1; j++) {
            os << A(m - 1, j) << ", ";
        }
        if (n) {
            os << A(m - 1, n - 1);
        }
    }
    os << " ]";
    return os;
}
template <typename T, size_t M, size_t N>
std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A) {
    return printMatrix(os, A);
}
// template <typename T>
// std::ostream &operator<<(std::ostream &os, StrideMatrix<T> &A) {
//     // std::ostream &operator<<(std::ostream &os, StrideMatrix<T> const &A) {
//     return printMatrix(os, A);
// }

// template <typename T> struct Tensor3 {
//     T *ptr;
//     size_t M;
//     size_t N;
//     size_t O;
//     Tensor3(T *ptr, size_t M, size_t N, size_t O)
//         : ptr(ptr), M(M), N(N), O(O){};

//     T &operator()(size_t m, size_t n, size_t o) {
// #ifndef DONOTBOUNDSCHECK
//         assert(m < M);
//         assert(n < N);
//         assert(o < O);
// #endif
//         return ptr[m + M * (n + N * o)];
//     }
// };
// template <typename T> size_t size(Tensor3<T> A, size_t i) {
//     return i == 0 ? A.M : (i == 1 ? A.N : A.O);
// }
// template <typename T> size_t length(Tensor3<T> A) { return A.M * A.N * A.O; }
// template <typename T, size_t M, size_t N>
// Matrix<T, M, N> subsetDim3(Tensor3<T> A, size_t d) {
//     return Matrix<T, M, N>(A.ptr + A.M * A.N * d, A.M, A.N);
// }

//
// Permutations
//
typedef Matrix<unsigned, 0, 2> PermutationData;
struct Permutation {
    PermutationData data;

    Permutation(size_t nloops) : data(PermutationData(nloops)) {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    unsigned &operator()(size_t i) { return data(i, 0); }
    unsigned operator()(size_t i) const { return data(i, 0); }
    bool operator==(Permutation y) {
        return data.getCol(0) == y.data.getCol(0);
    }
    size_t getNumLoops() const { return data.size(0); }
    size_t length() const { return data.length(); }

    llvm::ArrayRef<unsigned> inv() { return data.getCol(1); }

    unsigned &inv(size_t j) { return data(j, 1); }

    void init() {
        size_t numloops = getNumLoops();
        for (size_t n = 0; n < numloops; n++) {
            data(n, 0) = n;
            data(n, 1) = n;
        }
    }
    void swap(size_t i, size_t j) {
        size_t xi = data(i, 0);
        size_t xj = data(j, 0);
        data(i, 0) = xj;
        data(j, 0) = xi;
        data(xj, 1) = i;
        data(xi, 1) = j;
    }
};
template <typename T> struct UnitRange {
    T operator()(size_t i) { return T(i); }
    bool operator==(UnitRange<T>) { return true; }
};
template <typename T> UnitRange<T> inv(UnitRange<T> r) { return r; }

/*
struct PermutationVector {
    Int *ptr;
    size_t nloops;
    size_t nterms;
    Permutation operator()(size_t i) {
#ifndef DONOTBOUNDSCHECK
        assert(i < nterms);
#endif
        return Permutation(ptr + i * 2 * nloops, nloops);
    }
};
*/

std::ostream &operator<<(std::ostream &os, Permutation const &perm) {
    auto numloop = perm.getNumLoops();
    os << "perm: <";
    for (size_t j = 0; j < numloop - 1; j++) {
        os << perm(j) << " ";
    }
    os << ">" << perm(numloop - 1);
    return os;
}
/*
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
        Int nloops = getNumLoops(permobj);
        level = nloops - num_interior - lv;
        offset = nloops - num_interior;
    };

    PermutationLevelIterator(PermutationSubset ps) : permobj(ps.p) {
        auto lv = ps.subset_size + 1;
        Int num_exterior = getNumLoops(ps.p) - ps.num_interior;
        Int num_interior = (lv >= num_exterior) ? 0 : ps.num_interior;
        level = getNumLoops(ps.p) - num_interior - lv;
        offset = getNumLoops(ps.p) - num_interior;
    }
};

PermutationSubset initialize_state(PermutationLevelIterator p) {
    Int num_interior = getNumLoops(p.permobj) - p.offset;
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
        Int num_interior = getNumLoops(p.permobj) - p.offset;
        auto ps = PermutationSubset{.p = p.permobj,
                                    .subset_size = p.offset - p.level,
                                    .num_interior = num_interior};
        return std::make_pair(ps, (i + 1) < p.level);
    }
}
*/

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

template <int Bits, class T>
constexpr bool is_uint_v = sizeof(T) == (Bits / 8) && std::is_integral_v<T> &&
                           !std::is_signed_v<T>;

template <class T> inline T zeroUpper(T x) requires is_uint_v<16, T> {
    return x & 0x00ff;
}
template <class T> inline T zeroLower(T x) requires is_uint_v<16, T> {
    return x & 0xff00;
}
template <class T> inline T upperHalf(T x) requires is_uint_v<16, T> {
    return x >> 8;
}

template <class T> inline T zeroUpper(T x) requires is_uint_v<32, T> {
    return x & 0x0000ffff;
}
template <class T> inline T zeroLower(T x) requires is_uint_v<32, T> {
    return x & 0xffff0000;
}
template <class T> inline T upperHalf(T x) requires is_uint_v<32, T> {
    return x >> 16;
}
template <class T> inline T zeroUpper(T x) requires is_uint_v<64, T> {
    return x & 0x00000000ffffffff;
}
template <class T> inline T zeroLower(T x) requires is_uint_v<64, T> {
    return x & 0xffffffff00000000;
}
template <class T> inline T upperHalf(T x) requires is_uint_v<64, T> {
    return x >> 32;
}

template <typename T> std::pair<size_t, T> findMax(llvm::ArrayRef<T> x) {
    size_t i = 0;
    T max = std::numeric_limits<T>::min();
    for (size_t j = 0; j < x.size(); ++j) {
        T xj = x[j];
        if (max < xj) {
            max = xj;
            i = j;
        }
    }
    return std::make_pair(i, max);
}
