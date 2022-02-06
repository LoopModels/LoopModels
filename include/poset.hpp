#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
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
};

Interval negativeInterval() {
    return Interval{std::numeric_limits<intptr_t>::min(), -1};
}
Interval positiveInterval() {
    return Interval{1, std::numeric_limits<intptr_t>::max()};
}

std::ostream &operator<<(std::ostream &os, Interval a) {
    return os << a.lowerBound << " : " << a.upperBound << std::endl;
}

struct PartiallyOrderedSet {
    llvm::SmallVector<Interval, 0> delta;
    uintptr_t nVar;

    PartiallyOrderedSet() : nVar(0){};

    inline static uintptr_t bin2(uintptr_t i) { return (i * (i - 1)) >> 1; }
    inline static uintptr_t uncheckedLinearIndex(uintptr_t i, uintptr_t j) {
        return i + bin2(j);
    }
    inline static std::pair<uintptr_t, bool> checkedLinearIndex(uintptr_t ii,
                                                                uintptr_t jj) {
        uintptr_t i = std::min(ii, jj);
        uintptr_t j = std::max(ii, jj);
        return std::make_pair(i + bin2(j), jj < ii);
    }

    Interval update(uintptr_t i, uintptr_t j, Interval ji) {
        uintptr_t iOff = bin2(i);
        uintptr_t jOff = bin2(j);

        for (uintptr_t k = 0; k < i; ++k) {
            Interval ik = delta[k + iOff];
            Interval jk = delta[k + jOff];

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
        uintptr_t kOff = iOff;
        for (uintptr_t k = i + 1; k < j; ++k) {
            kOff += (k-1);
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
        for (uintptr_t k = j + 1; k < nVar; ++k) {
            kOff += (k-1);
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
    void push(uintptr_t i, uintptr_t j, Interval itv) {
        if (i > j) {
            return push(j, i, -itv);
        }
        assert(j > i); // i != j
        uintptr_t jOff = bin2(j);
        uintptr_t l = jOff + i;
        if (j >= nVar) {
            nVar = j+1;
            delta.resize((j*nVar)>>1, Interval::unconstrained());
        } else {
            itv = itv.intersect(delta[l]);
        }
        delta[l] = update(i, j, itv);
    }
    Interval operator()(uintptr_t i, uintptr_t j) {
        auto [l, f] = checkedLinearIndex(i, j);
        Interval d = delta[l];
        return f ? -d : d;
    }
};
