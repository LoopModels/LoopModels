#pragma once
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include "./Macro.hpp"
#include "./TypePromotion.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
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

template <typename R>
concept AbstractRange = requires(R r) {
                            { r.begin() };
                            { r.end() };
                        };
llvm::raw_ostream &printRange(llvm::raw_ostream &os, AbstractRange auto &r) {
    os << "[ ";
    bool needComma = false;
    for (auto x : r) {
        if (needComma)
            os << ", ";
        os << x;
        needComma = true;
    }
    os << " ]";
    return os;
}

[[maybe_unused]] static int64_t gcd(int64_t x, int64_t y) {
    if (x == 0) {
        return std::abs(y);
    } else if (y == 0) {
        return std::abs(x);
    }
    assert(x != std::numeric_limits<int64_t>::min());
    assert(y != std::numeric_limits<int64_t>::min());
    int64_t a = std::abs(x);
    int64_t b = std::abs(y);
    if ((a == 1) | (b == 1))
        return 1;
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
[[maybe_unused]] static int64_t lcm(int64_t x, int64_t y) {
    if (std::abs(x) == 1)
        return y;
    if (std::abs(y) == 1)
        return x;
    return x * (y / gcd(x, y));
}
// https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm
template <std::integral T> std::tuple<T, T, T> gcdx(T a, T b) {
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
    if (isOne(x))
        return T(One());
    int64_t t = std::countr_zero(i) + 1;
    i >>= t;
    // T z(std::move(x));
    T z(std::forward<TRC>(x));
    T b;
    while (--t) {
        b = z;
        z *= b;
    }
    if (i == 0)
        return z;
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
concept HasMul = requires(T t) { t.mul(t, t); };

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
    if (i == 0)
        return;
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
    if (isOne(x))
        return T(One());
    int64_t t = std::countr_zero(i) + 1;
    i >>= t;
    // T z(std::move(x));
    T z(std::forward<TRC>(x));
    T b;
    while (--t) {
        b.mul(z, z);
        std::swap(b, z);
    }
    if (i == 0)
        return z;
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

inline bool isZero(auto x) { return x == 0; }

[[maybe_unused]] static bool allZero(const auto &x) {
    for (auto &a : x)
        if (!isZero(a))
            return false;
    return true;
}
[[maybe_unused]] static bool allGEZero(const auto &x) {
    for (auto &a : x)
        if (a < 0)
            return false;
    return true;
}
[[maybe_unused]] static bool allLEZero(const auto &x) {
    for (auto &a : x)
        if (a > 0)
            return false;
    return true;
}

[[maybe_unused]] static size_t countNonZero(const auto &x) {
    size_t i = 0;
    for (auto &a : x)
        i += (a != 0);
    return i;
}

template <typename T>
concept AbstractVector =
    HasEltype<T> && requires(T t, size_t i) {
                        { t(i) } -> std::convertible_to<eltype_t<T>>;
                        { t.size() } -> std::convertible_to<size_t>;
                        { t.view() };
                        {
                            std::remove_reference_t<T>::canResize
                            } -> std::same_as<const bool &>;
                        // {t.extendOrAssertSize(i)};
                    };
// template <typename T>
// concept AbstractMatrix = HasEltype<T> && requires(T t, size_t i) {
//     { t(i, i) } -> std::convertible_to<typename T::eltype>;
//     { t.numRow() } -> std::convertible_to<size_t>;
//     { t.numCol() } -> std::convertible_to<size_t>;
// };
template <typename T>
concept AbstractMatrixCore =
    HasEltype<T> && requires(T t, size_t i) {
                        { t(i, i) } -> std::convertible_to<eltype_t<T>>;
                        { t.numRow() } -> std::convertible_to<size_t>;
                        { t.numCol() } -> std::convertible_to<size_t>;
                        {
                            std::remove_reference_t<T>::canResize
                            } -> std::same_as<const bool &>;
                        // {t.extendOrAssertSize(i, i)};
                    };
template <typename T>
concept AbstractMatrix =
    AbstractMatrixCore<T> && requires(T t, size_t i) {
                                 { t.view() } -> AbstractMatrixCore;
                             };

inline auto &copyto(AbstractVector auto &y, const AbstractVector auto &x) {
    const size_t M = x.size();
    y.extendOrAssertSize(M);
    for (size_t i = 0; i < M; ++i)
        y(i) = x(i);
    return y;
}
inline auto &copyto(AbstractMatrixCore auto &A,
                    const AbstractMatrixCore auto &B) {
    const size_t M = B.numRow();
    const size_t N = B.numCol();
    A.extendOrAssertSize(M, N);
    for (size_t r = 0; r < M; ++r)
        for (size_t c = 0; c < N; ++c)
            A(r, c) = B(r, c);
    return A;
}

struct Add {
    constexpr auto operator()(auto x, auto y) const { return x + y; }
};
struct Sub {
    constexpr auto operator()(auto x) const { return -x; }
    constexpr auto operator()(auto x, auto y) const { return x - y; }
};
struct Mul {
    constexpr auto operator()(auto x, auto y) const { return x * y; }
};
struct Div {
    constexpr auto operator()(auto x, auto y) const { return x / y; }
};

template <typename Op, typename A> struct ElementwiseUnaryOp {
    using eltype = typename A::eltype;
    [[no_unique_address]] const Op op;
    [[no_unique_address]] const A a;
    static constexpr bool canResize = false;
    auto operator()(size_t i) const { return op(a(i)); }
    auto operator()(size_t i, size_t j) const { return op(a(i, j)); }

    size_t size() const { return a.size(); }
    size_t numRow() const { return a.numRow(); }
    size_t numCol() const { return a.numCol(); }
    inline auto view() const { return *this; };
};
// scalars broadcast
inline auto get(const std::integral auto A, size_t) { return A; }
inline auto get(const std::floating_point auto A, size_t) { return A; }
inline auto get(const std::integral auto A, size_t, size_t) { return A; }
inline auto get(const std::floating_point auto A, size_t, size_t) { return A; }
inline auto get(const AbstractVector auto &A, size_t i) { return A(i); }
inline auto get(const AbstractMatrix auto &A, size_t i, size_t j) {
    return A(i, j);
}

constexpr size_t size(const std::integral auto) { return 1; }
constexpr size_t size(const std::floating_point auto) { return 1; }
inline size_t size(const AbstractVector auto &x) { return x.size(); }

struct Rational;
template <typename T>
concept Scalar =
    std::integral<T> || std::floating_point<T> || std::same_as<T, Rational>;

template <typename T>
concept VectorOrScalar = AbstractVector<T> || Scalar<T>;
template <typename T>
concept MatrixOrScalar = AbstractMatrix<T> || Scalar<T>;

template <typename Op, VectorOrScalar A, VectorOrScalar B>
struct ElementwiseVectorBinaryOp {
    using eltype = promote_eltype_t<A, B>;
    [[no_unique_address]] Op op;
    [[no_unique_address]] A a;
    [[no_unique_address]] B b;
    static constexpr bool canResize = false;
    auto operator()(size_t i) const { return op(get(a, i), get(b, i)); }
    size_t size() const {
        if constexpr (AbstractVector<A> && AbstractVector<B>) {
            const size_t N = a.size();
            assert(N == b.size());
            return N;
        } else if constexpr (AbstractVector<A>) {
            return a.size();
        } else { // if constexpr (AbstractVector<B>) {
            return b.size();
        }
    }
    auto &view() const { return *this; };
};

template <typename Op, MatrixOrScalar A, MatrixOrScalar B>
struct ElementwiseMatrixBinaryOp {
    using eltype = promote_eltype_t<A, B>;
    [[no_unique_address]] Op op;
    [[no_unique_address]] A a;
    [[no_unique_address]] B b;
    static constexpr bool canResize = false;
    auto operator()(size_t i, size_t j) const {
        return op(get(a, i, j), get(b, i, j));
    }
    size_t numRow() const {
        static_assert(AbstractMatrix<A> || std::integral<A> ||
                          std::floating_point<A>,
                      "Argument A to elementwise binary op is not a matrix.");
        static_assert(AbstractMatrix<B> || std::integral<B> ||
                          std::floating_point<B>,
                      "Argument B to elementwise binary op is not a matrix.");
        if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
            const size_t N = a.numRow();
            assert(N == b.numRow());
            return N;
        } else if constexpr (AbstractMatrix<A>) {
            return a.numRow();
        } else if constexpr (AbstractMatrix<B>) {
            return b.numRow();
        }
    }
    size_t numCol() const {
        static_assert(AbstractMatrix<A> || std::integral<A> ||
                          std::floating_point<A>,
                      "Argument A to elementwise binary op is not a matrix.");
        static_assert(AbstractMatrix<B> || std::integral<B> ||
                          std::floating_point<B>,
                      "Argument B to elementwise binary op is not a matrix.");
        if constexpr (AbstractMatrix<A> && AbstractMatrix<B>) {
            const size_t N = a.numCol();
            assert(N == b.numCol());
            return N;
        } else if constexpr (AbstractMatrix<A>) {
            return a.numCol();
        } else if constexpr (AbstractMatrix<B>) {
            return b.numCol();
        }
    }
    auto &view() const { return *this; };
};

template <typename A> struct Transpose {
    using eltype = eltype_t<A>;
    [[no_unique_address]] A a;
    static constexpr bool canResize = false;
    auto operator()(size_t i, size_t j) const { return a(j, i); }
    size_t numRow() const { return a.numCol(); }
    size_t numCol() const { return a.numRow(); }
    auto &view() const { return *this; };
};
template <AbstractMatrix A, AbstractMatrix B> struct MatMatMul {
    using eltype = promote_eltype_t<A, B>;
    [[no_unique_address]] A a;
    [[no_unique_address]] B b;
    static constexpr bool canResize = false;
    auto operator()(size_t i, size_t j) const {
        static_assert(AbstractMatrix<B>, "B should be an AbstractMatrix");
        auto s = (a(i, 0) * b(0, j)) * 0;
        for (size_t k = 0; k < a.numCol(); ++k)
            s += a(i, k) * b(k, j);
        return s;
    }
    size_t numRow() const { return a.numRow(); }
    size_t numCol() const { return b.numCol(); }
    inline auto view() const { return *this; };
};
template <AbstractMatrix A, AbstractVector B> struct MatVecMul {
    using eltype = promote_eltype_t<A, B>;
    [[no_unique_address]] A a;
    [[no_unique_address]] B b;
    static constexpr bool canResize = false;
    auto operator()(size_t i) const {
        static_assert(AbstractVector<B>, "B should be an AbstractVector");
        auto s = (a(i, 0) * b(0)) * 0;
        for (size_t k = 0; k < a.numCol(); ++k)
            s += a(i, k) * b(k);
        return s;
    }
    size_t size() const { return a.numRow(); }
    inline auto view() const { return *this; };
};

struct Begin {
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Begin) {
        return os << 0;
    }
} begin;
struct End {
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os, End) {
        return os << "end";
    }
} end;
struct OffsetBegin {
    size_t offset;
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os, OffsetBegin r) {
        return os << r.offset;
    }
};
constexpr OffsetBegin operator+(size_t x, Begin) { return OffsetBegin{x}; }
constexpr OffsetBegin operator+(Begin, size_t x) { return OffsetBegin{x}; }
constexpr OffsetBegin operator+(size_t x, OffsetBegin y) {
    return OffsetBegin{x + y.offset};
}
inline OffsetBegin operator+(OffsetBegin y, size_t x) {
    return OffsetBegin{x + y.offset};
}
struct OffsetEnd {
    size_t offset;
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os, OffsetEnd r) {
        return os << "end - " << r.offset;
    }
};
constexpr OffsetEnd operator-(End, size_t x) { return OffsetEnd{x}; }
constexpr OffsetEnd operator-(OffsetEnd y, size_t x) {
    return OffsetEnd{y.offset + x};
}
constexpr OffsetEnd operator+(OffsetEnd y, size_t x) {
    return OffsetEnd{y.offset - x};
}

