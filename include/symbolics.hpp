#pragma once
#include "math.hpp"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <math.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

template <typename T> T &negate(T &x) {
    x.negate();
    return x;
}
// template <typename T> T& negate(T &x) { return x.negate(); }
intptr_t negate(intptr_t &x) { return x *= -1; }
// pass by value to copy
// template <typename T> T cnegate(T x) { return negate(x); }
template <typename TRC> auto cnegate(TRC &&x) {
    typedef typename std::remove_reference<TRC>::type TR;
    typedef typename std::remove_const<TR>::type T;
    T y(std::forward<TRC>(x));
    negate(y);
    return y;
}
// template <typename T> T cnegate(T &x){ return negate(x); }
// template <typename T> T cnegate(T const &x){ return negate(x); }
// template <typename T> T cnegate(T &&x){ return negate(x); }

enum DivRemainder { Indeterminate, NoRemainder, HasRemainder };

struct Rational {
    intptr_t numerator;
    intptr_t denominator;

    Rational() = default;
    Rational(intptr_t coef) : numerator(coef), denominator(1){};
    Rational(intptr_t n, intptr_t d) : numerator(n), denominator(d){};

    Rational operator+(Rational y) const {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        return Rational{numerator * yd + y.numerator * xd, denominator * yd};
    }
    Rational &operator+=(Rational y) {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        numerator = numerator * yd + y.numerator * xd;
        denominator = denominator * yd;
        return *this;
    }
    Rational operator-(Rational y) const {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        return Rational{numerator * yd - y.numerator * xd, denominator * yd};
    }
    Rational &operator-=(Rational y) {
        auto [xd, yd] = divgcd(denominator, y.denominator);
        numerator = numerator * yd - y.numerator * xd;
        denominator = denominator * yd;
        return *this;
    }
    Rational operator*(Rational y) const {
        auto [xn, yd] = divgcd(numerator, y.denominator);
        auto [xd, yn] = divgcd(denominator, y.numerator);
        return Rational{xn * yn, xd * yd};
    }
    Rational &operator*=(Rational y) {
        auto [xn, yd] = divgcd(numerator, y.denominator);
        auto [xd, yn] = divgcd(denominator, y.numerator);
        numerator = xn * yn;
        denominator = xd * yd;
        return *this;
    }
    Rational inv() const {
        bool positive = numerator > 0;
        return Rational{positive ? denominator : -denominator,
                        positive ? numerator : -numerator};
    }
    Rational operator/(Rational y) const { return (*this) * y.inv(); }
    Rational operator/=(Rational y) { return (*this) *= y.inv(); }
    bool operator==(Rational y) const {
        return (numerator == y.numerator) & (denominator == y.denominator);
    }
    bool operator!=(Rational y) const {
        return (numerator != y.numerator) | (denominator != y.denominator);
    }
    bool operator<(Rational y) const {
        return (numerator * y.denominator) < (y.numerator * denominator);
    }
    bool operator<=(Rational y) const {
        return (numerator * y.denominator) <= (y.numerator * denominator);
    }
    bool operator>(Rational y) const {
        return (numerator * y.denominator) > (y.numerator * denominator);
    }
    bool operator>=(Rational y) const {
        return (numerator * y.denominator) >= (y.numerator * denominator);
    }

    operator double() { return numerator / denominator; }
    friend bool isZero(Rational x) { return x.numerator == 0; }
    friend bool isOne(Rational x) { return (x.numerator == x.denominator); }
    bool isInteger() const { return denominator == 1; }
    void negate() { ::negate(numerator); }

    friend std::ostream &operator<<(std::ostream &os, const Rational &x) {
        os << x.numerator;
        if (x.denominator != 1) {
            os << " // " << x.denominator;
        }
        return os;
    }
};

// template <typename T> bool isZero(T x) { return x.isZero(); }
bool isZero(intptr_t x) { return x == 0; }
bool isZero(size_t x) { return x == 0; }

Rational gcd(Rational x, Rational y) {
    intptr_t a = x.numerator * y.denominator;
    intptr_t b = x.denominator * y.numerator;
    intptr_t n = std::gcd(a, b);
    intptr_t d = x.denominator * y.denominator;
    if ((d != 1) & (n != 0)) {
        intptr_t g = std::gcd(n, d);
        n /= g;
        d /= g;
    }
    return n ? Rational{n, d} : Rational{1, 0};
}

template <typename T, typename S> void add_term(T &a, S &&x) {
    if (!isZero(x)) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            if ((it->termsMatch(x))) {
                if (it->addCoef(x)) {
                    a.erase(it);
                }
                return;
            } else if (x.lexGreater(*it)) {
                a.insert(it, std::forward<S>(x));
                return;
            }
        }
        a.push_back(std::forward<S>(x));
    }
    return;
}
template <typename T, typename S> void sub_term(T &a, S &&x) {
    if (!isZero(x)) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            if ((it->termsMatch(x))) {
                if (it->subCoef(x)) {
                    a.erase(it);
                }
                return;
            } else if (x.lexGreater(*it)) {
                a.insert(it, cnegate(std::forward<S>(x)));
                return;
            }
        }
        a.push_back(cnegate(std::forward<S>(x)));
    }
    return;
}
template <typename T, typename S> void sub_term(T &a, S &&x, size_t offset) {
    if (!isZero(x)) {
        for (auto it = a.begin() + offset; it != a.end(); ++it) {
            if ((it->termsMatch(x))) {
                if (it->subCoef(x)) {
                    a.erase(it);
                }
                return;
            } else if (x.lexGreater(*it)) {
                a.insert(it, cnegate(std::forward<S>(x)));
                return;
            }
        }
        a.push_back(cnegate(std::forward<S>(x)));
    }
    return;
}
static std::string programVarName(size_t i) {
    std::string s(1, 'L' + i);
    return s;
}
std::string monomialTermStr(size_t id, size_t exponent) {
    if (exponent) {
        if (exponent > 1) {
            return programVarName(id) + "^" + std::to_string(exponent);
        } else {
            return programVarName(id);
        }
    } else {
        return "";
    }
}

namespace Polynomial {

template <Integral I> bool tryDiv(I &z, I x, I y) {
    I a(x);
    z = x / y;
    return (z * y) != a;
}

struct Uninomial {
    size_t exponent;
    Uninomial(One) : exponent(0){};
    Uninomial() = default;
    explicit Uninomial(size_t e) : exponent(e){};
    size_t degree() const { return exponent; }
    bool termsMatch(Uninomial const &y) const { return exponent == y.exponent; }
    bool lexGreater(Uninomial const &y) const { return exponent > y.exponent; }
    template <typename T> bool lexGreater(T const &x) const {
        return lexGreater(x.monomial());
    }

    Uninomial &operator^=(size_t i) {
        exponent *= i;
        return *this;
    }
    Uninomial operator^(size_t i) { return Uninomial{exponent * i}; }

    friend bool isOne(Uninomial x) { return x.exponent == 0; }
    friend bool isZero(Uninomial) { return false; }

    bool operator==(Uninomial x) const { return x.exponent == exponent; }
    /*
    // TODO: add me when mac clang supports it
    std::strong_ordering operator<=>(Uninomial x) const {
        if (exponent < x.exponent) {
            return std::strong_ordering::less;
        } else if (exponent == x.exponent) {
            return std::strong_ordering::equal;
        } else {
            return std::strong_ordering::greater;
        }
    }
    */
    Uninomial operator*(Uninomial x) const {
        return Uninomial{exponent + x.exponent};
    }
    Uninomial &operator*=(Uninomial x) {
        exponent += x.exponent;
        return *this;
    }
    void mul(Uninomial x, Uninomial y) { exponent = x.exponent + y.exponent; }
    friend std::ostream &operator<<(std::ostream &os, const Uninomial &x) {
        switch (x.exponent) {
        case 0:
            os << '1';
            break;
        case 1:
            os << 'x';
            break;
        default:
            os << "x^" << x.exponent;
            break;
        }
        return os;
    }
    // Uninomial& operator=(Uninomial x) {
    //     exponent = x.exponent;
    //     return *this;
    // }
}; // Uninomial

bool tryDiv(Uninomial &z, Uninomial x, Uninomial y) {
    z.exponent = x.exponent - y.exponent;
    return x.exponent < y.exponent;
}
// // 24 bytes
// static constexpr unsigned MonomialSmallVectorSize = 2;
// typedef uint32_t MonomialIDType;
// 32 bytes
static constexpr unsigned MonomialSmallVectorSize = 4;
typedef uint32_t MonomialIDType;
// // 32 bytes
// static constexpr unsigned MonomialSmallVectorSize = 8;
// typedef uint8_t MonomialIDType;
struct ID {
    MonomialIDType id;
};

struct Monomial {
    // sorted symbolic terms being multiplied
    // std::vector<size_t> prodIDs;
    llvm::SmallVector<MonomialIDType, MonomialSmallVectorSize> prodIDs;
    // Monomial& operator=(Monomial const &x){
    //     prodIDs = x.prodIDs;
    //     return *this;
    // }
    // constructors
    Monomial() = default;
    // Monomial() : prodIDs(std::vector<size_t>()){};
    Monomial(llvm::SmallVector<MonomialIDType, MonomialSmallVectorSize> &x)
        : prodIDs(x){};
    Monomial(llvm::SmallVector<MonomialIDType, MonomialSmallVectorSize> &&x)
        : prodIDs(std::move(x)){};
    // Monomial(std::vector<size_t> &x) : prodIDs(x){};
    // Monomial(std::vector<size_t> &&x) : prodIDs(std::move(x)){};
    // Monomial(Monomial const &x) : prodIDs(x.prodIDs) {};
    Monomial(One)
        : prodIDs(
              llvm::SmallVector<MonomialIDType, MonomialSmallVectorSize>()){};

    explicit Monomial(ID id) : prodIDs({id.id}){};
    explicit Monomial(ID idx, ID idy) : prodIDs({idx.id, idy.id}){};
    explicit Monomial(ID idx, ID idy, ID idz)
        : prodIDs({idx.id, idy.id, idz.id}){};

    inline auto begin() { return prodIDs.begin(); }
    inline auto end() { return prodIDs.end(); }
    inline auto begin() const { return prodIDs.begin(); }
    inline auto end() const { return prodIDs.end(); }
    inline auto cbegin() const { return prodIDs.begin(); }
    inline auto cend() const { return prodIDs.end(); }

    void add_term(size_t x) {
        for (auto it = begin(); it != end(); ++it) {
            if (x <= *it) {
                prodIDs.insert(it, x);
                return;
            }
        }
        prodIDs.push_back(x);
    }
    void add_term(size_t x, size_t count) {
        // prodIDs.reserve(prodIDs.size() + rep);
        auto it = begin();
        // prodIDs [0, 1, 3]
        // x = 2
        for (; it != end(); ++it) {
            if (x <= *it) {
                break;
            }
        }
        prodIDs.insert(it, count, x);
    }
    void mul(Monomial const &x, Monomial const &y) {
        prodIDs.clear();
        size_t n0 = x.prodIDs.size();
        size_t n1 = y.prodIDs.size();
        prodIDs.reserve(n0 * n1);
        size_t i = 0;
        size_t j = 0;
        // prodIDs are sorted, so we can create sorted product in O(N)
        for (size_t k = 0; k < (n0 + n1); ++k) {
            size_t a =
                (i < n0) ? x.prodIDs[i] : std::numeric_limits<size_t>::max();
            size_t b =
                (j < n1) ? y.prodIDs[j] : std::numeric_limits<size_t>::max();
            bool aSmaller = a < b;
            aSmaller ? ++i : ++j;
            prodIDs.push_back(aSmaller ? a : b);
        }
    }
    Monomial operator*(Monomial const &x) const {
        Monomial r;
        r.mul(*this, x);
        return r;
    }
    Monomial &operator*=(Monomial const &x) {
        // optimize the length 0 and 1 cases.
        if (x.prodIDs.size() == 0) {
            return *this;
        } else if (x.prodIDs.size() == 1) {
            size_t y = x.prodIDs[0];
            for (auto it = begin(); it != end(); ++it) {
                if (y < *it) {
                    prodIDs.insert(it, y);
                    return *this;
                }
            }
            prodIDs.push_back(y);
            return *this;
        }
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        prodIDs.reserve(n0 + n1); // reserve capacity, to prevent invalidation
        auto ix = x.cbegin();
        auto ixe = x.cend();
        if (n0) {
            auto it = begin();
            while (ix != ixe) {
                if (*ix < *it) {
                    prodIDs.insert(it, *ix); // increments `end()`
                    ++it;
                    ++ix;
                } else {
                    ++it;
                    if (it == end()) {
                        break;
                    }
                }
            }
        }
        for (; ix != ixe; ++ix) {
            prodIDs.push_back(*ix);
        }
        return *this;
    }
    Monomial operator*(Monomial &&x) const {
        x *= (*this);
        return std::move(x);
    }
    bool operator==(Monomial const &x) const { return prodIDs == x.prodIDs; }
    bool operator!=(Monomial const &x) const { return prodIDs != x.prodIDs; }
    bool termsMatch(Monomial const &x) const { return *this == x; }

