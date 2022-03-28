#pragma once

#include "./Bipartite.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <llvm/ADT/SmallVector.h>
#include <tuple>

intptr_t saturatedAdd(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_add_overflow(a, b, &c)) {
        c = ((a > 0) & (b > 0)) ? std::numeric_limits<intptr_t>::max()
                                : std::numeric_limits<intptr_t>::min();
    }
    return c;
}
intptr_t saturatedSub(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_sub_overflow(a, b, &c)) {
        c = ((a > 0) & (b < 0)) ? std::numeric_limits<intptr_t>::max()
                                : std::numeric_limits<intptr_t>::min();
    }
    return c;
}
intptr_t saturatedMul(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_mul_overflow(a, b, &c)) {
        c = ((a > 0) ^ (b > 0)) ? std::numeric_limits<intptr_t>::min()
                                : std::numeric_limits<intptr_t>::max();
    }
    return c;
}
intptr_t saturatingAbs(intptr_t a) {
    if (a == std::numeric_limits<intptr_t>::min()) {
        return std::numeric_limits<intptr_t>::max();
    }
    return std::abs(a);
}
// struct Interval
// Provides saturating interval arithmetic.
struct Interval {
    intptr_t lowerBound, upperBound;
    Interval(intptr_t x) : lowerBound(x), upperBound(x){};
    Interval(intptr_t lb, intptr_t ub) : lowerBound(lb), upperBound(ub){};
    Interval intersect(Interval b) const {
        return Interval{std::max(lowerBound, b.lowerBound),
                        std::min(upperBound, b.upperBound)};
    }
    bool isEmpty() const { return lowerBound > upperBound; }
    Interval operator+(Interval b) const {
        return Interval{saturatedAdd(lowerBound, b.lowerBound),
                        saturatedAdd(upperBound, b.upperBound)};
    }
    Interval operator-(Interval b) const {
        return Interval{saturatedSub(lowerBound, b.upperBound),
                        saturatedSub(upperBound, b.lowerBound)};
    }

    Interval operator*(Interval b) const {
        intptr_t ll = saturatedMul(lowerBound, b.lowerBound);
        intptr_t lu = saturatedMul(lowerBound, b.upperBound);
        intptr_t ul = saturatedMul(upperBound, b.lowerBound);
        intptr_t uu = saturatedMul(upperBound, b.upperBound);
        return Interval{std::min(std::min(ll, lu), std::min(ul, uu)),
                        std::max(std::max(ll, lu), std::max(ul, uu))};
    }
    Interval &operator+=(Interval b) {
        *this = (*this) + b;
        return *this;
    }
    Interval &operator-=(Interval b) {
        *this = (*this) - b;
        return *this;
    }
    Interval &operator*=(Interval b) {
        *this = (*this) * b;
        return *this;
    }
    // *this = a + b;
    // update `*this` based on `a + b` and return new `a` and new `b`
    // corresponding to those constraints.
    std::pair<Interval, Interval> restrictAdd(Interval a, Interval b) {
        Interval cNew = this->intersect(a + b);
        Interval aNew = a.intersect(*this - b);
        Interval bNew = b.intersect(*this - a);
        assert(!cNew.isEmpty());
        assert(!aNew.isEmpty());
        assert(!bNew.isEmpty());
        lowerBound = cNew.lowerBound;
        upperBound = cNew.upperBound;
        return std::make_pair(aNew, bNew);
    }
    // *this = a - b; similar to `restrictAdd`
    std::pair<Interval, Interval> restrictSub(Interval a, Interval b) {
        Interval cNew = this->intersect(a - b);
        Interval aNew = a.intersect(*this + b);
        Interval bNew = b.intersect(a - *this);
        assert(!cNew.isEmpty());
        assert(!aNew.isEmpty());
        assert(!bNew.isEmpty());
        lowerBound = cNew.lowerBound;
        upperBound = cNew.upperBound;
        return std::make_pair(aNew, bNew);
    };

