#pragma once
#include "math.hpp"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <math.h>
#include <ostream>
#include <string>
#include <tuple>
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
    return std::move(y);
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
    bool isZero() const { return numerator == 0; }
    bool isOne() const { return (numerator == denominator); }
    bool isInteger() const { return denominator == 1; }
    void negate() { ::negate(numerator); }
};

template <typename T> bool isZero(T x) { return x.isZero(); }
bool isZero(intptr_t x) { return x == 0; }
bool isZero(size_t x) { return x == 0; }

template <typename T> bool isOne(T x) { return x.isOne(); }
bool isOne(intptr_t x) { return x == 1; }
bool isOne(size_t x) { return x == 1; }

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

namespace Polynomial {
struct Uninomial {
    uint_fast32_t exponent;
    Uninomial(One) : exponent(0){};
    Uninomial() = default;
    Uninomial(uint_fast32_t e) : exponent(e){};
    uint_fast32_t degree() const { return exponent; }
    bool termsMatch(Uninomial const &y) const { return exponent == y.exponent; }
    bool lexGreater(Uninomial const &y) const { return exponent > y.exponent; }
    template <typename T> bool lexGreater(T const &x) const {
        return lexGreater(x.monomial());
    }
    std::pair<Uninomial, bool> operator/(Uninomial const &y) {
        return std::make_pair(Uninomial{degree() - y.degree()},
                              degree() < y.degree());
    }
    Uninomial &operator^=(size_t i) {
        exponent *= i;
        return *this;
    }
    Uninomial operator^(size_t i) { return Uninomial{exponent * i}; }

    bool isOne() const { return exponent == 0; }
    bool isZero() const { return false; }

    bool operator==(Uninomial x) const { return x.exponent == exponent; }
    Uninomial operator*(Uninomial x) const {
        return Uninomial{exponent + x.exponent};
    }
    Uninomial &operator*=(Uninomial x) {
        exponent += x.exponent;
        return *this;
    }
    // Uninomial& operator=(Uninomial x) {
    //     exponent = x.exponent;
    //     return *this;
    // }
};
struct Monomial {
    // sorted symbolic terms being multiplied
    std::vector<uint_fast32_t> prodIDs;
    // Monomial& operator=(Monomial const &x){
    //     prodIDs = x.prodIDs;
    //     return *this;
    // }
    // constructors
    Monomial() : prodIDs(std::vector<size_t>()){};
    Monomial(std::vector<size_t> &x) : prodIDs(x){};
    Monomial(std::vector<size_t> &&x) : prodIDs(std::move(x)){};
    // Monomial(Monomial const &x) : prodIDs(x.prodIDs) {};
    Monomial(One) : prodIDs(std::vector<size_t>()){};
    Monomial(size_t x) : prodIDs(std::vector<size_t>{x}){};

    inline auto begin() { return prodIDs.begin(); }
    inline auto end() { return prodIDs.end(); }
    inline auto begin() const { return prodIDs.begin(); }
    inline auto end() const { return prodIDs.end(); }
    inline auto cbegin() const { return prodIDs.cbegin(); }
    inline auto cend() const { return prodIDs.cend(); }