    // numerator, denominator rational
    std::pair<Monomial, Monomial> rational(Monomial const &x) const {
        Monomial n;
        Monomial d;
        size_t i = 0;
        size_t j = 0;
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        while ((i + j) < (n0 + n1)) {
            size_t a =
                (i < n0) ? prodIDs[i] : std::numeric_limits<size_t>::max();
            size_t b =
                (j < n1) ? x.prodIDs[j] : std::numeric_limits<size_t>::max();
            if (a < b) {
                n.prodIDs.push_back(a);
                ++i;
            } else if (a == b) {
                ++i;
                ++j;
            } else {
                d.prodIDs.push_back(b);
                ++j;
            }
        }
        return std::make_pair(n, d);
    }
    // returns a 3-tuple, containing:
    // 0: Monomial(coef / gcd(coef, x.coefficient), setDiff(prodIDs,
    // x.prodIDs)) 1: x.coef / gcd(coef, x.coefficient) 2: whether the
    // division failed (i.e., true if prodIDs was not a superset of
    // x.prodIDs)

    friend bool isOne(Monomial const &x) { return (x.prodIDs.size() == 0); }
    friend bool isZero(Monomial const &) { return false; }
    bool isCompileTimeConstant() const { return prodIDs.size() == 0; }
    size_t degree() const { return prodIDs.size(); }
    size_t degree(size_t i) const {
        size_t d = 0;
        for (auto it : prodIDs) {
            d += (it == i);
        }
        return d;
    }
    bool lexGreater(Monomial const &x) const {
        // return `true` if `*this` should be sorted before `x`
        size_t d = degree();
        if (d != x.degree()) {
            return d > x.degree();
        }
        for (size_t i = 0; i < d; ++i) {
            size_t a = prodIDs[i];
            size_t b = x.prodIDs[i];
            if (a != b) {
                return a < b;
            }
        }
        return false;
    }
    template <typename T> bool lexGreater(T const &x) const {
        return lexGreater(x.monomial());
    }
    /*
    // TODO: add me when mac clang supports it
    std::strong_ordering operator<=>(Monomial &x) const {
        if (*this == x) {
            return std::strong_ordering::equal;
        } else if (lexGreater(x)) {
            return std::strong_ordering::greater;
        } else {
            return std::strong_ordering::less;
        }
    }
    */
    Monomial operator^(size_t i) { return powBySquare(*this, i); }
    Monomial operator^(size_t i) const { return powBySquare(*this, i); }
    MonomialIDType firstTermID() const { return prodIDs[0]; }

    friend std::ostream &operator<<(std::ostream &os, const Monomial &x) {
        size_t numIndex = x.prodIDs.size();
        if (numIndex) {
            if (numIndex != 1) { // not 0 by prev `if`
                size_t count = 0;
                size_t v = x.prodIDs[0];
                for (auto it : x) {
                    if (it == v) {
                        ++count;
                    } else {
                        os << monomialTermStr(v, count);
                        v = it;
                        count = 1;
                    }
                }
                os << monomialTermStr(v, count);
            } else { // numIndex == 1
                os << programVarName(x.prodIDs[0]);
            }
        } else {
            os << '1';
        }
        return os;
    }
}; // Monomial

bool tryDiv(Monomial &z, Monomial const &x, Monomial const &y) {
    z.prodIDs.clear();
    auto i = x.cbegin();
    auto j = y.cbegin();
    auto n0 = x.cend();
    auto n1 = y.cend();
    while ((i != n0) | (j != n1)) {
        size_t a = (i != n0) ? *i : std::numeric_limits<size_t>::max();
        size_t b = (j != n1) ? *j : std::numeric_limits<size_t>::max();
        if (a < b) {
            z.prodIDs.push_back(a);
            ++i;
        } else if (a == b) {
            ++i;
            ++j;
        } else {
            return true;
        }
    }
    return false;
}

template <size_t N> struct Val {};
constexpr uint64_t checkZeroMask(Val<7>) { return 0x8080808080808080; }
constexpr uint64_t checkZeroMask(Val<15>) { return 0x8000800080008000; }
constexpr uint64_t checkZeroMask(Val<31>) { return 0x8000000080000000; }
constexpr uint64_t checkZeroMask(Val<63>) { return 0x8000000000000000; }
constexpr uint64_t zeroNonDegreeMask(Val<7>) { return 0xff00000000000000; }
constexpr uint64_t zeroNonDegreeMask(Val<15>) { return 0xffff000000000000; }
constexpr uint64_t zeroNonDegreeMask(Val<31>) { return 0xffffffff00000000; }
constexpr uint64_t zeroNonDegreeMask(Val<63>) { return 0xffffffffffffffff; }
constexpr uint64_t zeroUpperMask(Val<7>) { return 0x00000000000000ff; }
constexpr uint64_t zeroUpperMask(Val<15>) { return 0x000000000000ffff; }
constexpr uint64_t zeroUpperMask(Val<31>) { return 0x00000000ffffffff; }
constexpr uint64_t zeroUpperMask(Val<63>) { return 0xffffffffffffffff; }

template <size_t L, size_t E> struct CalculateStorage {
    constexpr static size_t varPerUInt = 64 / (E + 1);
    constexpr static size_t needed = (L + varPerUInt) / varPerUInt;
    constexpr static size_t K =
        needed < 3 ? needed : (needed < 5 ? 4 : ((needed + 7) & -8));
    static_assert(K == needed,
                  "Try increasing `L` to avoid wasting space, e.g. try the "
                  "next power of 2 or set of 8 integers.");
};
uint64_t sumChunksUpper(Val<7>, uint64_t x) {
    uint64_t s32 = x + (x << 32);
    uint64_t s16 = s32 + (s32 << 16);
    return (s16 + (s16 << 8)) & zeroNonDegreeMask(Val<7>());
}
uint64_t sumChunksUpper(Val<15>, uint64_t x) {
    uint64_t s32 = x + (x << 32);
    return (s32 + (s32 << 16)) & zeroNonDegreeMask(Val<15>());
}
uint64_t sumChunksUpper(Val<31>, uint64_t x) {
    return (x + (x << 32)) & zeroNonDegreeMask(Val<31>());
}
uint64_t sumChunksUpper(Val<63>, uint64_t x) { return x; }
uint64_t sumChunksLower(Val<7>, uint64_t x) {
    uint64_t s32 = x + (x >> 32);
    uint64_t s16 = s32 + (s32 >> 16);
    return (s16 + (s16 >> 8)) & 0x00000000000000ff;
}
uint64_t sumChunksLower(Val<15>, uint64_t x) {
    uint64_t s32 = x + (x >> 32);
    return (s32 + (s32 >> 16)) & 0x000000000000ffff;
}
uint64_t sumChunksLower(Val<31>, uint64_t x) {
    return (x + (x >> 32)) & 0x00000000ffffffff;
}
uint64_t sumChunksLower(Val<63>, uint64_t x) { return x; }
template <size_t L = 15, size_t E = 7> struct PackedMonomial {
    static constexpr size_t K = CalculateStorage<L, E>::K;
    static_assert((E < 64) & (std::popcount(E + 1) == 1),
                  "E must be one less than a power of 2 and < 64.");
    static_assert((std::popcount(L + 1) == 1) | (((L + 1) & 7) == 0),
                  "L should be 1 less than a power of 2.");
    uint64_t bits[K];

    PackedMonomial(One) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] = 0;
        }
    }
    PackedMonomial() {
	for (size_t k = 0; k < K; ++k) {
            bits[k] = 0;
        }
    }
    PackedMonomial(ID id) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] = 0;
        }
        add_term(id.id);
    }
    PackedMonomial(ID idx, ID idy) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] = 0;
        }
        add_term(idx.id);
        add_term(idy.id);
    }
    PackedMonomial(ID idx, ID idy, ID idz) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] = 0;
        }
        add_term(idx.id);
        add_term(idy.id);
        add_term(idz.id);
    }
    void add_term(uint64_t id, uint64_t count = 1) {
        constexpr uint64_t varPerUInt = CalculateStorage<L, E>::varPerUInt;
        uint64_t d = K == 1 ? 0 : (id + 1) / varPerUInt;
        uint64_t r = (id + 1) - (d * varPerUInt);
	uint64_t o = count << ((E + 1) * (varPerUInt - 1));
        uint64_t b = o >> (r * (E + 1));
        if (d) {
            bits[0] += o;
        } else {
            b |= o;
        }
        bits[d] += b;
    }
    void mul(PackedMonomial<L, E> const &x, PackedMonomial<L, E> const &y) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] = x.bits[k] + y.bits[k];
        }
    }
    PackedMonomial<L, E> &operator*=(PackedMonomial<L, E> const &x) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] += x.bits[k];
        }
        return *this;
    }
    PackedMonomial<L, E> operator*(PackedMonomial<L, E> &&x) const {
        x *= *this;
        return std::move(x);
    }
    PackedMonomial<L, E> &operator^=(uint64_t y) {
        for (size_t k = 0; k < K; ++k) {
            bits[k] *= y;
        }
        return this;
    }
    bool operator==(PackedMonomial const &x) const {
        for (size_t k = 0; k < K; ++k) {
            if (x.bits[k] != bits[k])
                return false;
        }
        return true;
    }
    bool operator!=(PackedMonomial const &x) const { return !(*this == x); }
    bool termsMatch(PackedMonomial const &x) const { return *this == x; }
    size_t degree() const {
        return bits[0] >> ((E + 1) * (CalculateStorage<L, E>::varPerUInt - 1));
    }
    size_t degree(size_t id) const {
        constexpr uint64_t varPerUInt = CalculateStorage<L, E>::varPerUInt;
        uint64_t d = K == 1 ? 0 : (id + 1) / varPerUInt;
        uint64_t r = (id + 1) - (d * varPerUInt);
        uint64_t b = bits[d] << (r * (E + 1));
        return b >> ((E + 1) * (varPerUInt - 1));
    }
    void removeTerm(size_t id) {
        constexpr uint64_t varPerUInt = CalculateStorage<L, E>::varPerUInt;
        uint64_t d = K == 1 ? 0 : (id + 1) / varPerUInt;
        uint64_t r = (id + 1) - (d * varPerUInt);
	uint64_t m = zeroNonDegreeMask(Val<E>()) >> (r*(E+1));
	uint64_t oldBits = bits[d];
	uint64_t b = oldBits & (~m);
	uint64_t remDegree = (oldBits & m) >> ((E+1)*(varPerUInt-1-r));
	
        uint64_t o = remDegree << ((E + 1) * (varPerUInt - 1));
	if (d){
	    bits[d] = b;
	    bits[0] -= o;
	} else {
	    bits[0] = b - o;
	}
    }
    void calcDegree() {
        uint64_t oldBit = bits[0];
        uint64_t oldChunks = oldBit & (~zeroNonDegreeMask(Val<E>()));
        // uint64_t oldDegree = oldBit & zeroNonDegreeMask(Val<E>());
        if (K == 1) {
            bits[0] = sumChunksUpper(Val<E>(), oldChunks) | oldChunks;
        } else if (K == 2) {
            bits[0] = sumChunksUpper(Val<E>(), oldChunks + bits[1]) | oldChunks;
        } else {
            uint64_t d = -(oldBit & zeroNonDegreeMask(Val<E>()));
            for (size_t k = 0; k < K; ++k) {
                d += bits[k];
            }
            bits[0] = sumChunksUpper(Val<E>(), d) | oldChunks;
        }
    }
    bool lexGreater(PackedMonomial const &y) const {
        for (size_t k = 0; k < K; ++k) {
            if (bits[k] != y.bits[k]) {
                return bits[k] > y.bits[k];
            }
        }
        return false;
    }
    /*
    // TODO: add me when mac clang supports it
    std::strong_ordering operator<=>(PackedMonomial const &y) const {
        uint64_t *xp = this->bits;
        uint64_t *yp = y.bits;
        return std::lexicographical_compare_three_way(xp, xp + K, yp, yp + K);
    }
    */
    friend bool isOne(PackedMonomial const &x) { return (x.degree() == 0); }
    friend bool isZero(PackedMonomial const &) { return false; }

    uint64_t firstTermID() const {
	uint64_t b = bits[0] & (~zeroNonDegreeMask(Val<E>()));
	if (b){
	    return (std::countl_zero(b) / (E+1)) - 1;
	}
	b = CalculateStorage<L, E>::varPerUInt - 1;
	for (size_t k = 1; k < K; ++k){
	    uint64_t bk = bits[k];
	    if (bk){
		return (std::countl_zero(bk) / (E+1)) + b;
	    }
	    b += CalculateStorage<L, E>::varPerUInt;
	}
	assert("firstTermID should only be called if degree > 0.");
	return 0;
    }
    
    friend std::ostream &operator<<(std::ostream &os, const PackedMonomial &m) {
        size_t d = m.degree();
        size_t i = 0;
        size_t varPerUInt = CalculateStorage<L, E>::varPerUInt;
        if (d) {
            for (size_t k = 0; k < m.K; ++k) {
                uint64_t b = m.bits[k] << ((k) ? 0 : (E + 1));
		// os << "b bits: " << b << std::endl;
                for (size_t j = 0; j < varPerUInt - (k == 0); ++j) {
                    uint64_t exponent = b >> ((E + 1) * (varPerUInt - 1));
                    if (exponent) {
                        os << "x_{" << i << "}";
                        if (exponent > 1) {
                            os << "^{" << exponent << "}";
                        }
                        // os << std::endl;
                    }
		    ++i;
		    b <<= (E + 1);
		    if (L == i) {
			return os;
		    }
                }
            }
        } else {
            os << '1';
        }
        return os;
    }
}; // PackedMonomial

