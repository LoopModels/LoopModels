#include "affine.hpp"
#include "math.hpp"
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
    Symbol() = default;
    Symbol(intptr_t coef) : prodIDs(std::vector<size_t>()), coef(coef) {};
    
    Symbol operator*(Symbol x) {
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
    Symbol& operator*=(Symbol x) {
        coef *= x.coef;
	// optimize the length 0 and 1 cases.
	if (length(x.prodIDs)==0){
	    return *this;
	} else if (length(x.prodIDs) == 1) {
	    size_t y = x.prodIDs[0];
	    for (auto it = prodIDs.begin(); it != prodIDs.end(); ++it){
		if (y < *it){
		    prodIDs.insert(it, y);
		    return *this;
		}
	    }
	    prodIDs.push_back(y);
	    return *this;
	}
	// lazy implementation for length > 1. TODO: optimize this.
        prodIDs = ((*this) * x).prodIDs;
        return *this;
    }
    struct Affine;
    Affine operator+(Symbol x);
    Affine operator-(Symbol x);
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
            size_t b = i == n ? std::numeric_limits<size_t>::max() : x.prodIDs[i];
	    if (a == b){
		d.prodIDs.push_back(a);
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
};

// bool sameSym(Symbol x, Symbol y){ return x.prodIDs == y.prodIDs; }

std::tuple<Symbol, Symbol, Symbol> gcdm(Symbol x, Symbol y) {
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

struct Symbol::Affine {
    Symbol gcd;
    std::vector<Symbol> terms;
    Affine() = default;
    Affine(Symbol &gcd) : gcd(gcd), terms(std::vector<Symbol>()) {}
    Affine(Symbol &&gcd) : gcd(std::move(gcd)), terms(std::vector<Symbol>()) {}
    Affine(Symbol &gcd, Symbol &t0) : gcd(gcd), terms({t0}) {}
    Affine(Symbol &&gcd, Symbol &&t0) : gcd(std::move(gcd)), terms({std::move(t0)}) {}
    Affine(Symbol &&gcd, Symbol &&t0, Symbol &&t1) : gcd(std::move(gcd)), terms({std::move(t0), std::move(t1)}) {}
    Affine(Symbol &&gcd, std::vector<Symbol> &t) : gcd(std::move(gcd)), terms(t) {}
    Affine(Symbol &&gcd, std::vector<Symbol> &&t) : gcd(std::move(gcd)), terms(std::move(t)) {}
    
    Affine(std::tuple<Symbol, Symbol, Symbol> &&x)
        : gcd(std::get<0>(x)), terms({std::get<1>(x), std::get<2>(x)}) {}

    template <typename S>
    void push_term(S&& x){
	for (size_t i = 0; i < terms.size(); ++i){
	    if (terms[i].prodIDs == x.prodIDs){
		terms[i].coef += x.coef;
		return;
	    }
	}
	terms.push_back(std::forward<S>(x));
	return;
    }
    Affine operator*(Symbol x){ return Affine(gcd * x, terms); }
    Affine operator+(Symbol x){
	auto [g, a, b] = gcdm(gcd, x);
	Affine y(g);
	for (size_t i = 0; i < terms.size(); ++i){
	    y.terms.push_back(terms[i] * a); // skip equality check
	}
	y.push_term(std::move(b));
	return y;
    }
    Affine operator-(Symbol x){
	auto [g, a, b] = gcdm(gcd, x);
	Affine y(g);
	for (size_t i = 0; i < terms.size(); ++i){
	    y.terms.push_back(terms[i] * a);
	}
	b.coef *= -1;
	y.push_term(std::move(b));
	return y;
    }
    Affine& operator+=(Symbol x){
	auto [g, a, b] = gcdm(gcd, x);
	gcd = g;
	for (size_t i = 0; i < terms.size(); ++i){
	    terms[i] *= a;
	}
	push_term(std::move(b));
	return *this;
    }
    Affine operator+(Affine x){
	auto [g, a, b] = gcdm(gcd, x.gcd);
	Affine s(g);
	s.terms.reserve(terms.size() + x.terms.size());
	for (size_t i = 0; i < terms.size(); ++i){
	    s.terms.push_back(a * terms[i]);
	}
	for (size_t i = 0; i < x.terms.size(); ++i){
	    s.push_term(b * x.terms[i]);
	}
	return s;
    }
    Affine operator-(Affine x){
	auto [g, a, b] = gcdm(gcd, x.gcd);
	Affine s(g);
	s.terms.reserve(terms.size() + x.terms.size());
	for (size_t i = 0; i < terms.size(); ++i){
	    s.terms.push_back(a * terms[i]);
	}
	for (size_t i = 0; i < x.terms.size(); ++i){
	    b.coef *= -1;
	    s.push_term(b * x.terms[i]);
	}
	return s;
    }
    Affine& operator+=(Affine x){
	auto [g, a, b] = gcdm(gcd, x.gcd);
	gcd = g;
	terms.reserve(terms.size() + x.terms.size());
	for (size_t i = 0; i < terms.size(); ++i){
	    terms[i] *= a;
	}
	for (size_t i = 0; i < x.terms.size(); ++i){
	    push_term(b * x.terms[i]);
	}
	return *this;
    }
    Affine& operator-=(Affine x){
	auto [g, a, b] = gcdm(gcd, x.gcd);
	gcd = g;
	terms.reserve(terms.size() + x.terms.size());
	for (size_t i = 0; i < terms.size(); ++i){
	    terms[i] *= a;
	}
	for (size_t i = 0; i < x.terms.size(); ++i){
	    b.coef *= -1;
	    push_term(b * x.terms[i]);
	}
	return *this;
    }
    Affine& operator*=(Symbol x){
	gcd *= x;
	return *this;
    }
    // bool operator>=(Affine x){
	
    // 	return false;
    // }
};
bool isZero(Symbol &x){ return x.coef == 0; }
bool isZero(Symbol::Affine &x){ return isZero(x.gcd) | (x.terms.size() == 0); }


Symbol::Affine Symbol::operator+(Symbol y) {

    //return Symbol::Affine(gcd(*this, y));
    auto [g, c, d] = gcdm(*this, y);
    if ((c.prodIDs.size() == 0) & (d.prodIDs.size() == 0)){
        if (c.coef == -d.coef){
            return Symbol::Affine(Symbol(0));
        } else {
            c.coef += d.coef;
            return Symbol::Affine(std::move(g), std::move(c));
        }
    } else {
	return Symbol::Affine(std::move(g), std::move(c), std::move(d));
    }
    // return Symbol::Affine(g, c, d);
    /*
    Symbol::Affine aff;
    aff.gcd = std::move(g);
    aff.terms.push_back(std::move(c));
    aff.terms.push_back(std::move(d));
    return aff;
    */
}
Symbol::Affine Symbol::operator-(Symbol y) {

    auto [g, c, d] = gcdm(*this, y);
    if ((c.prodIDs.size() == 0) & (d.prodIDs.size() == 0)){
        if (c.coef == d.coef){
            return Symbol::Affine(Symbol(0));
        } else {
            c.coef -= d.coef;
            return Symbol::Affine(std::move(g), std::move(c));
        }
    } else {
	d.coef *= -1;
	return Symbol::Affine(std::move(g), std::move(c), std::move(d));
    }
}

std::tuple<Symbol::Affine, Symbol::Affine, Symbol::Affine> gcdm(Symbol::Affine x, Symbol::Affine y) {
    auto [gs, as, bs] = gcdm(x.gcd, y.gcd);
    Symbol::Affine g = Symbol::Affine(std::move(gs));
    Symbol::Affine a = Symbol::Affine(std::move(as), x.terms);
    // Symbol::Affine a = Symbol::Affine(std::move(as), x.terms);
    Symbol::Affine b = Symbol::Affine(std::move(bs), y.terms);
    return std::make_tuple(g, a, b);
}

static std::string programVarName(size_t i) { return "M_" + std::to_string(i); }
std::string toString(Symbol x){
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
    std::string poly = toString(x.gcd) + " * ( ";
    for (size_t j = 0; j < length(x.terms); ++j) {
	if (j) {
	    poly += " + ";
	}
        poly += toString(x.terms[j]);
    }
    return poly + " ) ";
}
void show(Symbol x){
    printf("%s", toString(x).c_str());
}
void show(Symbol::Affine x){
    printf("%s", toString(x).c_str());
}
Symbol::Affine loopToAffineUpperBound(Vector<Int,MAX_PROGRAM_VARIABLES> loopvars){
    Symbol firstSym = Symbol(0); // split to avoid vexing parse
    Symbol::Affine aff(std::move(firstSym));
    for (size_t i = 0; i < MAX_PROGRAM_VARIABLES; ++i){
	if (loopvars[i]){
	    Symbol sym = Symbol(loopvars[i]);
	    if (i) { sym.prodIDs.push_back(i); } // i == 0 => constant
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
