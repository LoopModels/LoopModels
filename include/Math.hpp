// #include <hwy/highway.h>
// #include <hwy/foreach_target.h>
// HWY_BEFORE_NAMESPACE();  // at file scope
// namespace project {  // optional
// namespace HWY_NAMESPACE {
// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#pragma once
#include "./Macro.hpp"
#include "./TypePromotion.hpp"
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

// using namespace hwy::HWY_NAMESPACE;
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "./Math.hpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
// namespace hn = hwy::HWY_NAMESPACE;
HWY_BEFORE_NAMESPACE();
// namespace project {  // optional
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
// #ifndef NDEBUGWY_NAMESPACE;
// #ifndef NDEBUG
// #include <memory>
// #include <stacktrace>
// using stacktrace =
//     std::basic_stacktrace<std::allocator<std::stacktrace_entry>>;
// #endif

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

template <typename T> struct VReference {
    T *ptr;
    // operator T() const { return *ptr;}
    template <typename V> operator V() const {
        return hn::Load(hn::ScalableTag<T>(), ptr);
    }
    //    void operator=(T x){
    //     *ptr = x;
    //    }
    void operator=(auto v) { hn::Store(v, hn::ScalableTag<T>(), ptr); }
};
static_assert(
    std::convertible_to<VReference<int64_t>, typename VType<int64_t>::type>);

template <typename T, typename VI> struct SVReference {
    T *ptr;
    VI vi;

    // operator T() const { return *ptr;}
    template <typename V> operator V() const {
        return hn::GatherIndex(hn::ScalableTag<std::remove_const_t<T>>(), ptr,
                               vi);
    }
    void operator=(auto v) {
        hn::ScatterIndex(v, hn::ScalableTag<std::remove_const_t<T>>(), ptr, vi);
    }
};
template <typename T> inline auto svreference(T *ptr, size_t i, size_t stride) {
    const hn::ScalableTag<std::remove_const_t<T>> d;
    auto vec_stride = hn::Set(d, int(stride));
    auto vec_i = hn::Set(d, int(i));
    auto vi{(hn::Iota(d, 0) + vec_i) * vec_stride};
    return SVReference<T, decltype(vi)>{ptr, vi};
}

struct VIndex {
    size_t i;
};
static_assert(!std::convertible_to<size_t, VIndex>);
static_assert(!std::convertible_to<int64_t, VIndex>);
static_assert(!std::convertible_to<int, VIndex>);
static_assert(!std::integral<VIndex>);

template <typename T>
concept AbstractVector =
    HasEltype<T> &&
    requires(T t, size_t i, VIndex vi) {
        {
            t(i)
            }
            -> std::convertible_to<typename std::remove_reference_t<T>::eltype>;
        {
            t(vi)
            } -> std::convertible_to<typename VType<
                typename std::remove_reference_t<T>::eltype>::type>;
        { t.size() } -> std::convertible_to<size_t>;
        { t.view() };
        { std::remove_reference_t<T>::canResize } -> std::same_as<const bool &>;
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
    HasEltype<T> &&
    requires(T t, size_t i) {
        {
            t(i, i)
            }
            -> std::convertible_to<typename std::remove_reference_t<T>::eltype>;
        { t.numRow() } -> std::convertible_to<size_t>;
        { t.numCol() } -> std::convertible_to<size_t>;
        { std::remove_reference_t<T>::canResize } -> std::same_as<const bool &>;
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
    size_t Lane = hn::Lanes(hn::ScalableTag<vtype_t<decltype(x)>>());
    size_t remainder = M % Lane;
    std::cout << "Vec Laneeeeeeeee:" << Lane << std::endl;
    for (size_t i = 0; i < M - remainder; i += Lane) {
        y(VIndex{i}) = x(VIndex{i});
    }
    for (size_t i = M - remainder; i < M; ++i) {
        y(i) = x(i);
    }
    return y;
}
inline auto &copyto(AbstractMatrixCore auto &A,
                    const AbstractMatrixCore auto &B) {
    const size_t M = B.numRow();
    const size_t N = B.numCol();
    A.extendOrAssertSize(M, N);
    // std::cout << "Mat Laneeeeeeeee:" << Lane << std::endl;
    size_t Lane = hn::Lanes(hn::ScalableTag<vtype_t<decltype(B)>>());
    size_t remainder = M % Lane;
    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < N - remainder; j += Lane)
            A(i, VIndex{j}) = B(i, VIndex{j});
        for (size_t j = N - remainder; j < N; j++)
            A(i, j) = B(i, j);
    }
    // for (size_t r = 0; r < M; ++r)
    //     for (size_t c = 0; c < N; ++c)
    //         A(r, c) = B(r, c);
    return A;
}

template <typename V>
concept HWVec = requires(V v) {
                    { v + v } -> std::same_as<hn::VFromD<hn::DFromV<V>>>;
                };

struct Add {
    constexpr auto operator()(std::integral auto x,
                              std::integral auto y) const {
        std::cout << "doing add in a scalar way" << std::endl;
        return x + y;
    }
    template <HWVec V>
    constexpr auto operator()(std::integral auto x, V y) const {
        V vx = Set(hn::DFromV<V>(), x);
        return vx + y;
    }
    template <HWVec V>
    constexpr auto operator()(V x, std::integral auto y) const {
        V vy = Set(hn::DFromV<V>(), y);
        return x + vy;
    }
    template <HWVec V> constexpr auto operator()(V x, V y) const {
        return x + y;
    }
};
struct Sub {
    constexpr auto operator()(auto x) const { return -x; }
    constexpr auto operator()(std::integral auto x,
                              std::integral auto y) const {
        std::cout << "doing sub in a scalar way" << std::endl;
        return x - y;
    }
    template <HWVec V>
    constexpr auto operator()(std::integral auto x, V y) const {
        V vx = Set(hn::DFromV<V>(), x);
        return vx - y;
    }
    template <HWVec V>
    constexpr auto operator()(V x, std::integral auto y) const {
        V vy = Set(hn::DFromV<V>(), y);
        return x - vy;
    }
    template <HWVec V> constexpr auto operator()(V x, V y) const {
        return x - y;
    }
};
struct Mul {
    constexpr auto operator()(std::integral auto x,
                              std::integral auto y) const {
        std::cout << "doing mul in a scalar way" << std::endl;
        return x * y;
    }
    template <HWVec V>
    constexpr auto operator()(std::integral auto x, V y) const {
        V vx = Set(hn::DFromV<V>(), x);
        return vx * y;
    }
    template <HWVec V>
    constexpr auto operator()(V x, std::integral auto y) const {
        V vy = Set(hn::DFromV<V>(), y);
        return x * vy;
    }
    template <HWVec V> constexpr auto operator()(V x, V y) const {
        return x * y;
    }
};
struct Div {
    constexpr auto operator()(std::integral auto x,
                              std::integral auto y) const {
        std::cout << "doing div in a scalar way" << std::endl;
        return x / y;
    }
    template <HWVec V>
    constexpr auto operator()(std::integral auto x, V y) const {
        V vx = Set(hn::DFromV<V>(), x);
        return vx / y;
    }
    template <HWVec V>
    constexpr auto operator()(V x, std::integral auto y) const {
        V vy = Set(hn::DFromV<V>(), y);
        return x / vy;
    }
    template <HWVec V> constexpr auto operator()(V x, V y) const {
        return x / y;
    }
};

