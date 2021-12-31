#include "math.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

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
                (j < n1) ? prodIDs[j] : std::numeric_limits<size_t>::max();
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
    /*
    std::pair<Symbol, Symbol> operator/(Symbol x) {
        Symbol d;
        Symbol r;
        d.coef = coef / x.coef;
        r.coef = coef - d.coef * x.coef;
        size_t i = 0;
        size_t n = x.prodIDs.size();
        for (size_t k = 0; k < prodIDs.size(); ++k){
            size_t a = prodIDs[k];
            while ((i < n)) {
                size_t b = x.prodIDs[i];
                if (b < a){
                    ++i;
                } else if (b == a) {
                    d.prodIDs.push_back();
                    ++i;
                }
            }
            size_t b = i == n ? std::numeric_limits<size_t>::max() :
    x.prodIDs[i]; if (a == b){ d.prodIDs.push_back(a);
                ++i;
            }
        }
        size_t n0 = prodIDs.size();
        size_t n1 = x.prodIDs.size();
        for (size_t k = 0; k < (n0 + n1); ++k) {
            size_t a =
                (i < n0) ? prodIDs[i] : std::numeric_limits<size_t>::max();
            size_t b =
                (j < n1) ? prodIDs[j] : std::numeric_limits<size_t>::max();
            bool aSmaller = a < b;
            aSmaller ? ++i : ++j;
            r.prodIDs.push_back(aSmaller ? a : b);
        }
        return std::make_pair(d, r);
        }
    */
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
            if ((*it).prodIDs == x.prodIDs) {
                (*it).coef += x.coef;
                if ((*it).coef == 0) {
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
            if ((*it).prodIDs == x.prodIDs) {
                (*it).coef -= x.coef;
                if ((*it).coef == 0) {
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
