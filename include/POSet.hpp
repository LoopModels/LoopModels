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
#include <utility>

int64_t saturatedAdd(int64_t a, int64_t b) {
    int64_t c;
    if (__builtin_add_overflow(a, b, &c)) {
        c = ((a > 0) & (b > 0)) ? std::numeric_limits<int64_t>::max()
                                : std::numeric_limits<int64_t>::min();
    }
    return c;
}
int64_t saturatedSub(int64_t a, int64_t b) {
    int64_t c;
    if (__builtin_sub_overflow(a, b, &c)) {
        c = ((a > 0) & (b < 0)) ? std::numeric_limits<int64_t>::max()
                                : std::numeric_limits<int64_t>::min();
    }
    return c;
}
int64_t saturatedMul(int64_t a, int64_t b) {
    int64_t c;
    if (__builtin_mul_overflow(a, b, &c)) {
        c = ((a > 0) ^ (b > 0)) ? std::numeric_limits<int64_t>::min()
                                : std::numeric_limits<int64_t>::max();
    }
    return c;
}
int64_t saturatingAbs(int64_t a) {
    if (a == std::numeric_limits<int64_t>::min()) {
        return std::numeric_limits<int64_t>::max();
    }
    return std::abs(a);
}
// struct Interval
// Provides saturating interval arithmetic.
struct Interval {
    int64_t lowerBound, upperBound;
    Interval(int64_t x) : lowerBound(x), upperBound(x){};
    Interval(int64_t lb, int64_t ub) : lowerBound(lb), upperBound(ub){};
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
        int64_t ll = saturatedMul(lowerBound, b.lowerBound);
        int64_t lu = saturatedMul(lowerBound, b.upperBound);
        int64_t ul = saturatedMul(upperBound, b.lowerBound);
        int64_t uu = saturatedMul(upperBound, b.upperBound);
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
        int64_t typeMin = std::numeric_limits<int64_t>::min();
        int64_t typeMax = std::numeric_limits<int64_t>::max();
        return Interval{upperBound == typeMin ? typeMax : -upperBound,
                        lowerBound == typeMin ? typeMax : -lowerBound};
    }
    bool isConstant() const { return lowerBound == upperBound; }

    bool knownLess(Interval a) { return lowerBound < a.upperBound; }
    bool knownLessEqual(Interval a) { return lowerBound <= a.upperBound; }
    bool knownGreater(Interval a) { return lowerBound > a.upperBound; }
    bool knownGreaterEqual(Interval a) { return lowerBound >= a.upperBound; }
    bool equivalentRange(Interval a) {
        return (lowerBound == a.lowerBound) & (upperBound == a.upperBound);
    }
    // to be different in a significant way, we require them to be different,
    // but only for values smaller than half of type max. Values so large
    // are unlikely to effect results, so we don't continue propogating.
    bool significantlyDifferent(Interval b) const {
        return ((lowerBound != b.lowerBound) &&
                (std::min(saturatingAbs(lowerBound),
                          saturatingAbs(b.lowerBound)) <
                 std::numeric_limits<int64_t>::max() >> 1)) ||
               ((upperBound != b.upperBound) &&
                (std::min(saturatingAbs(upperBound),
                          saturatingAbs(b.upperBound)) <
                 std::numeric_limits<int64_t>::max() >> 1));
    }
    bool signUnknown() const { return (lowerBound < 0) & (upperBound > 0); }

    static Interval negative() {
        return Interval{std::numeric_limits<int64_t>::min(), -1};
    }
    static Interval positive() {
        return Interval{1, std::numeric_limits<int64_t>::max()};
    }
    static Interval nonPositive() {
        return Interval{std::numeric_limits<int64_t>::min(), 0};
    }
    static Interval nonNegative() {
        return Interval{0, std::numeric_limits<int64_t>::max()};
    }
    static Interval unconstrained() {
        return Interval{std::numeric_limits<int64_t>::min(),
                        std::numeric_limits<int64_t>::max()};
    }
    static Interval LowerBound(int64_t x) {
        return Interval{x, std::numeric_limits<int64_t>::max()};
    }
    static Interval UpperBound(int64_t x) {
        return Interval{std::numeric_limits<int64_t>::min(), x};
    }
    static Interval zero() { return Interval{0, 0}; }
};

Interval negativeInterval() {
    return Interval{std::numeric_limits<int64_t>::min(), -1};
}
Interval positiveInterval() {
    return Interval{1, std::numeric_limits<int64_t>::max()};
}

