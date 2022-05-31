#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include "./Macro.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <numeric>
#include <string>
#include <tuple>
#include <type_traits>
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
    std::pair<VarType, IDType> getTypeAndId() const {
        return std::make_pair(getType(), getID());
    }
    bool isIndVar() { return getType() == VarType::LoopInductionVariable; }
    bool isLoopInductionVariable() const {
        return getType() == VarType::LoopInductionVariable;
    }
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
    T mem[M];

    // Vector(T *ptr) : ptr(ptr){};
    // Vector(Vector<T, M> &a) : data(a.data){};

    T &operator()(size_t i) const {
        assert(i < M);
        return mem[i];
    }
    T &operator[](size_t i) { return mem[i]; }
    const T &operator[](size_t i) const { return mem[i]; }
    T *begin() { return mem; }
    T *end() { return begin() + M; }
    const T *begin() const { return mem; }
    const T *end() const { return begin() + M; }
    T *data() { return mem; }
    const T *data() const { return mem; }
};
template <typename T, size_t M = 0> struct PtrVector {
    T *ptr;

    PtrVector(T *ptr) : ptr(ptr){};
    // Vector(Vector<T, M> &a) : ptr(a.ptr){};

    T &operator()(size_t i) const {
        assert(i < M);
        return ptr[i];
    }
    T &operator[](size_t i) { return ptr[i]; }
    const T &operator[](size_t i) const { return ptr[i]; }
    T *begin() { return ptr; }
    T *end() { return ptr + M; }
    const T *begin() const { return ptr; }
    const T *end() const { return ptr + M; }
    constexpr size_t size() const { return M; }
    T *data() { return ptr; }
    const T *data() const { return ptr; }
};
template <typename T> struct PtrVector<T, 0> {
    T *ptr;
    size_t M;
    // PtrVector(llvm::ArrayRef<T> A) : ptr(A.data()), M(A.size()) {};
    T &operator()(size_t i) const {
        assert(i < M);
        return ptr[i];
    }
    T &operator[](size_t i) { return ptr[i]; }
    const T &operator[](size_t i) const { return ptr[i]; }
    T *begin() { return ptr; }
    T *end() { return ptr + M; }
    const T *begin() const { return ptr; }
    const T *end() const { return ptr + M; }
    T *data() { return ptr; }
    const T *data() const { return ptr; }
    size_t size() const { return M; }
    operator llvm::ArrayRef<T>() { return llvm::ArrayRef<T>{ptr, M}; }
    llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
    bool operator==(const PtrVector<T, 0> x) const {
        return this->arrayref() == x.arrayref();
    }
    bool operator==(const llvm::ArrayRef<T> x) const {
        return this->arrayref() == x;
    }
};
template <typename T> struct Vector<T, 0> {
    llvm::SmallVector<T> mem;
    Vector(size_t N) : mem(llvm::SmallVector<T>(N)){};

    Vector(const llvm::SmallVector<T> &A) : mem(A.begin(), A.end()){};
    Vector(llvm::SmallVector<T> &&A) : mem(std::move(A)){};

    T &operator()(size_t i) {
        assert(i < mem.size());
        return mem[i];
    }
    const T &operator()(size_t i) const {
        assert(i < mem.size());
        return mem[i];
    }
    T &operator[](size_t i) { return mem[i]; }
    const T &operator[](size_t i) const { return mem[i]; }
    // bool operator==(Vector<T, 0> x0) const { return allMatch(*this, x0); }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }
    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};
template <typename T, size_t M>
bool operator==(Vector<T, M> const &x0, Vector<T, M> const &x1) {
    return allMatch(x0, x1);
}
static_assert(std::copyable<Vector<intptr_t, 4>>);
static_assert(std::copyable<Vector<intptr_t, 0>>);

template <typename T, size_t M> size_t length(Vector<T, M>) { return M; }
template <typename T> size_t length(Vector<T, 0> v) { return v.mem.size(); }

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
    y.mem.reserve(x.size());
    for (auto &i : x) {
        y.push_back(i);
    }
    return y;
}

inline bool isZero(auto x) { return x == 0; }