template <typename Op, typename A> struct ElementwiseUnaryOp {
    using eltype = typename A::eltype;
    const Op op;
    const A a;
    static constexpr bool canResize = false;
    auto operator()(auto i) const { return op(a(i)); }
    auto operator()(auto i, auto j) const { return op(a(i, j)); }

    size_t size() { return a.size(); }
    size_t numRow() { return a.numRow(); }
    size_t numCol() { return a.numCol(); }
    inline auto view() const { return *this; };
};
// scalars broadcast
inline auto get(const std::integral auto A, auto) { return A; }

// template <typename T> struct Broadcast {
//     T x;
//     template <typename V> operator V() const {
//	return hn::Set(hn::DFromV<V>(), x);
//     }
// };

// inline auto get(std::integral auto A, VIndex) {
//     return Broadcast<decltype(A)>{A};
// }

inline auto get(const std::floating_point auto A, auto) { return A; }
inline auto get(const std::integral auto A, auto, auto) { return A; }
inline auto get(const std::floating_point auto A, auto, auto) { return A; }

inline auto get(const AbstractVector auto &A, size_t i) { return A(i); }
inline auto get(const AbstractMatrix auto &A, size_t i, size_t j) {
    return A(i, j);
}
// inline auto get(const AbstractVector auto &A,  i) { return A(i); }
inline auto get(const AbstractVector auto &A, VIndex i) {
    // using V = hn::VFromD<hn::ScalableTag<decltype(A)::eltype>()>;
    vtype_t<decltype(A)> v(A(i));
    return v;
}

inline auto get(const AbstractMatrix auto &A, std::integral auto i,
                std::integral auto j) {
    std::cout << "ttttttt" << std::endl;
    return A(i, j);
}
inline auto get(const AbstractMatrix auto &A, auto i, auto j) {
    // std::cout << decltype(A) <<std::endl;
    // using V = typename VType<typename decltype(A)::eltype>::type;
    vtype_t<decltype(A)> v(A(i, j));
    return v;
}

constexpr size_t size(const std::integral auto) { return 1; }
constexpr size_t size(const std::floating_point auto) { return 1; }
inline size_t size(const AbstractVector auto &x) { return x.size(); }

template <typename T>
concept VectorOrScalar = AbstractVector<T> || Scalar<T>;
template <typename T>
concept MatrixOrScalar = AbstractMatrix<T> || Scalar<T>;

template <typename Op, VectorOrScalar A, VectorOrScalar B>
struct ElementwiseVectorBinaryOp {
    using eltype = typename PromoteEltype<A, B>::eltype;
    Op op;
    A a;
    B b;
    static constexpr bool canResize = false;
    auto operator()(auto i) const { return op(get(a, i), get(b, i)); }
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
    using eltype = typename PromoteEltype<A, B>::eltype;
    Op op;
    A a;
    B b;
    static constexpr bool canResize = false;
    auto operator()(auto i, auto j) const {
        return op(get(a, i, j), get(b, i, j));
    }
    // auto operator()(auto i, auto j) const {
    //     return op(get(a, i, j), get(b, i, j));
    // }
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
    using eltype = typename A::eltype;
    A a;
    static constexpr bool canResize = false;
    auto operator()(auto i, auto j) const { return a(j, i); }
    size_t numRow() const { return a.numCol(); }
    size_t numCol() const { return a.numRow(); }
    auto &view() const { return *this; };
};
template <AbstractMatrix A, AbstractMatrix B> struct MatMatMul {
    using eltype = typename PromoteEltype<A, B>::eltype;
    A a;
    B b;
    static constexpr bool canResize = false;
    auto operator()(auto i, auto j) const {
        static_assert(AbstractMatrix<B>, "B should be an AbstractMatrix");
        Mul m;
        auto s = m(m(a(i, 0), b(0, j)), 0);
        for (size_t k = 0; k < a.numCol(); ++k)
            s += m(a(i, k), b(k, j));
        return s;
    }
    size_t numRow() const { return a.numRow(); }
    size_t numCol() const { return b.numCol(); }
    inline auto view() const { return *this; };
};
template <AbstractMatrix A, AbstractVector B> struct MatVecMul {
    using eltype = typename PromoteEltype<A, B>::eltype;
    A a;
    B b;
    static constexpr bool canResize = false;
    auto operator()(auto i) const {
        static_assert(AbstractVector<B>, "B should be an AbstractVector");
        Mul m;
        auto s = m(m(a(i, 0), b(0)), 0);
        for (size_t k = 0; k < a.numCol(); ++k)
            s += m(a(i, k), b(k));
        return s;
    }
    size_t size() const { return a.numRow(); }
    inline auto view() const { return *this; };
};

struct Begin {
} begin;
struct End {
} end;
struct OffsetBegin {
    size_t offset;
};
inline OffsetBegin operator+(size_t x, Begin) { return OffsetBegin{x}; }
inline OffsetBegin operator+(Begin, size_t x) { return OffsetBegin{x}; }
inline OffsetBegin operator+(size_t x, OffsetBegin y) {
    return OffsetBegin{x + y.offset};
}
inline OffsetBegin operator+(OffsetBegin y, size_t x) {
    return OffsetBegin{x + y.offset};
}
struct OffsetEnd {
    size_t offset;
};
inline OffsetEnd operator-(End, size_t x) { return OffsetEnd{x}; }
inline OffsetEnd operator-(OffsetEnd y, size_t x) {
    return OffsetEnd{y.offset + x};
}

