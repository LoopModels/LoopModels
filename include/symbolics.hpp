#include "math.hpp"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

enum DivRemainder { Indeterminate, NoRemainder, HasRemainder };

// The basic symbol type represents a symbol as a product of some number of
// known IDs as well as with a constant term.
// `5` would thus be an empty `prodIDs` vector and `coef = 5`.
// While if `M, N, K = size(A)`, the strides would be `prodIDs = [], coef = 1`,
// `prodIDs = [M], coef = 1`, and `prodIDs = [M,N], coef = 1`, respectively.
// This lets us tell that each is a multiple of the others.
// Can we assume `prodIDs` are greater than 0?
struct Symbol {
    std::vector<size_t> prodIDs; // sorted symbolic terms being multiplied
    intptr_t coef;               // constant coef
    // constructors
    Symbol() : prodIDs(std::vector<size_t>()), coef(0){};
    Symbol(intptr_t coef) : prodIDs(std::vector<size_t>()), coef(coef){};

    inline auto begin() { return prodIDs.begin(); }
    inline auto end() { return prodIDs.end(); }
    Symbol operator*(Symbol &x) {
        Symbol r;
        r.coef = coef * x.coef;
        // prodIDs are sorted, so we can create sorted product in O(N)
        size_t i = 0;
        size_t j = 0;
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        r.prodIDs.reserve(n0 + n1);
        for (size_t k = 0; k < (n0 + n1); ++k) {
            size_t a =
                (i < n0) ? prodIDs[i] : std::numeric_limits<size_t>::max();
            size_t b =
                (j < n1) ? x.prodIDs[j] : std::numeric_limits<size_t>::max();
            bool aSmaller = a < b;
            aSmaller ? ++i : ++j;
            r.prodIDs.push_back(aSmaller ? a : b);
        }
        return r;
    }
    Symbol &operator*=(Symbol &x) {
        coef *= x.coef;
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
        auto ix = x.begin();
        auto ixe = x.end();
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
    Symbol operator*(Symbol &&x) {
        x *= (*this);
        return x;
    }
    bool operator==(Symbol x) {
        if (coef != x.coef) {
            return false;
        }
        return prodIDs == x.prodIDs;
    }
    bool operator!=(Symbol x) {
        if (coef == x.coef) {
            return false;
        }
        return prodIDs != x.prodIDs;
    }

    struct Affine;
    template <typename S> Affine operator+(S &&x);
    template <typename S> Affine operator-(S &&x);
    // numerator, denominator rational
    std::pair<Symbol, Symbol> rational(Symbol &x) {
        intptr_t g = std::gcd(coef, x.coef);
        Symbol n(coef / g);
        Symbol d(x.coef / g);
        if (d.coef < 0) {
            // guarantee the denominator coef is positive
            // assumption used in affine divRem, to avoid
            // checking for -1.
            n.coef *= -1;
            d.coef *= -1;
        }
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
    // 0: Symbol(coef / gcd(coef, x.coef), setDiff(prodIDs, x.prodIDs))
    // 1: x.coef / gcd(coef, x.coef)
    // 2: whether the division failed (i.e., true if prodIDs was not a superset
    // of x.prodIDs)
    std::tuple<Symbol, intptr_t, bool> operator/(Symbol &x) {
        intptr_t g = std::gcd(coef, x.coef);
        Symbol n(coef / g);
        intptr_t d = coef / x.coef;
        if (d < 0) {
            d *= -1;
            n.coef *= -1;
        }
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
                return std::make_tuple(n, d, true);
            }
        }
        return std::make_tuple(n, d, false);
    }
    bool isZero() { return coef == 0; }
    bool isOne() { return (coef == 0) & (prodIDs.size() == 0); }
    Symbol &negate() {
        coef *= -1;
        return *this;
    }
};

bool lexicographicalLess(Symbol &x, Symbol &y) {
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
}
std::strong_ordering lexicographicalCmp(Symbol &x, Symbol &y) {
    return std::lexicographical_compare_three_way(x.begin(), x.end(), y.begin(),
                                                  y.end());
}

