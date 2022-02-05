#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <tuple>

intptr_t saturated_add(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_add_overflow(a, b, &c)) {
        c = ((a > 0) & (b > 0)) ? std::numeric_limits<intptr_t>::max()
                                : std::numeric_limits<intptr_t>::min();
    }
    return c;
}
intptr_t saturated_sub(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_sub_overflow(a, b, &c)) {
        c = ((a > 0) & (b < 0)) ? std::numeric_limits<intptr_t>::max()
                                : std::numeric_limits<intptr_t>::min();
    }
    return c;
}
intptr_t saturated_mul(intptr_t a, intptr_t b) {
    intptr_t c;
    if (__builtin_mul_overflow(a, b, &c)) {
        c = ((a > 0) ^ (b > 0)) ? std::numeric_limits<intptr_t>::min()
                                : std::numeric_limits<intptr_t>::max();
    }
    return c;
}

struct Interval {
    intptr_t lowerBound, upperBound;
    Interval intersect(Interval b) const {
        return Interval{std::max(lowerBound, b.lowerBound),
                        std::min(upperBound, b.upperBound)};
    }
    bool isEmpty() const { return lowerBound > upperBound; }
    Interval operator+(Interval b) const {
        return Interval{saturated_add(lowerBound, b.lowerBound),
                        saturated_add(upperBound, b.upperBound)};
    }
    Interval operator-(Interval b) const {
        return Interval{saturated_sub(lowerBound, b.upperBound),
                        saturated_add(upperBound, b.lowerBound)};
    }

    Interval operator*(Interval b) const {
        intptr_t ll = saturated_mul(lowerBound, b.lowerBound);
        intptr_t lu = saturated_mul(lowerBound, b.upperBound);
        intptr_t ul = saturated_mul(upperBound, b.lowerBound);
        intptr_t uu = saturated_mul(upperBound, b.upperBound);
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