std::ostream &operator<<(std::ostream &os, Interval a) {
    return os << a.lowerBound << " : " << a.upperBound;
}

// struct PartiallyOrderedSet
// Gives partial ordering between variables, using intervals to indicate the
// range of differences in possible values.
// Example use case is for delinearization of indices
//
// for i = 0:I-1, j = 0:J-1, k = 0:K-1
//    A[M*N*i + N*j + k]
// end
//
// In the original code, this may have been A[k, j, i]
// N, M, _ = size(A)
//
// if:
// N = 10
// K = 12
// k = 11, j = 2
// then
// N*j + k == 10*2 + 11 == 31
// while if
// j = 3, k = 1
// then
// N*j + k == 10*3 + 1 == 31
//
// meaning two different values of j can give us the same
// linear index / memory address, which means we can't separate
// if on the other hand
// K <= N
// then for any particular value of `j`, no other value of `j`
// can produce the same memory address
//
// d, r = divrem(M*N*i + N*j + k, M*N)
// d = i
// r = N*j + k
//
// for this to be valid, we need M*N > N*j + k
// assuming J = M, K = N
// then we would have
// M*N > N * (M-1) + N-1 = N*M - 1
struct PartiallyOrderedSet {
    llvm::SmallVector<Interval, 0> delta;
    size_t nVar;