template <typename B, typename E> struct Range {
    [[no_unique_address]] B b;
    [[no_unique_address]] E e;
};
template <std::integral B, std::integral E> struct Range<B, E> {
    [[no_unique_address]] B b;
    [[no_unique_address]] E e;
    struct Iterator {
        B i;
        bool operator==(E e) { return i == e; }
        Iterator &operator++() {
            ++i;
            return *this;
        }
        Iterator operator++(int) {
            Iterator t = *this;
            ++*this;
            return t;
        }
        Iterator &operator--() {
            --i;
            return *this;
        }
        Iterator operator--(int) {
            Iterator t = *this;
            --*this;
            return t;
        }
        B operator*() { return i; }
    };
    constexpr Iterator begin() const { return Iterator{b}; }
    constexpr E end() const { return e; }
    constexpr auto size() const { return e - b; }
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Range<B, E> r) {
        return os << "[" << r.b << ":" << r.e << ")";
    }
};
// template <typename B, typename E>
// constexpr B std::ranges::begin(Range<B,E> r){ return r.b;}

// template <> struct std::iterator_traits<Range<size_t,size_t>> {
//     using difference_type = ptrdiff_t;
//     using iterator_category = std::forward_iterator_tag;
//     using value_type = size_t;
//     using reference_type = void;
//     using pointer_type = void;
// };

// static_assert(std::ranges::range<Range<size_t, size_t>>);

// template <> struct Range<Begin, int> {
//     static constexpr Begin b = begin;
//     int e;
//     operator Range<Begin, size_t>() {
//         return Range<Begin, size_t>{b, size_t(e)};
//     }
// };
// template <> struct Range<int, End> {
//     int b;
//     static constexpr End e = end;
//     operator Range<size_t, End>() { return Range<size_t, End>{size_t(b), e};
//     }
// };
// template <> struct Range<int, int> {
//     int b;
//     int e;
//     operator Range<size_t, size_t>() {
//         return Range<size_t, size_t>{.b = size_t(b), .e = size_t(e)};
//     }
// };
// template <> struct Range<Begin, size_t> {
//     static constexpr Begin b = begin;
//     size_t e;
//     Range(Range<Begin, int> r) : e(r.e){};
// };
// template <> struct Range<size_t, End> {
//     size_t b;
//     static constexpr End e = end;
//     Range(Range<int, End> r) : b(r.b){};
// };
// template <> struct Range<size_t,size_t> {
//     size_t b;
//     size_t e;
//     Range(Range<int, int> r) : b(r.b), e(r.e) {};
// };
struct Colon {
    constexpr Range<size_t, size_t> operator()(std::integral auto i,
                                               std::integral auto j) const {
        return Range<size_t, size_t>{size_t(i), size_t(j)};
    }
    template <typename B, typename E>
    constexpr Range<B, E> operator()(B i, E j) const {
        return Range<B, E>{i, j};
    }
} _;

constexpr size_t canonicalize(size_t e, size_t) { return e; }
constexpr size_t canonicalize(Begin, size_t) { return 0; }
constexpr size_t canonicalize(OffsetBegin b, size_t) { return b.offset; }
constexpr size_t canonicalize(End, size_t M) { return M; }
constexpr size_t canonicalize(OffsetEnd e, size_t M) { return M - e.offset; }

template <typename B, typename E>
constexpr Range<size_t, size_t> canonicalizeRange(Range<B, E> r, size_t M) {
    return Range<size_t, size_t>{canonicalize(r.b, M), canonicalize(r.e, M)};
}

template <typename B, typename E>
constexpr auto operator+(Range<B, E> r, size_t x) {
    return _(r.b + x, r.e + x);
}
template <typename B, typename E>
constexpr auto operator-(Range<B, E> r, size_t x) {
    return _(r.b - x, r.e - x);
}

template <typename T> struct PtrVector {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    using eltype = T;
    [[no_unique_address]] const T *const mem;
    [[no_unique_address]] const size_t N;
    static constexpr bool canResize = false;
    bool operator==(AbstractVector auto &x) {
        if (N != x.size())
            return false;
        for (size_t n = 0; n < N; ++n)
            if (mem[n] != x(n))
                return false;
        return true;
    }

    const T &operator[](size_t i) const {
        assert(i < N);
        return mem[i];
    }
    const T &operator()(size_t i) const {
        assert(i < N);
        return mem[i];
    }
    const T &operator()(End) const {
        assert(N);
        return mem[N - 1];
    }
    PtrVector<T> operator()(Range<size_t, size_t> i) const {
        assert(i.b <= i.e);
        assert(i.e <= N);
        return PtrVector<T>{.mem = mem + i.b, .N = i.e - i.b};
    }
    template <typename F, typename L>
    PtrVector<T> operator()(Range<F, L> i) const {
        return (*this)(canonicalizeRange(i, N));
    }
    const T *begin() const { return mem; }
    const T *end() const { return mem + N; }
    auto rbegin() const { return std::reverse_iterator(mem + N); }
    auto rend() const { return std::reverse_iterator(mem); }
    size_t size() const { return N; }
    operator llvm::ArrayRef<T>() const { return llvm::ArrayRef<T>{mem, N}; }
    // llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
    bool operator==(const PtrVector<T> x) const {
        return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
    }
    bool operator==(const llvm::ArrayRef<std::remove_const_t<T>> x) const {
        return llvm::ArrayRef<std::remove_const_t<T>>(*this) == x;
    }
    PtrVector<T> view() const { return *this; };

    void extendOrAssertSize(size_t M) const { assert(M == N); }
};
template <typename T> struct MutPtrVector {
    static_assert(!std::is_const_v<T>, "T shouldn't be const");
    using eltype = T;
    // using eltype = std::remove_const_t<T>;
    [[no_unique_address]] T *const mem;
    [[no_unique_address]] const size_t N;
    static constexpr bool canResize = false;
    T &operator[](size_t i) {
        assert(i < N);
        return mem[i];
    }
    const T &operator[](size_t i) const {
        assert(i < N);
        return mem[i];
    }
    T &operator()(size_t i) {
        assert(i < N);
        return mem[i];
    }
    const T &operator()(size_t i) const {
        assert(i < N);
        return mem[i];
    }
    T &operator()(End) {
        assert(N);
        return mem[N - 1];
    }
    const T &operator()(End) const {
        assert(N);
        return mem[N - 1];
    }
    T &operator()(OffsetEnd oe) {
        assert(N);
        assert(N > oe.offset);
        return mem[N - 1 - oe.offset];
    }
    const T &operator()(OffsetEnd oe) const {
        assert(N);
        assert(N > oe.offset);
        return mem[N - 1 - oe.offset];
    }
    // copy constructor
    // MutPtrVector(const MutPtrVector<T> &x) : mem(x.mem), N(x.N) {}
    MutPtrVector(const MutPtrVector<T> &x) = default;
    MutPtrVector(llvm::MutableArrayRef<T> x) : mem(x.data()), N(x.size()) {}
    MutPtrVector(T *mem, size_t N) : mem(mem), N(N) {}
    MutPtrVector<T> operator()(Range<size_t, size_t> i) {
        assert(i.b <= i.e);
        assert(i.e <= N);
        return MutPtrVector<T>{mem + i.b, i.e - i.b};
    }
    PtrVector<T> operator()(Range<size_t, size_t> i) const {
        assert(i.b <= i.e);
        assert(i.e <= N);
        return PtrVector<T>{.mem = mem + i.b, .N = i.e - i.b};
    }
    template <typename F, typename L>
    MutPtrVector<T> operator()(Range<F, L> i) {
        return (*this)(canonicalizeRange(i, N));
    }
    template <typename F, typename L>
    PtrVector<T> operator()(Range<F, L> i) const {
        return (*this)(canonicalizeRange(i, N));
    }
    T *begin() { return mem; }
    T *end() { return mem + N; }
    const T *begin() const { return mem; }
    const T *end() const { return mem + N; }
    size_t size() const { return N; }
    operator PtrVector<T>() const { return PtrVector<T>{.mem = mem, .N = N}; }
    operator llvm::ArrayRef<T>() const { return llvm::ArrayRef<T>{mem, N}; }
    operator llvm::MutableArrayRef<T>() {
        return llvm::MutableArrayRef<T>{mem, N};
    }
    // llvm::ArrayRef<T> arrayref() const { return llvm::ArrayRef<T>(ptr, M); }
    bool operator==(const MutPtrVector<T> x) const {
        return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
    }
    bool operator==(const PtrVector<T> x) const {
        return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
    }
    bool operator==(const llvm::ArrayRef<T> x) const {
        return llvm::ArrayRef<T>(*this) == x;
    }
    PtrVector<T> view() const { return *this; };
    // PtrVector<T> view() const {
    //     return PtrVector<T>{.mem = mem, .N = N};
    // };
    MutPtrVector<T> operator=(PtrVector<T> x) { return copyto(*this, x); }
    MutPtrVector<T> operator=(MutPtrVector<T> x) { return copyto(*this, x); }
    MutPtrVector<T> operator=(const AbstractVector auto &x) {
        return copyto(*this, x);
    }
    MutPtrVector<T> operator=(std::integral auto x) {
        for (auto &&y : *this)
            y = x;
        return *this;
    }
    MutPtrVector<T> operator+=(const AbstractVector auto &x) {
        assert(N == x.size());
        for (size_t i = 0; i < N; ++i)
            mem[i] += x(i);
        return *this;
    }
    MutPtrVector<T> operator-=(const AbstractVector auto &x) {
        assert(N == x.size());
        for (size_t i = 0; i < N; ++i)
            mem[i] -= x(i);
        return *this;
    }
    MutPtrVector<T> operator*=(const AbstractVector auto &x) {
        assert(N == x.size());
        for (size_t i = 0; i < N; ++i)
            mem[i] *= x(i);
        return *this;
    }
    MutPtrVector<T> operator/=(const AbstractVector auto &x) {
        assert(N == x.size());
        for (size_t i = 0; i < N; ++i)
            mem[i] /= x(i);
        return *this;
    }
    MutPtrVector<T> operator+=(const std::integral auto x) {
        for (size_t i = 0; i < N; ++i)
            mem[i] += x;
        return *this;
    }
    MutPtrVector<T> operator-=(const std::integral auto x) {
        for (size_t i = 0; i < N; ++i)
            mem[i] -= x;
        return *this;
    }
    MutPtrVector<T> operator*=(const std::integral auto x) {
        for (size_t i = 0; i < N; ++i)
            mem[i] *= x;
        return *this;
    }
    MutPtrVector<T> operator/=(const std::integral auto x) {
        for (size_t i = 0; i < N; ++i)
            mem[i] /= x;
        return *this;
    }
    void extendOrAssertSize(size_t M) const { assert(M == N); }
};

