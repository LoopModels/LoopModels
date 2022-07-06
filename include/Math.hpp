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
// #include <mlir/Analysis/Presburger/Matrix.h>
#include <numeric>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
// #ifndef NDEBUG
// #include <memory>
// #include <stacktrace>
// using stacktrace =
//     std::basic_stacktrace<std::allocator<std::stacktrace_entry>>;
// #endif
template <class T>
concept Integral = std::is_integral<T>::value;

static int64_t gcd(int64_t x, int64_t y) {
    if (x == 0) {
        return std::abs(y);
    } else if (y == 0) {
        return std::abs(x);
    }
    assert(x != std::numeric_limits<int64_t>::min());
    assert(y != std::numeric_limits<int64_t>::min());
    int64_t a = std::abs(x);
    int64_t b = std::abs(y);
    if ((a == 1) | (b == 1)) {
        return 1;
    }
    int64_t az = std::countr_zero(uint64_t(x));
    int64_t bz = std::countr_zero(uint64_t(y));
    b >>= bz;
    int64_t k = std::min(az, bz);
    while (a) {
        a >>= az;
        int64_t d = a - b;
        az = std::countr_zero(uint64_t(d));
        b = std::min(a, b);
        a = std::abs(d);
    }
    return b << k;
}
static int64_t lcm(int64_t x, int64_t y) {
    if (std::abs(x) == 1) {
        return y;
    } else if (std::abs(y) == 1) {
        return x;
    } else {
        return x * (y / gcd(x, y));
    }
}
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

std::pair<int64_t, int64_t> divgcd(int64_t x, int64_t y) {
    if (x) {
        if (y) {
            int64_t g = gcd(x, y);
            assert(g == std::gcd(x, y));
            return std::make_pair(x / g, y / g);
        } else {
            return std::make_pair(1, 0);
        }
    } else if (y) {
        return std::make_pair(0, 1);
    } else {
        return std::make_pair(0, 0);
    }
}

// template<typename T> T one(const T) { return T(1); }
struct One {
    operator int64_t() { return 1; };
    operator size_t() { return 1; };
};
bool isOne(int64_t x) { return x == 1; }
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
    int64_t t = std::countr_zero(i) + 1;
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
    int64_t t = std::countr_zero(i) + 1;
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
    int64_t t = std::countr_zero(i) + 1;
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

inline bool isZero(auto x) { return x == 0; }

bool allZero(const auto &x) {
    for (auto &a : x)
        if (!isZero(a))
            return false;
    return true;
}