template <size_t L, size_t E>
PackedMonomial<L, E> operator*(PackedMonomial<L, E> &&x,
                               PackedMonomial<L, E> const &y) {
    x *= y;
    return std::move(x);
}
template <size_t L, size_t E>
PackedMonomial<L, E> operator*(PackedMonomial<L, E> &&x,
                               PackedMonomial<L, E> &&y) {
    x *= std::move(y);
    return std::move(x);
}
template <size_t L, size_t E>
PackedMonomial<L, E> operator*(PackedMonomial<L, E> const &x,
                               PackedMonomial<L, E> const &y) {
    PackedMonomial<L, E> z;
    z.mul(x, y);
    return z;
}
template <size_t L, size_t E>
void gcd(PackedMonomial<L, E> &g, PackedMonomial<L, E> const &x,
         PackedMonomial<L, E> const &y) {
    uint64_t m = checkZeroMask(Val<E>());
    for (size_t i = 0; i < g.K; ++i) {
        uint64_t xi = x.bits[i];
        uint64_t yi = y.bits[i];
        uint64_t ySelector = m - (((yi - xi) & m) >> E);
        g.bits[i] = (ySelector & yi) | ((~ySelector) & xi);
    }
    g.calcDegree(); // degree was invalidated.
}
template <size_t L, size_t E>
PackedMonomial<L, E> gcd(PackedMonomial<L, E> const &x,
                         PackedMonomial<L, E> const &y) {
    PackedMonomial<L, E> g;
    gcd(g, x, y);
    return g;
}
template <size_t L, size_t E>
PackedMonomial<L, E> gcd(PackedMonomial<L, E> &&x,
                         PackedMonomial<L, E> const &y) {
    gcd(x, x, y);
    return std::move(x);
}
template <size_t L, size_t E>
PackedMonomial<L, E> gcd(PackedMonomial<L, E> const &x,
                         PackedMonomial<L, E> &&y) {
    gcd(y, x, y);
    return std::move(y);
}
template <size_t L, size_t E>
PackedMonomial<L, E> gcd(PackedMonomial<L, E> &&x, PackedMonomial<L, E> &&y) {
    gcd(x, x, y);
    return std::move(x);
}
template <size_t L, size_t E>
uint64_t tryDiv(PackedMonomial<L, E> &z, PackedMonomial<L, E> const &x,
                PackedMonomial<L, E> const &y) {
    uint64_t fail = 0;
    uint64_t mask = checkZeroMask(Val<E>());
    for (size_t i = 0; i < z.K; ++i) {
        uint64_t u = x.bits[i] - y.bits[i];
        z.bits[i] = u;
        fail |= (u & mask);
    }
    return fail;
}
template <size_t L, size_t E>
std::pair<PackedMonomial<L, E>, uint64_t>
tryDiv(PackedMonomial<L, E> const &x, PackedMonomial<L, E> const &y) {
    PackedMonomial<L, E> z;
    uint64_t fail = tryDiv(z, x, y);
    return std::make_pair(z, fail);
}
template <size_t L, size_t E>
std::pair<PackedMonomial<L, E>, uint64_t>
tryDiv(PackedMonomial<L, E> &&x, PackedMonomial<L, E> const &y) {
    uint64_t fail = tryDiv(x, x, y);
    return std::make_pair(std::move(x), fail);
}
template <size_t L, size_t E>
std::pair<PackedMonomial<L, E>, uint64_t> tryDiv(PackedMonomial<L, E> const &x,
                                                 PackedMonomial<L, E> &&y) {
    uint64_t fail = tryDiv(y, x, y);
    return std::make_pair(std::move(y), fail);
}
template <size_t L, size_t E>
std::pair<PackedMonomial<L, E>, uint64_t> tryDiv(PackedMonomial<L, E> &&x,
                                                 PackedMonomial<L, E> &&y) {
    uint64_t fail = tryDiv(x, x, y);
    return std::make_pair(std::move(x), fail);
}

template <size_t L, size_t E>
PackedMonomial<L, E> operator^(PackedMonomial<L, E> const &x, uint64_t y) {
    PackedMonomial<L, E> z;
    for (size_t k = 0; k < x.K; ++k) {
        z.bits[k] = x.bits[k] * y;
    }
    return z;
}
template <size_t L, size_t E>
PackedMonomial<L, E> operator^(PackedMonomial<L, E> &&x, uint64_t y) {
    x ^= y;
    return std::move(x);
}

template <typename M>
concept IsMultivariateMonomial = requires(M a) {
    { a.degree(0) } -> std::convertible_to<size_t>;
    // { a.add_term(0, 0) } -> std::same_as<void>;
}
&&std::same_as<M, typename std::remove_cvref<M>::type>;
template <typename M>
concept IsMonomial = std::same_as<M, Uninomial> || IsMultivariateMonomial<M>;
// std::same_as<M, Monomial> || std::same_as<M, PackedMonomial<>>;
//template <typename U>
//concept IsUnivariateWithPolyCoefs = requires(U a){
//    { a.terms[0].terms[0].monomial };
//};

template <IsMonomial M> std::pair<M, bool> operator/(M const &x, M const &y) {
    M z;
    bool fail = tryDiv(z, x, y);
    return std::make_pair(std::move(z), fail);
}

template <typename C, IsMonomial M> struct Term {
    C coefficient;
    M exponent;
    Term() = default;
    // Term(Uninomial u) : coefficient(1), exponent(u){};
    // Term(Term const &x) = default;//: coefficient(x.coefficient), u(x.u){};
    // Term(C coef, Uninomial u) : coefficient(coef), u(u){};
    Term(C c) : coefficient(c), exponent(One()){};
    Term(M m) : coefficient(One()), exponent(m){};
    Term(C const &c, M const &m) : coefficient(c), exponent(m){};
    Term(C const &c, M &&m) : coefficient(c), exponent(std::move(m)){};
    Term(C &&c, M const &m) : coefficient(std::move(c)), exponent(m){};
    Term(C &&c, M &&m) : coefficient(std::move(c)), exponent(std::move(m)){};
    Term(One) : coefficient(One()), exponent(One()){};
    bool termsMatch(Term const &y) const {
        return exponent.termsMatch(y.exponent);
    }
    bool termsMatch(M const &e) const { return exponent.termsMatch(e); }
    bool lexGreater(Term const &y) const {
        return exponent.lexGreater(y.exponent);
    }
    size_t degree() const { return exponent.degree(); }
    M &monomial() { return exponent; }
    const M &monomial() const { return exponent; }
    bool addCoef(C const &coef) { return isZero((coefficient += coef)); }
    bool subCoef(C const &coef) { return isZero((coefficient -= coef)); }
    bool addCoef(C &&coef) { return isZero((coefficient += std::move(coef))); }
    bool subCoef(C &&coef) { return isZero((coefficient -= std::move(coef))); }
    bool addCoef(Term const &t) { return addCoef(t.coefficient); }
    bool subCoef(Term const &t) { return subCoef(t.coefficient); }
    bool addCoef(M const &) { return isZero((coefficient += 1)); }
    bool subCoef(M const &) { return isZero((coefficient -= 1)); }
    bool addCoef(Term const &t, C const &c) {
        return addCoef(t.coefficient * c);
    }
    Term &operator*=(intptr_t x) {
        coefficient *= x;
        return *this;
    }
    Term operator*(intptr_t x) const {
        Term y(*this);
        return y *= x;
    }
    Term &operator*=(M const &m) {
        exponent *= m;
        return *this;
    }
    Term &operator*=(Term<C, M> const &t) {
        coefficient *= t.coefficient;
        exponent *= t.exponent;
        return *this;
    }

    void negate() { ::negate(coefficient); }
    friend bool isZero(Term const &x) { return isZero(x.coefficient); }
    friend bool isOne(Term const &x) {
        return isOne(x.coefficient) & isOne(x.exponent);
    }

    template <typename CC> operator Term<CC, M>() {
        return Term<CC, M>(CC(coefficient), exponent);
    }

    bool isCompileTimeConstant() const { return isOne(exponent); }
    bool operator==(Term<C, M> const &y) const {
        return (exponent == y.exponent) && (coefficient == y.coefficient);
    }
    bool operator!=(Term<C, M> const &y) const {
        return (exponent != y.exponent) || (coefficient != y.coefficient);
    }
    /*
    // TODO: add me when mac clang supports it
    std::strong_ordering operator<=>(Term<C, M> &x) const {
        return exponent <=> x.exponent;
    }
    */
    // bool tryDiv(Term<C, M> const &x, Term<C, M> const &y) {
    //     coefficient = x.coefficient / y.coefficient;
    //     return exponent.tryDiv(x.exponent, y.exponent);
    // }

    friend std::ostream &operator<<(std::ostream &os, const Term &t) {
        if (isOne(t.coefficient)) {
            os << t.exponent;
        } else if (t.isCompileTimeConstant()) {
            os << t.coefficient;
        } else {
            os << t.coefficient << " ( " << t.exponent << " ) ";
        }
        return os;
    }
};
// template <typename C,typename M>
// bool Term<C,M>::isOne() const { return ::isOne(coefficient) &
// ::isOne(exponent); }

