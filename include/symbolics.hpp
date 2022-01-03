#include "math.hpp"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

enum DivRemainder { Indeterminate, NoRemainder, HasRemainder };

std::pair<intptr_t, intptr_t> divgcd(intptr_t x, intptr_t y) {
    intptr_t g = std::gcd(x, y);
    return std::make_pair(x / g, y / g);
}

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
};

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

// The basic symbol type represents a symbol as a product of some number of
// known IDs as well as with a constant term.
// `5` would thus be an empty `prodIDs` vector and `coef = 5`.
// While if `M, N, K = size(A)`, the strides would be `prodIDs = [], coef = 1`,
// `prodIDs = [M], coef = 1`, and `prodIDs = [M,N], coef = 1`, respectively.
// This lets us tell that each is a multiple of the others.
// Can we assume `prodIDs` are greater than 0?
struct Polynomial {
    struct Monomial {
        std::vector<uint_fast32_t>
            prodIDs; // sorted symbolic terms being multiplied

        // constructors
        Monomial() : prodIDs(std::vector<size_t>()){};
        Monomial(std::vector<size_t> &x) : prodIDs(x){};

        inline auto begin() { return prodIDs.begin(); }
        inline auto end() { return prodIDs.end(); }
        inline auto cbegin() const { return prodIDs.cbegin(); }
        inline auto cend() const { return prodIDs.cend(); }
        Monomial operator*(Monomial const &x) const {
            Monomial r;
            // prodIDs are sorted, so we can create sorted product in O(N)
            size_t i = 0;
            size_t j = 0;
            size_t n0 = prodIDs.size();
            size_t n1 = x.prodIDs.size();
            r.prodIDs.reserve(n0 + n1);
            for (size_t k = 0; k < (n0 + n1); ++k) {
                uint_fast32_t a =
                    (i < n0) ? prodIDs[i]
                             : std::numeric_limits<uint_fast32_t>::max();
                uint_fast32_t b =
                    (j < n1) ? x.prodIDs[j]
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
            prodIDs.reserve(n0 +
                            n1); // reserve capacity, to prevent invalidation
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
        bool operator==(Monomial const &x) const {
            return prodIDs == x.prodIDs;
        }
        bool operator!=(Monomial const &x) const {
            return prodIDs != x.prodIDs;
        }

        // numerator, denominator rational
        std::pair<Monomial, Monomial> rational(Monomial const &x) const {
            Monomial n;
            Monomial d;
            size_t i = 0;
            size_t j = 0;
            size_t n0 = prodIDs.size();
            size_t n1 = x.prodIDs.size();
            while ((i + j) < (n0 + n1)) {
                uint_fast32_t a =
                    (i < n0) ? prodIDs[i]
                             : std::numeric_limits<uint_fast32_t>::max();
                uint_fast32_t b =
                    (j < n1) ? x.prodIDs[j]
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
                uint_fast32_t a =
                    (i < n0) ? prodIDs[i]
                             : std::numeric_limits<uint_fast32_t>::max();
                uint_fast32_t b =
                    (j < n1) ? x.prodIDs[j]
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
        bool isCompileTimeConstant() const { return prodIDs.size() == 0; }
        size_t degree() const { return prodIDs.size(); }
        bool lexLess(Monomial const &x) const {
            // return `true` if `*this.degree() > x.degree()`
            if (degree() > x.degree()) {
                return true;
            }
            auto it = cbegin();
            auto ite = cend();
            auto ix = x.cbegin();
            auto ixe = x.cend();
            while ((it != ite) && (ix != ixe)) {
                if ((*it) > (*ix)) {
                    // require syms in `x` be smaller, i.e. leading ones have
                    // higher exponents
                    return false;
                }
                ++it;
                ++ixe;
            }
            // return it != ite; // returns true if `*this` is longer
            return false;
        }
    };

    struct Term {
        Rational coefficient;
        Monomial monomial;
        Term() = default;
        // template <typename T> Term(T coef) : coefficient(coef) {}
        Term(intptr_t coef) : coefficient(coef) {}
        Term(Rational coef) : coefficient(coef) {}
        Term(Rational coef, Monomial monomial)
            : coefficient(coef), monomial(monomial) {}
        Term &negate() {
            coefficient.numerator *= -1;
            return *this;
        }
        bool termsMatch(Term const &x) const {
            return monomial.prodIDs == x.monomial.prodIDs;
        }
        bool addCoef(Rational coef) { return (coefficient += coef).isZero(); }
        bool subCoef(Rational coef) { return (coefficient -= coef).isZero(); }
        bool addCoef(Term const &t) { return addCoef(t.coefficient); }
        bool subCoef(Term const &t) { return subCoef(t.coefficient); }

        bool lexLess(Term const &t) const {
            return monomial.lexLess(t.monomial);
        }
        bool isZero() const { return coefficient.isZero(); }
        bool isOne() const { return coefficient.isOne() & monomial.isOne(); }

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
            // x.lexLess(*it)
            if (this->lexLess(y)) {
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
            if (this->lexLess(y)) {
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
        auto cbegin() const { return monomial.cbegin(); }
        auto cend() const { return monomial.cend(); }
    };

    std::vector<Term> terms;
    Polynomial() = default;
    Polynomial(Term &x) : terms({x}){};
    Polynomial(Term &&x) : terms({std::move(x)}){};
    Polynomial(Term &t0, Term &t1) : terms({t0, t1}){};
    // Polynomial(Term &&t0, Term &&t1) : terms({std::move(t0),
    // std::move(t1)})
    // {};
    // Polynomial(std::vector<Term> t) : terms(t){};
    Polynomial(std::vector<Term> const &t) : terms(t){};
    Polynomial(std::vector<Term> &&t) : terms(std::move(t)){};

    inline auto begin() { return terms.begin(); }
    inline auto end() { return terms.end(); }
    inline auto cbegin() const { return terms.cbegin(); }
    inline auto cend() const { return terms.cend(); }
    // Polynomial(std::tuple<Term, Term> &&x) : terms({std::get<0>(x),
    // std::get<1>(x)}) {}

    template <typename S> void add_term(S &&x) {
        if (!x.isZero()) {
            for (auto it = terms.begin(); it != terms.end(); ++it) {
                if ((it->termsMatch(x))) {
                    if (it->addCoef(x)) {
                        terms.erase(it);
                    }
                    return;
                } else if (x.lexLess(*it)) {
                    terms.insert(it, std::forward<S>(x));
                    return;
                }
            }
            terms.push_back(std::forward<S>(x));
        }
        return;
    }
    template <typename S> void sub_term(S &&x) {
        if (!x.isZero()) {
            for (auto it = terms.begin(); it != terms.end(); ++it) {
                if ((it->termsMatch(x))) {
                    if (it->subCoef(x)) {
                        terms.erase(it);
                    }
                    return;
                } else if (x.lexLess(*it)) {
                    Term y(x);
                    terms.insert(it, std::move(y.negate()));
                    return;
                }
            }
            Term y(x);
            terms.push_back(std::move(y.negate()));
        }
        return;
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
    /*
    std::pair<Polynomial, Polynomial> divRem(Term &x) {
        Polynomial d;
        Polynomial r;
        for (auto it = begin(); it != end(); ++it) {
            auto [nx, fail] = *it / x;
            if (fail) {
                r.add_term(*it);
            } else {
                if (nx.isInteger()) {
                    // perfectly divided
                    intptr_t div = nx.coef / dx;
                    intptr_t rem = nx.coef - dx * div;
                    if (div) {
                        nx.coef = div;
                        d.add_term(nx);
                    }
                    if (rem) {
                        nx.coef = rem;
                        r.add_term(nx);
                    }
                } else {
                    d.add_term(nx);
                }
            }
        }
        return std::make_pair(d, r);
    }
    */
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

/*
bool lexicographicalLess(Polynomial::Monomial &x, Polynomial::Monomial &y) {
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
}
std::strong_ordering lexicographicalCmp(Polynomial::Monomial &x,
                                        Polynomial::Monomial &y) {
    return std::lexicographical_compare_three_way(x.begin(), x.end(), y.begin(),
                                                  y.end());
}
*/

std::tuple<Polynomial::Monomial, Polynomial::Monomial, Polynomial::Monomial>
gcd(Polynomial::Monomial const &x, Polynomial::Monomial const &y) {
    Polynomial::Monomial g, a, b;
    size_t i = 0;
    size_t j = 0;
    size_t n0 = x.prodIDs.size();
    size_t n1 = y.prodIDs.size();
    for (size_t k = 0; k < (n0 + n1); ++k) {
        uint_fast32_t xk =
            (i < n0) ? x.prodIDs[i] : std::numeric_limits<uint_fast32_t>::max();
        uint_fast32_t yk =
            (j < n1) ? y.prodIDs[j] : std::numeric_limits<uint_fast32_t>::max();
        if (xk < yk) {
            a.prodIDs.push_back(xk);
            ++i;
        } else if (xk > yk) {
            b.prodIDs.push_back(yk);
            ++j;
        } else { // xk == yk
            g.prodIDs.push_back(xk);
            ++i;
            ++j;
            ++k;
        }
    }
    return std::make_tuple(g, a, b);
}
std::tuple<Polynomial::Term, Polynomial::Term, Polynomial::Term>
gcd(Polynomial::Term const &x, Polynomial::Term const &y) {
    auto [g, a, b] = gcd(x.monomial, y.monomial);
    Rational gr = gcd(x.coefficient, y.coefficient);
    return std::make_tuple(Polynomial::Term{gr, g},
                           Polynomial::Term{x.coefficient / gr, a},
                           Polynomial::Term{y.coefficient / gr, b});
}

std::pair<Polynomial::Term, std::vector<Polynomial::Term>>
gcd(std::vector<Polynomial::Term> const &x) {
    switch (x.size()) {
    case 0:
        return std::make_pair(Polynomial::Term(0), x);
    case 1:
        return std::make_pair(
            x[0], std::vector<Polynomial::Term>{Polynomial::Term(1)});
    default:
        auto [g, a, b] = gcd(x[0], x[1]);
        std::vector<Polynomial::Term> f;
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

std::pair<Polynomial::Term, Polynomial> gcd(Polynomial const &x) {
    std::pair<Polynomial::Term, std::vector<Polynomial::Term>> st =
        gcd(x.terms);
    return std::make_pair(std::move(st.first),
                          Polynomial(std::move(st.second)));
}
std::tuple<Polynomial, Polynomial, Polynomial> gcd(Polynomial const &a,
                                                   Polynomial const &b) {
    Polynomial x(a);
    Polynomial y(b);
    while (!y.isZero()) { // TODO: add tests and/or proof to make sure this
                          // terminates with symbolics
        x = x % y;
        std::swap(x, y);
    }
    return std::make_tuple(x, a / x, b / x);
}
std::tuple<Polynomial, Polynomial, Polynomial> gcdx(Polynomial const &a,
                                                    Polynomial const &b) {
    Polynomial x(a);
    Polynomial y(b);
    Polynomial oldS(1);
    Polynomial s(0);
    Polynomial oldT(0);
    Polynomial t(1);
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

template <typename T> T negate(T &&x) { return std::forward<T>(x.negate()); }

static std::string programVarName(size_t i) { return "M_" + std::to_string(i); }
std::string toString(Rational x) {
    if (x.denominator == 1) {
        return std::to_string(x.numerator);
    } else {
        return std::to_string(x.numerator) + " / " +
               std::to_string(x.denominator);
    }
}
std::string toString(Polynomial::Monomial const &x) {
    size_t numIndex = x.prodIDs.size();
    if (numIndex) {
        if (numIndex != 1) { // not 0 by prev `if`
            std::string poly = "";
            for (size_t k = 0; k < numIndex; ++k) {
                poly += programVarName(x.prodIDs[k]);
                if (k + 1 != numIndex)
                    poly += " ";
            }
            return poly;
        } else { // numIndex == 1
            return programVarName(x.prodIDs[0]);
        }
    } else {
        return "1";
    }
}
std::string toString(Polynomial::Term const &x) {
    if (x.coefficient.isOne()) {
        return toString(x.monomial);
    } else if (x.isCompileTimeConstant()) {
        return toString(x.coefficient);
    } else {
        return toString(x.coefficient) + " ( " + toString(x.monomial) + " ) ";
    }
}

std::string toString(Polynomial const &x) {
    std::string poly = " ( ";
    for (size_t j = 0; j < length(x.terms); ++j) {
        if (j) {
            poly += " + ";
        }
        poly += toString(x.terms[j]);
    }
    return poly + " ) ";
}
void show(Polynomial::Term const &x) { printf("%s", toString(x).c_str()); }
void show(Polynomial const &x) { printf("%s", toString(x).c_str()); }
Polynomial loopToAffineUpperBound(Vector<Int, MAX_PROGRAM_VARIABLES> loopvars) {
    // Polynomial::Term firstSym = Polynomial::Term(0); // split to avoid vexing
    // parse Polynomial aff(std::move(firstSym));
    Polynomial aff; //{Polynomial::Term(0)};
    for (size_t i = 0; i < MAX_PROGRAM_VARIABLES; ++i) {
        if (loopvars[i]) {
            Polynomial::Term sym = Polynomial::Term(loopvars[i]);
            if (i) {
                sym.monomial.prodIDs.push_back(i);
            } // i == 0 => constant
            // guaranteed no collision and lex ordering
            // so we push_back directly.
            aff.terms.push_back(std::move(sym));
        }
    }
    return aff;
}

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