// template <typename T> inline Vector<T, 0> emptyVector() {
//     return Vector<T, 0>(NULL, 0);
// }

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
    bool operator==(StridedVector<T> x) const {
        if (size() != x.size())
            return false;
        for (size_t i = 0; i < size(); ++i) {
            if ((*this)[i] != x[i])
                return false;
        }
        return true;
    }
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
    inline size_t numCol() const {
        return static_cast<const A *>(this)->numCol();
    }
    inline size_t numRow() const {
        return static_cast<const A *>(this)->numRow();
    }
    inline size_t rowStride() const {
        return static_cast<const A *>(this)->rowStride();
    }
    inline size_t colStride() const {
        return static_cast<const A *>(this)->colStride();
    }
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
        // #ifndef NDEBUG
        // 	if ((i >= numRow()) || (j >= numCol())){
        //         std::cout << "Bounds Error! Accessed (" << numRow() << ", "
        //         << numCol() << ") array at index (" << i << ", " << j <<
        //         ").\n" <<
        //             stacktrace::current() << std::endl;
        assert(i < numRow());
        assert(j < numCol());
        //     }
        // #endif
        return getLinearElement(i * rowStride() + j * colStride());
    }
    const T &operator()(size_t i, size_t j) const {
        const size_t M = numRow();
        assert(i < M);
        assert(j < numCol());
        return getLinearElement(i * rowStride() + j * colStride());
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

    static constexpr size_t getConstCol() { return A::getConstCol(); }
    auto getRow(size_t i) {
        constexpr size_t N = getConstCol();
        if constexpr (N) {
            return PtrVector<T, N>(data() + i * N);
        } else {
            const size_t _N = numCol();
            return llvm::MutableArrayRef<T>(data() + i * rowStride(), _N);
            // return PtrVector<T, 0>{data() + i * _M, _M};
        }
    }
    auto getRow(size_t i) const {
        constexpr size_t N = getConstCol();
        if constexpr (N) {
            return PtrVector<const T, N>(data() + i * N);
        } else {
            const size_t _N = numCol();
            return llvm::ArrayRef<T>(data() + i * rowStride(), _N);
            // return PtrVector<const T, 0>{
        }
    }
    StridedVector<T> getCol(size_t n) {
        return StridedVector<T>{data() + n, numRow(), rowStride()};
    }
    StridedVector<const T> getCol(size_t n) const {
        return StridedVector<const T>{data() + n, numRow(), rowStride()};
    }
};
template <typename T> struct SparseMatrix;
template <typename T> struct PtrMatrix : BaseMatrix<T, PtrMatrix<T>> {
    T *mem;
    const size_t M, N, X;

    // PtrMatrix(T *mem, size_t M, size_t N, size_t X)
    //     : mem(mem), M(M), N(N), X(X){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    T *begin() { return mem; }
    T *end() { return mem + (M * N); }
    const T *begin() const { return mem; }
    const T *end() const { return mem + (M * N); }

    inline size_t numRow() const { return M; }
    inline size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return 0; }

    T *data() { return mem; }
    const T *data() const { return mem; }

    operator PtrMatrix<const T>() const {
        return PtrMatrix<const T>(mem, M, N, X);
    }
    PtrMatrix<T> operator=(SparseMatrix<T> &A) {
        assert(M == A.numRow());
        assert(N == A.numCol());
        size_t k = 0;
        for (size_t i = 0; i < M; ++i) {
            uint32_t m = A.rows[i] & 0x00ffffff;
            size_t j = 0;
            while (m) {
                uint32_t tz = std::countr_zero(m);
                m >>= tz + 1;
                j += tz;
                mem[i * X + (j++)] = A.nonZeros[k++];
            }
        }
        assert(k == A.nonZeros.size());
        return *this;
    }
    PtrMatrix<T> view(size_t rowStart, size_t rowEnd, size_t colStart,
                      size_t colEnd) {
        assert(rowEnd > rowStart);
        assert(colEnd > colStart);
        return PtrMatrix<T>(mem + colStart + rowStart * X, rowEnd - rowStart,
                            colEnd - colStart, X);
    }
    PtrMatrix<T> view(size_t rowEnd, size_t colEnd) {
        return view(0, rowEnd, 0, colEnd);
    }
    PtrMatrix<T> view(size_t rowStart, size_t rowEnd, size_t colStart,
                      size_t colEnd) const {
        assert(rowEnd > rowStart);
        assert(colEnd > colStart);
        return PtrMatrix<T>(mem + colStart + rowStart * X, rowEnd - rowStart,
                            colEnd - colStart, X);
    }
    PtrMatrix<T> view(size_t rowEnd, size_t colEnd) const {
        return view(0, rowEnd, 0, colEnd);
    }
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
    static constexpr size_t numRow() { return M; }
    static constexpr size_t numCol() { return N; }
    static constexpr size_t rowStride() { return N; }
    static constexpr size_t colStride() { return 1; }

    T *data() { return mem; }
    const T *data() const { return mem; }

    static constexpr size_t getConstCol() { return N; }
};

template <typename T, size_t M, size_t S>
struct Matrix<T, M, 0, S> : BaseMatrix<T, Matrix<T, M, 0, S>> {
    llvm::SmallVector<T, S> mem;
    size_t N, X;

    Matrix(size_t n) : mem(llvm::SmallVector<T, S>(M * n)), N(n), X(n){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }
    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }
    static constexpr size_t colStride() { return 1; }

    static constexpr size_t getConstCol() { return 0; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};
template <typename T, size_t N, size_t S>
struct Matrix<T, 0, N, S> : BaseMatrix<T, Matrix<T, 0, N, S>> {
    llvm::SmallVector<T, S> mem;
    size_t M;

