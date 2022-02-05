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

    std::tuple<Interval, Interval, Interval> restrictAdd(Interval a,
                                                         Interval b) const {
        Interval cNew = this->intersect(a - b);
        Interval aNew = a.intersect(*this - b);
        Interval bNew = b.intersect(*this - a);
        assert(!cNew.isEmpty());
        assert(!aNew.isEmpty());
        assert(!bNew.isEmpty());
        return std::make_tuple(cNew, aNew, bNew);
    }
    std::tuple<Interval, Interval, Interval> restrictSub(Interval a,
                                                         Interval b) const {
        Interval cNew = this->intersect(a - b);
        Interval aNew = a.intersect(*this + b);
        Interval bNew = b.intersect(a - *this);
        assert(!cNew.isEmpty());
        assert(!aNew.isEmpty());
        assert(!bNew.isEmpty());
        return std::make_tuple(cNew, aNew, bNew);
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