bool allZero(const auto &x) {
    for (auto &a : x)
        if (!isZero(a))
            return false;
    return true;
}

template <typename T> inline Vector<T, 0> emptyVector() {
    return Vector<T, 0>(NULL, 0);
}

template <typename T> struct StridedVector {
    T *d;
    size_t N;
    size_t x;
    struct StridedIterator {
        T *d;
        size_t x;
        auto operator++() {
            d += x;
            return *this;
        }
        auto operator--() {
            d -= x;
            return *this;
        }
        T &operator*() { return *d; }
        bool operator==(const StridedIterator y) const { return d == y.d; }
    };
    auto begin() { return StridedIterator{d, x}; }
    auto end() { return StridedIterator{d + N * x, x}; }
    auto begin() const { return StridedIterator{d, x}; }
    auto end() const { return StridedIterator{d + N * x, x}; }
    T &operator[](size_t i) { return d[i * x]; }
    const T &operator[](size_t i) const { return d[i * x]; }
    size_t size() const { return N; }
};

template <typename T, typename A> struct BaseMatrix {
    inline T &getLinearElement(size_t i) {
        return static_cast<A *>(this)->getLinearElement(i);
    }
    inline const T &getLinearElement(size_t i) const {
        return static_cast<const A *>(this)->getLinearElement(i);
    }
    inline T &operator[](size_t i) { return getLinearElement(i); }
    inline const T &operator[](size_t i) const { return getLinearElement(i); }
    size_t numCol() const { return static_cast<const A *>(this)->numCol(); }
    size_t numRow() const { return static_cast<const A *>(this)->numRow(); }
    auto begin() { return static_cast<A *>(this)->begin(); }
    auto end() { return static_cast<A *>(this)->end(); }
    auto begin() const { return static_cast<const A *>(this)->begin(); }
    auto end() const { return static_cast<const A *>(this)->end(); }

    T *data() { return static_cast<A *>(this)->data(); }
    const T *data() const { return static_cast<const A *>(this)->data(); }

    std::pair<size_t, size_t> size() const {
        return std::make_pair(numRow(), numCol());
    }
    size_t size(size_t i) const {
        if (i) {
            return numCol();
        } else {
            return numRow();
        }
    }
    size_t length() const { return numRow() * numCol(); }

    T &operator()(size_t i, size_t j) {
        const size_t M = numRow();
        assert(i < M);
        assert(j < numCol());
        return getLinearElement(i + j * M);
    }
    const T &operator()(size_t i, size_t j) const {
        const size_t M = numRow();
        assert(i < M);
        assert(j < numCol());
        return (*this)[i + j * M];
        return getLinearElement(i + j * M);
    }

    template <typename C> bool operator==(const BaseMatrix<T, C> &B) const {
        const size_t M = numRow();
        const size_t N = numCol();
        if ((N != B.numCol()) || M != B.numRow()) {
            return false;
        }
        for (size_t i = 0; i < M * N; ++i) {
            if (getLinearElement(i) != B[i]) {
                return false;
            }
        }
        return true;
    }

    static constexpr size_t getConstRow() { return A::getConstRow(); }
    auto getCol(size_t i) {
        constexpr size_t M = getConstRow();
        if constexpr (M) {
            return PtrVector<T, M>(data() + i * M);
        } else {
            const size_t _M = numRow();
            return PtrVector<T, 0>{data() + i * _M, _M};
        }
    }
    auto getCol(size_t i) const {
        constexpr size_t M = getConstRow();
        if constexpr (M) {
            return PtrVector<const T, M>(data() + i * M);
        } else {
            const size_t _M = numRow();
            return llvm::ArrayRef<T>(data() + i * _M, _M);
            // return PtrVector<const T, 0>{
        }
    }
    StridedVector<T> getRow(size_t m) {
        return StridedVector<T>{data() + m, numCol(), numRow()};
    }
    StridedVector<const T> getRow(size_t m) const {
        return StridedVector<const T>{data() + m, numCol(), numRow()};
    }
};
template <typename T> struct PtrMatrix : BaseMatrix<T, PtrMatrix<T>> {
    T *mem;
    const size_t M, N;

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    T *begin() { return mem; }
    T *end() { return mem + (M * N); }
    const T *begin() const { return mem; }
    const T *end() const { return mem + (M * N); }

    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    static constexpr size_t getConstRow() { return 0; }

    T *data() { return mem; }
    const T *data() const { return mem; }

    // void truncateColumns(size_t Nnew) {
    //     assert(Nnew <= N);
    //     N = Nnew;
    // }
};