    Matrix(size_t m) : mem(llvm::SmallVector<T, S>(m * N)), M(m){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    inline size_t numRow() const { return M; }
    static constexpr size_t numCol() { return N; }
    static constexpr size_t rowStride() { return N; }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return N; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};

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
    inline size_t rowStride() const { return M; }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return 0; }

    T *data() { return mem; }
    const T *data() const { return mem; }
    explicit operator PtrMatrix<const T>() const {
        return PtrMatrix<const T>(mem, M, M, M);
    }
    operator SquarePtrMatrix<const T>() const {
        return SquarePtrMatrix<const T>(mem, M);
    }
};

template <typename T, unsigned STORAGE = 4>
struct SquareMatrix : BaseMatrix<T, SquareMatrix<T, STORAGE>> {
    typedef T eltype;
    static constexpr unsigned TOTALSTORAGE = STORAGE * STORAGE;
    llvm::SmallVector<T, TOTALSTORAGE> mem;
    size_t M;

    SquareMatrix(size_t m)
        : mem(llvm::SmallVector<T, TOTALSTORAGE>(m * m)), M(m){};

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    inline size_t rowStride() const { return M; }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return 0; }

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
        for (size_t r = 0; r < N; ++r)
            A(r, r) = 1;
        return A;
    }
    operator PtrMatrix<T>() { return PtrMatrix<T>(mem.data(), M, M, M); }
    operator PtrMatrix<const T>() const {
        return PtrMatrix<const T>(mem.data(), M, M, M);
    }
    operator SquarePtrMatrix<T>() {
        return SquarePtrMatrix(mem.data(), size_t(M));
    }
};

template <typename T, size_t S>
struct Matrix<T, 0, 0, S> : BaseMatrix<T, Matrix<T, 0, 0, S>> {
    llvm::SmallVector<T, S> mem;

    size_t M, N, X;

    Matrix(size_t m, size_t n)
        : mem(llvm::SmallVector<T, S>(m * n)), M(m), N(n), X(n){};

    Matrix() : M(0), N(0), X(0){};
    Matrix(SquareMatrix<T> &&A)
        : mem(std::move(A.mem)), M(A.M), N(A.M), X(A.M){};
    Matrix(const SquareMatrix<T> &A)
        : mem(A.mem.begin(), A.mem.end()), M(A.M), N(A.M), X(A.M){};

    operator PtrMatrix<const T>() const {
        return PtrMatrix<const T>(mem.data(), M, N, X);
    }
    operator PtrMatrix<T>() {
        return PtrMatrix<T>{.mem = mem.data(), .M = M, .N = N, .X = X};
    }