    Interval operator-() const {
        intptr_t typeMin = std::numeric_limits<intptr_t>::min();
        intptr_t typeMax = std::numeric_limits<intptr_t>::max();
        return Interval{upperBound == typeMin ? typeMax : -upperBound,
                        lowerBound == typeMin ? typeMax : -lowerBound};
    }
    bool isConstant() const { return lowerBound == upperBound; }

    bool knownLess(Interval a) { return lowerBound < a.upperBound; }
    bool knownLessEqual(Interval a) { return lowerBound <= a.upperBound; }
    bool knownGreater(Interval a) { return lowerBound > a.upperBound; }
    bool knownGreaterEqual(Interval a) { return lowerBound >= a.upperBound; }

    // to be different in a significant way, we require them to be different,
    // but only for values smaller than half of type max. Values so large
    // are unlikely to effect results, so we don't continue propogating.
    bool significantlyDifferent(Interval b) const {
        return ((lowerBound != b.lowerBound) &&
                (std::min(saturatingAbs(lowerBound),
                          saturatingAbs(b.lowerBound)) <
                 std::numeric_limits<intptr_t>::max() >> 1)) ||
               ((upperBound != b.upperBound) &&
                (std::min(saturatingAbs(upperBound),
                          saturatingAbs(b.upperBound)) <
                 std::numeric_limits<intptr_t>::max() >> 1));
    }
    bool signUnknown() const { return (lowerBound < 0) & (upperBound > 0); }

    static Interval negative() {
        return Interval{std::numeric_limits<intptr_t>::min(), -1};
    }
    static Interval positive() {
        return Interval{1, std::numeric_limits<intptr_t>::max()};
    }
    static Interval nonPositive() {
        return Interval{std::numeric_limits<intptr_t>::min(), 0};
    }
    static Interval nonNegative() {
        return Interval{0, std::numeric_limits<intptr_t>::max()};
    }
    static Interval unconstrained() {
        return Interval{std::numeric_limits<intptr_t>::min(),
                        std::numeric_limits<intptr_t>::max()};
    }
    static Interval LowerBound(intptr_t x) {
        return Interval{x, std::numeric_limits<intptr_t>::max()};
    }
    static Interval UpperBound(intptr_t x) {
        return Interval{std::numeric_limits<intptr_t>::min(), x};
    }
    static Interval zero() { return Interval{0, 0}; }
};

Interval negativeInterval() {
    return Interval{std::numeric_limits<intptr_t>::min(), -1};
}
Interval positiveInterval() {
    return Interval{1, std::numeric_limits<intptr_t>::max()};
}

std::ostream &operator<<(std::ostream &os, Interval a) {
    return os << a.lowerBound << " : " << a.upperBound;
}
// struct PartiallyOrderedSet
// Gives partial ordering between variables, using intervals to indicate the
// range of differences in possible values.
struct PartiallyOrderedSet {
    llvm::SmallVector<Interval, 0> delta;
    size_t nVar;

    PartiallyOrderedSet() : nVar(0){};

    inline static size_t bin2(size_t i) { return (i * (i - 1)) >> 1; }
    inline static size_t uncheckedLinearIndex(size_t i, size_t j) {
        return i + bin2(j);
    }
    inline static std::pair<size_t, bool> checkedLinearIndex(size_t ii,
                                                             size_t jj) {
        size_t i = std::min(ii, jj);
        size_t j = std::max(ii, jj);
        return std::make_pair(i + bin2(j), jj < ii);
    }