//
// Vectors
//

[[maybe_unused]] static int64_t gcd(PtrVector<int64_t> x) {
    int64_t g = std::abs(x[0]);
    for (size_t i = 1; i < x.size(); ++i)
        g = gcd(g, x[i]);
    return g;
}

template <typename T> constexpr auto view(llvm::SmallVectorImpl<T> &x) {
    return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> constexpr auto view(const llvm::SmallVectorImpl<T> &x) {
    return PtrVector<T>{.mem = x.data(), .N = x.size()};
}
template <typename T> constexpr auto view(llvm::MutableArrayRef<T> x) {
    return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> constexpr auto view(llvm::ArrayRef<T> x) {
    return PtrVector<T>{.mem = x.data(), .N = x.size()};
}

template <typename T> struct Vector {
    using eltype = T;
    [[no_unique_address]] llvm::SmallVector<T, 16> data;
    static constexpr bool canResize = true;

    Vector(int N) : data(llvm::SmallVector<T>(N)){};
    Vector(size_t N = 0) : data(llvm::SmallVector<T>(N)){};
    Vector(llvm::SmallVector<T> A) : data(std::move(A)){};

    T &operator()(size_t i) {
        assert(i < data.size());
        return data[i];
    }
    const T &operator()(size_t i) const {
        assert(i < data.size());
        return data[i];
    }
    MutPtrVector<T> operator()(Range<size_t, size_t> i) {
        assert(i.b <= i.e);
        assert(i.e <= data.size());
        return MutPtrVector<T>{data.data() + i.b, i.e - i.b};
    }
    PtrVector<T> operator()(Range<size_t, size_t> i) const {
        assert(i.b <= i.e);
        assert(i.e <= data.size());
        return PtrVector<T>{.mem = data.data() + i.b, .N = i.e - i.b};
    }
    template <typename F, typename L>
    MutPtrVector<T> operator()(Range<F, L> i) {
        return (*this)(canonicalizeRange(i, data.size()));
    }
    template <typename F, typename L>
    PtrVector<T> operator()(Range<F, L> i) const {
        return (*this)(canonicalizeRange(i, data.size()));
    }
    T &operator[](size_t i) { return data[i]; }
    const T &operator[](size_t i) const { return data[i]; }
    // bool operator==(Vector<T, 0> x0) const { return allMatch(*this, x0); }
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    size_t size() const { return data.size(); }
    // MutPtrVector<T> view() {
    //     return MutPtrVector<T>{.mem = data.data(), .N = data.size()};
    // };
    PtrVector<T> view() const {
        return PtrVector<T>{.mem = data.data(), .N = data.size()};
    };
    template <typename A> void push_back(A &&x) {
        data.push_back(std::forward<A>(x));
    }
    template <typename... A> void emplace_back(A &&...x) {
        data.emplace_back(std::forward<A>(x)...);
    }
    Vector(const AbstractVector auto &x) : data(llvm::SmallVector<T>{}) {
        const size_t N = x.size();
        data.resize_for_overwrite(N);
        for (size_t n = 0; n < N; ++n)
            data[n] = x(n);
    }
    void resize(size_t N) { data.resize(N); }
    void resizeForOverwrite(size_t N) { data.resize_for_overwrite(N); }

    operator MutPtrVector<T>() {
        return MutPtrVector<T>{data.data(), data.size()};
    }
    operator PtrVector<T>() const {
        return PtrVector<T>{.mem = data.data(), .N = data.size()};
    }
    operator llvm::MutableArrayRef<T>() {
        return llvm::MutableArrayRef<T>{data.data(), data.size()};
    }
    operator llvm::ArrayRef<T>() const {
        return llvm::ArrayRef<T>{data.data(), data.size()};
    }
    // MutPtrVector<T> operator=(AbstractVector auto &x) {
    Vector<T> &operator=(const T &x) {
        MutPtrVector<T> y{*this};
        y = x;
        return *this;
    }
    Vector<T> &operator=(AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y = x;
        return *this;
    }
    Vector<T> &operator+=(AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y += x;
        return *this;
    }
    Vector<T> &operator-=(AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y -= x;
        return *this;
    }
    Vector<T> &operator*=(AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y *= x;
        return *this;
    }
    Vector<T> &operator/=(AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y /= x;
        return *this;
    }
    Vector<T> &operator+=(const std::integral auto x) {
        for (auto &&y : data)
            y += x;
        return *this;
    }
    Vector<T> &operator-=(const std::integral auto x) {
        for (auto &&y : data)
            y -= x;
        return *this;
    }
    Vector<T> &operator*=(const std::integral auto x) {
        for (auto &&y : data)
            y *= x;
        return *this;
    }
    Vector<T> &operator/=(const std::integral auto x) {
        for (auto &&y : data)
            y /= x;
        return *this;
    }
    template <typename... Ts> Vector(Ts... inputs) : data{inputs...} {}
    void clear() { data.clear(); }
    void extendOrAssertSize(size_t N) const { assert(N == data.size()); }
    void extendOrAssertSize(size_t N) {
        if (N != data.size())
            data.resize_for_overwrite(N);
    }
    bool operator==(const Vector<T> &x) const {
        return llvm::ArrayRef<T>(*this) == llvm::ArrayRef<T>(x);
    }
    void pushBack(T x) { data.push_back(std::move(x)); }
};

static_assert(std::copyable<Vector<intptr_t>>);
static_assert(AbstractVector<Vector<int64_t>>);
static_assert(!AbstractVector<int64_t>);

template <typename T> struct StridedVector {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    using eltype = T;
    [[no_unique_address]] const T *const d;
    [[no_unique_address]] const size_t N;
    [[no_unique_address]] const size_t x;
    static constexpr bool canResize = false;
    struct StridedIterator {
        [[no_unique_address]] const T *d;
        [[no_unique_address]] size_t x;
        auto operator++() {
            d += x;
            return *this;
        }
        auto operator--() {
            d -= x;
            return *this;
        }
        const T &operator*() { return *d; }
        bool operator==(const StridedIterator y) const { return d == y.d; }
    };
    auto begin() const { return StridedIterator{d, x}; }
    auto end() const { return StridedIterator{d + N * x, x}; }
    const T &operator[](size_t i) const { return d[i * x]; }
    const T &operator()(size_t i) const { return d[i * x]; }

    StridedVector<T> operator()(Range<size_t, size_t> i) const {
        return StridedVector<T>{.d = d + i.b * x, .N = i.e - i.b, .x = x};
    }
    template <typename F, typename L>
    StridedVector<T> operator()(Range<F, L> i) const {
        return (*this)(canonicalizeRange(i, N));
    }

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
    StridedVector<T> view() const { return *this; }
    void extendOrAssertSize(size_t M) const { assert(N == M); }
};
template <typename T> struct MutStridedVector {
    static_assert(!std::is_const_v<T>, "T should not be const");
    using eltype = T;
    [[no_unique_address]] T *const d;
    [[no_unique_address]] const size_t N;
    [[no_unique_address]] const size_t x;
    static constexpr bool canResize = false;
    struct StridedIterator {
        [[no_unique_address]] T *d;
        [[no_unique_address]] size_t x;
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
    // FIXME: if `x` == 0, then it will not iterate!
    auto begin() { return StridedIterator{d, x}; }
    auto end() { return StridedIterator{d + N * x, x}; }
    auto begin() const { return StridedIterator{d, x}; }
    auto end() const { return StridedIterator{d + N * x, x}; }
    T &operator[](size_t i) { return d[i * x]; }
    const T &operator[](size_t i) const { return d[i * x]; }
    T &operator()(size_t i) { return d[i * x]; }
    const T &operator()(size_t i) const { return d[i * x]; }

    MutStridedVector<T> operator()(Range<size_t, size_t> i) {
        return MutStridedVector<T>{.d = d + i.b * x, .N = i.e - i.b, .x = x};
    }
    StridedVector<T> operator()(Range<size_t, size_t> i) const {
        return StridedVector<T>{.d = d + i.b * x, .N = i.e - i.b, .x = x};
    }
    template <typename F, typename L>
    MutStridedVector<T> operator()(Range<F, L> i) {
        return (*this)(canonicalizeRange(i, N));
    }
    template <typename F, typename L>
    StridedVector<T> operator()(Range<F, L> i) const {
        return (*this)(canonicalizeRange(i, N));
    }

    size_t size() const { return N; }
    // bool operator==(StridedVector<T> x) const {
    //     if (size() != x.size())
    //         return false;
    //     for (size_t i = 0; i < size(); ++i) {
    //         if ((*this)[i] != x[i])
    //             return false;
    //     }
    //     return true;
    // }
    operator StridedVector<T>() {
        const T *const p = d;
        return StridedVector<T>{.d = p, .N = N, .x = x};
    }
    StridedVector<T> view() const {
        return StridedVector<T>{.d = d, .N = N, .x = x};
    }
    MutStridedVector<T> &operator=(const T &y) {
        for (size_t i = 0; i < N; ++i)
            d[i * x] = y;
        return *this;
    }
    MutStridedVector<T> &operator=(const AbstractVector auto &x) {
        return copyto(*this, x);
    }
    MutStridedVector<T> &operator=(const MutStridedVector<T> &x) {
        return copyto(*this, x);
    }
    MutStridedVector<T> &operator+=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        for (size_t i = 0; i < M; ++i)
            self(i) += x(i);
        return self;
    }
    MutStridedVector<T> &operator-=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        for (size_t i = 0; i < M; ++i)
            self(i) -= x(i);
        return self;
    }
    MutStridedVector<T> &operator*=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        for (size_t i = 0; i < M; ++i)
            self(i) *= x(i);
        return self;
    }
    MutStridedVector<T> &operator/=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        for (size_t i = 0; i < M; ++i)
            self(i) /= x(i);
        return self;
    }
    void extendOrAssertSize(size_t M) const { assert(N == M); }
};