template <typename B, typename E> struct Range {
    B b;
    E e;
};
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
                                               std::integral auto j) {
        return Range<size_t, size_t>{size_t(i), size_t(j)};
    }
    template <typename B, typename E>
    constexpr Range<B, E> operator()(B i, E j) {
        return Range<B, E>{i, j};
    }
} _;

constexpr Range<size_t, size_t> canonicalizeRange(Range<size_t, size_t> r,
                                                  size_t) {
    return r;
}
constexpr Range<size_t, size_t> canonicalizeRange(Range<Begin, size_t> r,
                                                  size_t) {
    return Range<size_t, size_t>{0, r.e};
}
constexpr Range<size_t, size_t> canonicalizeRange(Range<size_t, End> r,
                                                  size_t M) {
    return Range<size_t, size_t>{r.b, M};
}
constexpr Range<size_t, size_t> canonicalizeRange(Range<Begin, End>, size_t M) {
    return Range<size_t, size_t>{0, M};
}
constexpr Range<size_t, size_t> canonicalizeRange(Colon, size_t M) {
    return Range<size_t, size_t>{0, M};
}

template <std::integral B, std::integral E>
constexpr Range<size_t, size_t> canonicalizeRange(Range<B, E> r, size_t) {
    return Range<size_t, size_t>{.b = size_t(r.b), .e = size_t(r.e)};
}
template <std::integral E>
constexpr Range<size_t, size_t> canonicalizeRange(Range<Begin, E> r, size_t) {
    return Range<size_t, size_t>{0, size_t(r.e)};
}
template <std::integral B>
constexpr Range<size_t, size_t> canonicalizeRange(Range<B, End> r, size_t M) {
    return Range<size_t, size_t>{size_t(r.b), M};
}

template <typename T> struct PtrVector {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    using eltype = T;
    const T *const mem;
    const size_t N;
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
    auto operator()(VIndex i) const {
        return hn::Load(hn::ScalableTag<T>(), mem + i.i);
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
    T *const mem;
    const size_t N;
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
    VReference<T> operator()(VIndex i) { return VReference<T>{mem + i.i}; }
    auto operator()(VIndex i) const {
        return hn::Load(hn::ScalableTag<T>(), mem + i.i);
    }
    // copy constructor
    // MutPtrVector(const MutPtrVector<T> &x) : mem(x.mem), N(x.N) {}
    MutPtrVector(const MutPtrVector<T> &x) = default;
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
    MutPtrVector<T> operator=(PtrVector<T> x) {
        std::cout << "1111111" << std::endl;
        return copyto(*this, x);
    }
    MutPtrVector<T> operator=(MutPtrVector<T> x) {
        std::cout << "22222222" << std::endl;
        return copyto(*this, x);
    }
    MutPtrVector<T> operator=(const AbstractVector auto &x) {
        return copyto(*this, x);
    }
    MutPtrVector<T> operator=(std::integral auto x) {
        auto &&y = *this;
        const hn::ScalableTag<vtype_t<decltype(x)>> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, x);
        for (size_t i = 0; i < N - remainder; i += Lane) {
            y(VIndex{i}) = x(VIndex{i});
        }
        for (size_t i = N - remainder; i < N; ++i) {
            y(i) = x(i);
        }
        // for (auto &&y : *this)
        //     y = x;
        return *this;
    }
    // MutPtrVector<T> operator+=(const AbstractVector auto &x) {
    //     assert(N == x.size());
    //     for (size_t i = 0; i < N; ++i)
    //         mem[i] += x(i);
    //     return *this;
    // }
    MutPtrVector<T> operator+=(const AbstractVector auto &x) {
        assert(N == x.size());
        *this = *this + x;
        return *this;
        // return *this = *this + x;

        // return *this; //?
    }
    MutPtrVector<T> operator-=(const AbstractVector auto &x) {
        assert(N == x.size());
        *this = *this - x;
        return *this;
    }
    MutPtrVector<T> operator*=(const AbstractVector auto &x) {
        assert(N == x.size());
        *this = *this * x;
        return *this;
    }
    MutPtrVector<T> operator/=(const AbstractVector auto &x) {
        assert(N == x.size());
        *this = *this / x;
        return *this;
    }
    MutPtrVector<T> operator+=(const std::integral auto x) {
        const hn::ScalableTag<T> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, x);
        for (size_t i = 0; i < N - remainder; i += Lane) {
            const auto mem_vec = hn::Load(d, mem + i);
            auto x_vec = hn::Load(d, mem + i);
            x_vec = x_vec + const_vec;
            hn::Store(x_vec, d, mem + i);
        }
        for (size_t i = N - remainder; i < N; ++i) {
            mem[i] += x;
        }
        return *this;
    }
    MutPtrVector<T> operator-=(const std::integral auto x) {
        const hn::ScalableTag<T> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, x);
        for (size_t i = 0; i < N - remainder; i += Lane) {
            const auto mem_vec = hn::Load(d, mem + i);
            auto x_vec = hn::Load(d, mem + i);
            x_vec = x_vec - const_vec;
            hn::Store(x_vec, d, mem + i);
        }
        for (size_t i = N - remainder; i < N; ++i) {
            mem[i] -= x;
        }
        return *this;
    }
    MutPtrVector<T> operator*=(const std::integral auto x) {
        const hn::ScalableTag<T> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, x);
        for (size_t i = 0; i < N - remainder; i += Lane) {
            const auto mem_vec = hn::Load(d, mem + i);
            auto x_vec = hn::Load(d, mem + i);
            x_vec = x_vec * const_vec;
            hn::Store(x_vec, d, mem + i);
        }
        for (size_t i = N - remainder; i < N; ++i) {
            mem[i] *= x;
        }
        return *this;
    }
    MutPtrVector<T> operator/=(const std::integral auto x) {
        const hn::ScalableTag<T> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, x);
        for (size_t i = 0; i < N - remainder; i += Lane) {
            const auto mem_vec = hn::Load(d, mem + i);
            auto x_vec = hn::Load(d, mem + i);
            x_vec = x_vec / const_vec;
            hn::Store(x_vec, d, mem + i);
        }
        for (size_t i = N - remainder; i < N; ++i) {
            mem[i] /= x;
        }
        return *this;
    }
    void extendOrAssertSize(size_t M) const {
        std::cout << "MMMMMMM: " << M << "NNNNNN: " << N << std::endl;
        assert(M == N);
    }
};

//
// Vectors
//

int64_t gcd(PtrVector<int64_t> x) {
    int64_t g = std::abs(x[0]);
    for (size_t i = 1; i < x.size(); ++i)
        g = gcd(g, x[i]);
    return g;
}