//
// Matrix
//
template <typename T, size_t M = 0, size_t N = 0,
          size_t S = std::max(M, size_t(3)) * std::max(N, size_t(3))>
struct Matrix : BaseMatrix<T, Matrix<T, M, N, S>> {
    static_assert(M * N == S,
                  "if specifying non-zero M and N, we should have M*N == S");
    T mem[S];
    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    T *begin() { return mem; }
    T *end() { return begin() + M; }
    const T *begin() const { return mem; }
    const T *end() const { return begin() + M; }
    size_t numRow() const { return M; }
    size_t numCol() const { return N; }

    T *data() { return mem; }
    const T *data() const { return mem; }

    static constexpr size_t getConstRow() { return M; }
};

template <typename T, size_t M, size_t S>
struct Matrix<T, M, 0, S> : BaseMatrix<T, Matrix<T, M, 0, S>> {
    llvm::SmallVector<T, S> mem;
    size_t N;

    Matrix(size_t n) : mem(llvm::SmallVector<T>(M * n)), N(n){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }
    size_t numRow() const { return M; }
    size_t numCol() const { return N; }

    static constexpr size_t getConstRow() { return M; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};
template <typename T, size_t N, size_t S>
struct Matrix<T, 0, N, S> : BaseMatrix<T, Matrix<T, 0, N, S>> {
    llvm::SmallVector<T, S> mem;
    size_t M;