    PartiallyOrderedSet() : delta(llvm::SmallVector<Interval, 0>()), nVar(0){};

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
            // for (size_t k = oldSize; k < delta.size(); ++k){
            // 	assert(delta[k].lowerBound ==
            // Interval::unconstrained().lowerBound); 	assert(delta[k].upperBound
            // == Interval::unconstrained().upperBound);
            // }
        } else {
            Interval itvNew = itv.intersect(delta[l]);
            if (itvNew.equivalentRange(itv)) {
                return;
            }
            itv = itvNew;
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
        size_t j = bin2(m.prodIDs[0].getID());
        Interval itv = j < delta.size() ? delta[j] : Interval::unconstrained();
	// std::cout << "j = " << j << "; delta.size() = " << delta.size() << "; itv = " << itv;
        for (size_t i = 1; i < m.prodIDs.size(); ++i) {
            size_t j = bin2(m.prodIDs[i].getID());
            itv *= (j < delta.size() ? delta[j] : Interval::unconstrained());
        }
        return itv;
    }
    Interval asInterval(
        const Polynomial::Term<int64_t, Polynomial::Monomial> &t) const {
        return asInterval(t.exponent) * t.coefficient;
    }
    bool knownGreaterEqual(
        const Polynomial::Term<int64_t, Polynomial::Monomial> &x,
        const Polynomial::Term<int64_t, Polynomial::Monomial> &y) const {
        return knownGreaterEqual(x.exponent, y.exponent, x.coefficient,
                                 y.coefficient);
    }
    std::pair<size_t, llvm::SmallVector<int>>
    matchMonomials(const Polynomial::Monomial &x, const Polynomial::Monomial &y,
                   int64_t cx, int64_t cy) const {
        const size_t N = x.prodIDs.size();
        const size_t M = y.prodIDs.size();
        const int64_t thresh = 0;
        // TODO: generalize to handle negatives...
        int64_t scx = cx > 0 ? 1 : -1;
        int64_t scy = cy > 0 ? 1 : -1;
        int64_t acx = scx * cx;
        int64_t acy = scy * cy;
        Matrix<bool, 0, 0> bpGraph(N + (acx > thresh), M + (acy > thresh));
        for (size_t n = 0; n < N; ++n) {
            auto xid = x.prodIDs[n].getID();
            Interval xb = (*this)(xid);
            if ((xb.lowerBound < 0) && (xb.upperBound > 0)) {
                // we don't match `xb`s of unknown sign
                continue;
            }
            for (size_t m = 0; m < M; ++m) {
                // xid - yid
                Interval xyb = (*this)(y.prodIDs[m].getID(), xid);
                if (xb.lowerBound >= 0) {
                    // if xb is positive, we want (x - y) >= 0
                    bpGraph(n, m) = xyb.lowerBound >= 0;
                } else {
                    // x nonPositive, we want (x - y) <= 0
                    // (i.e., we want x of greater absolute value)
                    bpGraph(n, m) = xyb.upperBound <= 0;
                }
            }
            if (acy > thresh) {
                if (xb.lowerBound >= 0) {
                    bpGraph(n, M) = xb.lowerBound >= cy;
                } else {
                    bpGraph(n, M) = xb.upperBound <= cy;
                }
            }
        }
        if (acx > thresh) {
            for (size_t m = 0; m < M; ++m) {
                auto yid = y.prodIDs[m].getID();
                Interval yb = (*this)(yid);
                if (cx >= 0) {
                    bpGraph(N, m) = cx >= yb.upperBound;
                } else {
                    bpGraph(N, m) = cx <= yb.lowerBound;
                }
            }
            if (acy > thresh) {
                bpGraph(N, M) = acx >= acy;
            }
        }
        return maxBipartiteMatch(bpGraph);
    }

    std::pair<Interval, Interval>
    unmatchedIntervals(const Polynomial::Monomial &x,
                       const Polynomial::Monomial &y, int64_t cx = 1,
                       int64_t cy = 1) const {
        const size_t N = x.prodIDs.size();
        const size_t M = y.prodIDs.size();
        auto [matches, matchR] = matchMonomials(x, y, cx, cy);
        // matchR.size() == N
        // matchR maps ys to xs
        // if (!positive) {
        //     cx = -cx;
        //     cy = -cy;
        // }
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
    // knownGreaterEqual(x, y, cx, xy)
    // is x*cx >= y*cy
    bool knownGreaterEqual(const Polynomial::Monomial &x,
                           const Polynomial::Monomial &y, int64_t cx = 1,
                           int64_t cy = 1) const {
        const size_t N = x.prodIDs.size();
        const size_t M = y.prodIDs.size();
        if (N == 0) {
            if (M == 0) {
                return cx >= cy;
            } else if (M == 1) {
                return Interval(cx).knownGreaterEqual(
                    (*this)(y.prodIDs[0].getID()) * cy);
            }
        } else if (N == 1) {
            if (M == 0) {
                return ((*this)(x.prodIDs[0].getID()) * cx)
                    .knownGreaterEqual(Interval(cy));
            } else if (M == 1) {
                if ((cx == 1) & (cy == 1)) {
                    // x >= y -> x - y >= 0
                    return (*this)(y.prodIDs[0].getID(), x.prodIDs[0].getID())
                               .lowerBound >= 0;
                } else if ((cx == -1) & (cy == -1)) {
                    // -x >= -y -> y - x >= 0
                    return (*this)(x.prodIDs[0].getID(), y.prodIDs[0].getID())
                               .lowerBound >= 0;
                }
            }
        }
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
                      const Polynomial::Monomial &y, int64_t cx = 1,
                      int64_t cy = 1) const {
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
            int64_t lowerBound =
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
            // std::cout << "tm = " << tm << "; tn = " << tn << std::endl;
            // std::cout << "asInterval(tm) = " << asInterval(tm)
            //           << "; asInterval(tn) = " << asInterval(tn) << std::endl;
            if (termSum.lowerBound >= 0) {
                continue;
            }
            bool mExpPos = knownPositive(tm.exponent);
            bool nExpPos = knownPositive(tn.exponent);
            bool mPos, mNeg, nPos, nNeg;
            if (mExpPos) {
                mPos = tm.coefficient > 0;
                mNeg = tm.coefficient < 0;
            } else if (knownNegative(tm.exponent)) {
                mPos = tm.coefficient < 0;
                mNeg = tm.coefficient > 0;
            } else {
                return false;
                // mPos = false;
                // mNeg = false;
            }
            if (nExpPos) {
                nPos = tn.coefficient > 0;
                nNeg = tn.coefficient < 0;
            } else if (knownNegative(tn.exponent)) {
                nPos = tn.coefficient < 0;
                nNeg = tn.coefficient > 0;
            } else {
                return false;
                // nPos = false;
                // nNeg = false;
            }
            if (mPos) {
                if (nPos) {
                    // tm + tn
                    continue;
                    // } else if (nNeg) {
                } else if (nNeg && (tn.coefficient < 0)) {
                    // if tm -tn
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
                // if (mNeg) {
                if (mNeg && (tm.coefficient < 0)) {
                    // tn - tm; monomial positive
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
    // bool knownOffsetLessEqual(MPoly &x, MPoly &y, int64_t offset = 0) {
    // return knownOffsetGreaterEqual(y, x, -offset);
    // }
};