    inline T &getLinearElement(size_t i) { return mem[i]; }
    inline const T &getLinearElement(size_t i) const { return mem[i]; }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }

    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return 0; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }

    static Matrix<T, 0, 0, S> Uninitialized(size_t MM, size_t NN) {
        Matrix<T, 0, 0, S> A(0, 0);
        A.M = MM;
        A.X = A.N = NN;
        A.mem.resize_for_overwrite(MM * NN);
        return A;
    }
    static Matrix<T, 0, 0, S> identity(size_t MM) {
        Matrix<T, 0, 0, S> A(MM, MM);
        for (size_t i = 0; i < MM; ++i) {
            A(i, i) = 1;
        }
        return A;
    }
    void clear() {
        M = N = X = 0;
        mem.clear();
    }
    void resize(size_t MM, size_t NN, size_t XX) {
        mem.resize(MM * XX);
        if (NN > X) {
            for (size_t m = 1; m < std::min(M, MM); ++m) {
                for (size_t n = 0; n < N; ++n) {
                    mem[m * NN + n] = mem[m * X + n];
                }
                for (size_t n = N; n < NN; ++n) {
                    mem[m * NN + n] = 0;
                }
            }
            X = NN;
        }
        M = MM;
        N = NN;
    }
    void resize(size_t MM, size_t NN) {
        size_t XX = NN > X ? NN : X;
        resize(MM, NN, XX);
    }
    void reserve(size_t MM, size_t NN) { mem.reserve(MM * std::max(X, NN)); }
    void resizeForOverwrite(size_t MM, size_t NN, size_t XX) {
        assert(XX >= NN);
        M = MM;
        N = NN;
        X = XX;
        if (M * X > mem.size())
            mem.resize_for_overwrite(M * X);
    }
    void resizeForOverwrite(size_t MM, size_t NN) {
        M = MM;
        X = N = NN;
        if (M * X > mem.size())
            mem.resize_for_overwrite(M * X);
    }

    void resizeRows(size_t MM) {
        if (MM > M) {
            mem.resize(MM * X);
        }
        M = MM;
    }
    void resizeRowsForOverwrite(size_t MM) {
        if (MM > M) {
            mem.resize_for_overwrite(M * X);
        }
        M = MM;
    }
    void resizeCols(size_t NN) { resize(M, NN); }
    void resizeColsForOverwrite(size_t NN) {
        if (NN > X) {
            X = NN;
            mem.resize_for_overwrite(M * X);
        }
        N = NN;
    }
    void eraseCol(size_t i) {
        assert(i < N);
        // TODO: optimize this to reduce copying
        for (size_t m = 0; m < M; ++m) {
            for (size_t n = 0; n < N; ++n) {
                mem.erase(mem.begin() + m * X + n);
            }
        }
        --N;
        --X;
    }
    void eraseRow(size_t i) {
        assert(i < M);
        auto it = mem.begin() + i * X;
        mem.erase(it, it + X);
        --M;
    }
    void truncateCols(size_t NN) {
        assert(NN <= N);
        N = NN;
    }
    void truncateRows(size_t MM) {
        assert(MM <= M);
        M = MM;
    }
    // Allocates a transposed copy
    Matrix<T, 0, 0, S> transpose() const {
        Matrix<T, 0, 0, S> A(Matrix<T, 0, 0, S>::Uninitialized(N, M));
        for (size_t n = 0; n < N; ++n) {
            for (size_t m = 0; m < M; ++m) {
                A(n, m) = (*this)(m, n);
            }
        }
        return A;
    }

    PtrMatrix<T> view(size_t rowStart, size_t rowEnd, size_t colStart,
                      size_t colEnd) {
        assert(rowEnd > rowStart);
        assert(colEnd > colStart);
        return PtrMatrix<T>(mem.data() + colStart + rowStart * X,
                            rowEnd - rowStart, colEnd - colStart, X);
    }
    PtrMatrix<T> view(size_t rowEnd, size_t colEnd) {
        return view(0, rowEnd, 0, colEnd);
    }
    PtrMatrix<T> view(size_t rowStart, size_t rowEnd, size_t colStart,
                      size_t colEnd) const {
        assert(rowEnd > rowStart);
        assert(colEnd > colStart);
        return PtrMatrix<T>(mem.data() + colStart + rowStart * X,
                            rowEnd - rowStart, colEnd - colStart, X);
    }
    PtrMatrix<T> view(size_t rowEnd, size_t colEnd) const {
        return view(0, rowEnd, 0, colEnd);
    }

    PtrMatrix<T> operator=(PtrMatrix<T> A) {
        assert(M == A.numRow());
        assert(N == A.numCol());
        for (size_t m = 0; m < M; ++m)
            for (size_t n = 0; n < N; ++n)
                mem[n + m * X] = A(m, n);
        return *this;
    }
};
template <typename T> using DynamicMatrix = Matrix<T, 0, 0, 64>;
typedef DynamicMatrix<int64_t> IntMatrix;
static_assert(std::copyable<Matrix<int64_t, 4, 4>>);
static_assert(std::copyable<Matrix<int64_t, 4, 0>>);
static_assert(std::copyable<Matrix<int64_t, 0, 4>>);
static_assert(std::copyable<Matrix<int64_t, 0, 0>>);
static_assert(std::copyable<SquareMatrix<int64_t>>);

template <typename T, typename P>
std::pair<size_t, size_t> size(BaseMatrix<T, P> const &A) {
    return std::make_pair(A.numRow(), A.numCol());
}

template <typename T>
std::ostream &printVector(std::ostream &os, llvm::ArrayRef<T> a) {
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
template <typename T>
std::ostream &printVector(std::ostream &os, const llvm::SmallVectorImpl<T> &a) {
    return printVector(os, llvm::ArrayRef<T>(a));
}

// template <typename T, size_t L>
// std::ostream &operator<<(std::ostream &os, llvm::PtrVector<T, L> const
// &A) {
//     return printVector(os, A);
// }
template <typename T, size_t M, size_t N, size_t L>
std::ostream &operator<<(std::ostream &os, Matrix<T, M, N, L> const &A) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A)
    // {
    return printMatrix(os, A);
}
template <typename T>
std::ostream &operator<<(std::ostream &os, SquareMatrix<T> const &A) {
    return printMatrix(os, A);
}

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