    void add_term(uint_fast32_t x) {
        for (auto it = begin(); it != end(); ++it) {
            if (x <= *it) {
                prodIDs.insert(it, x);
                return;
            }
        }
        prodIDs.push_back(x);
    }
    void add_term(uint_fast32_t x, size_t count) {
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
    Monomial operator*(Monomial const &x) const {
        Monomial r;
        // prodIDs are sorted, so we can create sorted product in O(N)
        size_t i = 0;
        size_t j = 0;
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        r.prodIDs.reserve(n0 + n1);
        for (size_t k = 0; k < (n0 + n1); ++k) {
            uint_fast32_t a = (i < n0)
                                  ? prodIDs[i]
                                  : std::numeric_limits<uint_fast32_t>::max();
            uint_fast32_t b = (j < n1)
                                  ? x.prodIDs[j]
                                  : std::numeric_limits<uint_fast32_t>::max();
            bool aSmaller = a < b;
            aSmaller ? ++i : ++j;
            r.prodIDs.push_back(aSmaller ? a : b);
        }
        return r;
    }
    Monomial &operator*=(Monomial const &x) {
        // optimize the length 0 and 1 cases.
        if (x.prodIDs.size() == 0) {
            return *this;
        } else if (x.prodIDs.size() == 1) {
            uint_fast32_t y = x.prodIDs[0];
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
        return x;
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
            uint_fast32_t a = (i < n0)
                                  ? prodIDs[i]
                                  : std::numeric_limits<uint_fast32_t>::max();
            uint_fast32_t b = (j < n1)
                                  ? x.prodIDs[j]
                                  : std::numeric_limits<uint_fast32_t>::max();
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
    std::pair<Monomial, bool> operator/(Monomial const &x) const {
        Monomial n;
        size_t i = 0;
        size_t j = 0;
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        while ((i + j) < (n0 + n1)) {
            uint_fast32_t a = (i < n0)
                                  ? prodIDs[i]
                                  : std::numeric_limits<uint_fast32_t>::max();
            uint_fast32_t b = (j < n1)
                                  ? x.prodIDs[j]
                                  : std::numeric_limits<uint_fast32_t>::max();
            if (a < b) {
                n.prodIDs.push_back(a);
                ++i;
            } else if (a == b) {
                ++i;
                ++j;
            } else {
                return std::make_pair(n, true);
            }
        }
        return std::make_pair(n, false);
    }
    bool isOne() const { return (prodIDs.size() == 0); }
    bool isZero() const { return false; }
    bool isCompileTimeConstant() const { return prodIDs.size() == 0; }
    size_t degree() const { return prodIDs.size(); }
    uint_fast32_t degree(uint_fast32_t i) const {
        uint_fast32_t d = 0;
        for (auto it = cbegin(); it != cend(); ++it) {
            d += (*it) == i;
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
            uint_fast32_t a = prodIDs[i];
            uint_fast32_t b = x.prodIDs[i];
            if (a != b) {
                return a < b;
            }
        }
        return false;
    }
    template <typename T> bool lexGreater(T const &x) const {
        return lexGreater(x.monomial());
    }
    Monomial operator^(size_t i) { return powBySquare(*this, i); }
    Monomial operator^(size_t i) const { return powBySquare(*this, i); }
};

template <typename C, typename M> struct Term {
    C coefficient;
    M exponent;
    // Term() = default;
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
    uint_fast32_t degree() const { return exponent.degree(); }
    M &monomial() { return exponent; }
    const M &monomial() const { return exponent; }
    bool addCoef(C const &coef) { return ::isZero((coefficient += coef)); }
    bool subCoef(C const &coef) { return ::isZero((coefficient -= coef)); }
    bool addCoef(C &&coef) {
        return ::isZero((coefficient += std::move(coef)));
    }
    bool subCoef(C &&coef) {
        return ::isZero((coefficient -= std::move(coef)));
    }
    bool addCoef(Term const &t) { return addCoef(t.coefficient); }
    bool subCoef(Term const &t) { return subCoef(t.coefficient); }
    bool addCoef(M const &) { return ::isZero((coefficient += 1)); }
    bool subCoef(M const &) { return ::isZero((coefficient -= 1)); }
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
    bool isZero() const { return ::isZero(coefficient); }
    bool isOne() const { return ::isOne(coefficient) & ::isOne(exponent); }

    template <typename CC> operator Term<CC, M>() {
        return Term<CC, M>(CC(coefficient), exponent);
    }

    bool isCompileTimeConstant() const { return ::isOne(exponent); }
    bool operator==(Term<C, M> const &y) const {
        return (exponent == y.exponent) && (coefficient == y.coefficient);
    }
    bool operator!=(Term<C, M> const &y) const {
        return (exponent != y.exponent) || (coefficient != y.coefficient);
    }
};
// template <typename C,typename M>
// bool Term<C,M>::isOne() const { return ::isOne(coefficient) &
// ::isOne(exponent); }

template <typename C, typename M> struct Terms {

    std::vector<Term<C, M>> terms;
    Terms() = default;
    Terms(Term<C, M> const &x) : terms({x}){};
    Terms(Term<C, M> &&x) : terms({std::move(x)}){};
    Terms(Term<C, M> const &x, Term<C, M> const &y) : terms({x, y}){};
    Terms(M &m0, M &m1) : terms({m0, m1}){};
    Terms(M &&m0, M &&m1) : terms({std::move(m0), std::move(m1)}){};
    Terms(M const &x, Term<C, M> const &y) : terms({x, y}){};
    Terms(Term<C, M> const &x, M const &y) : terms({x, y}){};
    auto begin() { return terms.begin(); }
    auto end() { return terms.end(); }
    auto begin() const { return terms.begin(); }
    auto end() const { return terms.end(); }
    auto cbegin() const { return terms.cbegin(); }
    auto cend() const { return terms.cend(); }

    template <typename S> void add_term(S &&x) {
        ::add_term(*this, std::forward<S>(x));
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
    Terms<C, M> &operator+=(M const &x) {
        add_term(x);
        return *this;
    }
    Terms<C, M> &operator+=(M &&x) {
        add_term(std::move(x));
        return *this;
    }
    Terms<C, M> &operator-=(M const &x) {
        sub_term(x);
        return *this;
    }
    Terms<C, M> &operator-=(M &&x) {
        sub_term(std::move(x));
        return *this;
    }
    Terms<C, M> &operator*=(Term<C, M> const &x) {
        if (x.isZero()) {
            terms.clear();
            return *this;
        } else if (x.isOne()) {
            return *this;
        }
        for (auto it = begin(); it != end(); ++it) {
            (*it) *= x; // add term for remainder
        }
        return *this;
    }
    Terms<C, M> &operator*=(M const &x) {
        if (x.isOne()) {
            return *this;
        }
        for (auto it = begin(); it != end(); ++it) {
            (*it) *= x; // add term for remainder
        }
        return *this;
    }
    Terms<C, M> &operator+=(Terms<C, M> const &x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            add_term(*it);
        }
        return *this;
    }
    Terms<C, M> &operator+=(Terms<C, M> &&x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            add_term(std::move(*it));
        }
        return *this;
    }
    Terms<C, M> &operator-=(Terms<C, M> const &x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            sub_term(*it);
        }
        return *this;
    }
    Terms<C, M> &operator-=(Terms<C, M> &&x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            sub_term(std::move(*it));
        }
        return *this;
    }