template <typename T>
concept DerivedMatrix =
    requires(T t, const T ct) {
        {
            t.data()
            } -> std::convertible_to<typename std::add_pointer_t<
                typename std::add_const_t<typename T::eltype>>>;
        {
            ct.data()
            } -> std::same_as<typename std::add_pointer_t<
                typename std::add_const_t<typename T::eltype>>>;
        { t.numRow() } -> std::convertible_to<size_t>;
        { t.numCol() } -> std::convertible_to<size_t>;
        { t.rowStride() } -> std::convertible_to<size_t>;
    };

template <typename T> struct SmallSparseMatrix;
template <typename T> struct PtrMatrix {
    using eltype = std::remove_reference_t<T>;
    static_assert(!std::is_const_v<T>, "const T is redundant");
    static constexpr bool canResize = false;
    [[no_unique_address]] const T *const mem;
    [[no_unique_address]] const size_t M, N, X;

    inline const T *data() const { return mem; }
    inline size_t numRow() const { return M; }
    inline size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }

    inline std::pair<size_t, size_t> size() const {
        return std::make_pair(M, N);
    }
    inline auto &operator()(size_t row, size_t col) const {
        assert(row < M);
        assert(col < N);
        return *(data() + col + row * X);
    }
    inline PtrMatrix<T> operator()(Range<size_t, size_t> rows,
                                   Range<size_t, size_t> cols) {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= N);
        return PtrMatrix<T>{.mem = mem + cols.b + rows.b * X,
                            .M = rows.e - rows.b,
                            .N = cols.e - cols.b,
                            .X = X};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline PtrMatrix<T> operator()(Range<R0, R1> rows, Range<C0, C1> cols) {
        return (*this)(canonicalizeRange(rows, M),
                       canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline PtrMatrix<T> operator()(Colon, Range<C0, C1> cols) {
        return (*this)(Range<size_t, size_t>{0, M},
                       canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline PtrMatrix<T> operator()(Range<R0, R1> rows, Colon) {
        return (*this)(canonicalizeRange(rows, M),
                       Range<size_t, size_t>{0, numCol()});
    }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, Begin) const { return getCol(0); }
    inline auto operator()(Colon, End) const { return getCol(N - 1); }

    inline auto operator()(Range<size_t, size_t> rows,
                           Range<size_t, size_t> cols) const {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= numCol());
        return PtrMatrix<T>{.mem = mem + cols.b + rows.b * X,
                            .M = rows.e - rows.b,
                            .N = cols.e - cols.b,
                            .X = X};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline auto operator()(Range<R0, R1> rows, Range<C0, C1> cols) const {
        return (*this)(canonicalizeRange(rows, M),
                       canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline auto operator()(Colon, Range<C0, C1> cols) const {
        return (*this)(Range<size_t, size_t>{0, numRow()},
                       canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, Colon) const {
        return (*this)(canonicalizeRange(rows, M),
                       Range<size_t, size_t>{0, numCol()});
    }
    inline const PtrMatrix<T> operator()(Colon, Colon) const { return *this; }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) const {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) const {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, size_t col) const { return getCol(col); }
    inline auto operator()(size_t row, Colon) const { return getRow(row); }

    inline PtrVector<T> getRow(size_t i) const {
        return PtrVector<T>{.mem = data() + i * rowStride(), .N = N};
    }
    inline StridedVector<T> getCol(size_t n) const {
        return StridedVector<T>{data() + n, M, rowStride()};
    }

    bool operator==(const AbstractMatrix auto &B) const {
        const size_t M = B.numRow();
        const size_t N = B.numCol();
        if ((M != numRow()) || (N != numCol()))
            return false;
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                if ((*this)(r, c) != B(r, c))
                    return false;
        return true;
    }
    bool isSquare() const { return M == N; }
    StridedVector<T> diag() const {
        return StridedVector<T>{data(), std::min(M, N), rowStride() + 1};
    }
    StridedVector<T> antiDiag() const {
        return StridedVector<T>{data() + N - 1, std::min(M, N),
                                rowStride() - 1};
    }
    // Vector<T> diag() const {
    //     size_t K = std::min(M, N);
    //     Vector<T> d;
    //     d.resizeForOverwrite(K);
    //     for (size_t k = 0; k < K; ++k)
    //         d(k) = mem[k * (1 + X)];
    //     return d;
    // }
    inline PtrMatrix<T> view() const { return *this; };
    Transpose<PtrMatrix<T>> transpose() const {
        return Transpose<PtrMatrix<T>>{*this};
    }
    void extendOrAssertSize(size_t MM, size_t NN) const {
        assert(MM == M);
        assert(NN == N);
    }
};
template <typename T> struct MutPtrMatrix {
    using eltype = std::remove_reference_t<T>;
    static_assert(!std::is_const_v<T>,
                  "MutPtrMatrix should never have const T");
    [[no_unique_address]] T *const mem;
    [[no_unique_address]] const size_t M, N, X;
    static constexpr bool canResize = false;

    static constexpr bool fixedNumRow = true;
    static constexpr bool fixedNumCol = true;
    inline size_t numRow() const { return M; }
    inline size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }
    inline T *data() { return mem; }
    inline const T *data() const { return mem; }
    inline PtrMatrix<T> view() const {
        return PtrMatrix<T>{.mem = data(), .M = M, .N = N, .X = X};
    };

    MutPtrMatrix<T> operator=(const SmallSparseMatrix<T> &A) {
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
    MutPtrMatrix<T> operator=(MutPtrMatrix<T> A) {
        return copyto(*this, PtrMatrix<T>(A));
    }
    // rule of 5 requires...
    MutPtrMatrix(const MutPtrMatrix<T> &A) = default;
    MutPtrMatrix(T *mem, size_t M, size_t N) : mem(mem), M(M), N(N), X(N){};
    MutPtrMatrix(T *mem, size_t M, size_t N, size_t X)
        : mem(mem), M(M), N(N), X(X){};

    inline std::pair<size_t, size_t> size() const {
        return std::make_pair(M, N);
    }

    inline auto &operator()(size_t row, size_t col) {
        assert(row < M);
        assert(col < numCol());
        return *(data() + col + row * rowStride());
    }
    inline auto &operator()(size_t row, size_t col) const {
        assert(row < M);
        assert(col < numCol());
        return *(data() + col + row * rowStride());
    }
    inline auto operator()(Range<size_t, size_t> rows,
                           Range<size_t, size_t> cols) {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= numCol());
        return MutPtrMatrix<T>{data() + cols.b + rows.b * rowStride(),
                               rows.e - rows.b, cols.e - cols.b, rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Range<C0, C1> cols) {
        return (*this)(canonicalizeRange(rows, M),
                       canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Colon, Range<C0, C1> cols) {
        return (*this)(Range<size_t, size_t>{0, M},
                       canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Colon) {
        return (*this)(canonicalizeRange(rows, M),
                       Range<size_t, size_t>{0, numCol()});
    }
    template <typename R0, typename R1>
    inline MutStridedVector<T> operator()(Range<R0, R1> rows, size_t col) {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline MutPtrVector<T> operator()(size_t row, Range<C0, C1> cols) {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, size_t col) { return getCol(col); }
    inline auto operator()(size_t row, Colon) { return getRow(row); }
    inline auto operator()(Colon, Begin) { return getCol(0); }
    inline auto operator()(Colon, End) { return getCol(N - 1); }
    inline auto operator()(Colon, Begin) const { return getCol(0); }
    inline auto operator()(Colon, End) const { return getCol(N - 1); }

    inline auto operator()(Range<size_t, size_t> rows,
                           Range<size_t, size_t> cols) const {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= numCol());
        return PtrMatrix<T>{.mem = data() + cols.b + rows.b * rowStride(),
                            .M = rows.e - rows.b,
                            .N = cols.e - cols.b,
                            .X = rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline auto operator()(Range<R0, R1> rows, Range<C0, C1> cols) const {
        return view(canonicalizeRange(rows, M),
                    canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline auto operator()(Colon, Range<C0, C1> cols) const {
        return view(Range<size_t, size_t>{0, M},
                    canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, Colon) const {
        return view(canonicalizeRange(rows, M),
                    Range<size_t, size_t>{0, numCol()});
    }
    inline MutPtrMatrix<T> operator()(Colon, Colon) { return *this; }
    inline PtrMatrix<T> operator()(Colon, Colon) const { return *this; }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) const {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) const {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, size_t col) const { return getCol(col); }
    inline auto operator()(size_t row, Colon) const { return getRow(row); }

    inline MutPtrVector<T> getRow(size_t i) {
        return MutPtrVector<T>{data() + i * rowStride(), N};
    }
    inline PtrVector<T> getRow(size_t i) const {
        return PtrVector<T>{.mem = data() + i * rowStride(), .N = N};
    }
    inline MutStridedVector<T> getCol(size_t n) {
        return MutStridedVector<T>{data() + n, M, rowStride()};
    }
    inline StridedVector<T> getCol(size_t n) const {
        return StridedVector<T>{data() + n, M, rowStride()};
    }
    MutPtrMatrix<T> operator=(const AbstractMatrix auto &B) {
        return copyto(*this, B);
    }
    MutPtrMatrix<T> operator=(const std::integral auto b) {
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) = b;
        return *this;
    }
    MutPtrMatrix<T> operator+=(const AbstractMatrix auto &B) {
        assert(M == B.numRow());
        assert(N == B.numCol());
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) += B(r, c);
        return *this;
    }
    MutPtrMatrix<T> operator-=(const AbstractMatrix auto &B) {
        assert(M == B.numRow());
        assert(N == B.numCol());
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) -= B(r, c);
        return *this;
    }
    MutPtrMatrix<T> operator*=(const std::integral auto b) {
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) *= b;
        return *this;
    }
    MutPtrMatrix<T> operator/=(const std::integral auto b) {
        const size_t M = numRow();
        const size_t N = numCol();
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) /= b;
        return *this;
    }

    bool operator==(const AbstractMatrix auto &B) const {
        const size_t M = B.numRow();
        const size_t N = B.numCol();
        if ((M != numRow()) || (N != numCol()))
            return false;
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                if ((*this)(r, c) != B(r, c))
                    return false;
        return true;
    }
    bool isSquare() const { return M == N; }
    MutStridedVector<T> diag() {
        return MutStridedVector<T>{data(), std::min(M, N), rowStride() + 1};
    }
    StridedVector<T> diag() const {
        return StridedVector<T>{data(), std::min(M, N), rowStride() + 1};
    }
    MutStridedVector<T> antiDiag() {
        return MutStridedVector<T>{data() + N - 1, std::min(M, N),
                                   rowStride() - 1};
    }
    StridedVector<T> antiDiag() const {
        return StridedVector<T>{data() + N - 1, std::min(M, N),
                                rowStride() - 1};
    }
    // Vector<T> diag() const {
    //     size_t K = std::min(M, N);
    //     Vector<T> d;
    //     d.resizeForOverwrite(N);
    //     for (size_t k = 0; k < K; ++k)
    //         d(k) = mem[k * (1 + X)];
    //     return d;
    // }
    Transpose<PtrMatrix<T>> transpose() const {
        return Transpose<PtrMatrix<T>>{view()};
    }
    void extendOrAssertSize(size_t M, size_t N) const {
        assert(numRow() == M);
        assert(numCol() == N);
    }
    operator PtrMatrix<T>() {
        return PtrMatrix<T>{.mem = mem, .M = M, .N = N, .X = X};
    }
};
template <typename T> constexpr auto ptrVector(T *p, size_t M) {
    if constexpr (std::is_const_v<T>) {
        return PtrVector<std::remove_const_t<T>>{.mem = p, .N = M};
    } else {
        return MutPtrVector<T>{p, M};
    }
}

template <typename T, typename P> struct BaseMatrix {
    using eltype = std::remove_reference_t<T>;
    inline T *mutdata() { return static_cast<P *>(this)->data(); }
    inline auto *data() {
        if constexpr (P::isMutable) {
            return mutdata();
        } else {
            return static_cast<P *>(this)->data();
            // const T* p = static_cast<P *>(this)->data();
            // return p;
        }
    }
    inline const auto *data() const {
        return static_cast<const P *>(this)->data();
    }
    inline size_t numRow() const {
        return static_cast<const P *>(this)->numRow();
    }
    inline size_t numCol() const {
        return static_cast<const P *>(this)->numCol();
    }
    inline size_t rowStride() const {
        return static_cast<const P *>(this)->rowStride();
    }
    static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return 0; }

    inline std::pair<size_t, size_t> size() const {
        return std::make_pair(numRow(), numCol());
    }

    inline auto &operator()(size_t row, size_t col) {
        assert(row < numRow());
        assert(col < numCol());
        return *(data() + col + row * rowStride());
    }
    inline auto &operator()(size_t row, size_t col) const {
        return *(data() + col + row * rowStride());
    }
    inline auto &operator()(size_t row, End) {
        assert(row < numRow());
        return *(data() + (numCol() - 1) + row * rowStride());
    }
    inline auto &operator()(End, size_t col) {
        assert(col < numCol());
        return *(data() + col + (numRow() - 1) * rowStride());
    }
    inline auto &operator()(size_t row, End) const {
        assert(row < numRow());
        return *(data() + (numCol() - 1) + row * rowStride());
    }
    inline auto &operator()(End, size_t col) const {
        assert(col < numCol());
        return *(data() + col + (numRow() - 1) * rowStride());
    }
    inline auto &operator()(size_t row, OffsetEnd oe) {
        assert(row < numRow());
        assert(oe.offset < numCol());
        return *(data() + (numCol() - 1 - oe.offset) + row * rowStride());
    }
    inline auto &operator()(OffsetEnd oe, size_t col) {
        assert(col < numCol());
        assert(oe.offset < numRow());
        return *(data() + col + (numRow() - 1 - oe.offset) * rowStride());
    }
    inline auto &operator()(size_t row, OffsetEnd oe) const {
        assert(row < numRow());
        assert(oe.offset < numCol());
        return *(data() + (numCol() - 1 - oe.offset) + row * rowStride());
    }
    inline auto &operator()(OffsetEnd oe, size_t col) const {
        assert(col < numCol());
        assert(oe.offset < numRow());
        return *(data() + col + (numRow() - 1 - oe.offset) * rowStride());
    }

    inline MutPtrMatrix<T> operator()(Range<size_t, size_t> rows,
                                      Range<size_t, size_t> cols) {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= numRow());
        assert(cols.e <= numCol());
        return MutPtrMatrix<T>{data() + cols.b + rows.b * rowStride(),
                               rows.e - rows.b, cols.e - cols.b, rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Range<C0, C1> cols) {
        return (*this)(canonicalizeRange(rows, numRow()),
                       canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Colon, Range<C0, C1> cols) {
        return (*this)(Range<size_t, size_t>{0, numRow()},
                       canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Colon) {
        return (*this)(canonicalizeRange(rows, numRow()),
                       Range<size_t, size_t>{0, numCol()});
    }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) {
        return getCol(col)(canonicalizeRange(rows, numRow()));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, size_t col) { return getCol(col); }
    inline auto operator()(size_t row, Colon) { return getRow(row); }
    inline auto operator()(Begin, Colon) { return getRow(0); }
    inline auto operator()(End, Colon) { return getRow(numRow() - 1); }
    inline auto operator()(Begin, Colon) const { return getRow(0); }
    inline auto operator()(End, Colon) const { return getRow(numRow() - 1); }
    inline auto operator()(Colon, Begin) { return getCol(0); }
    inline auto operator()(Colon, End) { return getCol(numCol() - 1); }
    inline auto operator()(Colon, Begin) const { return getCol(0); }
    inline auto operator()(Colon, End) const { return getCol(numCol() - 1); }

    template <typename C0, typename C1>
    inline auto operator()(End, Range<C0, C1> cols) {
        return getRow(numRow() - 1)(canonicalizeRange(cols, numCol()));
    }

    inline PtrMatrix<T> operator()(Range<size_t, size_t> rows,
                                   Range<size_t, size_t> cols) const {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= numRow());
        assert(cols.e <= numCol());
        return PtrMatrix<T>{.mem = data() + cols.b + rows.b * rowStride(),
                            .M = rows.e - rows.b,
                            .N = cols.e - cols.b,
                            .X = rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline PtrMatrix<T> operator()(Range<R0, R1> rows,
                                   Range<C0, C1> cols) const {
        return (*this)(canonicalizeRange(rows, numRow()),
                       canonicalizeRange(cols, numCol()));
    }
    template <typename C0, typename C1>
    inline PtrMatrix<T> operator()(Colon, Range<C0, C1> cols) const {
        return (*this)(Range<size_t, size_t>{0, numRow()},
                       canonicalizeRange(cols, numCol()));
    }
    template <typename R0, typename R1>
    inline PtrMatrix<T> operator()(Range<R0, R1> rows, Colon) const {
        return (*this)(canonicalizeRange(rows, numRow()),
                       Range<size_t, size_t>{0, numCol()});
    }
    inline P &self() { return *static_cast<P *>(this); }
    inline const P &self() const { return *static_cast<const P *>(this); }
    inline P &operator()(Colon, Colon) { return self(); }
    inline const P &operator()(Colon, Colon) const { return self(); }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) const {
        return getCol(col)(canonicalizeRange(rows, numRow()));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) const {
        return getRow(row)(canonicalizeRange(cols, numCol()));
    }
    inline auto operator()(Colon, size_t col) const { return getCol(col); }
    inline auto operator()(size_t row, Colon) const { return getRow(row); }

    inline auto getRow(size_t i) {
        return ptrVector(data() + i * rowStride(), numCol());
        //     return MutPtrVector<T>{data() + i * rowStride(), numCol()};
    }
    inline PtrVector<T> getRow(size_t i) const {
        return PtrVector<T>{.mem = data() + i * rowStride(), .N = numCol()};
    }
    // void copyRow(llvm::ArrayRef<T> x, size_t i) {
    //     for (size_t j = 0; j < numCol(); ++j)
    //         (*this)(i, j) = x[j];
    // }
    inline MutStridedVector<T> getCol(size_t n) {
        return MutStridedVector<T>{data() + n, numRow(), rowStride()};
    }
    inline StridedVector<T> getCol(size_t n) const {
        return StridedVector<T>{data() + n, numRow(), rowStride()};
    }
    operator MutPtrMatrix<T>() {
        return MutPtrMatrix<T>{data(), numRow(), numCol(), rowStride()};
    }
    operator PtrMatrix<T>() const {
        return PtrMatrix<T>{
            .mem = data(), .M = numRow(), .N = numCol(), .X = rowStride()};
    }
    MutPtrMatrix<T> operator=(const AbstractMatrix auto &B) {
        MutPtrMatrix<T> A{*this};
        return copyto(A, B);
    }
    MutPtrMatrix<T> operator=(const std::integral auto b) {
        MutPtrMatrix<T> A{*this};
        return A = b;
    }
    MutPtrMatrix<T> operator+=(const AbstractMatrix auto &B) {
        MutPtrMatrix<T> A{*this};
        return A += B;
    }
    MutPtrMatrix<T> operator-=(const AbstractMatrix auto &B) {
        MutPtrMatrix<T> A{*this};
        return A -= B;
    }
    MutPtrMatrix<T> operator*=(const std::integral auto b) {
        MutPtrMatrix<T> A{*this};
        return A *= b;
    }
    MutPtrMatrix<T> operator/=(const std::integral auto b) {
        MutPtrMatrix<T> A{*this};
        return A /= b;
    }

    bool operator==(const AbstractMatrix auto &B) const {
        const size_t M = B.numRow();
        const size_t N = B.numCol();
        if ((M != numRow()) || (N != numCol()))
            return false;
        const P &A = self();
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                if (A(r, c) != B(r, c))
                    return false;
        return true;
    }
    bool isSquare() const { return numRow() == numCol(); }
    MutStridedVector<T> diag() {
        return MutStridedVector<T>{data(), std::min(numRow(), numCol()),
                                   rowStride() + 1};
    }
    StridedVector<T> diag() const {
        return StridedVector<T>{data(), std::min(numRow(), numCol()),
                                rowStride() + 1};
    }
    MutStridedVector<T> antiDiag() {
        return MutStridedVector<T>{data() + numCol() - 1,
                                   std::min(numRow(), numCol()),
                                   rowStride() - 1};
    }
    StridedVector<T> antiDiag() const {
        return StridedVector<T>{data() + numCol() - 1,
                                std::min(numRow(), numCol()), rowStride() - 1};
    }
    // Vector<T> diag() const {
    //     size_t N = std::min(numRow(), numCol());
    //     Vector<T> d;
    //     d.resizeForOverwrite(N);
    //     const P &A = self();
    //     for (size_t n = 0; n < N; ++n)
    //         d(n) = A(n, n);
    //     return d;
    // }
    inline PtrMatrix<T> view() const {
        return PtrMatrix<T>{
            .mem = data(), .M = numRow(), .N = numCol(), .X = rowStride()};
    };
    Transpose<PtrMatrix<T>> transpose() const {
        return Transpose<PtrMatrix<T>>{view()};
    }
    void extendOrAssertSize(size_t M, size_t N) {
        if constexpr (P::fixedNumRow) {
            assert(numRow() == M);
            if constexpr (P::fixedNumCol) {
                assert(numCol() == N);
            } else if (N != numCol())
                static_cast<P *>(this)->resizeColsForOverwrite(N);
        } else if constexpr (P::fixedNumCol) {
            assert(numCol() == N);
            if (M != numRow())
                static_cast<P *>(this)->resizeRowsForOverwrite(M);
        } else if ((M != numRow()) || (N != numCol()))
            static_cast<P *>(this)->resizeForOverwrite(M, N);
    }
};

// template <typename T>
// constexpr auto ptrmat(T *ptr, size_t numRow, size_t numCol, size_t stride) {
//     if constexpr (std::is_const_v<T>) {
//         return PtrMatrix<std::remove_const_t<T>>{
//             .mem = ptr, .M = numRow, .N = numCol, .X = stride};
//     } else {
//         return MutPtrMatrix<T>{
//             .mem = ptr, .M = numRow, .N = numCol, .X = stride};
//     }
// }

static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> is not trivially copyable!");
static_assert(std::is_trivially_copyable_v<PtrVector<int64_t>>,
              "PtrVector<int64_t,0> is not trivially copyable!");

static_assert(!AbstractVector<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");

static_assert(AbstractMatrix<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");

static_assert(AbstractVector<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");
static_assert(AbstractVector<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");

static_assert(AbstractVector<Vector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");

static_assert(!AbstractMatrix<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");

static_assert(
    AbstractMatrix<ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>,
    "ElementwiseBinaryOp isa AbstractMatrix failed");

static_assert(
    !AbstractVector<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
    "MatMul should not be an AbstractVector!");
static_assert(AbstractMatrix<MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>,
              "MatMul is not an AbstractMatrix!");

template <typename T>
concept IntVector = requires(T t, int64_t y) {
                        { t.size() } -> std::convertible_to<size_t>;
                        { t[y] } -> std::convertible_to<int64_t>;
                    };

//
// Matrix
//
template <typename T, size_t M = 0, size_t N = 0, size_t S = 64>
struct Matrix : BaseMatrix<T, Matrix<T, M, N, S>> {
    // static_assert(M * N == S,
    //               "if specifying non-zero M and N, we should have M*N == S");
    static constexpr bool fixedNumRow = M;
    static constexpr bool fixedNumCol = N;
    static constexpr bool canResize = false;
    static constexpr bool isMutable = true;
    T mem[S];
    static constexpr size_t numRow() { return M; }
    static constexpr size_t numCol() { return N; }
    static constexpr size_t rowStride() { return N; }

    T *data() { return mem; }
    const T *data() const { return mem; }

    static constexpr size_t getConstCol() { return N; }
};

template <typename T, size_t M, size_t S>
struct Matrix<T, M, 0, S> : BaseMatrix<T, Matrix<T, M, 0, S>> {
    [[no_unique_address]] llvm::SmallVector<T, S> mem;
    [[no_unique_address]] size_t N, X;
    static constexpr bool canResize = true;
    static constexpr bool isMutable = true;

    Matrix(size_t n) : mem(llvm::SmallVector<T, S>(M * n)), N(n), X(n){};

    inline size_t numRow() const { return M; }
    inline size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
    void resizeColsForOverwrite(size_t NN, size_t XX) {
        N = NN;
        X = XX;
        mem.resize_for_overwrite(M * XX);
    }
    void resizeColsForOverwrite(size_t NN) { resizeColsForOverwrite(NN, NN); }
};
template <typename T, size_t N, size_t S>
struct Matrix<T, 0, N, S> : BaseMatrix<T, Matrix<T, 0, N, S>> {
    [[no_unique_address]] llvm::SmallVector<T, S> mem;
    [[no_unique_address]] size_t M;
    static constexpr bool canResize = true;
    static constexpr bool isMutable = true;

    Matrix(size_t m) : mem(llvm::SmallVector<T, S>(m * N)), M(m){};

    inline size_t numRow() const { return M; }
    static constexpr size_t numCol() { return N; }
    static constexpr size_t rowStride() { return N; }
    static constexpr size_t getConstCol() { return N; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};

template <typename T>
struct SquarePtrMatrix : BaseMatrix<T, SquarePtrMatrix<T>> {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    [[no_unique_address]] const T *const mem;
    [[no_unique_address]] const size_t M;
    static constexpr bool fixedNumCol = true;
    static constexpr bool fixedNumRow = true;
    static constexpr bool canResize = false;
    static constexpr bool isMutable = false;

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    inline size_t rowStride() const { return M; }
    const T *data() { return mem; }
    const T *data() const { return mem; }
    constexpr bool isSquare() const { return true; }
};
template <typename T>
struct MutSquarePtrMatrix : BaseMatrix<T, MutSquarePtrMatrix<T>> {
    static_assert(!std::is_const_v<T>, "T should not be const");
    [[no_unique_address]] T *const mem;
    [[no_unique_address]] const size_t M;
    static constexpr bool fixedNumCol = true;
    static constexpr bool fixedNumRow = true;
    static constexpr bool canResize = false;
    static constexpr bool isMutable = true;

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    inline size_t rowStride() const { return M; }

    T *data() { return mem; }
    const T *data() const { return mem; }
    operator SquarePtrMatrix<T>() const {
        return SquarePtrMatrix<T>{{}, mem, M};
    }
    constexpr bool isSquare() const { return true; }
    MutSquarePtrMatrix<T> operator=(const AbstractMatrix auto &B) {
        return copyto(*this, B);
    }
};

template <typename T, unsigned STORAGE = 8>
struct SquareMatrix : BaseMatrix<T, SquareMatrix<T, STORAGE>> {
    static constexpr unsigned TOTALSTORAGE = STORAGE * STORAGE;
    [[no_unique_address]] llvm::SmallVector<T, TOTALSTORAGE> mem;
    [[no_unique_address]] size_t M;
    static constexpr bool fixedNumCol = true;
    static constexpr bool fixedNumRow = true;
    static constexpr bool canResize = false;
    static constexpr bool isMutable = true;

    SquareMatrix(size_t m)
        : mem(llvm::SmallVector<T, TOTALSTORAGE>(m * m)), M(m){};

    size_t numRow() const { return M; }
    size_t numCol() const { return M; }
    inline size_t rowStride() const { return M; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }

    T *begin() { return data(); }
    T *end() { return data() + M * M; }
    const T *begin() const { return data(); }
    const T *end() const { return data() + M * M; }
    T &operator[](size_t i) { return mem[i]; }
    const T &operator[](size_t i) const { return mem[i]; }

    static SquareMatrix<T> identity(size_t N) {
        SquareMatrix<T> A(N);
        for (size_t r = 0; r < N; ++r)
            A(r, r) = 1;
        return A;
    }
    operator MutSquarePtrMatrix<T>() {
        return MutSquarePtrMatrix<T>{{}, mem.data(), size_t(M)};
    }
    operator SquarePtrMatrix<T>() const {
        return SquarePtrMatrix<T>{{}, mem.data(), M};
    }
    static constexpr bool isSquare() { return true; }
};

template <typename T, size_t S>
struct Matrix<T, 0, 0, S> : BaseMatrix<T, Matrix<T, 0, 0, S>> {
    [[no_unique_address]] llvm::SmallVector<T, S> mem;

    [[no_unique_address]] size_t M, N, X;
    static constexpr bool canResize = true;
    static constexpr bool isMutable = true;

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }

    Matrix(llvm::SmallVector<T, S> content, size_t m, size_t n)
        : mem(std::move(content)), M(m), N(n), X(n){};

    Matrix(size_t m, size_t n)
        : mem(llvm::SmallVector<T, S>(m * n)), M(m), N(n), X(n){};

    Matrix() : M(0), N(0), X(0){};
    Matrix(SquareMatrix<T> &&A)
        : mem(std::move(A.mem)), M(A.M), N(A.M), X(A.M){};
    Matrix(const SquareMatrix<T> &A)
        : mem(A.begin(), A.end()), M(A.M), N(A.M), X(A.M){};
    Matrix(const AbstractMatrix auto &A)
        : mem(llvm::SmallVector<T>{}), M(A.numRow()), N(A.numCol()),
          X(A.numCol()) {
        mem.resize_for_overwrite(M * N);
        for (size_t m = 0; m < M; ++m)
            for (size_t n = 0; n < N; ++n)
                mem[m * X + n] = A(m, n);
    }
    auto begin() { return mem.begin(); }
    auto end() { return mem.begin() + rowStride() * M; }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.begin() + rowStride() * M; }
    size_t numRow() const { return M; }
    size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }

    static Matrix<T, 0, 0, S> uninitialized(size_t MM, size_t NN) {
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
        size_t minMMM = std::min(M, MM);
        if ((XX > X) && M && N)
            // need to copy
            for (size_t m = minMMM - 1; m > 0; --m)
                for (size_t n = N; n-- > 0;)
                    mem[m * XX + n] = mem[m * X + n];
        // zero
        for (size_t m = 0; m < minMMM; ++m)
            for (size_t n = N; n < NN; ++n)
                mem[m * XX + n] = 0;
        for (size_t m = minMMM; m < MM; ++m)
            for (size_t n = 0; n < NN; ++n)
                mem[m * XX + n] = 0;
        X = XX;
        M = MM;
        N = NN;
    }
    void insertZeroColumn(size_t i) {
        llvm::errs() << "before";
        CSHOWLN(*this);
        size_t NN = N + 1;
        size_t XX = std::max(X, NN);
        mem.resize(M * XX);
        size_t nLower = (XX > X) ? 0 : i;
        if (M && N)
            // need to copy
            for (size_t m = M; m-- > 0;)
                for (size_t n = N; n-- > nLower;)
                    mem[m * XX + n + (n >= i)] = mem[m * X + n];
        // zero
        for (size_t m = 0; m < M; ++m)
            mem[m * XX + i] = 0;
        X = XX;
        N = NN;
        llvm::errs() << "after";
        CSHOWLN(*this);
    }
    void resize(size_t MM, size_t NN) { resize(MM, NN, std::max(NN, X)); }
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
        size_t Mold = M;
        M = MM;
        if (M * rowStride() > mem.size())
            mem.resize(M * X);
        if (M > Mold)
            (*this)(_(Mold, M), _) = 0;
    }
    void resizeRowsForOverwrite(size_t MM) {
        if (MM * rowStride() > mem.size())
            mem.resize_for_overwrite(M * X);
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
        for (size_t m = 0; m < M; ++m)
            for (size_t n = 0; n < N; ++n)
                mem.erase(mem.begin() + m * X + n);
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
    MutPtrMatrix<T> view() { return MutPtrMatrix<T>{mem.data(), M, N, X}; }
    PtrMatrix<T> view() const {
        return PtrMatrix<T>{.mem = mem.data(), .M = M, .N = N, .X = X};
    }
    Transpose<PtrMatrix<T>> transpose() const {
        return Transpose<PtrMatrix<T>>{view()};
    }
    bool operator==(const AbstractMatrix auto &B) const {
        const size_t R = B.numRow();
        const size_t C = B.numCol();
        if ((M != R) || (N != C))
            return false;
        for (size_t r = 0; r < R; ++r)
            for (size_t c = 0; c < C; ++c)
                if ((*this)(r, c) != B(r, c))
                    return false;
        return true;
    }

    bool operator==(const Matrix<T, 0, 0, S> &B) const {
        const size_t M = B.numRow();
        const size_t N = B.numCol();
        if ((M != numRow()) || (N != numCol()))
            return false;
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                if ((*this)(r, c) != B(r, c))
                    return false;
        return true;
    }
    Matrix<T, 0, 0, S> &operator=(T x) {
        const size_t M = numRow();
        const size_t N = numCol();
        for (size_t r = 0; r < M; ++r)
            for (size_t c = 0; c < N; ++c)
                (*this)(r, c) = x;
        return *this;
    }
    void moveColLast(size_t j) {
        if (j == N)
            return;
        for (size_t m = 0; m < M; ++m) {
            auto x = (*this)(m, j);
            for (size_t n = j; n < N - 1;) {
                size_t o = n++;
                (*this)(m, o) = (*this)(m, n);
            }
            (*this)(m, N - 1) = x;
        }
    }
    Matrix<T, 0, 0, S> deleteCol(size_t c) const {
        Matrix<T, 0, 0, S> A(M, N - 1);
        for (size_t m = 0; m < M; ++m) {
            A(m, _(0, c)) = (*this)(m, _(0, c));
            A(m, _(c, ::end)) = (*this)(m, _(c + 1, ::end));
        }
        return A;
    }
};
template <typename T> using DynamicMatrix = Matrix<T, 0, 0, 64>;
static_assert(std::same_as<DynamicMatrix<int64_t>, Matrix<int64_t>>,
              "DynamicMatrix should be identical to Matrix");
typedef DynamicMatrix<int64_t> IntMatrix;

llvm::raw_ostream &printVectorImpl(llvm::raw_ostream &os,
                                   const AbstractVector auto &a) {
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
llvm::raw_ostream &printVector(llvm::raw_ostream &os, PtrVector<T> a) {
    return printVectorImpl(os, a);
}
template <typename T>
llvm::raw_ostream &printVector(llvm::raw_ostream &os, StridedVector<T> a) {
    return printVectorImpl(os, a);
}
template <typename T>
llvm::raw_ostream &printVector(llvm::raw_ostream &os,
                               const llvm::SmallVectorImpl<T> &a) {
    return printVector(os, PtrVector<T>{a.data(), a.size()});
}

template <typename T>
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, PtrVector<T> const &A) {
    return printVector(os, A);
}
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const AbstractVector auto &A) {
    return printVector(os, A.view());
}

bool allMatch(const AbstractVector auto &x0, const AbstractVector auto &x1) {
    size_t N = x0.size();
    if (N != x1.size())
        return false;
    for (size_t n = 0; n < N; ++n)
        if (x0(n) != x1(n))
            return false;
    return true;
}

MULTIVERSION inline void swapRows(MutPtrMatrix<int64_t> A, size_t i, size_t j) {
    if (i == j)
        return;
    const size_t N = A.numCol();
    assert((i < A.numRow()) && (j < A.numRow()));
    VECTORIZE
    for (size_t n = 0; n < N; ++n)
        std::swap(A(i, n), A(j, n));
}
MULTIVERSION inline void swapCols(MutPtrMatrix<int64_t> A, size_t i, size_t j) {
    if (i == j) {
        return;
    }
    const size_t M = A.numRow();
    assert((i < A.numCol()) && (j < A.numCol()));
    VECTORIZE
    for (size_t m = 0; m < M; ++m)
        std::swap(A(m, i), A(m, j));
}
template <typename T>
[[maybe_unused]] static void swapCols(llvm::SmallVectorImpl<T> &A, size_t i,
                                      size_t j) {
    std::swap(A[i], A[j]);
}
template <typename T>
[[maybe_unused]] static void swapRows(llvm::SmallVectorImpl<T> &A, size_t i,
                                      size_t j) {
    std::swap(A[i], A[j]);
}

template <int Bits, class T>
constexpr bool is_uint_v =
    sizeof(T) == (Bits / 8) && std::is_integral_v<T> && !std::is_signed_v<T>;

template <class T>
constexpr T zeroUpper(T x)
requires is_uint_v<16, T>
{
    return x & 0x00ff;
}
template <class T>
constexpr T zeroLower(T x)
requires is_uint_v<16, T>
{
    return x & 0xff00;
}
template <class T>
constexpr T upperHalf(T x)
requires is_uint_v<16, T>
{
    return x >> 8;
}

template <class T>
constexpr T zeroUpper(T x)
requires is_uint_v<32, T>
{
    return x & 0x0000ffff;
}
template <class T>
constexpr T zeroLower(T x)
requires is_uint_v<32, T>
{
    return x & 0xffff0000;
}
template <class T>
constexpr T upperHalf(T x)
requires is_uint_v<32, T>
{
    return x >> 16;
}
template <class T>
constexpr T zeroUpper(T x)
requires is_uint_v<64, T>
{
    return x & 0x00000000ffffffff;
}
template <class T>
constexpr T zeroLower(T x)
requires is_uint_v<64, T>
{
    return x & 0xffffffff00000000;
}
template <class T>
constexpr T upperHalf(T x)
requires is_uint_v<64, T>
{
    return x >> 32;
}

template <typename T>
[[maybe_unused]] static std::pair<size_t, T> findMax(llvm::ArrayRef<T> x) {
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

template <class T, int Bits>
concept is_int_v = std::signed_integral<T> && sizeof(T) == (Bits / 8);

template <is_int_v<64> T> constexpr __int128_t widen(T x) { return x; }
template <is_int_v<32> T> constexpr int64_t splitInt(T x) { return x; }

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <typename T>
concept TriviallyCopyableVectorOrScalar =
    std::is_trivially_copyable_v<T> && VectorOrScalar<T>;
template <typename T>
concept TriviallyCopyableMatrixOrScalar =
    std::is_trivially_copyable_v<T> && MatrixOrScalar<T>;

static_assert(std::copy_constructible<PtrMatrix<int64_t>>);
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>);
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>);
static_assert(TriviallyCopyableMatrixOrScalar<PtrMatrix<int64_t>>);
static_assert(TriviallyCopyableMatrixOrScalar<int>);
static_assert(TriviallyCopyable<Mul>);
static_assert(TriviallyCopyableMatrixOrScalar<
              ElementwiseMatrixBinaryOp<Mul, PtrMatrix<int64_t>, int>>);
static_assert(TriviallyCopyableMatrixOrScalar<
              MatMatMul<PtrMatrix<int64_t>, PtrMatrix<int64_t>>>);

template <TriviallyCopyable OP, TriviallyCopyableVectorOrScalar A,
          TriviallyCopyableVectorOrScalar B>
constexpr auto _binaryOp(OP op, A a, B b) {
    return ElementwiseVectorBinaryOp<OP, A, B>{.op = op, .a = a, .b = b};
}
template <TriviallyCopyable OP, TriviallyCopyableMatrixOrScalar A,
          TriviallyCopyableMatrixOrScalar B>
constexpr auto _binaryOp(OP op, A a, B b) {
    return ElementwiseMatrixBinaryOp<OP, A, B>{.op = op, .a = a, .b = b};
}

// template <TriviallyCopyable OP, TriviallyCopyable A, TriviallyCopyable B>
// inline auto binaryOp(const OP op, const A a, const B b) {
//     return _binaryOp(op, a, b);
// }
// template <TriviallyCopyable OP, typename A, TriviallyCopyable B>
// inline auto binaryOp(const OP op, const A &a, const B b) {
//     return _binaryOp(op, a.view(), b);
// }
// template <TriviallyCopyable OP, TriviallyCopyable A, typename B>
// inline auto binaryOp(const OP op, const A a, const B &b) {
//     return _binaryOp(op, a, b.view());
// }
template <TriviallyCopyable OP, typename A, typename B>
constexpr auto binaryOp(const OP op, const A &a, const B &b) {
    if constexpr (std::is_trivially_copyable_v<A>) {
        if constexpr (std::is_trivially_copyable_v<B>) {
            return _binaryOp(op, a, b);
        } else {
            return _binaryOp(op, a, b.view());
        }
    } else if constexpr (std::is_trivially_copyable_v<B>) {
        return _binaryOp(op, a.view(), b);
    } else {
        return _binaryOp(op, a.view(), b.view());
    }
}

constexpr auto bin2(std::integral auto x) { return (x * (x - 1)) >> 1; }

struct Rational {
    [[no_unique_address]] int64_t numerator{0};
    [[no_unique_address]] int64_t denominator{1};

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

    llvm::Optional<Rational> safeAdd(Rational y) const {
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
    Rational operator+(Rational y) const { return safeAdd(y).getValue(); }
    Rational &operator+=(Rational y) {
        llvm::Optional<Rational> a = *this + y;
        assert(a.hasValue());
        *this = a.getValue();
        return *this;
    }
    llvm::Optional<Rational> safeSub(Rational y) const {
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
    Rational operator-(Rational y) const { return safeSub(y).getValue(); }
    Rational &operator-=(Rational y) {
        llvm::Optional<Rational> a = *this - y;
        assert(a.hasValue());
        *this = a.getValue();
        return *this;
    }
    llvm::Optional<Rational> safeMul(int64_t y) const {
        auto [xd, yn] = divgcd(denominator, y);
        int64_t n;
        if (__builtin_mul_overflow(numerator, yn, &n)) {
            return llvm::Optional<Rational>();
        } else {
            return Rational{n, xd};
        }
    }
    llvm::Optional<Rational> safeMul(Rational y) const {
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
    Rational operator*(int64_t y) const { return safeMul(y).getValue(); }
    Rational operator*(Rational y) const { return safeMul(y).getValue(); }
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
    llvm::Optional<Rational> safeDiv(Rational y) const {
        return (*this) * y.inv();
    }
    Rational operator/(Rational y) const { return safeDiv(y).getValue(); }
    // *this -= a*b
    bool fnmadd(Rational a, Rational b) {
        if (llvm::Optional<Rational> ab = a.safeMul(b)) {
            if (llvm::Optional<Rational> c = safeSub(ab.getValue())) {
                *this = c.getValue();
                return false;
            }
        }
        return true;
    }
    bool div(Rational a) {
        if (llvm::Optional<Rational> d = safeDiv(a)) {
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

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const Rational &x) {
        os << x.numerator;
        if (x.denominator != 1) {
            os << " // " << x.denominator;
        }
        return os;
    }
    void dump() const { llvm::errs() << *this << "\n"; }

    template <AbstractMatrix B> constexpr auto operator+(B &&b) {
        return binaryOp(Add{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> constexpr auto operator+(B &&b) {
        return binaryOp(Add{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> constexpr auto operator-(B &&b) {
        return binaryOp(Sub{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> constexpr auto operator-(B &&b) {
        return binaryOp(Sub{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> constexpr auto operator/(B &&b) {
        return binaryOp(Div{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> constexpr auto operator/(B &&b) {
        return binaryOp(Div{}, *this, std::forward<B>(b));
    }

    template <AbstractVector B> constexpr auto operator*(B &&b) {
        return binaryOp(Mul{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> constexpr auto operator*(B &&b) {
        return binaryOp(Mul{}, *this, std::forward<B>(b));
    }
};
llvm::Optional<Rational> gcd(Rational x, Rational y) {
    return Rational{gcd(x.numerator, y.numerator),
                    lcm(x.denominator, y.denominator)};
}
int64_t denomLCM(PtrVector<Rational> x) {
    int64_t l = 1;
    for (auto r : x)
        l = lcm(l, r.denominator);
    return l;
}

template <> struct GetEltype<Rational> {
    using eltype = Rational;
};
template <> struct PromoteType<Rational, Rational> {
    using eltype = Rational;
};
template <std::integral I> struct PromoteType<I, Rational> {
    using eltype = Rational;
};
template <std::integral I> struct PromoteType<Rational, I> {
    using eltype = Rational;
};

[[maybe_unused]] static void normalizeByGCD(MutPtrVector<int64_t> x) {
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
llvm::raw_ostream &printMatrix(llvm::raw_ostream &os, PtrMatrix<T> A) {
    // llvm::raw_ostream &printMatrix(llvm::raw_ostream &os, T const &A) {
    auto [m, n] = A.size();
    if (m == 0)
        return os << "[ ]";
    for (size_t i = 0; i < m; i++) {
        if (i) {
            os << "  ";
        } else {
            os << "\n[ ";
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
            os << "\n";
        }
    }
    os << " ]";
    return os;
}

template <typename T> struct SmallSparseMatrix {
    // non-zeros
    [[no_unique_address]] llvm::SmallVector<T> nonZeros;
    // masks, the upper 8 bits give the number of elements in previous rows
    // the remaining 24 bits are a mask indicating non-zeros within this row
    static constexpr size_t maxElemPerRow = 24;
    [[no_unique_address]] llvm::SmallVector<uint32_t> rows;
    [[no_unique_address]] size_t col;
    static constexpr bool canResize = false;
    size_t numRow() const { return rows.size(); }
    size_t numCol() const { return col; }
    SmallSparseMatrix(size_t numRows, size_t numCols)
        : nonZeros{}, rows{llvm::SmallVector<uint32_t>(numRows)}, col{numCols} {
        assert(col <= maxElemPerRow);
    }
    T get(size_t i, size_t j) const {
        assert(j < col);
        uint32_t r(rows[i]);
        uint32_t jshift = uint32_t(1) << j;
        if (r & (jshift)) {
            // offset from previous rows
            uint32_t prevRowOffset = r >> maxElemPerRow;
            uint32_t rowOffset = std::popcount(r & (jshift - 1));
            return nonZeros[rowOffset + prevRowOffset];
        } else {
            return 0;
        }
    }
    constexpr T operator()(size_t i, size_t j) const { return get(i, j); }
    void insert(T x, size_t i, size_t j) {
        assert(j < col);
        uint32_t r{rows[i]};
        uint32_t jshift = uint32_t(1) << j;
        // offset from previous rows
        uint32_t prevRowOffset = r >> maxElemPerRow;
        uint32_t rowOffset = std::popcount(r & (jshift - 1));
        size_t k = rowOffset + prevRowOffset;
        if (r & jshift) {
            nonZeros[k] = std::move(x);
        } else {
            nonZeros.insert(nonZeros.begin() + k, std::move(x));
            rows[i] = r | jshift;
            for (size_t k = i + 1; k < rows.size(); ++k)
                rows[k] += uint32_t(1) << maxElemPerRow;
        }
    }

    struct Reference {
        [[no_unique_address]] SmallSparseMatrix<T> *A;
        [[no_unique_address]] size_t i, j;
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
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              SmallSparseMatrix<T> const &A) {
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
    return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, PtrMatrix<int64_t> A) {
    // llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Matrix<T, M, N>
    // const &A)
    // {
    return printMatrix(os, A);
}
template <typename T, typename A>
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const BaseMatrix<T, A> &B) {
    // llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Matrix<T, M, N>
    // const &A)
    // {
    return printMatrix(os, PtrMatrix<T>(B));
}
template <AbstractMatrix T>
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const T &A) {
    // llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Matrix<T, M, N>
    // const &A)
    // {
    Matrix<std::remove_const_t<typename T::eltype>> B{A};
    return printMatrix(os, PtrMatrix<typename T::eltype>(B));
}
// template <typename T>
// llvm::raw_ostream &operator<<(llvm::raw_ostream &os, PtrMatrix<const T> &A) {
//     // llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Matrix<T, M, N>
//     const &A)
//     // {
//     return printMatrix(os, A);
// }

constexpr auto operator-(const AbstractVector auto &a) {
    auto AA{a.view()};
    return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
constexpr auto operator-(const AbstractMatrix auto &a) {
    auto AA{a.view()};
    return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
static_assert(AbstractMatrix<ElementwiseUnaryOp<Sub, PtrMatrix<int64_t>>>);

template <AbstractMatrix A, typename B> constexpr auto operator+(A &&a, B &&b) {
    return binaryOp(Add{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> constexpr auto operator+(A &&a, B &&b) {
    return binaryOp(Add{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B>
constexpr auto operator+(std::integral auto a, B &&b) {
    return binaryOp(Add{}, a, std::forward<B>(b));
}
template <AbstractVector B>
constexpr auto operator+(std::integral auto a, B &&b) {
    return binaryOp(Add{}, a, std::forward<B>(b));
}

template <AbstractMatrix A, typename B> constexpr auto operator-(A &&a, B &&b) {
    return binaryOp(Sub{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> constexpr auto operator-(A &&a, B &&b) {
    return binaryOp(Sub{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B>
constexpr auto operator-(std::integral auto a, B &&b) {
    return binaryOp(Sub{}, a, std::forward<B>(b));
}
template <AbstractVector B>
constexpr auto operator-(std::integral auto a, B &&b) {
    return binaryOp(Sub{}, a, std::forward<B>(b));
}

template <AbstractMatrix A, typename B> constexpr auto operator/(A &&a, B &&b) {
    return binaryOp(Div{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> constexpr auto operator/(A &&a, B &&b) {
    return binaryOp(Div{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B>
constexpr auto operator/(std::integral auto a, B &&b) {
    return binaryOp(Div{}, a, std::forward<B>(b));
}
template <AbstractVector B>
constexpr auto operator/(std::integral auto a, B &&b) {
    return binaryOp(Div{}, a, std::forward<B>(b));
}
constexpr auto operator*(const AbstractMatrix auto &a,
                         const AbstractMatrix auto &b) {
    auto AA{a.view()};
    auto BB{b.view()};
    assert(AA.numCol() == BB.numRow());
    return MatMatMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
constexpr auto operator*(const AbstractMatrix auto &a,
                         const AbstractVector auto &b) {
    auto AA{a.view()};
    auto BB{b.view()};
    assert(AA.numCol() == BB.size());
    return MatVecMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
template <AbstractMatrix A>
constexpr auto operator*(A &&a, std::integral auto b) {
    return binaryOp(Mul{}, std::forward<A>(a), b);
}
// template <AbstractMatrix A> constexpr auto operator*(A &&a, Rational b) {
//     return binaryOp(Mul{}, std::forward<A>(a), b);
// }
template <AbstractVector A, AbstractVector B>
constexpr auto operator*(A &&a, B &&b) {
    return binaryOp(Mul{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A>
constexpr auto operator*(A &&a, std::integral auto b) {
    return binaryOp(Mul{}, std::forward<A>(a), b);
}
// template <AbstractVector A> constexpr auto operator*(A &&a, Rational b) {
//     return binaryOp(Mul{}, std::forward<A>(a), b);
// }
template <AbstractMatrix B>
constexpr auto operator*(std::integral auto a, B &&b) {
    return binaryOp(Mul{}, a, std::forward<B>(b));
}
template <AbstractVector B>
constexpr auto operator*(std::integral auto a, B &&b) {
    return binaryOp(Mul{}, a, std::forward<B>(b));
}

// constexpr auto operator*(AbstractMatrix auto &A, AbstractVector auto &x) {
//     auto AA{A.view()};
//     auto xx{x.view()};
//     return MatMul<decltype(AA), decltype(xx)>{.a = AA, .b = xx};
// }

template <AbstractVector V>
constexpr auto operator*(const Transpose<V> &a, const AbstractVector auto &b) {
    typename V::eltype s = 0;
    for (size_t i = 0; i < b.size(); ++i)
        s += a.a(i) * b(i);
    return s;
}

static_assert(AbstractVector<Vector<int64_t>>);
static_assert(AbstractVector<const Vector<int64_t>>);
static_assert(AbstractVector<Vector<int64_t> &>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(AbstractMatrix<IntMatrix &>);

static_assert(std::copyable<Matrix<int64_t, 4, 4>>);
static_assert(std::copyable<Matrix<int64_t, 4, 0>>);
static_assert(std::copyable<Matrix<int64_t, 0, 4>>);
static_assert(std::copyable<Matrix<int64_t, 0, 0>>);
static_assert(std::copyable<SquareMatrix<int64_t>>);

static_assert(DerivedMatrix<Matrix<int64_t, 4, 4>>);
static_assert(DerivedMatrix<Matrix<int64_t, 4, 0>>);
static_assert(DerivedMatrix<Matrix<int64_t, 0, 4>>);
static_assert(DerivedMatrix<Matrix<int64_t, 0, 0>>);
static_assert(DerivedMatrix<IntMatrix>);
static_assert(DerivedMatrix<IntMatrix>);
static_assert(DerivedMatrix<IntMatrix>);

static_assert(std::is_same_v<SquareMatrix<int64_t>::eltype, int64_t>);
static_assert(std::is_same_v<IntMatrix::eltype, int64_t>);

static_assert(AbstractVector<PtrVector<Rational>>);
static_assert(AbstractVector<ElementwiseVectorBinaryOp<Sub, PtrVector<Rational>,
                                                       PtrVector<Rational>>>);

template <typename T, typename I> struct SliceView {
    using eltype = T;
    static constexpr bool canResize = false;
    [[no_unique_address]] MutPtrVector<T> a;
    [[no_unique_address]] llvm::ArrayRef<I> i;
    struct Iterator {
        [[no_unique_address]] MutPtrVector<T> a;
        [[no_unique_address]] llvm::ArrayRef<I> i;
        [[no_unique_address]] size_t j;
        bool operator==(const Iterator &k) const { return j == k.j; }
        Iterator &operator++() {
            ++j;
            return *this;
        }
        T &operator*() { return a[i[j]]; }
        const T &operator*() const { return a[i[j]]; }
        T *operator->() { return &a[i[j]]; }
        const T *operator->() const { return &a[i[j]]; }
    };
    Iterator begin() { return Iterator{a, i, 0}; }
    Iterator end() { return Iterator{a, i, i.size()}; }
    T &operator()(size_t j) { return a[i[j]]; }
    const T &operator()(size_t j) const { return a[i[j]]; }
    size_t size() const { return i.size(); }
    SliceView<T, I> view() { return *this; }
};

static_assert(AbstractVector<SliceView<int64_t, unsigned>>);