template <typename T> inline auto view(llvm::SmallVectorImpl<T> &x) {
    return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> inline auto view(const llvm::SmallVectorImpl<T> &x) {
    return PtrVector<T>{.mem = x.data(), .N = x.size()};
}
template <typename T> inline auto view(llvm::MutableArrayRef<T> x) {
    return MutPtrVector<T>{x.data(), x.size()};
}
template <typename T> inline auto view(llvm::ArrayRef<T> x) {
    return PtrVector<T>{.mem = x.data(), .N = x.size()};
}

template <typename T> struct Vector {
    using eltype = T;
    llvm::SmallVector<T, 16> data;
    static constexpr bool canResize = true;

    Vector(size_t N = 0) : data(llvm::SmallVector<T>(N)){};
    Vector(llvm::SmallVector<T> A) : data(std::move(A)){};

    const T *getPtr(size_t i) const { return data.data() + i; }
    T *getPtr(size_t i) { return data.data() + i; }

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
    VReference<T> operator()(VIndex i) {
        return VReference<T>{data.data() + i.i};
    }
    auto operator()(VIndex i) const {
        return hn::Load(hn::ScalableTag<T>(), data.data() + i.i);
    }
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
    // auto x = (a - b) * c - (d/4);
    // Vector e(x);
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
    // Vector<T> &operator=(AbstractVector auto &x) {
    //     std::cout << "Vector ======" << std::endl;
    //     MutPtrVector<T> y{*this};
    //     y = x;
    //     return *this;
    // }
    Vector<T> &operator=(const AbstractVector auto &x) {
        std::cout << "Vector ======" << std::endl;
        MutPtrVector<T> y{*this};
        y = x;
        return *this;
    }
    Vector<T> &operator+=(const AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y += x;
        return *this;
    }
    Vector<T> &operator-=(const AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y -= x;
        return *this;
    }
    Vector<T> &operator*=(const AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y *= x;
        return *this;
    }
    Vector<T> &operator/=(const AbstractVector auto &x) {
        MutPtrVector<T> y{*this};
        y /= x;
        return *this;
    }
    Vector<T> &operator+=(const std::integral auto x) {
        MutPtrVector<T> y{*this};
        y += x;
        return *this;
    }
    Vector<T> &operator-=(const std::integral auto x) {
        MutPtrVector<T> y{*this};
        y -= x;
        return *this;
    }
    Vector<T> &operator*=(const std::integral auto x) {
        MutPtrVector<T> y{*this};
        y *= x;
        return *this;
    }
    Vector<T> &operator/=(const std::integral auto x) {
        MutPtrVector<T> y{*this};
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
};

static_assert(std::copyable<Vector<intptr_t>>);

template <typename T> struct StridedVector {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    using eltype = T;
    const T *const d;
    const size_t N;
    const size_t x;
    static constexpr bool canResize = false;
    struct StridedIterator {
        const T *d;
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

    auto operator()(VIndex i) const { return svreference(d, i.i, x); }
};
template <typename T> struct MutStridedVector {
    static_assert(!std::is_const_v<T>, "T should not be const");
    using eltype = T;
    T *const d;
    const size_t N;
    const size_t x;
    static constexpr bool canResize = false;
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
    T &operator()(size_t i) { return d[i * x]; }
    const T &operator()(size_t i) const { return d[i * x]; }

    auto operator()(VIndex i) { return svreference(d, i.i, x); }
    auto operator()(VIndex i) const {
        const hn::ScalableTag<T> dtag;
        auto vec_x = hn::Set(dtag, int(x));
        auto vec_i = hn::Set(dtag, int(i.i));
        auto vi{(hn::Iota(hn::ScalableTag<T>(), 0) + vec_i) * vec_x};
        return hn::GatherIndex(hn::ScalableTag<T>(), d, vi);
    }
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
    StridedVector<T> view() const { return *this; }
    MutStridedVector<T> &operator=(const T &x) {
        for (auto &&y : *this)
            y = x;
        return *this;
    }
    MutStridedVector<T> &operator=(const AbstractVector auto &x) {
        return copyto(*this, x);
    }
    MutStridedVector<T> &operator+=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        const hn::ScalableTag<T> dtag;
        size_t Lane = hn::Lanes(dtag);
        size_t remainder = N % Lane;
        for (size_t i = 0; i < N - remainder; i += Lane) {
            auto vec_x = x(VIndex{i});
            decltype(vec_x) vcol = self(VIndex{i});
            self(VIndex{i}) = vcol + vec_x;
        }
        for (size_t i = N - remainder; i < N; ++i)
            self(i) += x(i);
        return self;
    }
    MutStridedVector<T> &operator-=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        const hn::ScalableTag<T> dtag;
        size_t Lane = hn::Lanes(dtag);
        size_t remainder = N % Lane;
        for (size_t i = 0; i < N - remainder; i += Lane) {
            auto vec_x = x(VIndex{i});
            decltype(vec_x) vcol = self(VIndex{i});
            self(VIndex{i}) = vcol - vec_x;
        }
        for (size_t i = N - remainder; i < N; ++i)
            self(i) -= x(i);
        return self;
    }
    MutStridedVector<T> &operator*=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        const hn::ScalableTag<T> dtag;
        size_t Lane = hn::Lanes(dtag);
        size_t remainder = N % Lane;
        for (size_t i = 0; i < N - remainder; i += Lane) {
            auto vec_x = x(VIndex{i});
            decltype(vec_x) vcol = self(VIndex{i});
            self(VIndex{i}) = vcol * vec_x;
        }
        for (size_t i = N - remainder; i < N; ++i)
            self(i) *= x(i);
        return self;
    }
    MutStridedVector<T> &operator/=(const AbstractVector auto &x) {
        const size_t M = x.size();
        MutStridedVector<T> &self = *this;
        assert(M == N);
        const hn::ScalableTag<T> dtag;
        size_t Lane = hn::Lanes(dtag);
        size_t remainder = N % Lane;
        for (size_t i = 0; i < N - remainder; i += Lane) {
            auto vec_x = x(VIndex{i});
            decltype(vec_x) vcol = self(VIndex{i});
            self(VIndex{i}) = vcol / vec_x;
        }
        for (size_t i = N - remainder; i < N; ++i)
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
    const T *const mem;
    const size_t M, N, X;

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
    VReference<T> operator()(size_t row, VIndex col) {
        assert(row < M);
        assert(col.i < numCol());
        return VReference<T>{data() + col.i + row * rowStride()};
    }
    auto operator()(size_t row, VIndex col) const {
        assert(row < M);
        assert(col.i < numCol());
        return hn::Load(hn::ScalableTag<T>(),
                        data() + col.i + row * rowStride());
    }
    auto operator()(VIndex row, size_t col) const {
        assert(row.i < M);
        assert(col < N);
        auto sv = svreference(data(), row.i * rowStride() + col, rowStride());
        // vtype_t<std::add_const_t<T>> v = sv;
        vtype_t<std::remove_const_t<T>> v = sv;
        return v;
    }

    // inline auto &operator()(size_t row, VIndex col) const {
    //     assert(row < M);
    //     assert(col.i < N);
    //     return *(data() + col.i + row * X);
    // }
    inline PtrMatrix<T> operator()(Range<size_t, size_t> rows,
                                   Range<size_t, size_t> cols) {
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
    inline auto operator()(Colon, size_t col) { return getCol(col); }
    inline auto operator()(size_t row, Colon) { return getRow(row); }

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
    Vector<T> diag() const {
        size_t K = std::min(M, N);
        Vector<T> d;
        d.resizeForOverwrite(K);
        for (size_t k = 0; k < K; ++k)
            d(k) = mem[k * (1 + X)];
        return d;
    }
    inline PtrMatrix<T> view() const { return *this; };
    Transpose<PtrMatrix<T>> transpose() const {
        return Transpose<PtrMatrix<T>>{*this};
    }
    void extendOrAssertSize(size_t MM, size_t NN) const {
        assert(MM == M);
        assert(NN == N);
    }
};

// HWY_BEFORE_NAMESPACE();  // required if not using HWY_ATTR
// // namespace project{
// namespace HWY_NAMESPACE {
// // namespace hn = hwy::HWY_NAMESPACE;

template <typename T> struct MutPtrMatrix {
    using eltype = std::remove_reference_t<T>;
    static_assert(!std::is_const_v<T>,
                  "MutPtrMatrix should never have const T");
    T *const mem;
    const size_t M, N, X;
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

    inline std::pair<size_t, size_t> size() const {
        return std::make_pair(M, N);
    }

    inline auto &operator()(size_t row, size_t col) {
        assert(row < M);
        assert(col < N);
        return *(data() + col + row * rowStride());
    }
    inline auto &operator()(size_t row, size_t col) const {
        assert(row < M);
        assert(col < N);
        return *(data() + col + row * rowStride());
    }
    // TODO
    VReference<T> operator()(size_t row, VIndex col) {
        assert(row < M);
        assert(col.i < N);
        return VReference<T>{data() + col.i + row * rowStride()};
    }
    auto operator()(size_t row, VIndex col) const {
        assert(row < M);
        assert(col.i < N);
        return hn::Load(hn::ScalableTag<T>(),
                        data() + col.i + row * rowStride());
    }
    auto operator()(VIndex row, size_t col) {
        assert(row.i < M);
        assert(col < N);
        return svreference(data(), row.i * rowStride() + col, rowStride());
    }
    auto operator()(VIndex row, size_t col) const {
        assert(row.i < M);
        assert(col < N);
        auto sv = svreference(data(), row.i * rowStride() + col, rowStride());
        vtype_t<T> v = sv;
        return v;
    }

    inline auto operator()(Range<size_t, size_t> rows,
                           Range<size_t, size_t> cols) {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= N);
        return MutPtrMatrix<T>{.mem = data() + cols.b + rows.b * rowStride(),
                               .M = rows.e - rows.b,
                               .N = cols.e - cols.b,
                               .X = rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Range<C0, C1> cols) {
        return (*this)(canonicalizeRange(rows, M), canonicalizeRange(cols, N));
    }
    template <typename C0, typename C1>
    inline MutPtrMatrix<T> operator()(Colon, Range<C0, C1> cols) {
        return (*this)(Range<size_t, size_t>{0, M}, canonicalizeRange(cols, N));
    }
    template <typename R0, typename R1>
    inline MutPtrMatrix<T> operator()(Range<R0, R1> rows, Colon) {
        return (*this)(canonicalizeRange(rows, M), Range<size_t, size_t>{0, N});
    }
    template <typename R0, typename R1>
    inline MutStridedVector<T> operator()(Range<R0, R1> rows, size_t col) {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline MutPtrVector<T> operator()(size_t row, Range<C0, C1> cols) {
        return getRow(row)(canonicalizeRange(cols, N));
    }
    inline auto operator()(Colon, size_t col) { return getCol(col); }
    inline auto operator()(size_t row, Colon) { return getRow(row); }

    inline auto operator()(Range<size_t, size_t> rows,
                           Range<size_t, size_t> cols) const {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= M);
        assert(cols.e <= N);
        return PtrMatrix<T>{.mem = data() + cols.b + rows.b * rowStride(),
                            .M = rows.e - rows.b,
                            .N = cols.e - cols.b,
                            .X = rowStride()};
    }
    template <typename R0, typename R1, typename C0, typename C1>
    inline auto operator()(Range<R0, R1> rows, Range<C0, C1> cols) const {
        return view(canonicalizeRange(rows, M), canonicalizeRange(cols, N));
    }
    template <typename C0, typename C1>
    inline auto operator()(Colon, Range<C0, C1> cols) const {
        return view(Range<size_t, size_t>{0, M}, canonicalizeRange(cols, N));
    }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, Colon) const {
        return view(canonicalizeRange(rows, M), Range<size_t, size_t>{0, N});
    }
    inline MutPtrMatrix<T> operator()(Colon, Colon) { return *this; }
    inline PtrMatrix<T> operator()(Colon, Colon) const { return *this; }
    template <typename R0, typename R1>
    inline auto operator()(Range<R0, R1> rows, size_t col) const {
        return getCol(col)(canonicalizeRange(rows, M));
    }
    template <typename C0, typename C1>
    inline auto operator()(size_t row, Range<C0, C1> cols) const {
        return getRow(row)(canonicalizeRange(cols, N));
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
    MutPtrMatrix<T> operator+=(const AbstractMatrix auto &B) {
        assert(M == B.numRow());
        assert(N == B.numCol());
        // const hn::ScalableTag<T> d;
        // size_t Lane = hn::Lanes(d);
        // size_t remainder = N % Lane;
        // for (size_t i = 0; i < M; ++i)
        //     for (size_t j = 0; j < N - remainder; j += Lane)

        //          row_vec = (*this)(i, VIndex{j});
        //         (*this)(i, VIndex{j}) = (*this)(i, VIndex{j}) + B(i,
        //         VIndex{j});
        // B(i, VIndex{j});

        // HWY_BEFORE_NAMESPACE();  // required if not using HWY_ATTR
        // namespace project{
        // namespace HWY_NAMESPACE {
        // namespace hn = hwy::HWY_NAMESPACE;
        // for (size_t r = 0; r < M; ++r)
        //     for (size_t c = 0; c < N; ++c)
        //         (*this)(r, c) += B(r, c);
        return *this = *this + B;
    }
    // }
    // HWY_AFTER_NAMESPACE();
    // }
    MutPtrMatrix<T> operator-=(const AbstractMatrix auto &B) {
        assert(M == B.numRow());
        assert(N == B.numCol());
        return *this = *this - B;
    }
    MutPtrMatrix<T> operator*=(const std::integral auto b) {
        const hn::ScalableTag<T> d;
        size_t Lane = hn::Lanes(d);
        size_t remainder = N % Lane;
        const auto const_vec = hn::Set(d, b);
        for (size_t i = 0; i < M; ++i)
            for (size_t j = 0; j < N - remainder; j += Lane) {
                decltype(const_vec) vec_row = (*this)(i, VIndex{j});
                (*this)(i, VIndex{j}) = vec_row * const_vec;
            }
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
    Vector<T> diag() const {
        size_t K = std::min(M, N);
        Vector<T> d;
        d.resizeForOverwrite(N);
        for (size_t k = 0; k < K; ++k)
            d(k) = mem[k * (1 + X)];
        return d;
    }
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
template <typename T> inline auto ptrVector(T *p, size_t M) {
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
        assert(row < numRow());
        assert(col < numCol());
        return *(data() + col + row * rowStride());
    }
    VReference<T> operator()(size_t row, VIndex col) {
        assert(row < numRow());
        assert(col.i < numCol());
        return VReference<T>{data() + col.i + row * rowStride()};
    }
    auto operator()(size_t row, VIndex col) const {
        assert(row < numRow());
        assert(col.i < numCol());
        return hn::Load(hn::ScalableTag<T>(),
                        data() + col.i + row * rowStride());
    }
    inline MutPtrMatrix<T> operator()(Range<size_t, size_t> rows,
                                      Range<size_t, size_t> cols) {
        assert(rows.e >= rows.b);
        assert(cols.e >= cols.b);
        assert(rows.e <= numRow());
        assert(cols.e <= numCol());
        return MutPtrMatrix<T>{.mem = data() + cols.b + rows.b * rowStride(),
                               .M = rows.e - rows.b,
                               .N = cols.e - cols.b,
                               .X = rowStride()};
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
        return MutPtrMatrix<T>{
            .mem = data(), .M = numRow(), .N = numCol(), .X = rowStride()};
    }
    operator PtrMatrix<T>() const {
        return PtrMatrix<T>{
            .mem = data(), .M = numRow(), .N = numCol(), .X = rowStride()};
    }
    MutPtrMatrix<T> operator=(const AbstractMatrix auto &B) {
        MutPtrMatrix<T> A{*this};
        return copyto(A, B);
    }
    // Mark
    //  MutPtrMatrix<T>& operator=(const AbstractMatrix auto &B) {
    //      MutPtrMatrix<T> A{*this};
    //      return copyto(A, B);
    //  }
    MutPtrMatrix<T> operator+=(const AbstractMatrix auto &B) {
        // MutPtrMatrix<T> A{*this};
        // TODO?
        MutPtrMatrix<T> A = *this;
        return A += B;
    }
    MutPtrMatrix<T> operator-=(const AbstractMatrix auto &B) {
        MutPtrMatrix<T> A = *this;
        return A -= B;
    }
    MutPtrMatrix<T> operator*=(const std::integral auto b) {
        MutPtrMatrix<T> A = *this;
        return A *= b;
    }
    MutPtrMatrix<T> operator/=(const std::integral auto b) {
        MutPtrMatrix<T> A = *this;
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
    Vector<T> diag() const {
        size_t N = std::min(numRow(), numCol());
        Vector<T> d;
        d.resizeForOverwrite(N);
        const P &A = self();
        for (size_t n = 0; n < N; ++n)
            d(n) = A(n, n);
        return d;
    }
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

// }
// }
// HWY_AFTER_NAMESPACE();

template <typename T>
inline auto ptrmat(T *ptr, size_t numRow, size_t numCol, size_t stride) {
    if constexpr (std::is_const_v<T>) {
        return PtrMatrix<std::remove_const_t<T>>{
            .mem = ptr, .M = numRow, .N = numCol, .X = stride};
    } else {
        return MutPtrMatrix<T>{
            .mem = ptr, .M = numRow, .N = numCol, .X = stride};
    }
}

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
    llvm::SmallVector<T, S> mem;
    size_t N, X;
    static constexpr bool fixedNumRow = M;
    static constexpr bool fixedNumCol = 0;
    static constexpr bool canResize = true;
    static constexpr bool isMutable = true;

    Matrix(size_t n) : mem(llvm::SmallVector<T, S>(M * n)), N(n), X(n){};

    // inline T &getLinearElement(size_t i) { return mem[i]; }
    // inline const T &getLinearElement(size_t i) const { return mem[i]; }
    // auto begin() { return mem.begin(); }
    // auto end() { return mem.end(); }
    // auto begin() const { return mem.begin(); }
    // auto end() const { return mem.end(); }
    inline size_t numRow() const { return M; }
    inline size_t numCol() const { return N; }
    inline size_t rowStride() const { return X; }
    // static constexpr size_t colStride() { return 1; }
    // static constexpr size_t getConstCol() { return 0; }

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
    llvm::SmallVector<T, S> mem;
    size_t M;
    static constexpr bool fixedNumRow = 0;
    static constexpr bool fixedNumCol = N;
    static constexpr bool canResize = true;
    static constexpr bool isMutable = true;

    Matrix(size_t m) : mem(llvm::SmallVector<T, S>(m * N)), M(m){};

    // inline T &getLinearElement(size_t i) { return mem[i]; }
    // inline const T &getLinearElement(size_t i) const { return mem[i]; }
    // auto begin() { return mem.begin(); }
    // auto end() { return mem.end(); }
    // auto begin() const { return mem.begin(); }
    // auto end() const { return mem.end(); }

    inline size_t numRow() const { return M; }
    static constexpr size_t numCol() { return N; }
    static constexpr size_t rowStride() { return N; }
    // static constexpr size_t colStride() { return 1; }
    static constexpr size_t getConstCol() { return N; }

    T *data() { return mem.data(); }
    const T *data() const { return mem.data(); }
};

template <typename T>
struct SquarePtrMatrix : BaseMatrix<T, SquarePtrMatrix<T>> {
    static_assert(!std::is_const_v<T>, "const T is redundant");
    const T *const mem;
    const size_t M;
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
    T *const mem;
    const size_t M;
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
    llvm::SmallVector<T, TOTALSTORAGE> mem;
    size_t M;
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
    llvm::SmallVector<T, S> mem;

    size_t M, N, X;
    static constexpr bool fixedNumRow = 0;
    static constexpr bool fixedNumCol = 0;
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
        : mem(A.mem.begin(), A.mem.end()), M(A.M), N(A.M), X(A.M){};
    Matrix(const AbstractMatrix auto &A)
        : mem(llvm::SmallVector<T>{}), M(A.numRow()), N(A.numCol()),
          X(A.numCol()) {
        mem.resize_for_overwrite(M * N);
        copyto(*this, A);
    }
    auto begin() { return mem.begin(); }
    auto end() { return mem.end(); }
    auto begin() const { return mem.begin(); }
    auto end() const { return mem.end(); }
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
        if ((XX > X) && M && N) {
            // need to copy
            for (size_t m = minMMM - 1; m > 0; --m) {
                for (size_t n = N; n > 0;) {
                    --n;
                    mem[m * XX + n] = mem[m * X + n];
                }
            }
        }
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
    MutPtrMatrix<T> view() {
        return MutPtrMatrix<T>{.mem = mem.data(), .M = M, .N = N, .X = X};
    }
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
};
template <typename T> using DynamicMatrix = Matrix<T, 0, 0, 64>;
static_assert(std::same_as<DynamicMatrix<int64_t>, Matrix<int64_t>>,
              "DynamicMatrix should be identical to Matrix");
typedef DynamicMatrix<int64_t> IntMatrix;

template <typename T>
std::ostream &printVector(std::ostream &os, PtrVector<T> a) {
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
    return printVector(os, PtrVector<T>{a.data(), a.size()});
}

template <typename T>
std::ostream &operator<<(std::ostream &os, PtrVector<T> const &A) {
    return printVector(os, A);
}
inline std::ostream &operator<<(std::ostream &os,
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
    const unsigned int M = A.numRow();
    const unsigned int N = A.numCol();
    assert((i < M) & (j < M));
    VECTORIZE
    for (size_t n = 0; n < N; ++n)
        std::swap(A(i, n), A(j, n));
}
MULTIVERSION inline void swapCols(MutPtrMatrix<int64_t> A, size_t i, size_t j) {
    if (i == j) {
        return;
    }
    const unsigned int M = A.numRow();
    const unsigned int N = A.numCol();
    assert((i < N) & (j < N));
    VECTORIZE
    for (size_t m = 0; m < M; ++m)
        std::swap(A(m, i), A(m, j));
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
constexpr bool is_uint_v =
    sizeof(T) == (Bits / 8) && std::is_integral_v<T> && !std::is_signed_v<T>;

template <class T>
inline T zeroUpper(T x)
requires is_uint_v<16, T>
{
    return x & 0x00ff;
}
template <class T>
inline T zeroLower(T x)
requires is_uint_v<16, T>
{
    return x & 0xff00;
}
template <class T>
inline T upperHalf(T x)
requires is_uint_v<16, T>
{
    return x >> 8;
}

template <class T>
inline T zeroUpper(T x)
requires is_uint_v<32, T>
{
    return x & 0x0000ffff;
}
template <class T>
inline T zeroLower(T x)
requires is_uint_v<32, T>
{
    return x & 0xffff0000;
}
template <class T>
inline T upperHalf(T x)
requires is_uint_v<32, T>
{
    return x >> 16;
}
template <class T>
inline T zeroUpper(T x)
requires is_uint_v<64, T>
{
    return x & 0x00000000ffffffff;
}
template <class T>
inline T zeroLower(T x)
requires is_uint_v<64, T>
{
    return x & 0xffffffff00000000;
}
template <class T>
inline T upperHalf(T x)
requires is_uint_v<64, T>
{
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

template <class T, int Bits>
concept is_int_v = std::signed_integral<T> && sizeof(T) == (Bits / 8);

template <is_int_v<64> T> inline __int128_t widen(T x) { return x; }
template <is_int_v<32> T> inline int64_t splitInt(T x) { return x; }

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <typename T>
concept TriviallyCopyableVectorOrScalar =
    std::is_trivially_copyable_v<T> && VectorOrScalar<T>;
template <typename T>
concept TriviallyCopyableMatrixOrScalar =
    std::is_trivially_copyable_v<T> && MatrixOrScalar<T>;

static_assert(std::copy_constructible<PtrMatrix<int64_t>>);
static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>);
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
inline auto _binaryOp(OP op, A a, B b) {
    return ElementwiseVectorBinaryOp<OP, A, B>{.op = op, .a = a, .b = b};
}
template <TriviallyCopyable OP, TriviallyCopyableMatrixOrScalar A,
          TriviallyCopyableMatrixOrScalar B>
inline auto _binaryOp(OP op, A a, B b) {
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
inline auto binaryOp(const OP op, const A &a, const B &b) {
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

inline auto bin2(std::integral auto x) { return (x * (x - 1)) >> 1; }

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

    template <AbstractMatrix B> inline auto operator+(B &&b) {
        return binaryOp(Add{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> inline auto operator+(B &&b) {
        return binaryOp(Add{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> inline auto operator-(B &&b) {
        return binaryOp(Sub{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> inline auto operator-(B &&b) {
        return binaryOp(Sub{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> inline auto operator/(B &&b) {
        return binaryOp(Div{}, *this, std::forward<B>(b));
    }
    template <AbstractVector B> inline auto operator/(B &&b) {
        return binaryOp(Div{}, *this, std::forward<B>(b));
    }

    template <AbstractVector B> inline auto operator*(B &&b) {
        return binaryOp(Mul{}, *this, std::forward<B>(b));
    }
    template <AbstractMatrix B> inline auto operator*(B &&b) {
        return binaryOp(Mul{}, *this, std::forward<B>(b));
    }
};
llvm::Optional<Rational> gcd(Rational x, Rational y) {
    return Rational{gcd(x.numerator, y.numerator),
                    lcm(x.denominator, y.denominator)};
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
std::ostream &printMatrix(std::ostream &os, PtrMatrix<T> A) {
    // std::ostream &printMatrix(std::ostream &os, T const &A) {
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
            os << std::endl;
        }
    }
    os << " ]";
    return os;
}

template <typename T> struct SmallSparseMatrix {
    // non-zeros
    llvm::SmallVector<T> nonZeros;
    // masks, the upper 8 bits give the number of elements in previous rows
    // the remaining 24 bits are a mask indicating non-zeros within this row
    static constexpr size_t maxElemPerRow = 24;
    llvm::SmallVector<uint32_t> rows;
    size_t col;
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
    inline T operator()(size_t i, size_t j) const { return get(i, j); }
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
        SmallSparseMatrix<T> *A;
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
std::ostream &operator<<(std::ostream &os, SmallSparseMatrix<T> const &A) {
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

std::ostream &operator<<(std::ostream &os, PtrMatrix<int64_t> A) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A)
    // {
    return printMatrix(os, A);
}
template <typename T, typename A>
std::ostream &operator<<(std::ostream &os, const BaseMatrix<T, A> &B) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A)
    // {
    return printMatrix(os, PtrMatrix<T>(B));
}
template <AbstractMatrix T>
std::ostream &operator<<(std::ostream &os, const T &A) {
    // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A)
    // {
    Matrix<std::remove_const_t<typename T::eltype>> B{A};
    return printMatrix(os, PtrMatrix<typename T::eltype>(B));
}
// template <typename T>
// std::ostream &operator<<(std::ostream &os, PtrMatrix<const T> &A) {
//     // std::ostream &operator<<(std::ostream &os, Matrix<T, M, N> const &A)
//     // {
//     return printMatrix(os, A);
// }

inline auto operator-(const AbstractVector auto &a) {
    auto AA{a.view()};
    return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
inline auto operator-(const AbstractMatrix auto &a) {
    auto AA{a.view()};
    return ElementwiseUnaryOp<Sub, decltype(AA)>{.op = Sub{}, .a = AA};
}
static_assert(AbstractMatrix<ElementwiseUnaryOp<Sub, PtrMatrix<int64_t>>>);

template <AbstractMatrix A, typename B> inline auto operator+(A &&a, B &&b) {
    return binaryOp(Add{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> inline auto operator+(A &&a, B &&b) {
    return binaryOp(Add{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B> inline auto operator+(std::integral auto a, B &&b) {
    return binaryOp(Add{}, a, std::forward<B>(b));
}
template <AbstractVector B> inline auto operator+(std::integral auto a, B &&b) {
    return binaryOp(Add{}, a, std::forward<B>(b));
}

template <AbstractMatrix A, typename B> inline auto operator-(A &&a, B &&b) {
    return binaryOp(Sub{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> inline auto operator-(A &&a, B &&b) {
    return binaryOp(Sub{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B> inline auto operator-(std::integral auto a, B &&b) {
    return binaryOp(Sub{}, a, std::forward<B>(b));
}
template <AbstractVector B> inline auto operator-(std::integral auto a, B &&b) {
    return binaryOp(Sub{}, a, std::forward<B>(b));
}

template <AbstractMatrix A, typename B> inline auto operator/(A &&a, B &&b) {
    return binaryOp(Div{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A, typename B> inline auto operator/(A &&a, B &&b) {
    return binaryOp(Div{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractMatrix B> inline auto operator/(std::integral auto a, B &&b) {
    return binaryOp(Div{}, a, std::forward<B>(b));
}
template <AbstractVector B> inline auto operator/(std::integral auto a, B &&b) {
    return binaryOp(Div{}, a, std::forward<B>(b));
}
inline auto operator*(const AbstractMatrix auto &a,
                      const AbstractMatrix auto &b) {
    auto AA{a.view()};
    auto BB{b.view()};
    // std::cout << "a.numRow() = " << a.numRow()
    //           << "; AA.numRow() = " << AA.numRow() << std::endl;
    // std::cout << "b.numRow() = " << b.numRow()
    //           << "; BB.numRow() = " << BB.numRow() << std::endl;
    // std::cout << "a.numCol() = " << a.numCol()
    //           << "; AA.numCol() = " << AA.numCol() << std::endl;
    // std::cout << "b.numCol() = " << b.numCol()
    //           << "; BB.numCol() = " << BB.numCol() << std::endl;
    // std::cout << "a ="
    //           << a << "\nAA ="
    //           << AA << "\nb ="
    //           << b << "\nBB ="
    //           << BB << std::endl;
    assert(AA.numCol() == BB.numRow());
    return MatMatMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
inline auto operator*(const AbstractMatrix auto &a,
                      const AbstractVector auto &b) {
    auto AA{a.view()};
    auto BB{b.view()};
    assert(AA.numCol() == BB.size());
    return MatVecMul<decltype(AA), decltype(BB)>{.a = AA, .b = BB};
}
template <AbstractMatrix A> inline auto operator*(A &&a, std::integral auto b) {
    return binaryOp(Mul{}, std::forward<A>(a), b);
}
// template <AbstractMatrix A> inline auto operator*(A &&a, Rational b) {
//     return binaryOp(Mul{}, std::forward<A>(a), b);
// }
template <AbstractVector A, AbstractVector B>
inline auto operator*(A &&a, B &&b) {
    return binaryOp(Mul{}, std::forward<A>(a), std::forward<B>(b));
}
template <AbstractVector A> inline auto operator*(A &&a, std::integral auto b) {
    return binaryOp(Mul{}, std::forward<A>(a), b);
}
// template <AbstractVector A> inline auto operator*(A &&a, Rational b) {
//     return binaryOp(Mul{}, std::forward<A>(a), b);
// }
template <AbstractMatrix B> inline auto operator*(std::integral auto a, B &&b) {
    return binaryOp(Mul{}, a, std::forward<B>(b));
}
template <AbstractVector B> inline auto operator*(std::integral auto a, B &&b) {
    return binaryOp(Mul{}, a, std::forward<B>(b));
}

// inline auto operator*(AbstractMatrix auto &A, AbstractVector auto &x) {
//     auto AA{A.view()};
//     auto xx{x.view()};
//     return MatMul<decltype(AA), decltype(xx)>{.a = AA, .b = xx};
// }

template <AbstractVector V>
inline auto operator*(const Transpose<V> &a, const AbstractVector auto &b) {
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

static_assert(AbstractVector<
              ElementwiseVectorBinaryOp<Mul, PtrVector<int64_t>, int64_t>>);

} // namespace HWY_NAMESPACE
// }  // namespace project - optional
HWY_AFTER_NAMESPACE();