    // transitive closure of a graph
    Interval update(size_t i, size_t j, Interval ji) {
        // bin2s here are for indexing columns
        size_t iOff = bin2(i);
        size_t jOff = bin2(j);
        // we require that i < j
        for (size_t k = 0; k < i; ++k) {
            Interval ik = delta[k + iOff];
            Interval jk = delta[k + jOff];
            // j - i = (j - k) - (i - k)
            auto [jkt, ikt] = ji.restrictSub(jk, ik);
            delta[k + iOff] = ikt;
            delta[k + jOff] = jkt;
            if (ikt.significantlyDifferent(ik)) {
                delta[i + jOff] = ji;
                delta[k + iOff] = update(k, i, ikt);
                ji = delta[i + jOff];
            }
            if (jkt.significantlyDifferent(jk)) {
                delta[i + jOff] = ji;
                delta[k + jOff] = update(k, j, jkt);
                ji = delta[i + jOff];
            }
        }
        size_t kOff = iOff;
        for (size_t k = i + 1; k < j; ++k) {
            kOff += (k - 1);
            Interval ki = delta[i + kOff];
            Interval jk = delta[k + jOff];
            auto [kit, jkt] = ji.restrictAdd(ki, jk);
            delta[i + kOff] = kit;
            delta[k + jOff] = jkt;
            if (kit.significantlyDifferent(ki)) {
                delta[i + jOff] = ji;
                delta[i + kOff] = update(i, k, kit);
                ji = delta[i + jOff];
            }
            if (jkt.significantlyDifferent(jk)) {
                delta[i + jOff] = ji;
                delta[k + jOff] = update(k, j, jkt);
                ji = delta[i + jOff];
            }
        }
        kOff = jOff;
        for (size_t k = j + 1; k < nVar; ++k) {
            kOff += (k - 1);
            Interval ki = delta[i + kOff];
            Interval kj = delta[j + kOff];
            auto [kit, kjt] = ji.restrictSub(ki, kj);
            delta[i + kOff] = kit;
            delta[j + kOff] = kjt;
            if (kit.significantlyDifferent(ki)) {
                delta[i + jOff] = ji;
                delta[i + kOff] = update(i, k, kit);
                ji = delta[i + jOff];
            }
            if (kjt.significantlyDifferent(kj)) {
                delta[i + jOff] = ji;
                delta[j + kOff] = update(j, k, kjt);
                ji = delta[i + jOff];
            }
        }
        return ji;
    }
    // j - i = itv
    void push(size_t i, size_t j, Interval itv) {
        if (i > j) {
            return push(j, i, -itv);
        }
        assert(j > i); // i != j
        size_t jOff = bin2(j);
        size_t l = jOff + i;
        if (j >= nVar) {
            nVar = j + 1;
            delta.resize((j * nVar) >> 1, Interval::unconstrained());
        } else {
            itv = itv.intersect(delta[l]);
        }
        delta[l] = update(i, j, itv);
    }
    Interval operator()(size_t i, size_t j) const {
        if (i == j) {
            return Interval::zero();
        }
        auto [l, f] = checkedLinearIndex(i, j);
        if (l > delta.size()) {
            return Interval::unconstrained();
        }
        Interval d = delta[l];
        return f ? -d : d;
    }
    Interval operator()(size_t i) const {
        return i < nVar ? (*this)(0, i) : Interval::unconstrained();
    }
    Interval asInterval(const Polynomial::Monomial &m) const {
        if (isOne(m)) {
            return Interval{1, 1};
        }
        assert(m.prodIDs[m.prodIDs.size() - 1].getType() == VarType::Constant);
        Interval itv = delta[bin2(m.prodIDs[0].getID())];
        for (size_t i = 1; i < m.prodIDs.size(); ++i) {
            itv *= delta[bin2(m.prodIDs[i].getID())];
        }
        return itv;
    }
    Interval asInterval(
        const Polynomial::Term<intptr_t, Polynomial::Monomial> &t) const {
        return asInterval(t.exponent) * t.coefficient;
    }
    bool knownGreaterEqual(
        const Polynomial::Term<intptr_t, Polynomial::Monomial> &x,
        const Polynomial::Term<intptr_t, Polynomial::Monomial> &y) const {
        return knownGreaterEqual(x.exponent, y.exponent, x.coefficient,
                                 y.coefficient);
    }
    std::pair<std::pair<size_t, llvm::SmallVector<int>>,bool>
    matchMonomials(const Polynomial::Monomial &x,
                   const Polynomial::Monomial &y,
		   intptr_t cx, intptr_t cy) const {
        const size_t N = x.prodIDs.size();
        const size_t M = y.prodIDs.size();
	const intptr_t thresh = 0;
        Matrix<bool, 0, 0> bpGraph(N+(cx>thresh), M+(cy>thresh));
	bool positive = true;
        for (size_t n = 0; n < N; ++n) {
            auto xid = x.prodIDs[n].getID();
            Interval xb = (*this)(xid);
	    if ((xb.lowerBound < 0) & (xb.upperBound > 0)){
		continue;
	    }
            for (size_t m = 0; m < M; ++m) {
		// xid - yid
                Interval xyb = (*this)(y.prodIDs[m].getID(), xid);
		if (xb.lowerBound >= 0){
		    bpGraph(n, m) = xyb.lowerBound >= 0;
		} else {
		    // x nonPositive
		    positive = !positive;
		    bpGraph(n, m) = xyb.upperBound <= 0;
		}
            }
	    if (cy > thresh) {
		bpGraph(n, M) = xb.lowerBound >= cy;
	    }
        }
	if (cx > thresh){
	    for (size_t m = 0; m < M; ++m){
		auto yid = y.prodIDs[m].getID();
		Interval yb = (*this)(yid);
		bpGraph(N, m) = cx >= yb.upperBound;
	    }
	    if (cy > thresh){
		bpGraph(N, M) = cx >= cy;
	    }
	}
        return std::make_pair(maxBipartiteMatch(bpGraph), positive);
    }