    Matrix(size_t m) : mem(llvm::SmallVector<T>(m * N)), M(m){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    static constexpr size_t getConstRow() { return 0; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};

template <typename T, unsigned STORAGE = 3>
struct SquareMatrix : BaseMatrix<T, SquareMatrix<T, STORAGE>> {
    typedef T eltype;
    static constexpr unsigned TOTALSTORAGE = STORAGE * STORAGE;
    llvm::SmallVector<T, TOTALSTORAGE> mem;
    size_t M;

    SquareMatrix(size_t m) : mem(llvm::SmallVector<T>(m * m)), M(m){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    static constexpr size_t getConstRow() { return 0; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
    void copyRow(llvm::ArrayRef<T> a, size_t j) {
        for (size_t m = 0; m < M; ++m) {
            (*this)(j, m) = a[m];
        }
    }
    void copyCol(llvm::ArrayRef<T> a, size_t j) {
        for (size_t m = 0; m < M; ++m) {
            (*this)(m, j) = a[m];
        }
    }
    void copyCol(const SquareMatrix<T> &A, size_t j) {
        copyCol(A.getCol(j), j);
    }
    // returns the inverse, followed by bool where true means failure

    static SquareMatrix<T> identity(size_t N) {
        SquareMatrix<T> A(N);
        for (size_t c = 0; c < N; ++c) {
            for (size_t r = 0; r < N; ++r) {
                A(r, c) = (r == c);
            }
        }
        return A;
    }
    operator PtrMatrix<T>() {
        return {.mem = mem.data(), .M = size_t(M), .N = size_t(M)};
    }
    operator PtrMatrix<const T>() const {
        return {.mem = mem.data(), .M = size_t(M), .N = size_t(M)};
    }
};

template <typename T, size_t S>
struct Matrix<T, 0, 0, S> : BaseMatrix<T, Matrix<T, 0, 0, S>> {
    llvm::SmallVector<T, S> mem;

    size_t M;
    size_t N;

    Matrix(size_t m, size_t n) : mem(llvm::SmallVector<T>(m * n)), M(m), N(n){};

    Matrix() : M(0), N(0){};
    Matrix(SquareMatrix<T> &&A) : mem(std::move(A.mem)), M(A.M), N(A.M){};
    Matrix(const SquareMatrix<T> &A)
        : mem(A.mem.begin(), A.mem.end()), M(A.M), N(A.M){};

    operator PtrMatrix<T>() {
        return {.mem = mem.data(), .M = size_t(M), .N = size_t(N)};
    }
    operator PtrMatrix<const T>() const {
        return {.mem = mem.data(), .M = size_t(M), .N = size_t(N)};
    }

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    static constexpr size_t getConstRow() { return 0; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }

    static Matrix<T, 0, 0> Uninitialized(size_t MM, size_t NN) {
        Matrix<T, 0, 0> A(0, 0);
        A.M = MM;
        A.N = NN;
        A.mem.resize_for_overwrite(MM * NN);
        return A;
    }
    void clear() {
        M = 0;
        N = 0;
        mem.clear();
    }
    void resize(size_t MM, size_t NN) {
        M = MM;
        N = NN;
        mem.resize(M * N);
    }
    void reserve(size_t MM, size_t NN) { mem.reserve(MM * NN); }
    void resizeForOverwrite(size_t MM, size_t NN) {
        M = MM;
        N = NN;
        mem.resize_for_overwrite(M * N);
    }
    void resizeRows(size_t NN) {
        N = NN;
        mem.resize(M * N);
    }
    void resizeRowsForOverwrite(size_t NN) {
        N = NN;
        mem.resize_for_overwrite(M * N);
    }
    void eraseCol(size_t i) {
        auto it = mem.begin() + i * M;
        mem.erase(it, it + M);
        --N;
    }
    void truncateColumns(size_t Nnew) {
        assert(Nnew <= N);
        N = Nnew;
    }

    void increaseNumRows(size_t MM) {
        if (M == MM)
            return;
        mem.resize_for_overwrite(M * N);
        for (size_t n = N; n != 0;) {
            --n;
            for (size_t m = 0; m < M; ++m) {
                mem[m + n * MM] = mem[m + n * M];
            }
        }
        M = MM;
    }
    void reduceNumRows(size_t MM) {
        if (M == MM)
            return;
        for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < MM; ++m) {
                mem[m + n * MM] = mem[m + n * M];
            }
        }
        M = MM;
        mem.resize(M * N);
    }
    void resizeCols(size_t MM) {
        if (M < MM) {
            increaseNumRows(MM);
        } else {
            reduceNumRows(MM);
        }
    }
};
static_assert(std::copyable<Matrix<intptr_t, 4, 4>>);
static_assert(std::copyable<Matrix<intptr_t, 4, 0>>);
static_assert(std::copyable<Matrix<intptr_t, 0, 4>>);
static_assert(std::copyable<Matrix<intptr_t, 0, 0>>);
static_assert(std::copyable<SquareMatrix<intptr_t>>);

template <typename T>
struct SquarePtrMatrix : BaseMatrix<T, SquarePtrMatrix<T>> {
    T *mem;
    const size_t M;
    SquarePtrMatrix(T *data, size_t M) : mem(data), M(M){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    T *begin() { return mem; }
    T *end() { return mem + (M * M); }
    const T *begin() const { return mem; }
    const T *end() const { return mem + (M * M); }

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    static constexpr size_t getConstRow() { return 0; }

    T *data() { return mem; }
    const T *data() const { return mem; }
};

template <typename T, typename P>
std::pair<size_t, size_t> size(BaseMatrix<T, P> const &A) {
    return std::make_pair(A.numRow(), A.numCol());
}

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

template <typename T> std::ostream &printVector(std::ostream &os, T const &a) {
    // std::ostream &printMatrix(std::ostream &os, T const &A) {
    os << "[ ";
    if (size_t M = a.size()) {
        os << a[0];
        for (size_t m = 1; m < M; m++) {
            os << ", " << a[m];
        }
    }
    os << " ]";
    return os;
}
template <typename T, size_t L>
std::ostream &operator<<(std::ostream &os, PtrVector<T, L> const &A) {
    return printVector(os, A);
}

template <typename T> std::ostream &printMatrix(std::ostream &os, T const &A) {
    // std::ostream &printMatrix(std::ostream &os, T const &A) {
    auto [m, n] = A.size();
    for (size_t i = 0; i < m; i++) {
        if (i) {
            os << "  ";
        } else {
            os << "[ ";
        }
        for (intptr_t j = 0; j < intptr_t(n) - 1; j++) {
            auto Aij = A(i, j);
            if (Aij >= 0) {
                os << " ";
            }
            os << Aij << " ";
        }
        if (n) {
            auto Aij = A(i, n - 1);
            if (Aij >= 0) {
                os << " ";
            }
            os << Aij;
        }
        if (i != m - 1) {
            os << std::endl;
        }
    }
    os << " ]";
    return os;
}
template <typename T, size_t M, size_t N, size_t L>
std::ostream &operator<<(std::ostream &os, Matrix<T, M, N, L> const &A) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A) {
    return printMatrix(os, A);
}
template <typename T>
std::ostream &operator<<(std::ostream &os, SquareMatrix<T> const &A) {
    return printMatrix(os, A);
}

template <typename A>
concept IntVector = requires(A a) {
    { a[size_t(0)] } -> std::same_as<intptr_t &>;
    { a.size() } -> std::same_as<size_t>;
};

template <typename A>
concept AbstractMatrix = requires(A a) {
    {a(size_t(0), size_t(0))};

    { a.size() } -> std::same_as<std::pair<size_t, size_t>>;
    { a.size(0) } -> std::same_as<size_t>;
};

template <typename A>
concept IntMatrix = requires(A a) {
    { a(size_t(0), size_t(0)) } -> std::same_as<intptr_t &>;

    { a.size() } -> std::same_as<std::pair<size_t, size_t>>;
    { a.size(0) } -> std::same_as<size_t>;
};
/*
template<typename T, AbstractMatrix<T> A>
constexpr bool isIntMatrix() { return std::is_same_v<intptr_t,T>(); }
*/

AbstractMatrix auto matmul(const AbstractMatrix auto &A,
                           const AbstractMatrix auto &B) {
    auto [M, K] = A.size();
    auto [K2, N] = B.size();
    assert(K == K2);
    Matrix<std::remove_cvref_t<decltype(A(0, 0))>, 0, 0> C(M, N);
    for (size_t n = 0; n < N; ++n) {
        for (size_t k = 0; k < K; ++k) {
            for (size_t m = 0; m < M; ++m) {
                C(m, n) += A(m, k) * B(k, n);
            }
        }
    }
    return C;
}
AbstractMatrix auto matmultn(const AbstractMatrix auto &A,
                             const AbstractMatrix auto &B) {
    auto [K, M] = A.size();
    auto [K2, N] = B.size();
    assert(K == K2);
    Matrix<std::remove_cvref_t<decltype(A(0, 0))>, 0, 0> C(M, N);
    for (size_t n = 0; n < N; ++n) {
        for (size_t m = 0; m < M; ++m) {
            for (size_t k = 0; k < K; ++k) {
                C(m, n) += A(k, m) * B(k, n);
            }
        }
    }
    return C;
}
AbstractMatrix auto matmultn(AbstractMatrix auto &C,
                             const AbstractMatrix auto &A,
                             const AbstractMatrix auto &B) {
    auto [K, M] = A.size();
    auto [K2, N] = B.size();
    assert(K == K2);
    C.resize(M, N);
    for (size_t n = 0; n < N; ++n) {
        for (size_t m = 0; m < M; ++m) {
            for (size_t k = 0; k < K; ++k) {
                C(m, n) += A(k, m) * B(k, n);
            }
        }
    }
    return C;
}

void swapRows(PtrMatrix<intptr_t> A, size_t i, size_t j) {
    if (i == j) {
        return;
    }
    auto [M, N] = A.size();
    assert((i < M) & (j < M));
    for (size_t n = 0; n < N; ++n) {
        std::swap(A(i, n), A(j, n));
    }
}
MULTIVERSION inline void swapCols(PtrMatrix<intptr_t> A, size_t i, size_t j) {
    if (i == j) {
        return;
    }
    auto [M, N] = A.size();
    assert((i < N) & (j < N));
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(enable)
#pragma clang loop vectorize_predicate(enable)
    for (size_t m = 0; m < M; ++m) {
        std::swap(A(m, i), A(m, j));
    }
}
template <typename T>
void swapCols(llvm::SmallVectorImpl<T> &A, size_t i, size_t j) {
    std::swap(A[i], A[j]);
}

// // template <Integral T> T sign(T i) {
// auto sign(Integral auto i) {
//     if (i) {
//         return i > 0 ? 1 : -1;
//     } else {
//         return 0;
//     }
// }

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
template <Integral T> std::tuple<T, T, T> gcdx(T a, T b) {
    T old_r = a;
    T r = b;
    T old_s = 1;
    T s = 0;
    T old_t = 0;
    T t = 1;
    while (r) {
        T quotient = old_r / r;
        old_r -= quotient * r;
        old_s -= quotient * s;
        old_t -= quotient * t;
        std::swap(r, old_r);
        std::swap(s, old_s);
        std::swap(t, old_t);
    }
    // Solving for `t` at the end has 1 extra division, but lets us remove
    // the `t` updates in the loop:
    // T t = (b == 0) ? 0 : ((old_r - old_s * a) / b);
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

template <int Bits, class T>
constexpr bool is_int_v =
    sizeof(T) == (Bits / 8) && std::is_integral_v<T> &&std::is_signed_v<T>;

template <class T> inline __int128_t widen(T x) requires is_int_v<64, T> {
    return x;
}
template <class T> inline int64_t splitInt(T x) requires is_int_v<32, T> {
    return x;
}

inline auto bin2(Integral auto x) { return (x * (x - 1)) >> 1; }

struct Rational {
    intptr_t numerator;
    intptr_t denominator;

    Rational() = default;
    Rational(intptr_t coef) : numerator(coef), denominator(1){};
    Rational(int coef) : numerator(coef), denominator(1){};
    Rational(intptr_t n, intptr_t d) : numerator(n), denominator(n ? d : 1){};
    llvm::Optional<Rational> operator+(Rational y) const {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        intptr_t a, b, n, d;
        bool o1 = __builtin_mul_overflow(numerator, yd, &a);
        bool o2 = __builtin_mul_overflow(y.numerator, xd, &b);
        bool o3 = __builtin_mul_overflow(denominator, yd, &d);
        bool o4 = __builtin_add_overflow(a, b, &n);
        if ((o1 | o2) | (o3 | o4)) {
            return llvm::Optional<Rational>();
        } else if (n) {
            auto [nn, nd] = divgcd(n, d);
            return Rational{nn, nd};
        } else {
            return Rational{0, 1};
        }
    }
    Rational &operator+=(Rational y) {
        llvm::Optional<Rational> a = *this + y;
        assert(a.hasValue());
        *this = a.getValue();
        return *this;
    }
    llvm::Optional<Rational> operator-(Rational y) const {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        intptr_t a, b, n, d;
        bool o1 = __builtin_mul_overflow(numerator, yd, &a);
        bool o2 = __builtin_mul_overflow(y.numerator, xd, &b);
        bool o3 = __builtin_mul_overflow(denominator, yd, &d);
        bool o4 = __builtin_sub_overflow(a, b, &n);
        if ((o1 | o2) | (o3 | o4)) {
            return llvm::Optional<Rational>();
        } else if (n) {
            auto [nn, nd] = divgcd(n, d);
            return Rational{nn, nd};
        } else {
            return Rational{0, 1};
        }
    }
    Rational &operator-=(Rational y) {
        llvm::Optional<Rational> a = *this - y;
        assert(a.hasValue());
        *this = a.getValue();
        return *this;
    }
    llvm::Optional<Rational> operator*(Rational y) const {
        auto [xn, yd] = divgcd(numerator, y.denominator);
        auto [xd, yn] = divgcd(denominator, y.numerator);
        intptr_t n, d;
        bool o1 = __builtin_mul_overflow(xn, yn, &n);
        bool o2 = __builtin_mul_overflow(xd, yd, &d);
        if (o1 | o2) {
            return llvm::Optional<Rational>();
        } else {
            return Rational(n, d);
        }
    }
    Rational &operator*=(Rational y) {
        auto [xn, yd] = divgcd(numerator, y.denominator);
        auto [xd, yn] = divgcd(denominator, y.numerator);
        numerator = xn * yn;
        denominator = xd * yd;
        return *this;
    }
    Rational inv() const {
        if (numerator < 0) {
            // make sure we don't have overflow
            assert(denominator != std::numeric_limits<intptr_t>::min());
            return Rational{-denominator, -numerator};
        } else {
            return Rational{denominator, numerator};
        }
        // return Rational{denominator, numerator};
        // bool positive = numerator > 0;
        // return Rational{positive ? denominator : -denominator,
        //                 positive ? numerator : -numerator};
    }
    llvm::Optional<Rational> operator/(Rational y) const {
        return (*this) * y.inv();
    }
    // *this -= a*b
    bool fnmadd(Rational a, Rational b) {
        if (auto ab = a * b) {
            if (auto c = *this - ab.getValue()) {
                *this = c.getValue();
                return false;
            }
        }
        return true;
    }
    bool div(Rational a) {
        if (auto d = *this / a) {
            *this = d.getValue();
            return false;
        }
        return true;
    }
    // Rational operator/=(Rational y) { return (*this) *= y.inv(); }
    operator double() { return numerator / denominator; }

    bool operator==(Rational y) const {
        return (numerator == y.numerator) & (denominator == y.denominator);
    }
    bool operator!=(Rational y) const {
        return (numerator != y.numerator) | (denominator != y.denominator);
    }
    bool isEqual(intptr_t y) const {
        if (denominator == 1)
            return (numerator == y);
        else if (denominator == -1)
            return (numerator == -y);
        else
            return false;
    }
    bool operator==(int y) const { return isEqual(y); }
    bool operator==(intptr_t y) const { return isEqual(y); }
    bool operator!=(int y) const { return !isEqual(y); }
    bool operator!=(intptr_t y) const { return !isEqual(y); }
    bool operator<(Rational y) const {
        return (widen(numerator) * widen(y.denominator)) <
               (widen(y.numerator) * widen(denominator));
    }
    bool operator<=(Rational y) const {
        return (widen(numerator) * widen(y.denominator)) <=
               (widen(y.numerator) * widen(denominator));
    }
    bool operator>(Rational y) const {
        return (widen(numerator) * widen(y.denominator)) >
               (widen(y.numerator) * widen(denominator));
    }
    bool operator>=(Rational y) const {
        return (widen(numerator) * widen(y.denominator)) >=
               (widen(y.numerator) * widen(denominator));
    }
    bool operator>=(int y) const { return *this >= Rational(y); }

    friend bool isZero(Rational x) { return x.numerator == 0; }
    friend bool isOne(Rational x) { return (x.numerator == x.denominator); }
    bool isInteger() const { return denominator == 1; }
    void negate() { numerator = -numerator; }
    operator bool() const { return numerator != 0; }

    friend std::ostream &operator<<(std::ostream &os, const Rational &x) {
        os << x.numerator;
        if (x.denominator != 1) {
            os << " // " << x.denominator;
        }
        return os;
    }
    void dump() const { std::cout << *this << std::endl; }
};
llvm::Optional<Rational> gcd(Rational x, Rational y) {
    return Rational{std::gcd(x.numerator, y.numerator),
                    std::lcm(x.denominator, y.denominator)};
}
template <typename A>
concept RationalMatrix = requires(A a) {
    { a(size_t(0), size_t(0)) } -> std::same_as<Rational &>;
    { a.size() } -> std::same_as<std::pair<size_t, size_t>>;
    { a.size(0) } -> std::same_as<size_t>;
};
template <typename A>
concept RationalVector = requires(A a) {
    { a[size_t(0)] } -> std::same_as<Rational &>;
    { a.size() } -> std::same_as<size_t>;
};

// template <IntMatrix AM>