template <typename C, IsMonomial M> struct Terms {
    llvm::SmallVector<Term<C, M>, 1> terms;
    // std::vector<Term<C, M>> terms;
    Terms() = default;
    Terms(Term<C, M> const &x) : terms({x}){};
    Terms(Term<C, M> &&x) : terms({std::move(x)}){};
    Terms(Term<C, M> const &x, Term<C, M> const &y) : terms({x, y}){};
    // Terms(M &m0, M &m1) : terms({m0, m1}){};
    Terms(M &&m0, M &&m1) : terms({std::move(m0), std::move(m1)}){};
    Terms(M const &x, Term<C, M> const &y) : terms({x, y}){};
    Terms(Term<C, M> const &x, M const &y) : terms({x, y}){};
    Terms(M const &x, M const &y) : terms({x, y}){};
    Terms(One) : terms{Term<C, M>(One())} {};
    auto begin() { return terms.begin(); }
    auto end() { return terms.end(); }
    auto begin() const { return terms.begin(); }
    auto end() const { return terms.end(); }
    auto cbegin() const { return terms.begin(); }
    auto cend() const { return terms.end(); }

    template <typename S> void add_term(S &&x) {
        ::add_term(*this, std::forward<S>(x));
    }
    template <typename S> void add_term_scale(S &&x, C const &c) {
        if (!isZero(x)) {
            for (auto it = begin(); it != end(); ++it) {
                if ((it->termsMatch(x))) {
                    if (it->addCoef(x, c)) {
                        erase(it);
                    }
                    return;
                } else if (x.lexGreater(*it)) {
                    insert(it, std::forward<S>(x) * c);
                    return;
                }
            }
            push_back(std::forward<S>(x) * c);
        }
        return;
    }
    template <typename S> void sub_term(S &&x) {
        ::sub_term(*this, std::forward<S>(x));
    }

    template <typename I> void erase(I it) { terms.erase(it); }
    template <typename I> void insert(I it, Term<C, M> const &c) {
        terms.insert(it, c);
    }
    template <typename I> void insert(I it, Term<C, M> &&c) {
        terms.insert(it, std::move(c));
    }
    void push_back(Term<C, M> const &c) { terms.push_back(c); }
    void push_back(Term<C, M> &&c) { terms.push_back(std::move(c)); }

    template <typename I> void insert(I it, M const &c) { terms.insert(it, c); }
    template <typename I> void insert(I it, M &&c) {
        terms.insert(it, std::move(c));
    }
    void push_back(M const &c) { terms.emplace_back(1, c); }
    void push_back(M &&c) { terms.emplace_back(1, std::move(c)); }

    Terms<C, M> &operator+=(Term<C, M> const &x) {
        add_term(x);
        return *this;
    }
    Terms<C, M> &operator+=(Term<C, M> &&x) {
        add_term(std::move(x));
        return *this;
    }
    Terms<C, M> &operator-=(Term<C, M> const &x) {
        sub_term(x);
        return *this;
    }
    Terms<C, M> &operator-=(Term<C, M> &&x) {
        sub_term(std::move(x));
        return *this;
    }
    Terms<C, M> &operator-=(C const &x) {
        sub_term(Term<C, M>{x});
        return *this;
    }
    // Terms<C, M> &operator+=(M const &x) {
    //     add_term(x);
    //     return *this;
    // }
    Terms<C, M> &operator+=(M &&x) {
        add_term(std::forward<M>(x));
        return *this;
    }
    // Terms<C, M> &operator-=(M const &x) {
    //     sub_term(x);
    //     return *this;
    // }
    Terms<C, M> &operator-=(M &&x) {
        sub_term(std::forward<M>(x));
        return *this;
    }
    Terms<C, M> &operator*=(Term<C, M> const &x) {
        if (isZero(x)) {
            terms.clear();
            return *this;
        } else if (isOne(x)) {
            return *this;
        }
        for (auto &term : terms) {
            term *= x;
        }
        return *this;
    }
    Terms<C, M> &operator*=(M const &x) {
        if (isOne(x)) {
            return *this;
        }
        for (auto &term : terms) {
            term *= x;
        }
        return *this;
    }
    Terms<C, M> &operator+=(Terms<C, M> const &x) {
        for (auto &term : x) {
            add_term(term);
        }
        return *this;
    }
    Terms<C, M> &operator+=(Terms<C, M> &&x) {
        for (auto &&term : x) {
            add_term(std::move(term));
        }
        return *this;
    }
    Terms<C, M> &operator-=(Terms<C, M> const &x) {
        for (auto &term : x) {
            sub_term(term);
        }
        return *this;
    }
    Terms<C, M> &operator-=(Terms<C, M> &&x) {
        for (auto &&term : x) {
            sub_term(std::move(term));
        }
        return *this;
    }
    void mul(Terms<C, M> const &x, Terms<C, M> const &y) {
        terms.clear();
        terms.reserve(x.terms.size() * y.terms.size());
        for (auto &termx : x) {
            for (auto &termy : y) {
                add_term(termx * termy);
            }
        }
    }

    Terms<C, M> &operator+=(C const &x) {
        size_t numTerms = terms.size();
        if (numTerms) {
            auto it = end() - 1;
            if (!(it->degree())) {
                (it->coefficient) += x;
                if (isZero(it->coefficient)) {
                    terms.erase(it);
                }
                return *this;
            }
        }
        terms.emplace_back(x);
        return *this;
    }
    Terms<C, M> operator*(Terms<C, M> const &x) const {
        Terms<C, M> p;
        p.mul(*this, x);
        return p;
    }
    Terms<C, M> &operator*=(Terms<C, M> const &x) {
        // terms = ((*this) * x).terms;
        // return *this;
        if (isZero(x)) {
            terms.clear();
            return *this;
        }
        Terms<C, M> z = x * (*this);
        terms = z.terms;
        return *this;

        // this commented out code is incorrect, because it relies
        // on the order being maintained, but of course `add_term` sorts
        // We could use `push_back` and then `std::sort` at the end instead.
        // terms.reserve(terms.size() * x.terms.size());
        // auto itx = x.cbegin();
        // auto iti = begin();
        // auto ite = end();
        // for (; itx != x.cend() - 1; ++itx) {
        //     for (auto it = iti; it != ite; ++it) {
        //         add_term((*it) * (*itx));
        //     }
        // }
        // for (; iti != ite; ++iti) {
        //     (*iti) *= (*itx);
        // }
        // return *this;
    }
    bool isCompileTimeConstant() const {
        switch (terms.size()) {
        case 0:
            return true;
        case 1:
            return terms[0].isCompileTimeConstant();
        default:
            return false;
        }
    }

    bool operator==(Terms<C, M> const &x) const { return (terms == x.terms); }
    bool operator!=(Terms<C, M> const &x) const { return (terms != x.terms); }
    bool operator==(C const &x) const {
        return isCompileTimeConstant() && leadingCoefficient() == x;
    }
    Terms<C, M> largerCapacityCopy(size_t i) const {
        Terms<C, M> s;
        s.terms.reserve(i + terms.size()); // reserve full size
        for (auto &term : terms) {
            s.terms.push_back(term);
        }
        return s;
    }

    void negate() {
        for (auto &&term : terms) {
            term.negate();
        }
    }

    Term<C, M> &leadingTerm() { return terms[0]; }
    Term<C, M> const &leadingTerm() const { return terms[0]; }
    C &leadingCoefficient() { return begin()->coefficient; }
    const C &leadingCoefficient() const { return begin()->coefficient; }
    void removeLeadingTerm() { terms.erase(terms.begin()); }
    // void takeLeadingTerm(Term<C,M> &x) {
    //     add_term(std::move(x.leadingTerm()));
    //     x.removeLeadingTerm();
    // }

    friend bool isZero(Terms const &x) { return x.terms.size() == 0; }
    friend bool isOne(Terms const &x) {
        return (x.terms.size() == 1) && isOne(x.terms[0]);
    }

    Terms<C, M> operator^(size_t i) const { return powBySquare(*this, i); }

    size_t degree() const {
        if (terms.size()) {
            return leadingTerm().degree();
        } else {
            return 0;
        }
    }

    friend std::ostream &operator<<(std::ostream &os, Terms const &x) {
        os << " ( ";
        for (size_t j = 0; j < length(x.terms); ++j) {
            if (j) {
                os << " + ";
            }
            os << x.terms[j];
        }
        os << " ) ";
        return os;
    }
    constexpr One isPoly() {return One();}
};

template <typename C> using UnivariateTerm = Term<C, Uninomial>;
template <typename C, IsMultivariateMonomial M> using MultiTerm = Term<C, M>;

template <typename C> using Univariate = Terms<C, Uninomial>;
// template <typename C> using Multivariate = Terms<C, Monomial>;

template <typename C, IsMultivariateMonomial M>
using MultivariateTerm = Term<C, M>;
template <typename C, IsMultivariateMonomial M>
using Multivariate = Terms<C, M>;

// template <MultiTerm<C> M> using Multi = Terms<C, M>;
// template <typename C, MultiTerm<C> M> using Multi = Terms<C, M>;
// template <typename C, typename M> concept Multi = Terms<C, M>;

// template <typename C, Mu M>
// concept Multi =
// template <typename T>

// P is Multivariate<C,M> for any C
template <typename P>
concept IsMPoly = requires(P p){
    {p.isPoly()} -> std::same_as<One>;
};

template <typename C, IsMultivariateMonomial M>
bool operator==(Multivariate<C, M> const &x, M const &y) {
    return (x.terms.size() == 1) && (x.leadingTerm() == y);
}

Terms<intptr_t, Uninomial> operator+(Uninomial x, Uninomial y) {
    if (x.termsMatch(y)) {
        return Terms<intptr_t, Uninomial>(Term<intptr_t, Uninomial>(2, x));
    } else if (x.lexGreater(y)) {
        return Terms<intptr_t, Uninomial>(x, y);
    } else {
        return Terms<intptr_t, Uninomial>(y, x);
    }
    // Terms<intptr_t, Uninomial> z(x);
    // return z += y;
}