MULTIVERSION void matmul(PtrMatrix<int64_t> C, PtrMatrix<const int64_t> A,
                         PtrMatrix<const int64_t> B) {
    unsigned M = A.numRow();
    unsigned K = A.numCol();
    unsigned N = B.numCol();
    assert(K == B.numRow());
    assert(M == C.numRow());
    assert(N == C.numCol());
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            VECTORIZE
            for (size_t n = 0; n < N; ++n) {
                C(m, n) += A(m, k) * B(k, n);
            }
        }
    }
}
MULTIVERSION IntMatrix matmul(PtrMatrix<const int64_t> A,
                              PtrMatrix<const int64_t> B) {
    unsigned M = A.numRow();
    unsigned N = B.numCol();
    IntMatrix C(M, N);
    matmul(C, A, B);
    return C;
}
MULTIVERSION void matmulnt(PtrMatrix<int64_t> C, PtrMatrix<const int64_t> A,
                           PtrMatrix<const int64_t> B) {
    unsigned M = A.numRow();
    unsigned K = A.numCol();
    unsigned N = B.numRow();
    assert(K == B.numCol());
    assert(M == C.numRow());
    assert(N == C.numCol());
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            VECTORIZE
            for (size_t n = 0; n < N; ++n) {
                C(m, n) += A(m, k) * B(n, k);
            }
        }
    }
}
MULTIVERSION IntMatrix matmulnt(PtrMatrix<const int64_t> A,
                                PtrMatrix<const int64_t> B) {
    unsigned M = A.numRow();
    unsigned N = B.numRow();
    IntMatrix C(M, N);
    matmulnt(C, A, B);
    return C;
}
MULTIVERSION void matmultn(PtrMatrix<int64_t> C, PtrMatrix<const int64_t> A,
                           PtrMatrix<const int64_t> B) {
    unsigned M = A.numCol();
    unsigned K = A.numRow();
    unsigned N = B.numCol();
    assert(K == B.numRow());
    assert(M == C.numRow());
    assert(N == C.numCol());
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            VECTORIZE
            for (size_t n = 0; n < N; ++n) {
                C(m, n) += A(k, m) * B(k, n);
            }
        }
    }
}
MULTIVERSION IntMatrix matmultn(PtrMatrix<const int64_t> A,
                                PtrMatrix<const int64_t> B) {
    unsigned M = A.numCol();
    unsigned N = B.numCol();
    IntMatrix C(M, N);
    matmultn(C, A, B);
    return C;
}
MULTIVERSION void matmultt(PtrMatrix<int64_t> C, PtrMatrix<const int64_t> A,
                           PtrMatrix<const int64_t> B) {
    unsigned M = A.numCol();
    unsigned K = A.numRow();
    unsigned N = B.numRow();
    assert(K == B.numCol());
    assert(M == C.numRow());
    assert(N == C.numCol());
    for (size_t m = 0; m < M; ++m) {
        for (size_t k = 0; k < K; ++k) {
            VECTORIZE
            for (size_t n = 0; n < N; ++n) {
                C(m, n) += A(k, m) * B(n, k);
            }
        }
    }
}
MULTIVERSION IntMatrix matmultt(PtrMatrix<const int64_t> A,
                                PtrMatrix<const int64_t> B) {
    unsigned M = A.numCol();
    unsigned N = B.numRow();
    IntMatrix C(M, N);
    matmultt(C, A, B);
    return C;
}