std::tuple<Symbol, Symbol, Symbol> gcdm(Symbol &x, Symbol &y) {
    Symbol g, a, b;
    intptr_t c = std::gcd(x.coef, y.coef);
    g.coef = c;
    a.coef = x.coef / c;
    b.coef = y.coef / c;
    size_t i = 0;
    size_t j = 0;
    size_t n0 = x.prodIDs.size();
    size_t n1 = y.prodIDs.size();
    for (size_t k = 0; k < (n0 + n1); ++k) {
        size_t xk =
            (i < n0) ? x.prodIDs[i] : std::numeric_limits<size_t>::max();
        size_t yk =
            (j < n1) ? y.prodIDs[j] : std::numeric_limits<size_t>::max();
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

std::tuple<Symbol, std::vector<Symbol>> gcdm(std::vector<Symbol> &x) {
    switch (x.size()) {
    case 0:
        return std::tuple<Symbol, std::vector<Symbol>>(Symbol(0), x);
    case 1:
        return std::tuple<Symbol, std::vector<Symbol>>(x[0], {Symbol(1)});
    default:
        auto [g, a, b] = gcdm(x[0], x[1]);
        std::vector<Symbol> f;
        f.reserve(x.size());
        f.push_back(std::move(a));
        f.push_back(std::move(b));
        for (size_t i = 2; i < x.size(); ++i) {
            auto [gt, a, b] = gcdm(g, x[i]);
            std::swap(g, gt);
            if (!a.isOne()) {
                for (auto it = f.begin(); it != f.end(); ++it) {
                    (*it) *= a;
                }
            }
            f.push_back(std::move(b));
        }
        return std::make_tuple(g, x);
    }
}

// Affine terms are sorted by symbols lexicographically
struct Symbol::Affine {
    std::vector<Symbol> terms;
    Affine() = default;
    Affine(Symbol &x) : terms({x}){};
    Affine(Symbol &&x) : terms({std::move(x)}){};
    // Affine(Symbol &t0, Symbol &t1) : terms({t0, t1}) {};
    // Affine(Symbol &&t0, Symbol &&t1) : terms({std::move(t0), std::move(t1)})
    // {};
    Affine(std::vector<Symbol> &t) : terms(t){};
    Affine(std::vector<Symbol> &&t) : terms(std::move(t)){};

    inline auto begin() { return terms.begin(); }
    inline auto end() { return terms.end(); }
    // Affine(std::tuple<Symbol, Symbol> &&x) : terms({std::get<0>(x),
    // std::get<1>(x)}) {}

    template <typename S> void add_term(S &&x) {
        for (auto it = terms.begin(); it != terms.end(); ++it) {
            if ((it->prodIDs) == x.prodIDs) {
                (it->coef) += x.coef;
                if ((it->coef) == 0) {
                    terms.erase(it);
                }
                return;
            } else if (lexicographicalLess(x, *it)) {
                terms.insert(it, std::forward<S>(x));
                return;
            }
        }
        terms.push_back(std::forward<S>(x));
        return;
    }
    template <typename S> void sub_term(S &&x) {
        for (auto it = terms.begin(); it != terms.end(); ++it) {
            if ((it->prodIDs) == x.prodIDs) {
                (it->coef) -= x.coef;
                if ((it->coef) == 0) {
                    terms.erase(it);
                }
                return;
            } else if (lexicographicalLess(x, *it)) {
                Symbol y(x);
                y.coef *= -1;
                terms.insert(it, std::move(y));
                return;
            }
        }
        Symbol y(x);
        y.coef *= -1;
        terms.push_back(std::move(y));
        return;
    }
    Affine &operator+=(Symbol &x) {
        add_term(x);
        return *this;
    }
    Affine &operator+=(Symbol &&x) {
        add_term(std::move(x));
        return *this;
    }
    Affine &operator-=(Symbol &x) {
        sub_term(x);
        return *this;
    }
    Affine &operator-=(Symbol &&x) {
        sub_term(std::move(x));
        return *this;
    }

    bool operator==(Affine &x) { return (terms == x.terms); }
    bool operator!=(Affine &x) { return (terms != x.terms); }
    Affine operator*(Symbol &x) {
        Affine p(terms);
        for (auto it = p.begin(); it != p.end(); ++it) {
            (*it) *= x;
        }
        return p;
    }
    Affine operator+(Symbol &x) {
        Affine y(terms);
        y.add_term(x);
        return y;
    }
    Affine operator-(Symbol x) {
        Affine y(terms);
        y.sub_term(x);
        return y;
    }
    Affine largerCapacityCopy(size_t i) {
        Affine s;
        s.terms.reserve(i + terms.size()); // reserve full size
        for (auto it = begin(); it != end(); ++it) {
            s.terms.push_back(*it); // copy initial batch
        }
        return s;
    }
    Affine operator+(Affine x) {
        Affine s = largerCapacityCopy(x.terms.size());
        for (auto it = x.begin(); it != x.end(); ++it) {
            s.add_term(*it); // add term for remainder
        }
        return s;
    }
    Affine operator-(Affine x) {
        Affine s = largerCapacityCopy(x.terms.size());
        for (auto it = x.begin(); it != x.end(); ++it) {
            s.sub_term(*it);
        }
        return s;
    }
    Affine &operator+=(Affine x) {
        terms.reserve(terms.size() + x.terms.size());
        for (auto it = x.begin(); it != x.end(); ++it) {
            add_term(*it); // add term for remainder
        }
        return *this;
    }
    Affine &operator-=(Affine x) {
        terms.reserve(terms.size() + x.terms.size());
        for (auto it = x.begin(); it != x.end(); ++it) {
            sub_term(*it); // add term for remainder
        }
        return *this;
    }
    Affine &operator*=(Symbol x) {
        if (x.coef == 0) {
            terms.clear();
            return *this;
        }
        for (auto it = begin(); it != end(); ++it) {
            (*it) *= x; // add term for remainder
        }
        return *this;
    }
    // bool operator>=(Affine x){

    // 	return false;
    // }
    bool isZero() { return (terms.size() == 0); }
    bool isOne() { return (terms.size() == 1) & terms[0].isOne(); }

    Affine &negate() {
        for (auto it = begin(); it != end(); ++it) {
            (*it).negate();
        }
        return *this;
    }
    // returns a tuple with three elements:
    // 0: div
    // 1: rem
    // 2: exit status, `true` if failed, `false` otherwise
    void divRem(Symbol::Affine &d, Symbol::Affine &r, Symbol &x) {
        for (auto it = begin(); it != end(); ++it) {
            auto [nx, dx, fail] = *it / x;
            if (fail) {
                r.terms.push_back(*it);
            } else {
                if (dx == 1) {
                    // perfectly divided
                    d.terms.push_back(nx);
                } else {
                    intptr_t div = nx.coef / dx;
                    intptr_t rem = nx.coef - dx * div;
                    if (div) {
                        nx.coef = div;
                        d.terms.push_back(nx);
                    }
                    if (rem) {
                        nx.coef = rem;
                        r.terms.push_back(nx);
                    }
                }
            }
        }
    }
    std::pair<Symbol::Affine, Symbol::Affine> divRem(Symbol &x) {
        Symbol::Affine d;
        Symbol::Affine r;
        divRem(d, r, x);
        return std::make_pair(d, r);
    }
    std::pair<Symbol::Affine, Symbol::Affine> divRem(Symbol::Affine &x) {
        Symbol::Affine d;
        Symbol::Affine r;
        Symbol::Affine y(terms); // copy
        auto it = x.begin();
        auto ite = x.end();
        if (it != ite) {
            while (true) {
                y.divRem(d, r, *it);
                ++it;
                if (it == ite) {
                    break;
                }
                std::swap(y, r);
                r.terms.clear();
            }
        }
        return std::make_pair(d, r);
    }
};

template <typename T> T negate(T &&x) { return std::forward<T>(x.negate()); }

template <typename S> Symbol::Affine Symbol::operator+(S &&y) {
    if (prodIDs == y.prodIDs) {
        if (coef == -y.coef) {
            return Symbol::Affine(Symbol(0));
        } else {
            Symbol s(std::forward<S>(y));
            s.coef += coef;
            return Symbol::Affine(std::move(s));
        }
    }
    Symbol::Affine s;
    s.terms.reserve(2);
    if (lexicographicalLess(*this, y)) {
        s.terms.push_back(*this);
    } else {
        s.terms.push_back(std::forward<S>(y));
    }
    return s;
}
template <typename S> Symbol::Affine Symbol::operator-(S &&y) {
    if (prodIDs == y.prodIDs) {
        if (coef == -y.coef) {
            return Symbol::Affine(Symbol(0));
        } else {
            Symbol s(std::forward<S>(y));
            s.coef = coef - s.coef;
            return Symbol::Affine(std::move(s));
        }
    }
    Symbol::Affine s;
    s.terms.reserve(2);
    if (lexicographicalLess(*this, y)) {
        s.terms.push_back(*this);
    } else {
        s.terms.push_back(std::forward<S>(y));
    }
    return s;
}

std::tuple<Symbol, Symbol::Affine, Symbol::Affine> gcdm(Symbol::Affine &x,
                                                        Symbol::Affine &y) {
    auto [gx, gxt] = gcdm(x.terms);
    auto [gy, gyt] = gcdm(y.terms);
    auto [gs, as, bs] = gcdm(gx, gy);
    if (gs.isOne()) {
        return std::make_tuple(std::move(gs), x, y);
    }
    if (!as.isOne()) {
        for (auto it = gxt.begin(); it != gxt.end(); ++it) {
            (*it) *= as;
        }
    }
    if (!bs.isOne()) {
        for (auto it = gyt.begin(); it != gyt.end(); ++it) {
            (*it) *= bs;
        }
    }
    return std::make_tuple(std::move(gs), std::move(gxt), std::move(gyt));
}

static std::string programVarName(size_t i) { return "M_" + std::to_string(i); }
std::string toString(Symbol x) {
    std::string poly = "";
    size_t numIndex = x.prodIDs.size();
    Int coef = x.coef;
    if (numIndex) {
        if (numIndex != 1) { // not 0 by prev `if`
            if (coef != 1) {
                poly += std::to_string(coef) + " (";
            }
            for (size_t k = 0; k < numIndex; ++k) {
                poly += programVarName(x.prodIDs[k]);
                if (k + 1 != numIndex)
                    poly += " ";
            }
            if (coef != 1) {
                poly += ")";
            }
        } else { // numIndex == 1
            if (coef != 1) {
                poly += std::to_string(coef) + " ";
            }
            poly += programVarName(x.prodIDs[0]);
        }
    } else {
        poly += std::to_string(coef);
    }
    return poly;
}

std::string toString(Symbol::Affine x) {
    std::string poly = " ( ";
    for (size_t j = 0; j < length(x.terms); ++j) {
        if (j) {
            poly += " + ";
        }
        poly += toString(x.terms[j]);
    }
    return poly + " ) ";
}
void show(Symbol x) { printf("%s", toString(x).c_str()); }
void show(Symbol::Affine x) { printf("%s", toString(x).c_str()); }
Symbol::Affine
loopToAffineUpperBound(Vector<Int, MAX_PROGRAM_VARIABLES> loopvars) {
    Symbol firstSym = Symbol(0); // split to avoid vexing parse
    Symbol::Affine aff(std::move(firstSym));
    for (size_t i = 0; i < MAX_PROGRAM_VARIABLES; ++i) {
        if (loopvars[i]) {
            Symbol sym = Symbol(loopvars[i]);
            if (i) {
                sym.prodIDs.push_back(i);
            } // i == 0 => constant
            aff += sym;
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

/*
struct Strides {
    Affine affine;
    std::vector<size_t> loopInductVars;
};
*/

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