    std::pair<Interval, Interval>
    unmatchedIntervals(const Polynomial::Monomial &x,
                       const Polynomial::Monomial &y, intptr_t cx = 1,
                       intptr_t cy = 1) const {
        const size_t N = x.prodIDs.size();
        const size_t M = y.prodIDs.size();
        auto [matchesmatchR,positive] = matchMonomials(x, y, cx, cy);
        auto& [matches, matchR] = matchesmatchR;
        // matchR.size() == N
        // matchR maps ys to xs
	if (!positive){
	    cx = -cx;
	    cy = -cy;
	}
        Interval itvx(cx);
        Interval itvy(cy);
	// not all Y matched;
	// update itvy with unmatched
        llvm::SmallVector<bool> mMatched(M, false);
        for (size_t n = 0; n < N; ++n) {
            size_t m = matchR[n];
            // size_t(m) is defined to wrap around, so if
            // m == -1, we get typemax
            if (m < M) {
                mMatched[m] = true;
            }
        }
        for (size_t m = 0; m < M; ++m) {
            if (mMatched[m]) {
                continue;
            }
            itvy *= (*this)(y.prodIDs[m].getID());
        }
        // all N matched; need to make sure remaining `Y` are negative.
        for (size_t n = 0; n < N; ++n) {
            if (size_t(matchR[n]) < M) {
                continue;
            }
            itvx *= (*this)(x.prodIDs[n].getID());
        }
        return std::make_pair(itvx, itvy);
    }
    bool knownGreaterEqual(const Polynomial::Monomial &x,
                           const Polynomial::Monomial &y, intptr_t cx = 1,
                           intptr_t cy = 1) const {
        if (cx < 0) {
            if (cy < 0) {
                return knownGreaterEqual(y, x, -cy, -cx);
            } else {
                return false;
            }
        } else if (cy < 0) {
            return true;
        }
        auto [itvx, itvy] = unmatchedIntervals(x, y, cx, cy);
        return itvx.knownGreaterEqual(itvy);
    }
    bool knownGreater(const Polynomial::Monomial &x,
                      const Polynomial::Monomial &y, intptr_t cx = 1,
                      intptr_t cy = 1) const {
        if (cx < 0) {
            if (cy < 0) {
                return knownGreater(y, x, -cy, -cx);
            } else {
                return false;
            }
        } else if (cy < 0) {
            return true;
        }
        auto [itvx, itvy] = unmatchedIntervals(x, y, cx, cy);
        return itvx.knownGreater(itvy);
    }
    bool atLeastOnePositive(const Polynomial::Monomial &x,
                            const Polynomial::Monomial &y,
                            const llvm::SmallVectorImpl<int> &matchR) const {
        for (size_t n = 0; n < matchR.size(); ++n) {
            size_t m = matchR[n];
            intptr_t lowerBound =
                (*this)(y.prodIDs[n].getID(), x.prodIDs[m].getID()).lowerBound;
            if (lowerBound > 0) {
                return true;
            }
        }
        return false;
    }
    bool signUnknown(const Polynomial::Monomial &m) const {
        for (auto &v : m) {
            if ((*this)(v.getID()).signUnknown()) {
                return true;
            }
        }
        return false;
    }
    bool knownFlipSign(const Polynomial::Monomial &m, bool pos) const {
        for (auto &v : m) {
            Interval itv = (*this)(v.getID());
            if (itv.upperBound < 0) {
                pos ^= true;
            } else if ((itv.lowerBound < 0) & (itv.upperBound > 0)) {
                return false;
            }
        }
        return pos;
    }
    bool knownPositive(const Polynomial::Monomial &m) const {
        return knownFlipSign(m, true);
    }
    bool knownNegative(const Polynomial::Monomial &m) const {
        return knownFlipSign(m, false);
    }
    bool knownGreaterEqualZero(const MPoly &x) const {
        // TODO: implement carrying between differences
        if (isZero(x)) {
            return true;
        }
        size_t N = x.size();
        // Interval carriedInterval = Interval::zero();
        for (size_t n = 0; n < N - 1; n += 2) {
            auto &tm = x.terms[n];
            // if (signUnknown(tm.exponent)) {
            //     return false;
            // }

            // if (!tmi.notZero()){ return false; }
            auto &tn = x.terms[n + 1];
            // if (signUnknown(tn.exponent)) {
            //     return false;
            // }
            Interval termSum = asInterval(tm) + asInterval(tn);
            if (termSum.lowerBound >= 0) {
                continue;
            }
            bool mPos = (tm.coefficient > 0) & (knownPositive(tm.exponent));
            bool nPos = (tn.coefficient > 0) & (knownPositive(tn.exponent));
            if (mPos) {
                if (nPos) {
                    // tm + tn
                    continue;
                } else if (tn.coefficient < 0) {
                    // tm - tn; monomial positive
                    // if ((tm.coefficient + tn.coefficient >= 0) &&
                    //     knownGreaterEqual(tm.exponent, tn.exponent,
                    //                       tm.coefficient, -tn.coefficient)) {
                    if (knownGreaterEqual(tm.exponent, tn.exponent,
                                          tm.coefficient, -tn.coefficient)) {
                        continue;
                    } else {
                        return false;
                    }
                } else {
                    // tm - tn; monomial negative; TODO: something
                    return false;
                }
            } else if (nPos) {
                if (tm.coefficient < 0) {
                    // tn - tm; monomial positive
                    // if ((tm.coefficient + tn.coefficient >= 0) &&
                    //     knownGreaterEqual(tn.exponent, tm.exponent,
                    //                       tn.coefficient, -tm.coefficient)) {
                    if (knownGreaterEqual(tn.exponent, tm.exponent,
                                          tn.coefficient, -tm.coefficient)) {
                        continue;
                    } else {
                        return false;
                    }
                } else {
                    // tn - tm; monomial negative; TODO: something
                    return false;
                }
            } else {
                return false;
                // -tm - tn
            }
        }
        if (N & 1) {
            return asInterval(x.terms[N - 1]).lowerBound >= 0;
        }
        return true;
    }
    bool knownLessEqualZero(MPoly x) const {
        // TODO: check if this avoids the copy on negation.
        return knownGreaterEqualZero(-std::move(x));
    }
    bool knownLessThanZero(MPoly x) const {
        // TODO: optimize this
        x *= -1;
        x -= 1;
        return knownGreaterEqualZero(x);
    }
    // bool knownOffsetLessEqual(MPoly &x, MPoly &y, intptr_t offset = 0) {
    // return knownOffsetGreaterEqual(y, x, -offset);
    // }
};