    Terms<C, M> operator*(Terms<C, M> const &x) const {
        Terms<C, M> p;
        p.terms.reserve(x.terms.size() * terms.size());
        for (auto it = cbegin(); it != cend(); ++it) {
            for (auto itx = x.cbegin(); itx != x.cend(); ++itx) {
                p.add_term((*it) * (*itx));
            }
        }
        return p;
    }
    Terms<C, M> &operator*=(Terms<C, M> const &x) {
        // terms = ((*this) * x).terms;
        // return *this;
        if (x.isZero()) {
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
        for (auto it = cbegin(); it != cend(); ++it) {
            s.terms.push_back(*it); // copy initial batch
        }
        return s;
    }

    void negate() {
        for (auto it = begin(); it != end(); ++it) {
            it->negate();
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

    bool isZero() const { return terms.size() == 0; }
    bool isOne() const { return (terms.size() == 1) && terms[0].isOne(); }

    Terms<C, M> operator^(size_t i) const { return powBySquare(*this, i); }

    size_t degree() const {
        if (terms.size()) {
            return leadingTerm().degree();
        } else {
            return 0;
        }
    }
};

template <typename C> using UnivariateTerm = Term<C, Uninomial>;
template <typename C> using MultivariateTerm = Term<C, Monomial>;
template <typename C> using Univariate = Terms<C, Uninomial>;
template <typename C> using Multivariate = Terms<C, Monomial>;

Terms<intptr_t, Uninomial> operator+(Uninomial x, Uninomial y) {
    if (x.termsMatch(y)) {
        return Terms<intptr_t, Uninomial>(Term<intptr_t, Uninomial>(2, x));
    } else if (x.lexGreater(y)) {
        return Terms<intptr_t, Uninomial>(x, y);
    } else {
        return Terms<intptr_t, Uninomial>(y, x);
    }
    Terms<intptr_t, Uninomial> z(x);
    return z += y;
}
Terms<intptr_t, Monomial> operator+(Monomial const &x, Monomial const &y) {
    Terms<intptr_t, Monomial> z(x);
    return z += y;
}
Terms<intptr_t, Monomial> operator+(Monomial const &x, Monomial &&y) {
    Terms<intptr_t, Monomial> z(std::move(y));
    return z += x;
}
Terms<intptr_t, Monomial> operator+(Monomial &&x, Monomial const &y) {
    Terms<intptr_t, Monomial> z(std::move(x));
    return z += y;
}
Terms<intptr_t, Monomial> operator+(Monomial &&x, Monomial &&y) {
    Terms<intptr_t, Monomial> z(std::move(x));
    return z += std::move(y);
}

template <typename C> Terms<C, Uninomial> operator+(Uninomial x, C y) {
    return Term<C, Uninomial>{C(One()), x} + Term<C, Uninomial>{y};
}
template <typename C> Terms<C, Uninomial> operator+(C y, Uninomial x) {
    return Term<C, Uninomial>{C(One()), x} + Term<C, Uninomial>{y};
}

template <typename C>
Terms<C, Uninomial> operator+(Term<C, Uninomial> const &x, Uninomial y) {
    Terms<C, Uninomial> z(x);
    return z += y;
}
template <typename C>
Terms<C, Uninomial> operator+(Term<C, Uninomial> &&x, Uninomial y) {
    Terms<C, Uninomial> z(std::move(x));
    return z += y;
}
template <typename C>
Terms<C, Uninomial> operator-(Term<C, Uninomial> &&x, Uninomial y) {
    Terms<C, Uninomial> z(std::move(x));
    return std::move(z -= Term{1, y});
}
template <typename C>
Terms<C, Monomial> operator+(Term<C, Monomial> const &x, Monomial const &y) {
    Terms<C, Monomial> z(x);
    return z += y;
}
template <typename C>
Terms<intptr_t, Monomial> operator+(Term<C, Monomial> const &x, Monomial &&y) {
    Terms<C, Monomial> z(std::move(y));
    return z += x;
}
template <typename C>
Terms<intptr_t, Monomial> operator+(Term<C, Monomial> &&x, Monomial const &y) {
    Terms<C, Monomial> z(std::move(x));
    return z += y;
}
template <typename C>
Terms<intptr_t, Monomial> operator+(Term<C, Monomial> &&x, Monomial &&y) {
    Terms<C, Monomial> z(std::move(x));
    return z += std::move(y);
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
    return z += x;
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Term<C, M> const &y) {
    Terms<C, M> z(std::move(x));
    return z += y;
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Term<C, M> &&y) {
    Terms<C, M> z(std::move(x));
    return z += std::move(y);
}

template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z(x);
    return std::move(z -= std::move(y));
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Term<C, M> const &y) {
    Terms<C, M> z(std::move(x));
    return std::move(z -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Term<C, M> &&y) {
    Terms<C, M> z(std::move(x));
    return std::move(z -= std::move(y));
}

template <typename C, typename M>
Term<C, M> operator*(Term<C, M> const &x, Term<C, M> const &y) {
    Term<C, M> z(x);
    return z *= y;
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
    return std::move(z -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, intptr_t y) {
    return std::move(x -= y);
}

template <typename C, typename M>
Terms<C, M> operator+(intptr_t x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += x);
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
    return std::move(z);
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
    return z += y;
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, C &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z += std::move(y));
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
    return std::move(z -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, C &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z -= std::move(y));
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, C const &y) {
    return std::move(x -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> &&x, C &&y) {
    return std::move(x -= std::move(y));
}
Terms<intptr_t, Monomial> operator-(size_t x, Monomial &y) {
    Terms<intptr_t, Monomial> z;
    z.terms.reserve(2);
    z.terms.emplace_back(-1, y);
    z.terms.push_back(x);
    return z; // no std::move because of copy elision
}

template <typename C, typename M>
Terms<C, M> operator+(C const &x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += x);
}
template <typename C, typename M>
Terms<C, M> operator+(C const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(C &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += std::move(x));
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
    return std::move(z);
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
    return std::move(z);
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
    return std::move(z += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, M &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z += std::move(y));
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
    return std::move(z -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, M &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z -= std::move(y));
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
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += x);
}
template <typename C, typename M>
Terms<C, M> operator+(M const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(M &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += std::move(x));
}
template <typename C, typename M>
Terms<C, M> operator+(M &&x, Terms<C, M> &&y) {
    return std::move(y += std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator-(M const &x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= x;
    z.negate();
    return std::move(z);
}
template <typename C, typename M>
Terms<C, M> operator-(M const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(M &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= std::move(x);
    z.negate();
    return std::move(z);
}
template <typename C, typename M>
Terms<C, M> operator-(M &&x, Terms<C, M> &&y) {
    y -= std::move(x);
    y.negate();
    return std::move(y);
}

template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Term<C, M> const &y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z += y);
}
template <typename C, typename M>
Terms<C, M> operator+(Terms<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z += std::move(y));
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
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z -= y);
}
template <typename C, typename M>
Terms<C, M> operator-(Terms<C, M> const &x, Term<C, M> &&y) {
    Terms<C, M> z = x.largerCapacityCopy(1);
    return std::move(z -= std::move(y));
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
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += x);
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> const &x, Terms<C, M> &&y) {
    return std::move(y += x);
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    return std::move(z += std::move(x));
}
template <typename C, typename M>
Terms<C, M> operator+(Term<C, M> &&x, Terms<C, M> &&y) {
    return std::move(y += std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= x;
    z.negate();
    return std::move(z);
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> const &x, Terms<C, M> &&y) {
    y -= x;
    y.negate();
    return std::move(y);
}
template <typename C, typename M>
Terms<C, M> operator-(Term<C, M> &&x, Terms<C, M> const &y) {
    Terms<C, M> z = y.largerCapacityCopy(1);
    z -= std::move(x);
    z.negate();
    return std::move(z);
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
    Terms<C, M> z = x.largerCapacityCopy(y.size());
    return std::move(z += y);
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
    Terms<C, M> z = x.largerCapacityCopy(y.size());
    return std::move(z -= y);
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

template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> const &x, Terms<C, M> &&y) {
    return std::move(y *= x);
}
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &&x, Terms<C, M> const &y) {
    return std::move(x *= y);
}
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &&x, Terms<C, M> &&y) {
    // return x * y;
    // Terms<C,M> z(x);
    // Terms<C,M> a(y);
    // z *= a;
    // return z;
    return std::move(x *= std::move(y));
}
template <typename C, typename M>
Terms<C, M> operator*(Terms<C, M> &x, M const &y) {
    Terms<C, M> z(x);
    return std::move(z *= y);
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

template <typename C> Univariate<C> &divExact(Univariate<C> &d, C const &x) {
    for (auto it = d.begin(); it != d.end(); ++it) {
        divExact(it->coefficient, x);
    }
    return d;
}

template <typename C>
std::pair<Multivariate<C>, Multivariate<C>>
divRemBang(Multivariate<C> &p, Multivariate<C> const &d) {
    Multivariate<C> q;
    Multivariate<C> r;
    while (!p.terms.empty()) {
        auto [nx, fail] = p.leadingTerm() / d.leadingTerm();
        if (fail) {
            r.add_term(std::move(p.leadingTerm()));
            p.removeLeadingTerm();
            // r.takeLeadingTerm(p);
        } else {
            p -= d * nx;
            q += std::move(nx);
        }
    }
    std::swap(q, p);
    return std::make_pair(p, std::move(r));
}
template <typename C>
std::pair<Multivariate<C>, Multivariate<C>> divRem(Multivariate<C> const &n,
                                                   Multivariate<C> const &d) {
    Multivariate<C> p(n);
    return divRemBang(p, d);
}

template <typename C>
Multivariate<C> &divExact(Multivariate<C> &x, Multivariate<C> const &y) {
    auto [d, r] = divRemBang(x, y);
    assert(isZero(r));
    return d;
}

template <typename C>
Term<C, Uninomial> operator*(Term<C, Uninomial> const &x,
                             Term<C, Uninomial> const &y) {
    return Term<C, Uninomial>{x.coefficient * y.coefficient,
                              x.degree() + y.degree()};
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
std::pair<Term<C, M>, bool> operator/(Term<C, M> &x, Term<C, M> const &y) {
    auto [u, f] = x.exponent / y.exponent;
    return std::make_pair(Term{x.coefficient / y.coefficient, u}, f);
}
template <typename C>
Term<C, Uninomial> &operator^=(Term<C, Uninomial> &x, size_t i) {
    x.coefficient = powBySquare(x.coefficient, i);
    x.exponent ^= i;
    return x;
}
template <typename C>
Term<C, Monomial> &operator^=(Term<C, Uninomial> &x, size_t i) {
    x.coefficient = powBySquare(x.coefficient, i);
    x.exponent = x.exponent ^ i;
    return x;
}

template <typename C, typename M>
Term<C, Uninomial> operator^(Term<C, M> &x, size_t i) {
    Term t(x);
    return t ^= i;
}

// template <typename C>
// std::pair<Term<C,Monomial>, bool> operator/(Term<C,Monomial> &x,
// Term<C,Monomial> const &y) { 	auto [u, f] = x.exponent / y.exponent;
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

template <typename C>
Multivariate<C> operator*(intptr_t x, Multivariate<C> &c) {
    Multivariate<C> p(c);
    p *= x;
    return std::move(p);
}
template <typename C>
Multivariate<C> operator*(Multivariate<C> &c, intptr_t x) {
    Multivariate<C> p(c);
    p *= x;
    return std::move(p);
}
template <typename C>
Multivariate<C> operator*(intptr_t x, Multivariate<C> &&c) {
    c *= x;
    return std::move(c);
}
template <typename C>
Multivariate<C> operator*(Multivariate<C> &&c, intptr_t x) {
    c *= x;
    return std::move(c);
}

template <typename C>
Term<Polynomial::Multivariate<C>, Uninomial>
operator*(Uninomial const &x, Polynomial::Multivariate<C> &c) {
    return Term<Polynomial::Multivariate<C>, Uninomial>{c, x};
}
template <typename C>
Term<Polynomial::Multivariate<C>, Uninomial>
operator*(Polynomial::Multivariate<C> &c, Uninomial const &x) {
    return Term<Polynomial::Multivariate<C>, Uninomial>{c, x};
}
template <typename C>
Term<Polynomial::Multivariate<C>, Uninomial>
operator*(Uninomial const &x, Polynomial::Multivariate<C> &&c) {
    return Term<Polynomial::Multivariate<C>, Uninomial>{std::move(c), x};
}
template <typename C>
Term<Polynomial::Multivariate<C>, Uninomial>
operator*(Polynomial::Multivariate<C> &&c, Uninomial const &x) {
    return Term<Polynomial::Multivariate<C>, Uninomial>{std::move(c), x};
}

Term<intptr_t, Monomial> operator*(Monomial &x, intptr_t c) {
    return Term<intptr_t, Monomial>(c, x);
}
Term<intptr_t, Monomial> operator*(intptr_t c, Monomial &x) {
    return Term<intptr_t, Monomial>(c, x);
}
Term<intptr_t, Monomial> operator*(Monomial &&x, intptr_t c) {
    return Term<intptr_t, Monomial>(c, std::move(x));
}
Term<intptr_t, Monomial> operator*(intptr_t c, Monomial &&x) {
    return Term<intptr_t, Monomial>(c, std::move(x));
}

Term<Rational, Monomial> operator*(Monomial &x, Rational c) {
    return Term<Rational, Monomial>(c, x);
}
Term<Rational, Monomial> operator*(Rational c, Monomial &x) {
    return Term<Rational, Monomial>(c, x);
}
Term<Rational, Monomial> operator*(Monomial &&x, Rational c) {
    return Term<Rational, Monomial>(c, std::move(x));
}
Term<Rational, Monomial> operator*(Rational c, Monomial &&x) {
    return Term<Rational, Monomial>(c, std::move(x));
}

template <typename C, typename M>
Terms<C, M> operator*=(Terms<C, M> &x, C const &y) {
    for (auto it = x.begin(); it != x.end(); ++it) {
        (*it) *= y;
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
template <typename C>
Univariate<C> pseudorem(Univariate<C> &p, Univariate<C> &d) {
    if (p.degree() < d.degree()) {
        return p;
    }
    uint_fast32_t k = 1 + p.degree() - d.degree();
    C l = d.leadingCoefficient();
    Univariate<C> dd(d);
    Univariate<C> pp(p);
    while ((!p.isZero()) && (pp.degree() >= d.degree())) {
        mulPow(dd, d,
               Term<C, Uninomial>(pp.leadingCoefficient(),
                                  pp.degree() - d.degree()));
        pp *= l;
        pp -= dd;
        // pp = pp * l - dd;
        k -= 1;
    }
    return powBySquare(l, k) * std::move(pp);
}

template <typename C> C content(Univariate<C> const &a) {
    switch (a.terms.size()) {
    case 0:
        return C(1);
    case 1:
        return a.terms[0].coefficient;
    default:
        break;
    }
    C g = gcd(a.terms[0].coefficient, a.terms[1].coefficient);
    for (size_t i = 2; i < a.terms.size(); ++i) {
        g = gcd(g, a.terms[i].coefficient);
    }
    return std::move(g);
}
template <typename C> Univariate<C> primPart(Univariate<C> const &p) {
    Univariate<C> d(p);
    divExact(d, content(p));
    return std::move(d);
}
template <typename C>
std::pair<C, Univariate<C>> contPrim(Univariate<C> const &p) {
    C c = content(p);
    Univariate<C> d(p);
    divExact(d, c);
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
    }
    auto [c1, xx] = contPrim(x);
    auto [c2, yy] = contPrim(y);
    C c = gcd(c1, c2);
    C g(1);
    C h(1);
    // TODO: allocate less memory; can you avoid the copies?
    while (true) {
        Univariate<C> r = pseudorem(xx, yy);
        if (isZero(r)) {
            break;
        }
        if (r.degree() == 0) {
            return Univariate<C>(c);
        }
        uint_fast32_t d = xx.degree() - yy.degree();
        divExact(r, g * (h ^ d)); // defines new y
        std::swap(xx, yy);
        std::swap(yy, r);
        g = x.leadingCoefficient();
        if (d > 1) {
            C htemp = h ^ (d - 1);
            h = g ^ d;
            divExact(h, htemp);
        } else {
            h = (h ^ (1 - d)) * (g ^ d);
        }
    }
    return c * primPart(y);
}

/*
// The basic symbol type represents a symbol as a product of some number of
// known IDs as well as with a constant term.
// `5` would thus be an empty `prodIDs` vector and `coef = 5`.
// While if `M, N, K = size(A)`, the strides would be `prodIDs = [], coef = 1`,
// `prodIDs = [M], coef = 1`, and `prodIDs = [M,N], coef = 1`, respectively.
// This lets us tell that each is a multiple of the others.
// Can we assume `prodIDs` are greater than 0?
template <typename C> struct Polynomial {
struct Term {
    C coefficient;
    Monomial monomial;
    Term() = default;
    // template <typename T> Term(T coef) : coefficient(coef) {}
    // Term(intptr_t coef) : coefficient(coef) {}
    template <typename S> Term(S coef) : coefficient(coef) {}
    Term(C coef, Monomial monomial)
        : coefficient(coef), monomial(monomial) {}
    Term &negate() {
        coefficient *= -1;
        return *this;
    }
    bool termsMatch(Term const &x) const {
        return monomial.prodIDs == x.monomial.prodIDs;
    }
    bool addCoef(C coef) { return ::isZero((coefficient += coef)); }
    bool subCoef(C coef) { return ::isZero((coefficient -= coef)); }
    bool addCoef(Term const &t) { return addCoef(t.coefficient); }
    bool subCoef(Term const &t) { return subCoef(t.coefficient); }

    bool lexGreater(Term const &t) const {
        return monomial.lexGreater(t.monomial);
    }
    bool isZero() const { return ::isZero(coefficient); }
    bool isOne() const { return ::isOne(coefficient) & ::isOne(monomial); }

    Term &operator*=(Term const &x) {
        coefficient *= x.coefficient;
        monomial *= x.monomial;
        return *this;
    }
    Term operator*(Term const &x) const {
        Term y(*this);
        return std::move(y *= x);
    }
    std::pair<Term, intptr_t> operator/(Term const &x) const {
        auto [m, s] = monomial / x.monomial;
        return std::make_pair(Term{coefficient / x.coefficient, m}, s);
    }
    bool isInteger() const { return coefficient.isInteger(); }

    template <typename T> Polynomial operator+(T &&y) const {
        if (termsMatch(y)) {
            if (coefficient == y.coefficient.negate()) {
                return Polynomial(Term(0));
            } else {
                Term t(std::forward<T>(y));
                t.coefficient += coefficient;
                return Polynomial(std::move(t));
            }
        }
        // x.lexGreater(*it)
        if (this->lexGreater(y)) {
            return Polynomial(*this, std::forward<T>(y));
        } else {
            return Polynomial(std::forward<T>(y), *this);
        }
    }
    template <typename T> Polynomial operator-(T &&y) const {
        if (termsMatch(y)) {
            if (coefficient == y.coefficient) {
                return Polynomial(Term(0));
            } else {
                Term t(std::forward<T>(y));
                t.coefficient = coefficient - t.coefficient;
                return Polynomial(std::move(t));
            }
        }
        Term t(std::forward<T>(y));
        t.negate();
        if (this->lexGreater(y)) {
            return Polynomial(*this, std::forward<T>(t));
        } else {
            return Polynomial(std::forward<T>(t), *this);
        }
    }
    bool isCompileTimeConstant() const {
        return monomial.isCompileTimeConstant();
    }
    auto begin() { return monomial.begin(); }
    auto end() { return monomial.end(); }
    auto begin() const { return monomial.begin(); }
    auto end() const { return monomial.end(); }
    auto cbegin() const { return monomial.cbegin(); }
    auto cend() const { return monomial.cend(); }

    bool operator==(Term const &x) const {
        return (coefficient == x.coefficient) && (monomial == x.monomial);
    }
    bool operator!=(Term const &x) const {
        return (coefficient != x.coefficient) || (monomial != x.monomial);
    }

    uint_fast32_t degree() const { return monomial.degree(); }
    uint_fast32_t degree(uint_fast32_t u) const {
        return monomial.degree(u);
    }
};

std::vector<Term> terms;
Polynomial<C>() = default;
Polynomial<C>(Term &x) : terms({x}){};
Polynomial<C>(Term &&x) : terms({std::move(x)}){};
Polynomial<C>(Term &t0, Term &t1) : terms({t0, t1}){};
// Polynomial(Term &&t0, Term &&t1) : terms({std::move(t0),
// std::move(t1)})
// {};
// Polynomial(std::vector<Term> t) : terms(t){};
Polynomial<C>(std::vector<Term> const &t) : terms(t){};
Polynomial<C>(std::vector<Term> &&t) : terms(std::move(t)){};

inline auto begin() { return terms.begin(); }
inline auto end() { return terms.end(); }
inline auto begin() const { return terms.begin(); }
inline auto end() const { return terms.end(); }
inline auto cbegin() const { return terms.cbegin(); }
inline auto cend() const { return terms.cend(); }
// Polynomial(std::tuple<Term, Term> &&x) : terms({std::get<0>(x),
// std::get<1>(x)}) {}
template <typename I> void erase(I it) { terms.erase(it); }
template <typename I, typename T> void insert(I it, T &&x) {
    terms.insert(it, std::forward<T>(x));
}
template <typename T> void push_back(T &&x) {
    terms.push_back(std::forward<T>(x));
}

template <typename S> void add_term(S &&x) {
    ::add_term(*this, std::forward<S>(x));
}
template <typename S> void sub_term(S &&x) {
    ::sub_term(*this, std::forward<S>(x));
}
Polynomial &operator+=(Term const &x) {
    add_term(x);
    return *this;
}
Polynomial &operator+=(Term &&x) {
    add_term(std::move(x));
    return *this;
}
Polynomial &operator-=(Term const &x) {
    sub_term(x);
    return *this;
}
Polynomial &operator-=(Term &&x) {
    sub_term(std::move(x));
    return *this;
}

bool operator==(Polynomial const &x) const { return (terms == x.terms); }
bool operator!=(Polynomial const &x) const { return (terms != x.terms); }
Polynomial operator*(Term const &x) const {
    Polynomial p(terms);
    for (auto it = p.begin(); it != p.end(); ++it) {
        (*it) *= x;
    }
    return p;
}
Polynomial operator*(Polynomial const &x) const {
    Polynomial p;
    p.terms.reserve(terms.size() * x.terms.size());
    for (auto it = cbegin(); it != cend(); ++it) {
        for (auto itx = x.cbegin(); itx != x.cend(); ++itx) {
            p.add_term((*it) * (*itx));
        }
    }
    return p;
}
Polynomial operator+(Term const &x) const {
    Polynomial y(terms);
    y.add_term(x);
    return y;
}
Polynomial operator-(Term const &x) const {
    Polynomial y(terms);
    y.sub_term(x);
    return y;
}
Polynomial largerCapacityCopy(size_t i) const {
    Polynomial s;
    s.terms.reserve(i + terms.size()); // reserve full size
    for (auto it = cbegin(); it != cend(); ++it) {
        s.terms.push_back(*it); // copy initial batch
    }
    return s;
}
Polynomial operator+(Polynomial const &x) const {
    Polynomial s = largerCapacityCopy(x.terms.size());
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        s.add_term(*it); // add term for remainder
    }
    return s;
}
Polynomial operator-(Polynomial const &x) const {
    Polynomial s = largerCapacityCopy(x.terms.size());
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        s.sub_term(*it);
    }
    return s;
}
Polynomial &operator+=(Polynomial const &x) {
    terms.reserve(terms.size() + x.terms.size());
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        add_term(*it); // add term for remainder
    }
    return *this;
}
Polynomial &operator-=(Polynomial const &x) {
    terms.reserve(terms.size() + x.terms.size());
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        sub_term(*it); // add term for remainder
    }
    return *this;
}
Polynomial &operator*=(Term const &x) {
    if (x.isZero()) {
        terms.clear();
        return *this;
    } else if (x.isOne()) {
        return *this;
    }
    for (auto it = begin(); it != end(); ++it) {
        (*it) *= x; // add term for remainder
    }
    return *this;
}
// bool operator>=(Polynomial x){

// 	return false;
// }
bool isZero() const { return (terms.size() == 0); }
bool isOne() const { return (terms.size() == 1) & terms[0].isOne(); }

bool isCompileTimeConstant() const {
    return (terms.size() == 1) &&
           (terms.begin()->monomial.isCompileTimeConstant());
}

Polynomial &negate() {
    for (auto it = begin(); it != end(); ++it) {
        (*it).negate();
    }
    return *this;
}
// returns a <div, rem> pair

// std::pair<Polynomial, Polynomial> divRem(Term &x) {
//     Polynomial d;
//     Polynomial r;
//     for (auto it = begin(); it != end(); ++it) {
//         auto [nx, fail] = *it / x;
//         if (fail) {
//             r.add_term(*it);
//         } else {
//             if (nx.isInteger()) {
//                 // perfectly divided
//                 intptr_t div = nx.coef / dx;
//                 intptr_t rem = nx.coef - dx * div;
//                 if (div) {
//                     nx.coef = div;
//                     d.add_term(nx);
//                 }
//                 if (rem) {
//                     nx.coef = rem;
//                     r.add_term(nx);
//                 }
//             } else {
//                 d.add_term(nx);
//             }
//         }
//     }
//     return std::make_pair(d, r);
// }

// returns a <div, rem> pair
Term const &leadingTerm() const { return terms[0]; }
void removeLeadingTerm() { terms.erase(terms.begin()); }
void takeLeadingTerm(Polynomial &x) {
    add_term(std::move(x.leadingTerm()));
    x.removeLeadingTerm();
}
std::pair<Polynomial, Polynomial> divRem(Polynomial const &d) const {
    Polynomial q;
    Polynomial r;
    Polynomial p(*this);
    while (!p.terms.empty()) {
        auto [nx, fail] = p.leadingTerm() / d.leadingTerm();
        if (fail) {
            r.takeLeadingTerm(p);
        } else {
            p -= d * nx;
            q += std::move(nx);
        }
    }
    return std::make_pair(std::move(q), std::move(r));
}
Polynomial operator/(Polynomial const &x) const { return divRem(x).first; }
Polynomial operator%(Polynomial const &x) const { return divRem(x).second; }
};


// bool lexicographicalLess(Polynomial::Monomial &x, Polynomial::Monomial &y) {
//     return std::lexicographical_compare(x.begin(), x.end(), y.begin(),
y.end());
// }
// std::strong_ordering lexicographicalCmp(Polynomial::Monomial &x,
//                                         Polynomial::Monomial &y) {
//     return std::lexicographical_compare_three_way(x.begin(), x.end(),
y.begin(),
//                                                   y.end());
// }
*/

Monomial gcd(Monomial const &x, Monomial const &y) {
    Monomial g, a, b;
    auto ix = x.cbegin();
    auto ixe = x.cend();
    auto iy = y.cbegin();
    auto iye = y.cend();
    while ((ix != ixe) | (iy != iye)) {
        uint_fast32_t xk =
            (ix != ixe) ? *ix : std::numeric_limits<uint_fast32_t>::max();
        uint_fast32_t yk =
            (iy != iye) ? *iy : std::numeric_limits<uint_fast32_t>::max();
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
    return g;
}

size_t gcd(intptr_t x, intptr_t y) { return std::gcd(x, y); }

template <typename C, typename M>
Term<C, M> gcd(Term<C, M> const &x, Term<C, M> const &y) {
    M g = gcd(x.exponent, y.exponent);
    C gr = gcd(x.coefficient, y.coefficient);
    return Term<C, M>(gr, g);
}

std::tuple<Monomial, Monomial, Monomial> gcdd(Monomial const &x,
                                              Monomial const &y) {
    Monomial g, a, b;
    auto ix = x.cbegin();
    auto ixe = x.cend();
    auto iy = y.cbegin();
    auto iye = y.cend();
    while ((ix != ixe) | (iy != iye)) {
        uint_fast32_t xk =
            (ix != ixe) ? *ix : std::numeric_limits<uint_fast32_t>::max();
        uint_fast32_t yk =
            (iy != iye) ? *iy : std::numeric_limits<uint_fast32_t>::max();
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
    return std::make_tuple(g, a, b);
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
            if (!a.isOne()) {
                for (auto it = f.begin(); it != f.end(); ++it) {
                    (*it) *= a;
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
Term<C, Monomial> termToPolyCoeff(Term<C, Monomial> const &t, uint_fast32_t i) {
    Term<C, Monomial> a(t.coefficient);
    for (auto it = t.exponent.cbegin(); it != t.exponent.cend(); ++it) {
        if ((*it) != i) {
            a.exponent.prodIDs.push_back(*it);
        }
    }
    return a;
}
template <typename C>
Term<C, Monomial> termToPolyCoeff(Term<C, Monomial> &&t, uint_fast32_t i) {
    auto start = t.end();
    auto stop = t.begin();
    auto it = t.begin();
    for (auto it = t.begin(); it != t.end(); ++it) {
        if ((*it) == i) {
            break;
        }
    }
    if (it == t.end()) {
        return t;
    }
    auto ite = it;
    for (; ite != t.end(); ++ite) {
        if ((*it) != i) {
            break;
        }
    }
    t.erase(it, ite);
    return t;
}

uint_fast32_t count(std::vector<uint_fast32_t> const &x, uint_fast32_t v) {
    uint_fast32_t s = 0;
    for (auto it = x.begin(); it != x.end(); ++it) {
        s += ((*it) == v);
    }
    return s;
}

template <typename C>
uint_fast32_t count(Term<C, Monomial> const &p, uint_fast32_t v) {
    return count(p.exponent.prodIDs, v);
}

struct FirstGreater {
    template <typename T, typename S>
    bool operator()(std::pair<T, S> const &x, std::pair<T, S> const &y) {
        return x.first > y.first;
    }
};

template <typename C>
void emplace_back(Univariate<Multivariate<C>> &u, Multivariate<C> const &p,
                  std::vector<std::pair<uint_fast32_t, size_t>> const &pows,
                  uint_fast32_t oldDegree, uint_fast32_t chunkStartIdx,
                  uint_fast32_t idx, uint_fast32_t v) {
    Multivariate<C> coef;
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
    // std::cout << "oldDegree: " << oldDegree << std::endl;
    // std::cout << "coef (degree, numTerms): (" << coef.degree() << "," <<
    // coef.terms.size() << " )\n"; if (coef.terms.size()){
    //     std::cout << "prodIDs: ";// << coef.terms[0].exponent.prodIDs <<
    //     std::endl; for (auto it = coef.terms[0].exponent.prodIDs.begin(); it
    //     != coef.terms[0].exponent.prodIDs.end(); ++it){
    //         std::cout << *it << ", ";
    //     }
    //     std::cout << "\n";
    // }
    u.terms.emplace_back(coef, oldDegree);
}

template <typename C>
Univariate<Multivariate<C>> multivariateToUnivariate(Multivariate<C> const &p,
                                                     uint_fast32_t v) {
    std::vector<std::pair<uint_fast32_t, size_t>> pows;
    pows.reserve(p.terms.size());
    for (size_t i = 0; i < p.terms.size(); ++i) {
        pows.emplace_back(count(p.terms[i], v), i);
    }
    std::sort(pows.begin(), pows.end(), FirstGreater());

    Univariate<Multivariate<C>> u;
    if (pows.size() == 0) {
        return u;
    }
    uint_fast32_t oldDegree = pows[0].first;
    uint_fast32_t chunkStartIdx = 0;
    uint_fast32_t idx = 0;
    while (idx < pows.size()) {
        uint_fast32_t degree = pows[idx].first;
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

template <typename C>
Multivariate<C> univariateToMultivariate(Univariate<Multivariate<C>> &&g,
                                         uint_fast32_t v) {
    Multivariate<C> p;
    for (auto it = g.begin(); it != g.end(); ++it) {
        Multivariate<C> coef = it->coefficient;
        size_t exponent = (it->exponent).exponent;
        // std::cout << "Exponent: " << exponent << "; v: " << v << std::endl;
        if (exponent) {
            for (auto ic = coef.begin(); ic != coef.end(); ++ic) {
                (ic->exponent).add_term(v, exponent);
            }
        }
        p += coef;
    }
    return p;
}

const size_t NOT_A_VAR = std::numeric_limits<size_t>::max();

template <typename C> size_t pickVar(Multivariate<C> const &x) {
    size_t v = NOT_A_VAR;
    for (auto it = x.begin(); it != x.end(); ++it) {
        if (it->degree()) {
            size_t vv = *((it->exponent).begin());
            if (vv < v) {
                v = vv;
            }
        }
    }
    return v;
}

template <typename C>
Multivariate<C> gcd(Multivariate<C> const &x, Multivariate<C> const &y) {
    if (x.isZero() || y.isOne()) {
        return y;
    } else if ((y.isZero() || x.isOne()) || (x == y)) {
        return x;
    }

    size_t v1 = pickVar(x);
    size_t v2 = pickVar(y);
    if (v1 < v2) {
        return gcd(y, content(multivariateToUnivariate(x, v1)));
    } else if (v2 < v1) {
        return gcd(x, content(multivariateToUnivariate(y, v2)));
    } else if (v1 == NOT_A_VAR) {
        return Multivariate<C>(gcd(x.leadingTerm(), y.leadingTerm()));
    } else { // v1 == v2, and neither == NOT_A_VAR
        return univariateToMultivariate(gcd(multivariateToUnivariate(x, v1),
                                            multivariateToUnivariate(y, v2)),
                                        v1);
    }
}

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

} // end namespace Polynomial
/*
template <typename C>
std::tuple<Multivariate<C>, Multivariate<C>, Multivariate<C>>
gcd(Multivariate<C> const &a, Multivariate<C> const &b) { Multivariate<C> x(a);
Multivariate<C> y(b); while
(!y.isZero()) { // TODO: add tests and/or proof to make sure this
                          // terminates with symbolics
        x = x % y;
        std::swap(x, y);
    }
    return std::make_tuple(x, a / x, b / x);
}

template <typename C>
std::tuple<Multivariate<C>, Multivariate<C>, Multivariate<C>>
gcdx(Multivariate<C> const &a, Multivariate<C> const &b) { Multivariate<C> x(a);
Multivariate<C> y(b); Multivariate<C> oldS(1); Multivariate<C> s(0);
    Multivariate<C> oldT(0);
    Multivariate<C> t(1);
    while (!y.isZero()) {
        auto [q, r] = x.divRem(y);
        oldS -= q * s;
        oldT -= q * t;
        x = y;
        y = r;
        std::swap(oldS, s);
        std::swap(oldT, t);
    }
    return std::make_tuple(x, a / x, b / x);
}
*/
// out of place/copy
// template <typename T> T negate(T &&x) { return std::forward<T>(x.negate()); }
// Polynomial::Term &negate(Polynomial::Term x) { return x.negate(); }

// Polynomial::Term& negate(Polynomial::Term &&x){ return x.negate(); }
// Polynomial::Uninomial one(const Polynomial::Uninomial){ return
// Polynomial::Uninomial{0}; } Polynomial::Monomial one(const
// Polynomial::Monomial){ return Polynomial::Monomial(); }

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
        uint_fast32_t power;
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
    UniPoly(Polynomial const &x, uint_fast32_t u) {
        for (auto it = x.cbegin(); it != x.cend(); ++it){
            it -> degree(u);
        }
    }
};
*/