template <IsMonomial M> Terms<intptr_t, M> operator+(M const &x, M const &y) {
    Terms<intptr_t, M> z(x);
    // typedef typename std::remove_reference<M>::type MR;
    // Terms<intptr_t, MR> z(x);
    z += y;
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator+(M const &x, M &&y) {
    Terms<intptr_t, M> z(std::move(y));
    z += x;
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator+(M &&x, M const &y) {
    Terms<intptr_t, M> z(std::move(x));
    z += y;
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator+(M &&x, M &&y) {
    Terms<intptr_t, M> z(std::move(x));
    z += std::move(y);
    return z;
}

Terms<intptr_t, Uninomial> operator-(Uninomial x, Uninomial y) {
    if (x.termsMatch(y)) {
        return Terms<intptr_t, Uninomial>();
    } else if (x.lexGreater(y)) {
        return Terms<intptr_t, Uninomial>(Term<intptr_t, Uninomial>{1, x},
                                          Term<intptr_t, Uninomial>{-1, y});
    } else {
        return Terms<intptr_t, Uninomial>(Term<intptr_t, Uninomial>{-1, y},
                                          Term<intptr_t, Uninomial>{1, x});
    }
}
template <IsMonomial M> Terms<intptr_t, M> operator-(M const &x, M const &y) {
    Terms<intptr_t, M> z(x);
    z += Term<intptr_t, M>{-1, y};
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator-(M const &x, M &&y) {
    Terms<intptr_t, M> z(Term<intptr_t, M>{-1, std::move(y)});
    z += x;
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator-(M &&x, M const &y) {
    Terms<intptr_t, M> z(std::move(x));
    z += Term<intptr_t, M>{-1, y};
    return z;
}
template <IsMonomial M> Terms<intptr_t, M> operator-(M &&x, M &&y) {
    Terms<intptr_t, M> z(std::move(x));
    z += Term<intptr_t, M>{-1, std::move(y)};
    return z;
}

template <typename C> Terms<C, Uninomial> operator+(Uninomial x, C y) {
    return Terms<C, Uninomial>(Term<C, Uninomial>(x), Term<C, Uninomial>(y));
}
template <typename C> Terms<C, Uninomial> operator+(C y, Uninomial x) {
    return Terms<C, Uninomial>(Term<C, Uninomial>(x), Term<C, Uninomial>(y));
}

template <typename C, IsMonomial M> Terms<C, M> operator+(M x, C y) {
    return Terms<C, M>(Term<C, M>(x), Term<C, M>(y));
}
template <typename C, IsMonomial M> Terms<C, M> operator+(C y, M x) {
    return Terms<C, M>(Term<C, M>(x), Term<C, M>(y));
}
template <IsMonomial M> Terms<intptr_t, M> operator+(M x, int y) {
    return Terms<intptr_t, M>(Term<intptr_t, M>(x), Term<intptr_t, M>(y));
}
template <IsMonomial M> Terms<intptr_t, M> operator+(int y, M x) {
    return Terms<intptr_t, M>(Term<intptr_t, M>(x), Term<intptr_t, M>(y));
}

template <typename C>
Terms<C, Uninomial> operator+(Term<C, Uninomial> const &x, Uninomial y) {
    Terms<C, Uninomial> z(x);
    z += y;
    return z;
}
template <typename C>
Terms<C, Uninomial> operator+(Term<C, Uninomial> &&x, Uninomial y) {
    Terms<C, Uninomial> z(std::move(x));
    z += y;
    return z;
}
template <typename C>
Terms<C, Uninomial> operator-(Term<C, Uninomial> &&x, Uninomial y) {
    Terms<C, Uninomial> z(std::move(x));
    z -= Term{1, y};
    return z;
}

template <typename C, IsMonomial M>
Terms<C, M> operator+(Term<C, M> const &x, M const &y) {
    Terms<C, M> z(x);
    z += y;
    return z;
}
template <typename C, IsMonomial M>
Terms<intptr_t, M> operator+(Term<C, M> const &x, M &&y) {
    Terms<C, M> z(std::move(y));
    z += x;
    return z;
}
template <typename C, IsMonomial M>
Terms<intptr_t, M> operator+(Term<C, M> &&x, M const &y) {
    Terms<C, M> z(std::move(x));
    z += y;
    return z;
}
template <typename C, IsMonomial M>
Terms<intptr_t, M> operator+(Term<C, M> &&x, M &&y) {
    Terms<C, M> z(std::move(x));
    return z += std::move(y);
    return z;
}

Terms<intptr_t, Uninomial> operator+(Uninomial x, int y) {
    return Term<intptr_t, Uninomial>{y} + x;
}
Terms<intptr_t, Uninomial> operator+(int y, Uninomial x) {
    return Term<intptr_t, Uninomial>{y} + x;
}
Terms<intptr_t, Uninomial> operator-(Uninomial x, int y) {
    return Term<intptr_t, Uninomial>{-y} + x;
}
Terms<intptr_t, Uninomial> operator-(int y, Uninomial x) {
    return Term<intptr_t, Uninomial>{y} - x;
}
template <typename C>
Terms<intptr_t, Uninomial> operator-(Term<C, Uninomial> &x, int y) {
    return Term<intptr_t, Uninomial>{-y} + x;
}
template <typename C>
Terms<intptr_t, Uninomial> operator-(int y, Term<C, Uninomial> &x) {
    return Term<intptr_t, Uninomial>{y} - x;
}
template <typename C>
Terms<intptr_t, Uninomial> operator-(Term<C, Uninomial> &&x, int y) {
    return Term<intptr_t, Uninomial>{-y} + std::move(x);
}
template <typename C>
Terms<intptr_t, Uninomial> operator-(int y, Term<C, Uninomial> &&x) {
    return Term<intptr_t, Uninomial>{y} - std::move(x);
}

template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> const &x, Term<C, M> const &y) {
    if (x.termsMatch(y)) {
        return Terms<C, M>{
            Term<C, M>(x.coefficient + y.coefficient, x.exponent)};
    } else if (lexGreater(y)) {
        return Terms<C, M>(x, y);
    } else {
        return Terms<C, M>(y, x);
    }
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Term<C, M> const &y) {
    if (x.termsMatch(y)) {
        return Terms<C, M>{
            Term<C, M>(x.coefficient - y.coefficient, x.exponent)};
    } else if (lexGreater(y)) {
        return Terms<C, M>(x, negate(y));
    } else {
        return Terms<C, M>(negate(y), x);
    }
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z(std::move(y));
    z += x;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Term<C, M> const &y) {
    Terms<C, M> z(std::move(x));
    z += y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Term<C, M> &&y) {
    Terms<C, M> z(std::move(x));
    z += std::move(y);
    return z;
}

template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z(x);
    z -= std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Term<C, M> const &y) {
    Terms<C, M> z(std::move(x));
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Term<C, M> &&y) {
    Terms<C, M> z(std::move(x));
    z -= std::move(y);
    return z;
}

template <typename C, typename M>
Term<C, M> operator*(Term<C, M> const &x, Term<C, M> const &y) {
    Term<C, M> z(x);
    z *= y;
    return z;
}
template <typename C, typename M>
Term<C, M> operator*(Term<C, M> const &x, Term<C, M> &&y) {
    return std::move(y *= x);
}
template <typename C, typename M>
Term<C, M> operator*(Term<C, M> &&x, Term<C, M> const &y) {
    return std::move(x *= y);
}
template <typename C, typename M>
Term<C, M> operator*(Term<C, M> &&x, Term<C, M> &&y) {
    return std::move(x *= std::move(y));
}

template <typename C, IsMonomial M>
Term<C, M> &operator*=(Term<C, M> &x, M const &y) {
    x.exponent *= y;
    return x;
}
template <typename C, IsMonomial M>
Term<C, M> operator*(Term<C, M> const &x, M const &y) {
    Term<C, M> z(x);
    z.exponent *= y;
    return z;
}
template <typename C, IsMonomial M>
Term<C, M> operator*(M const &y, Term<C, M> const &x) {
    Term<C, M> z(x);
    z.exponent *= y;
    return z;
}

template <typename C, IsMonomial M>
Term<C, M> operator*(Term<C, M> &&x, M const &y) {
    x.exponent *= y;
    return std::move(x);
}
template <typename C, IsMonomial M>
Term<C, M> operator*(M const &y, Term<C, M> &&x) {
    x.exponent *= y;
    return std::move(x);
}

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, intptr_t y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    // Term<C,M> tt = Term<C,M>(C(y));
    // return std::move(z += tt);
    return std::move(z += Term<C, M>(C(y)));
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, intptr_t y) {
    return std::move(x += Term<C, M>(C(y)));
}

template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, intptr_t y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, intptr_t y) {
    return std::move(x -= y);
}

template <typename C, typename M>
Terms<C, M> operator+(intptr_t x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z += x;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(intptr_t x, Terms<C, M> &&y) {
    return std::move(y += x);
}

template <typename C, typename M>
Terms<C, M> operator-(intptr_t x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= x;
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(intptr_t x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, C const &y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z += y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, C &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z += std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, C const &y) {
    return std::move(x += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, C &&y) {
    return std::move(x += std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, C const &y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, C &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z -= std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, C const &y) {
    return std::move(x -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, C &&y) {
    return std::move(x -= std::move(y));
}
template <IsMonomial M> Terms<intptr_t, M> operator-(size_t x, M &y) {
    Terms<intptr_t, M> z;
    z.terms.reserve(2);
    z.terms.emplace_back(-1, y);
    z.terms.push_back(x);
    return z; // no std::move because of copy elision
}

template <typename C, typename M>
Terms<C, M> operator+(C const &x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z += x;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(C const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(C &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z += std::move(x);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(C &&x, Terms<C, M> &&y) {
    return std::move(y += std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator-(C const &x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= x;
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(C const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(C &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= std::move(x);
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(C &&x, Terms<C, M> &&y) {
    y -= std::move(x);
    y.negate();
    return std::move(y);
}

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, M const &y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z += y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, M &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z += std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, M const &y) {
    return std::move(x += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, M &&y) {
    return std::move(x += std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, M const &y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, M &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    z -= std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, M const &y) {
    return std::move(x -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, M &&y) {
    return std::move(x -= std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator+(M const &x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z += x;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(M const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(M &&x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z += std::move(x);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(M &&x, Terms<C, M> &&y) {
    return std::move(y += std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator-(M const &x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z -= x;
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(M const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(M &&x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z -= std::move(x);
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(M &&x, Terms<C, M> &&y) {
    y -= std::move(x);
    y.negate();
    return std::move(y);
}

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Term<C, M> const &y) {
    Terms<C, M> z(x.largerCapacityCopy(1));
    z += y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z(x.largerCapacityCopy(1));
    z += std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, Term<C, M> const &y) {
    return std::move(x += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, Term<C, M> &&y) {
    return std::move(x += std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, Term<C, M> const &y) {
    Terms<C, M> z(x.largerCapacityCopy(1));
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z(x.largerCapacityCopy(1));
    z -= std::move(y);
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, Term<C, M> const &y) {
    return std::move(x -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, Term<C, M> &&y) {
    return std::move(x -= std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> const &x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z += x;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    return std::move(z += std::move(x));
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Terms<C, M> &&y) {
    return std::move(y += std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z -= x;
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Terms<C, M> const &y) {
    Terms<C, M> z(y.largerCapacityCopy(1));
    z -= std::move(x);
    z.negate();
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Terms<C, M> &&y) {
    y -= std::move(x);
    y.negate();
    return std::move(y);
}

// template <typename C, typename M>
// Terms<C,M>& negate(Terms<C,M> &x){ return x.negate(); }

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Terms<C, M> const &y) {
    Terms<C, M> z(x.largerCapacityCopy(y.size()));
    z += y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, Terms<C, M> const &y) {
    return std::move(x += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> &&x, Terms<C, M> &&y) {
    return std::move(x += std::move(y));
}

template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, Terms<C, M> const &y) {
    Terms<C, M> z(x.largerCapacityCopy(y.terms.size()));
    z -= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, Terms<C, M> const &y) {
    return std::move(x -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, Terms<C, M> &&y) {
    return std::move(x -= std::move(y));
}

// template <typename C, typename M>
// Terms<C, M> operator*(Terms<C, M> const &x, Terms<C, M> &&y) {
//     return std::move(y *= x);
// }
// template <typename C, typename M>
// Terms<C, M> operator*(Terms<C, M> &&x, Terms<C, M> const &y) {
//     return std::move(x *= y);
// }
// template <typename C, typename M>
// Terms<C, M> operator*(Terms<C, M> &&x, Terms<C, M> &&y) {
//     return std::move(x *= std::move(y));
// }
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &x, M const &y) {
    Terms<C, M> z(x);
    z *= y;
    return z;
}
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &&x, M const &y) {
    return std::move(x *= y);
}
template <typename C, typename M>
Terms<C, M> operator*(M const &y, Terms<C, M> &x) {
    Terms<C, M> z(x);
    return std::move(z *= y);
}
template <typename C, typename M>
Terms<C, M> operator*(M const &y, Terms<C, M> &&x) {
    return std::move(x *= y);
}
template <typename C> void divExact(Univariate<C> &d, C const &x) {
    for (auto &&term : d) {
        divExact(term.coefficient, x);
    }
}
template <typename C>
void divExact(Univariate<C> &q, Univariate<C> &d, C const &x) {
    size_t N = d.terms.size();
    q.terms.resize(N);
    for (size_t n = 0; n < N; ++n) {
        divExact(q.terms[n].coefficient, d.terms[n].coefficient, x);
        q.terms[n].exponent = d.terms[n].exponent;
    }
}
template <typename C, typename M>
void fnmadd(Terms<C, M> &x, Terms<C, M> const &y, Term<C, M> const &z) {
    for (auto &term : y) {
        x.sub_term(term * z);
    }
}
template <typename C, typename M>
void fnmadd(Terms<C, M> &x, Terms<C, M> const &y, Term<C, M> const &z,
            size_t offset) {
    for (auto &term : y) {
        sub_term(x, term * z, offset);
    }
}
template <typename C, typename M>
void fnmadd(Terms<C, M> &x, Terms<C, M> const &y, C &c) {
    for (auto &term : y) {
        x.add_term_scale(term, -c);
    }
}

template <typename C, IsMonomial M>
std::pair<Multivariate<C, M>, Multivariate<C, M>>
divRemBang(Multivariate<C, M> &p, Multivariate<C, M> const &d) {
    if (isZero(p)) {
        return std::make_pair(p, p);
    }
    Multivariate<C, M> q;
    Multivariate<C, M> r;
    Term<C, M> nx;
    size_t offset = 0;
    while (offset != p.terms.size()) {
        bool fail = tryDiv(nx, p.terms[offset], d.leadingTerm());
        if (fail) {
            r.add_term(std::move(p.terms[offset]));
            ++offset;
        } else {
            fnmadd(p, d, nx, offset);
            // p -= d * nx;
            q += nx;
        }
    }
    std::swap(q, p);
    return std::make_pair(p, std::move(r));
}
template <typename C, IsMonomial M>
std::pair<Multivariate<C, M>, Multivariate<C, M>>
divRem(Multivariate<C, M> const &n, Multivariate<C, M> const &d) {
    Multivariate<C, M> p(n);
    return divRemBang(p, d);
}

template <typename C, IsMonomial M>
void divExact(Multivariate<C, M> &p, Multivariate<C, M> const &d) {
    if (isZero(p)) {
        return;
    }
    Multivariate<C, M> q;
    Term<C, M> nx;
    while (p.terms.size()) {
        bool fail = tryDiv(nx, p.leadingTerm(), d.leadingTerm());
        assert(!fail);
        fnmadd(p, d, nx);
        q += nx;
    }
    std::swap(q, p);
}

// destroys `p`, writes answer in `q`
template <typename C, IsMonomial M>
void divExact(Multivariate<C, M> &q, Multivariate<C, M> &p,
              Multivariate<C, M> const &d) {
    q.terms.clear();
    if (isZero(p)) {
        return;
    }
    Term<C, M> nx;
    while (p.terms.size()) {
        bool fail = tryDiv(nx, p.leadingTerm(), d.leadingTerm());
        assert(!fail);
        fnmadd(p, d, nx);
        q += nx;
    }
}

template <typename C>
Term<C, Uninomial> operator*(Term<C, Uninomial> const &x,
                             Term<C, Uninomial> const &y) {
    return Term<C, Uninomial>{x.coefficient * y.coefficient,
                              Uninomial(x.degree() + y.degree())};
}
template <typename C>
Term<C, Uninomial> &operator*=(Term<C, Uninomial> &x,
                               Term<C, Uninomial> const &y) {
    x.coefficient *= y.coefficient;
    x.exponent.exponent += y.degree();
    return x;
}
// template <typename C>
// std::pair<Term<C,Uninomial>, bool> operator/(Term<C,Uninomial> &x,
// Term<C,Uninomial> const &y) { 	auto [u, f] = x.exponent / y.exponent;
// return std::make_pair(Term{x.coefficient / y.coefficient, u},
// f);
// }
template <typename C, typename M>
bool tryDiv(Term<C, M> &z, Term<C, M> &x, Term<C, M> const &y) {
    return tryDiv(z.coefficient, x.coefficient, y.coefficient) ||
           tryDiv(z.exponent, x.exponent, y.exponent);
}
template <typename C, typename M>
std::pair<Term<C, M>, bool> operator/(Term<C, M> &x, Term<C, M> const &y) {
    Term<C, M> z;
    bool fail = tryDiv(z, x, y);
    return std::make_pair(std::move(z), fail);
}
template <typename C>
Term<C, Uninomial> &operator^=(Term<C, Uninomial> &x, size_t i) {
    x.coefficient = powBySquare(x.coefficient, i);
    x.exponent ^= i;
    return x;
}
template <typename C, IsMonomial M>
Term<C, M> &operator^=(Term<C, M> &x, size_t i) {
    x.coefficient = powBySquare(x.coefficient, i);
    x.exponent ^= i;
    return x;
}

template <typename C, typename M>
Term<C, Uninomial> operator^(Term<C, M> &x, size_t i) {
    Term t(x);
    return std::move(t ^= i);
}

// template <typename C>
// std::pair<Term<C,M>, bool> operator/(Term<C,M> &x,
// Term<C,M> const &y) { 	auto [u, f] = x.exponent / y.exponent;
// return std::make_pair(Term{x.coefficient / y.coefficient, u}, f);
// }

Term<intptr_t, Uninomial> operator*(Uninomial const &x, intptr_t c) {
    return Term<intptr_t, Uninomial>{c, x};
}
Term<intptr_t, Uninomial> operator*(intptr_t c, Uninomial const &x) {
    return Term<intptr_t, Uninomial>{c, x};
}
Term<Rational, Uninomial> operator*(Uninomial const &x, Rational c) {
    return Term<Rational, Uninomial>{c, x};
}
Term<Rational, Uninomial> operator*(Rational c, Uninomial const &x) {
    return Term<Rational, Uninomial>{c, x};
}

template <typename C, IsMonomial M>
Multivariate<C, M> operator*(intptr_t x, Multivariate<C, M> &c) {
    Multivariate<C, M> p(c);
    p *= x;
    return p; // copy elision
}
template <typename C, IsMonomial M>
Multivariate<C, M> operator*(Multivariate<C, M> &c, intptr_t x) {
    Multivariate<C, M> p(c);
    p *= x;
    return std::move(p);
}
template <typename C, IsMonomial M>
Multivariate<C, M> operator*(intptr_t x, Multivariate<C, M> &&c) {
    c *= x;
    return std::move(c);
}
template <typename C, IsMonomial M>
Multivariate<C, M> operator*(Multivariate<C, M> &&c, intptr_t x) {
    c *= x;
    return std::move(c);
}

template <typename C, IsMonomial M>
Term<Polynomial::Multivariate<C, M>, Uninomial>
operator*(Uninomial const &x, Polynomial::Multivariate<C, M> &c) {
    return Term<Polynomial::Multivariate<C, M>, Uninomial>{c, x};
}
template <typename C, IsMonomial M>
Term<Polynomial::Multivariate<C, M>, Uninomial>
operator*(Polynomial::Multivariate<C, M> &c, Uninomial const &x) {
    return Term<Polynomial::Multivariate<C, M>, Uninomial>{c, x};
}
template <typename C, IsMonomial M>
Term<Polynomial::Multivariate<C, M>, Uninomial>
operator*(Uninomial const &x, Polynomial::Multivariate<C, M> &&c) {
    return Term<Polynomial::Multivariate<C, M>, Uninomial>{std::move(c), x};
}
template <typename C, IsMonomial M>
Term<Polynomial::Multivariate<C, M>, Uninomial>
operator*(Polynomial::Multivariate<C, M> &&c, Uninomial const &x) {
    return Term<Polynomial::Multivariate<C, M>, Uninomial>{std::move(c), x};
}
template <IsMonomial M> Term<intptr_t, M> operator*(M &x, intptr_t c) {
    return Term<intptr_t, M>(c, x);
}
template <IsMonomial M> Term<intptr_t, M> operator*(intptr_t c, M &x) {
    return Term<intptr_t, M>(c, x);
}
template <IsMonomial M> Term<intptr_t, M> operator*(M &&x, intptr_t c) {
    return Term<intptr_t, M>(c, std::move(x));
}
template <IsMonomial M> Term<intptr_t, M> operator*(intptr_t c, M &&x) {
    return Term<intptr_t, M>(c, std::move(x));
}
template <IsMonomial M> Term<Rational, M> operator*(M &x, Rational c) {
    return Term<Rational, M>(c, x);
}
template <IsMonomial M> Term<Rational, M> operator*(Rational c, M &x) {
    return Term<Rational, M>(c, x);
}
template <IsMonomial M> Term<Rational, M> operator*(M &&x, Rational c) {
    return Term<Rational, M>(c, std::move(x));
}
template <IsMonomial M> Term<Rational, M> operator*(Rational c, M &&x) {
    return Term<Rational, M>(c, std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator*=(Terms<C, M> &x, C const &y) {
    for (auto &&term : x) {
        term *= y;
    }
    return x;
}
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &&x, C const &y) {
    // x *= y;
    return std::move(x *= y);
}
template <typename C, typename M>
Terms<C, M> operator*(C const &y, Terms<C, M> &&x) {
    // x *= y;
    return std::move(x *= y);
}

template <typename C>
void mulPow(Univariate<C> &dest, Univariate<C> const &p,
            Term<C, Uninomial> const &a) {
    for (size_t i = 0; i < dest.terms.size(); ++i) {
        dest.terms[i] = p.terms[i] * a;
    }
}
template <HasMul C, typename M>
void mul(Term<C, M> &z, Term<C, M> const &x, Term<C, M> const &y) {
    z.coefficient.mul(x.coefficient, y.coefficient);
    z.exponent.mul(x.exponent, y.exponent);
}
template <HasMul C>
void mulPow(Univariate<C> &dest, Univariate<C> const &p,
            Term<C, Uninomial> const &a) {
    for (size_t i = 0; i < dest.terms.size(); ++i) {
        mul(dest.terms[i], p.terms[i], a);
    }
}

template <typename C>
Univariate<C> pseudorem(Univariate<C> const &p, Univariate<C> const &d) {
    if (p.degree() < d.degree()) {
        return p;
    }
    size_t k = (1 + p.degree()) - d.degree();
    const C &l = d.leadingCoefficient();
    Univariate<C> dd(d);
    Univariate<C> pp(p);
    while ((!isZero(p)) && (pp.degree() >= d.degree())) {
        mulPow(dd, d,
               Term<C, Uninomial>(pp.leadingCoefficient(),
                                  Uninomial(pp.degree() - d.degree())));
        pp *= l;
        pp -= dd;
        k -= 1;
    }
    pp *= powBySquare(l, k);
    return pp;
}
template <typename C>
void pseudorem(Univariate<C> &pp, Univariate<C> const &p,
               Univariate<C> const &d) {
    pp = p;
    if (p.degree() < d.degree()) {
        return;
    }
    size_t k = (1 + p.degree()) - d.degree();
    const C &l = d.leadingCoefficient();
    Univariate<C> dd(d);
    while ((!isZero(p)) && (pp.degree() >= d.degree())) {
        mulPow(dd, d,
               Term<C, Uninomial>(pp.leadingCoefficient(),
                                  Uninomial(pp.degree() - d.degree())));
        pp *= l;
        pp -= dd;
        k -= 1;
    }
    pp *= powBySquare(l, k);
}
// template <typename C, IsMultivariateMonomial M> C termwiseContent(llvm::ArrayRef<Term<C,M>> a){
//template <typename C, IsMultivariateMonomial M, unsigned L> C termwiseContent(llvm::SmallVector<Term<C,M>,L> const &a){
template <typename K> auto termwiseContent(K const &a){
    if (a.size() == 1){ return a[0]; }
    auto g = gcd(a[0], a[1]);
    for (size_t i = 2; i < a.size(); ++i){
        if (isOne(g)){ break; }
        g = gcd(g, a[i]);
    }
    return g;
}

// IsMPoly<M> C === IsMPoly<C,M>
template <IsMPoly C> auto content(Univariate<C> const &a) {
    if (a.terms.size() == 1) {
        return a.terms[0].coefficient;
    }
    for (auto &term : a){
        // term is Term<C,Unimonial>, where C is a multivariate polynomial.
        // access the multivariate polynomial via `term.coefficient`.
        // The multivariate polynomials 
        if (term.coefficient.terms.size() == 1){
            // term.coefficient is a single {term}
            auto g = gcd(termwiseContent(a.terms[0].coefficient.terms), termwiseContent(a.terms[1].coefficient.terms));
            for (size_t i = 2; i < a.terms.size(); ++i){
                if(isOne(g)) { break; }
                g = gcd(g, termwiseContent(a.terms[i].coefficient.terms));
            }
            return C(g);
        }
    }
    auto g = gcd(a.terms[0].coefficient, a.terms[1].coefficient);
    for (size_t i = 2; i < a.terms.size(); ++i) {
        g = gcd(g, a.terms[i].coefficient);
    }
    return g;
}
template <typename C> C content(Univariate<C> const &a) {
    if (a.terms.size() == 1) {
        return a.terms[0].coefficient;
    }
    C g = gcd(a.terms[0].coefficient, a.terms[1].coefficient);
    for (size_t i = 2; i < a.terms.size(); ++i) {
        g = gcd(g, a.terms[i].coefficient);
    }
    return g;
}
template <typename C> C content(C &g, Univariate<C> const &a) {
    if (a.terms.size() == 1) {
        return a.terms[0].coefficient;
    }
    g = gcd(a.terms[0].coefficient, a.terms[1].coefficient);
    if (a.terms.size() == 2) {
        return g;
    }
    C t;
    for (size_t i = 2; i < a.terms.size(); ++i) {
        t = gcd(g, a.terms[i].coefficient);
        std::swap(t, g);
    }
    return g;
}
/*
template <typename C> void content(C &g, C &t, Univariate<C> const &a) {
    if (a.terms.size() == 1) {
        g = a.terms[0].coefficient;
        return;
    }
    gcd(g, a.terms[0].coefficient, a.terms[1].coefficient);
    for (size_t i = 2; i < a.terms.size(); ++i) {
        gcd(t, g, a.terms[i].coefficient);
        std::swap(t, g);
    }
    return;
}*/
template <typename C> void primPart(Univariate<C> &d, Univariate<C> &p) {
    divExact(d, p, content(p));
}
template <typename C> Univariate<C> primPart(Univariate<C> const &p) {
    Univariate<C> d(p);
    divExact(d, content(p));
    return d;
}
template <typename C>
std::pair<C, Univariate<C>> contPrim(Univariate<C> const &p) {
    C c = content(p);
    Univariate<C> d(p);
    divExact(d, c);
    return std::make_pair(std::move(c), std::move(d));
}
template <typename C>
std::pair<C, Univariate<C>> contPrim(C &c, Univariate<C> const &p) {
    content(c, p);
    Univariate<C> d(p);
    divExact(d, c);
    return std::make_pair(std::move(c), std::move(d));
}
template <typename C>
std::pair<C, Univariate<C>> contPrim(Univariate<C> &t, Univariate<C> const &p) {
    C c = content(p);
    Univariate<C> d;
    t = p;
    divExact(d, t, c);
    return std::make_pair(std::move(c), std::move(d));
}

template <typename C>
Univariate<C> gcd(Univariate<C> const &x, Univariate<C> const &y) {
    if (x.degree() < y.degree()) {
        return gcd(y, x);
    }
    if (isZero(y)) {
        return x;
    } else if (isOne(y)) {
        return y;
        // } else if (x == y){
        // return x;
    }
    Univariate<C> r;
    auto [t0, xx] = contPrim(r, x);
    auto [t1, yy] = contPrim(r, y);
    C c = gcd(t0, t1);
    C g{One()};
    C h{One()};
    C t2;
    // TODO: allocate less memory; can you avoid the copies?
    while (true) {
        pseudorem(r, xx, yy);
        if (isZero(r)) {
            break;
        }
        if (r.degree() == 0) {
            return Univariate<C>(c);
        }
        size_t d = xx.degree() - yy.degree(); 
        powBySquare(t0, t1, t2, h, d);
        // t0 = h^d
        t1.mul(t0, g);
        // xx = r;
        divExact(xx, r, t1);
        // divExact(r, g * (h ^ d)); // defines new y
        std::swap(xx, yy);
        // std::swap(yy, r);
        g = xx.leadingCoefficient();
        if (d > 1) {
            powBySquare(t0, t1, t2, h, d - 1);
            // t0 = h ^ (d - 1);
            powBySquare(t1, h, t2, g, d);
            // h = g ^ d;
            divExact(h, t1, t0);
        } else {
            powBySquare(t0, t1, t2, h, 1 - d);
            powBySquare(t1, h, t2, g, d);
            h.mul(t0, t1);
            // h = (h ^ (1 - d)) * (g ^ d);
        }
    }
    primPart(xx, yy);
    xx *= std::move(c);
    return xx;
}

Monomial gcd(Monomial const &x, Monomial const &y) {
    if (isOne(x)) {
        return x;
    } else if (isOne(y) || (x == y)) {
        return y;
    }
    Monomial g;
    auto ix = x.cbegin();
    auto ixe = x.cend();
    auto iy = y.cbegin();
    auto iye = y.cend();
    while ((ix != ixe) | (iy != iye)) {
        size_t xk = (ix != ixe) ? *ix : std::numeric_limits<size_t>::max();
        size_t yk = (iy != iye) ? *iy : std::numeric_limits<size_t>::max();
        if (xk < yk) {
            ++ix;
        } else if (xk > yk) {
            ++iy;
        } else { // xk == yk
            g.prodIDs.push_back(xk);
            ++ix;
            ++iy;
        }
    }
    return g;
}
/*
Monomial gcd(Monomial const &x, Monomial const &y) {
    if (isOne(x)) {
        return x;
    } else if (isOne(y) || (x == y)) {
        return y;
    } else {
        return gcdimpl(x, y);
    }
}
Monomial gcd(Monomial &&x, Monomial const &y) {
    if (isOne(x) || (x == y)) {
        return std::move(x);
    } else if (isOne(y)) {
        return y;
    } else {
        return gcdimpl(x, y);
    }
}
Monomial gcd(Monomial const &x, Monomial &&y) {
    if (isOne(y) || (x == y)) {
        return std::move(y);
    } else if (isOne(x)) {
        return x;
    } else {
        return gcdimpl(x, y);
    }
}
Monomial gcd(Monomial &&x, Monomial &&y) {
    if (isOne(x)) {
        return std::move(x);
    } else if (isOne(y) || (x == y)) {
        return std::move(y);
    } else {
        return gcdimpl(x, y);
    }
}
*/
size_t gcd(intptr_t x, intptr_t y) { return std::gcd(x, y); }

template <typename C, typename M>
Term<C, M> gcd(Term<C, M> const &x, Term<C, M> const &y) {
    M g = gcd(x.exponent, y.exponent);
    C gr = gcd(x.coefficient, y.coefficient);
    return Term<C, M>(std::move(gr), std::move(g));
}
template <typename C, IsMonomial M>
Term<C, M> gcd(Term<C, M> const &x, Term<C, M> const &y) {
    C gr = gcd(x.coefficient, y.coefficient);
    if (isOne(x.exponent)) {
        return Term<C, M>(gr, x.exponent);
    } else if (isOne(y.exponent)) {
        return Term<C, M>(gr, y.exponent);
    } else {
        return Term<C, M>(gr, gcd(x.exponent, y.exponent));
    }
}

std::tuple<Monomial, Monomial, Monomial> gcdd(Monomial const &x,
                                              Monomial const &y) {
    Monomial g, a, b;
    auto ix = x.cbegin();
    auto ixe = x.cend();
    auto iy = y.cbegin();
    auto iye = y.cend();
    while ((ix != ixe) | (iy != iye)) {
        size_t xk = (ix != ixe) ? *ix : std::numeric_limits<size_t>::max();
        size_t yk = (iy != iye) ? *iy : std::numeric_limits<size_t>::max();
        if (xk < yk) {
            a.prodIDs.push_back(xk);
            ++ix;
        } else if (xk > yk) {
            b.prodIDs.push_back(yk);
            ++iy;
        } else { // xk == yk
            g.prodIDs.push_back(xk);
            ++ix;
            ++iy;
        }
    }
    return std::make_tuple(std::move(g), std::move(a), std::move(b));
}
template <typename C, typename M>
std::tuple<Term<C, M>, Term<C, M>, Term<C, M>> gcdd(Term<C, M> const &x,
                                                    Term<C, M> const &y) {
    auto [g, a, b] = gcdd(x.monomial, y.monomial);
    C gr = gcd(x.coefficient, y.coefficient);
    return std::make_tuple(Term<C, M>(gr, g), Term<C, M>(x.coefficient / gr, a),
                           Term<C, M>(y.coefficient / gr, b));
}

template <typename C, typename M>
std::pair<Term<C, M>, std::vector<Term<C, M>>>
contentd(std::vector<Term<C, M>> const &x) {
    switch (x.size()) {
    case 0:
        return std::make_pair(Term<C, M>(0), x);
    case 1:
        return std::make_pair(x[0], std::vector<Term<C, M>>{Term<C, M>(1)});
    default:
        auto [g, a, b] = gcd(x[0], x[1]);
        std::vector<Term<C, M>> f;
        f.reserve(x.size());
        f.push_back(std::move(a));
        f.push_back(std::move(b));
        for (size_t i = 2; i < x.size(); ++i) {
            auto [gt, a, b] = gcd(g, x[i]);
            std::swap(g, gt);
            if (!isOne(a)) {
                for (auto &&it : f) {
                    it *= a;
                }
            }
            f.push_back(std::move(b));
        }
        return std::make_pair(g, x);
    }
}

template <typename C, typename M>
std::pair<Term<C, M>, Terms<C, M>> contentd(Terms<C, M> const &x) {
    std::pair<Term<C, M>, std::vector<Term<C, M>>> st = contentd(x.terms);
    return std::make_pair(std::move(st.first),
                          Terms<C, M>(std::move(st.second)));
}

template <typename C>
Term<C, Monomial> termToPolyCoeff(Term<C, Monomial> const &t, size_t i) {
    Term<C, Monomial> a(t.coefficient);
    for (auto e : t.exponent) {
        if (e != i) {
            a.exponent.prodIDs.push_back(e);
        }
    }
    return a;
}
template <typename C, size_t L, size_t E>
Term<C, PackedMonomial<L,E>> termToPolyCoeff(Term<C, PackedMonomial<L,E>> const &t, size_t i) {
    Term<C, PackedMonomial<L,E>> a(t);
    a.exponent.removeTerm(i);
    return a;
}
/* commented out, because probably broken
template <typename C>
Term<C, M> termToPolyCoeff(Term<C, M> &&t, size_t i) {
auto start = t.end();
auto stop = t.begin();
auto it = t.begin();
for (; it != t.end(); ++it) {
    if ((*it) == i) {
        break;
    }
}
if (it == t.end()) {
    return std::move(t);
}
auto ite = it;
for (; ite != t.end(); ++ite) {
    if ((*it) != i) {
        break;
    }
}
t.erase(it, ite);
return std::move(t);
}
*/
    /*
template <typename I> size_t count(I it, I ite, size_t v) {
    size_t s = 0;
    for (; it != ite; ++it) {
        s += ((*it) == v);
    }
    return s;
}

template <typename C> size_t count(Term<C, Monomial> const &p, size_t v) {
    return count(p.exponent.prodIDs.begin(), p.exponent.prodIDs.end(), v);
}
    */
struct FirstGreater {
    template <typename T, typename S>
    bool operator()(std::pair<T, S> const &x, std::pair<T, S> const &y) {
        return x.first > y.first;
    }
};

template <typename C, IsMonomial M>
void emplace_back(Univariate<Multivariate<C, M>> &u,
                  Multivariate<C, M> const &p,
                  llvm::ArrayRef<std::pair<size_t, size_t>> const &pows,
                  size_t oldDegree, size_t chunkStartIdx, size_t idx,
                  size_t v) {
    Multivariate<C, M> coef;
    if (oldDegree) {
        coef = termToPolyCoeff(p.terms[pows[chunkStartIdx].second], v);
        for (size_t i = chunkStartIdx + 1; i < idx; ++i) {
            coef += termToPolyCoeff(p.terms[pows[i].second], v);
        }
    } else {
        coef = p.terms[pows[chunkStartIdx].second];
        for (size_t i = chunkStartIdx + 1; i < idx; ++i) {
            coef += p.terms[pows[i].second];
        }
    }
    u.terms.emplace_back(std::move(coef), Uninomial(oldDegree));
}

template <typename C, IsMonomial M>
Univariate<Multivariate<C, M>>
multivariateToUnivariate(Multivariate<C, M> const &p, size_t v) {
    llvm::SmallVector<std::pair<size_t, size_t>> pows;
    pows.reserve(p.terms.size());
    for (size_t i = 0; i < p.terms.size(); ++i) {
        // pows.emplace_back(count(p.terms[i], v), i);
        pows.emplace_back(p.terms[i].exponent.degree(v), i);
    }
    std::sort(pows.begin(), pows.end(), FirstGreater());

    Univariate<Multivariate<C, M>> u;
    if (pows.size() == 0) {
        return u;
    }
    size_t oldDegree = pows[0].first;
    size_t chunkStartIdx = 0;
    size_t idx = 0;
    while (idx < pows.size()) {
        size_t degree = pows[idx].first;
        if (oldDegree != degree) {
            emplace_back(u, p, pows, oldDegree, chunkStartIdx, idx, v);
            chunkStartIdx = idx;
            oldDegree = degree;
        }
        idx += 1;
    }
    // if (chunkStartIdx + 1 != idx) {
    emplace_back(u, p, pows, oldDegree, chunkStartIdx, idx, v);
    //}
    return u;
}

template <typename C, IsMonomial M>
Multivariate<C, M> univariateToMultivariate(Univariate<Multivariate<C, M>> &&g,
                                            size_t v) {
    Multivariate<C, M> p;
    for (auto &&it : g) {
        Multivariate<C, M> coef = it.coefficient;
        size_t exponent = (it.exponent).exponent;
        if (exponent) {
            for (auto &&ic : coef) {
                ic.exponent.add_term(v, exponent);
            }
        }
        p += coef;
    }
    return p;
}

static constexpr MonomialIDType NOT_A_VAR =
    std::numeric_limits<MonomialIDType>::max();

template <typename C, IsMonomial M>
MonomialIDType pickVar(Multivariate<C, M> const &x) {
    MonomialIDType v = NOT_A_VAR;
    for (auto &it : x) {
        if (it.degree()) {
            // v = std::min(v, *(it.exponent.begin()));
            v = std::min(v, MonomialIDType(it.exponent.firstTermID()));
        }
    }
    return v;
}

template <typename C, IsMonomial M>
Multivariate<C, M> gcd(Multivariate<C, M> const &x,
                       Multivariate<C, M> const &y) {
    if (isZero(x) || isOne(y)) {
        return y;
    } else if ((isZero(y) || isOne(x)) || (x == y)) {
        return x;
    }
    size_t v1 = pickVar(x);
    size_t v2 = pickVar(y);
    if (v1 < v2) {
        return gcd(y, content(multivariateToUnivariate(x, v1)));
    } else if (v1 > v2) {
        return gcd(x, content(multivariateToUnivariate(y, v2)));
    } else if (v1 == NOT_A_VAR) {
        return Multivariate<C, M>(gcd(x.leadingTerm(), y.leadingTerm()));
    } else { // v1 == v2, and neither == NOT_A_VAR
        return univariateToMultivariate(gcd(multivariateToUnivariate(x, v1),
                                            multivariateToUnivariate(y, v2)),
                                        v1);
    }
}
/*
template <typename C>
Multivariate<C,M> gcd(Multivariate<C,M> const &x, Multivariate<C,M> const &y) {
if (isZero(x) || isOne(y)) {
    return y;
} else if ((isZero(y) || isOne(x)) || (x == y)) {
    return x;
} else {
    return gcdimpl(x, y);
}
}
template <typename C>
Multivariate<C,M> gcd(Multivariate<C,M> const &x, Multivariate<C,M> &&y) {
if (isZero(x) || isOne(y) || (x == y)) {
    return std::move(y);
} else if ((isZero(y) || isOne(x))) {
    return x;
} else {
    return gcdimpl(x, std::move(y));
}
}
template <typename C>
Multivariate<C,M> gcd(Multivariate<C,M> &&x, Multivariate<C,M> const &y) {
if (isZero(x) || isOne(y)) {
    return y;
} else if ((isZero(y) || isOne(x)) || (x == y)) {
    return std::move(x);
} else {
    return gcdimpl(x, y);
}
}
template <typename C>
Multivariate<C,M> gcd(Multivariate<C,M> &&x, Multivariate<C,M> &&y) {
if (isZero(x) || isOne(y)) {
    return std::move(y);
} else if ((isZero(y) || isOne(x)) || (x == y)) {
    return std::move(x);
} else {
    return gcdimpl(std::move(x), std::move(y));
}
}
*/
template <typename C, IsMonomial M>
Multivariate<C, M> gcd(Multivariate<C, M> const &x,
                       MultivariateTerm<C, M> const &y) {
    return gcd(x, Multivariate<C, M>(y));
}
template <typename C, IsMonomial M>
Multivariate<C, M> gcd(MultivariateTerm<C, M> const &x,
                       Multivariate<C, M> const &y) {
    return gcd(Multivariate<C, M>(x), y);
}

/*
Multivariate<intptr_t>
loopToAffineUpperBound(Vector<Int, MAX_PROGRAM_VARIABLES> loopvars) {
// loopvars is a vector, first index corresponds to constant, remaining
// indices are offset by 1 with respect to program indices.
Multivariate<intptr_t> aff;
for (size_t i = 0; i < MAX_PROGRAM_VARIABLES; ++i) {
    if (loopvars[i]) {
        MultivariateTerm<intptr_t> sym(loopvars[i]);
        if (i) {
            sym.exponent.prodIDs.push_back(i - 1);
        } // i == 0 => constant
        // guaranteed no collision and lex ordering
        // so we push_back directly.
        aff.terms.push_back(std::move(sym));
    }
}
return aff;
}
*/
} // end namespace Polynomial

// A(m, n + k + 1)
// A( 1*m1 + M*(n1 + k1 + 1) )
// A( 1*m2 + M*n2 )
// if m1 != m2 => noalias // find strides
// if n1 == n2 => noalias
// if n1 < n2 => alias
//
// 1. partition into strides
// 2. check if a dependency may exist
//   a) solve Diophantine equations / Banerjee equations
// 3. calc difference vector
//
// [ 0 ], k1 + 1;
//

enum Order {
    InvalidOrder,
    EqualTo,
    LessThan,
    LessOrEqual,
    GreaterThan,
    GreaterOrEqual,
    NotEqual,
    UnknownOrder
};
auto maybeEqual(Order o) { return o & 1; }
auto maybeLess(Order o) { return o & 2; }
auto maybeGreater(Order o) { return o & 4; }

struct ValueRange {
    double lowerBound;
    double upperBound;
    template <typename T> ValueRange(T x) : lowerBound(x), upperBound(x) {}
    template <typename T> ValueRange(T l, T u) : lowerBound(l), upperBound(u) {}
    ValueRange(const ValueRange &x)
        : lowerBound(x.lowerBound), upperBound(x.upperBound) {}
    ValueRange &operator=(const ValueRange &x) = default;
    bool isKnown() const { return lowerBound == upperBound; }
    bool operator>(ValueRange x) const { return lowerBound > x.upperBound; }
    bool operator<(ValueRange x) const { return upperBound < x.lowerBound; }
    bool operator>=(ValueRange x) const { return lowerBound >= x.upperBound; }
    bool operator<=(ValueRange x) const { return upperBound <= x.lowerBound; }
    bool operator==(ValueRange x) const {
        return (isKnown() & x.isKnown()) & (lowerBound == x.lowerBound);
    }
    Order compare(ValueRange x) const {
        // return upperBound < x.lowerBound;
        if (isKnown() & x.isKnown()) {
            return upperBound == x.upperBound ? EqualTo : NotEqual;
        }
        if (upperBound < x.lowerBound) {
            return LessThan;
        } else if (upperBound == x.lowerBound) {
            return LessOrEqual;
        } else if (lowerBound > x.upperBound) {
            return GreaterThan;
        } else if (lowerBound == x.upperBound) {
            return GreaterOrEqual;
        } else {
            return UnknownOrder;
        }
    }
    Order compare(intptr_t x) const { return compare(ValueRange{x, x}); }
    ValueRange &operator+=(ValueRange x) {
        lowerBound += x.lowerBound;
        upperBound += x.upperBound;
        return *this;
    }
    ValueRange &operator-=(ValueRange x) {
        lowerBound -= x.upperBound;
        upperBound -= x.lowerBound;
        return *this;
    }
    ValueRange &operator*=(ValueRange x) {
        intptr_t a = lowerBound * x.lowerBound;
        intptr_t b = lowerBound * x.upperBound;
        intptr_t c = upperBound * x.lowerBound;
        intptr_t d = upperBound * x.upperBound;
        lowerBound = std::min(std::min(a, b), std::min(c, d));
        upperBound = std::max(std::max(a, b), std::max(c, d));
        return *this;
    }
    ValueRange operator+(ValueRange x) const {
        ValueRange y(*this);
        return y += x;
    }
    ValueRange operator-(ValueRange x) const {
        ValueRange y(*this);
        return y -= x;
    }
    ValueRange operator*(ValueRange x) const {
        ValueRange y(*this);
        return y *= x;
    }
    ValueRange negate() const { return ValueRange{-upperBound, -lowerBound}; }
    void negate() {
        double lb = -upperBound;
        upperBound = -lowerBound;
        lowerBound = lb;
    }
};
/*
std::intptr_t addWithOverflow(intptr_t x, intptr_t y) {
    intptr_t z;
    if (__builtin_add_overflow(x, y, &z)) {
        z = std::numeric_limits<intptr_t>::max();
    }
    return z;
}
std::intptr_t subWithOverflow(intptr_t x, intptr_t y) {
    intptr_t z;
    if (__builtin_sub_overflow(x, y, &z)) {
        z = std::numeric_limits<intptr_t>::min();
    }
    return z;
}
std::intptr_t mulWithOverflow(intptr_t x, intptr_t y) {
    intptr_t z;
    if (__builtin_mul_overflow(x, y, &z)) {
        if ((x > 0) ^ (y > 0)) { // wouldn't overflow if `x` or `y` were `0`
            z = std::numeric_limits<intptr_t>::min(); // opposite sign
        } else {
            z = std::numeric_limits<intptr_t>::max(); // same sign
        }
    }
    return z;
}

struct ValueRange {
    intptr_t lowerBound;
    intptr_t upperBound;
    ValueRange(intptr_t x) : lowerBound(x), upperBound(x) {}
    ValueRange(intptr_t l, intptr_t u) : lowerBound(l), upperBound(u) {}
    ValueRange(const ValueRange &x)
        : lowerBound(x.lowerBound), upperBound(x.upperBound) {}
    ValueRange &operator=(const ValueRange &x) = default;
    bool isKnown() { return lowerBound == upperBound; }
    bool operator<(ValueRange x) { return upperBound < x.lowerBound; }
    Order compare(ValueRange x) {
        // return upperBound < x.lowerBound;
        if (isKnown() & x.isKnown()) {
            return upperBound == x.upperBound ? EqualTo : NotEqual;
        }
        if (upperBound < x.lowerBound) {
            return LessThan;
        } else if (upperBound == x.lowerBound) {
            return LessOrEqual;
        } else if (lowerBound > x.upperBound) {
            return GreaterThan;
        } else if (lowerBound == x.upperBound) {
            return GreaterOrEqual;
        } else {
            return UnknownOrder;
        }
    }
    Order compare(intptr_t x) { return compare(ValueRange{x, x}); }
    ValueRange &operator+=(ValueRange x) {
        lowerBound = addWithOverflow(lowerBound, x.lowerBound);
        upperBound = addWithOverflow(upperBound, x.upperBound);
        return *this;
    }
    ValueRange &operator-=(ValueRange x) {
        lowerBound = subWithOverflow(lowerBound, x.upperBound);
        upperBound = subWithOverflow(upperBound, x.lowerBound);
        return *this;
    }
    ValueRange &operator*=(ValueRange x) {
        intptr_t a = mulWithOverflow(lowerBound, x.lowerBound);
        intptr_t b = mulWithOverflow(lowerBound, x.upperBound);
        intptr_t c = mulWithOverflow(upperBound, x.lowerBound);
        intptr_t d = mulWithOverflow(upperBound, x.upperBound);
        lowerBound = std::min(std::min(a, b), std::min(c, d));
        upperBound = std::max(std::max(a, b), std::max(c, d));
        return *this;
    }
    ValueRange operator+(ValueRange x) {
        ValueRange y(*this);
        return y += x;
    }
    ValueRange operator-(ValueRange x) {
        ValueRange y(*this);
        return y -= x;
    }
    ValueRange operator*(ValueRange x) {
        ValueRange y(*this);
        return y *= x;
    }
};
*/

// Univariate Polynomial for multivariate gcd
/*
struct UniPoly{
    struct Monomial{
        size_t power;
    };
    struct Term{
        Polynomial coefficient;
        Monomial monomial;
    };
    std::vector<Term> terms;
    inline auto begin() { return terms.begin(); }
    inline auto end() { return terms.end(); }
    inline auto begin() const { return terms.begin(); }
    inline auto end() const { return terms.end(); }
    inline auto cbegin() const { return terms.cbegin(); }
    inline auto cend() const { return terms.cend(); }
    // Polynomial(std::tuple<Term, Term> &&x) : terms({std::get<0>(x),
    // std::get<1>(x)}) {}
    template<typename I> void erase(I it){ terms.erase(it); }
    template<typename I, typename T> void insert(I it, T &&x){ terms.insert(it,
std::forward<T>(x)); }

    template <typename S> void add_term(S &&x) {
        ::add_term(*this, std::forward<S>(x));
    }
    template <typename S> void sub_term(S &&x) {
        ::sub_term(*this, std::forward<S>(x));
    }
    UniPoly(Polynomial const &x, size_t u) {
        for (auto it = x.cbegin(); it != x.cend(); ++it){
            it -> degree(u);
        }
    }
};
*/