MULTIVERSION inline void swapRows(PtrMatrix<int64_t> A, size_t i, size_t j) {
    if (i == j)
        return;
    const unsigned int M = A.numRow();
    const unsigned int N = A.numCol();
    assert((i < M) & (j < M));
    VECTORIZE
    for (size_t n = 0; n < N; ++n) {
        std::swap(A(i, n), A(j, n));
    }
}
MULTIVERSION inline void swapCols(PtrMatrix<int64_t> A, size_t i, size_t j) {
    if (i == j) {
        return;
    }
    const unsigned int M = A.numRow();
    const unsigned int N = A.numCol();
    assert((i < N) & (j < N));
    VECTORIZE
    for (size_t m = 0; m < M; ++m) {
        std::swap(A(m, i), A(m, j));
    }
}
template <typename T>
void swapCols(llvm::SmallVectorImpl<T> &A, size_t i, size_t j) {
    std::swap(A[i], A[j]);
}
template <typename T>
void swapRows(llvm::SmallVectorImpl<T> &A, size_t i, size_t j) {
    std::swap(A[i], A[j]);
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
    int64_t numerator;
    int64_t denominator;

    Rational() : numerator(0), denominator(1){};
    Rational(int64_t coef) : numerator(coef), denominator(1){};
    Rational(int coef) : numerator(coef), denominator(1){};
    Rational(int64_t n, int64_t d)
        : numerator(d > 0 ? n : -n), denominator(n ? (d > 0 ? d : -d) : 1) {}
    static Rational create(int64_t n, int64_t d) {
        if (n) {
            int64_t sign = 2 * (d > 0) - 1;
            int64_t g = gcd(n, d);
            n *= sign;
            d *= sign;
            if (g != 1) {
                n /= g;
                d /= g;
            }
            return Rational{n, d};
        } else {
            return Rational{0, 1};
        }
    }
    static Rational createPositiveDenominator(int64_t n, int64_t d) {
        if (n) {
            int64_t g = gcd(n, d);
            if (g != 1) {
                n /= g;
                d /= g;
            }
            return Rational{n, d};
        } else {
            return Rational{0, 1};
        }
    }
    llvm::Optional<Rational> operator+(Rational y) const {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        int64_t a, b, n, d;
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
        int64_t a, b, n, d;
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
    llvm::Optional<Rational> operator*(int64_t y) const {
        auto [xd, yn] = divgcd(denominator, y);
        int64_t n;
        if (__builtin_mul_overflow(numerator, yn, &n)) {
            return llvm::Optional<Rational>();
        } else {
            return Rational{n, xd};
        }
    }
    llvm::Optional<Rational> operator*(Rational y) const {
        if ((numerator != 0) & (y.numerator != 0)) {
            auto [xn, yd] = divgcd(numerator, y.denominator);
            auto [xd, yn] = divgcd(denominator, y.numerator);
            int64_t n, d;
            bool o1 = __builtin_mul_overflow(xn, yn, &n);
            bool o2 = __builtin_mul_overflow(xd, yd, &d);
            if (o1 | o2) {
                return llvm::Optional<Rational>();
            } else {
                return Rational{n, d};
            }
        } else {
            return Rational{0, 1};
        }
    }
    Rational &operator*=(Rational y) {
        if ((numerator != 0) & (y.numerator != 0)) {
            auto [xn, yd] = divgcd(numerator, y.denominator);
            auto [xd, yn] = divgcd(denominator, y.numerator);
            numerator = xn * yn;
            denominator = xd * yd;
        } else {
            numerator = 0;
            denominator = 1;
        }
        return *this;
    }
    Rational inv() const {
        if (numerator < 0) {
            // make sure we don't have overflow
            assert(denominator != std::numeric_limits<int64_t>::min());
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
        if (llvm::Optional<Rational> ab = a * b) {
            if (llvm::Optional<Rational> c = *this - ab.getValue()) {
                *this = c.getValue();
                return false;
            }
        }
        return true;
    }
    bool div(Rational a) {
        if (llvm::Optional<Rational> d = *this / a) {
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
    bool isEqual(int64_t y) const {
        if (denominator == 1)
            return (numerator == y);
        else if (denominator == -1)
            return (numerator == -y);
        else
            return false;
    }
    bool operator==(int y) const { return isEqual(y); }
    bool operator==(int64_t y) const { return isEqual(y); }
    bool operator!=(int y) const { return !isEqual(y); }
    bool operator!=(int64_t y) const { return !isEqual(y); }
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
    return Rational{gcd(x.numerator, y.numerator),
                    lcm(x.denominator, y.denominator)};
}

static void normalizeByGCD(llvm::MutableArrayRef<int64_t> x) {
    if (size_t N = x.size()) {
        if (N == 1) {
            x[0] = 1;
            return;
        }
        int64_t g = gcd(x[0], x[1]);
        for (size_t n = 2; (n < N) & (g != 1); ++n)
            g = gcd(g, x[n]);
        if (g > 1)
            for (auto &&a : x)
                a /= g;
    }
}

template <typename T>
std::ostream &printMatrixImpl(std::ostream &os, PtrMatrix<const T> A) {
    // std::ostream &printMatrix(std::ostream &os, T const &A) {
    auto [m, n] = A.size();
    for (size_t i = 0; i < m; i++) {
        if (i) {
            os << "  ";
        } else {
            os << "[ ";
        }
        for (int64_t j = 0; j < int64_t(n) - 1; j++) {
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

template <typename T> struct SparseMatrix {
    // non-zeros
    llvm::SmallVector<T> nonZeros;
    // masks, the upper 8 bits give the number of elements in previous rows
    // the remaining 24 bits are a mask indicating non-zeros within this row
    llvm::SmallVector<uint32_t> rows;
    size_t col;
    size_t numRow() const { return rows.size(); }
    size_t numCol() const { return col; }
    SparseMatrix(size_t numRows, size_t numCols)
        : nonZeros{}, rows{llvm::SmallVector<uint32_t>(numRows)}, col{numCols} {
        assert(col <= 24);
    }
    T get(size_t i, size_t j) const {
        assert(j < col);
        uint32_t r(rows[i]);
        uint32_t jshift = uint32_t(1) << j;
        if (r & (jshift)) {
            // offset from previous rows
            uint32_t prevRowOffset = r >> 24;
            uint32_t rowOffset = std::popcount(r & (jshift - 1));
            return nonZeros[rowOffset + prevRowOffset];
        } else {
            return 0;
        }
    }
    inline T operator()(size_t i, size_t j) const { return get(i, j); }
    void insert(T x, size_t i, size_t j) {
        assert(j < col);
        uint32_t r{rows[i]};
        uint32_t jshift = uint32_t(1) << j;
        // offset from previous rows
        uint32_t prevRowOffset = r >> 24;
        uint32_t rowOffset = std::popcount(r & (jshift - 1));
        size_t k = rowOffset + prevRowOffset;
        if (r & jshift) {
            nonZeros[k] = std::move(x);
        } else {
            nonZeros.insert(nonZeros.begin() + k, std::move(x));
            rows[i] = r | jshift;
            for (size_t k = i + 1; k < rows.size(); ++k)
                rows[k] += uint32_t(1) << 24;
        }
    }

    struct Reference {
        SparseMatrix<T> *A;
        size_t i, j;
        operator T() const { return A->get(i, j); }
        void operator=(T x) {
            A->insert(std::move(x), i, j);
            return;
        }
    };
    Reference operator()(size_t i, size_t j) { return Reference{this, i, j}; }
    operator DynamicMatrix<T>() {
        DynamicMatrix<T> A(numRow(), numCol());
        size_t k = 0;
        for (size_t i = 0; i < numRow(); ++i) {
            uint32_t m = rows[i] & 0x00ffffff;
            size_t j = 0;
            while (m) {
                uint32_t tz = std::countr_zero(m);
                m >>= tz + 1;
                j += tz;
                A(i, j++) = nonZeros[k++];
            }
        }
        assert(k == nonZeros.size());
        return A;
    }
};

template <typename T>
std::ostream &operator<<(std::ostream &os, SparseMatrix<T> const &A) {
    size_t k = 0;
    os << "[ ";
    for (size_t i = 0; i < A.numRow(); ++i) {
        if (i)
            os << "  ";
        uint32_t m = A.rows[i] & 0x00ffffff;
        size_t j = 0;
        while (m) {
            if (j)
                os << " ";
            uint32_t tz = std::countr_zero(m);
            m >>= (tz + 1);
            j += (tz + 1);
            while (tz--)
                os << " 0 ";
            const T &x = A.nonZeros[k++];
            if (x >= 0)
                os << " ";
            os << x;
        }
        for (; j < A.numCol(); ++j)
            os << "  0";
        os << "\n";
    }
    os << " ]";
    assert(k == A.nonZeros.size());
}

std::ostream &printMatrix(std::ostream &os, PtrMatrix<const Rational> A) {
    return printMatrixImpl(os, A);
}
std::ostream &printMatrix(std::ostream &os, PtrMatrix<const int64_t> A) {
    return printMatrixImpl(os, A);
}
