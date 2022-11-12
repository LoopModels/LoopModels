#pragma once
#include "./Math.hpp"
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

// A set of `size_t` elements.
// Initially constructed
struct BitSet {
    llvm::SmallVector<uint64_t> data;
    // size_t operator[](size_t i) const {
    //     return data[i];
    // } // allow `getindex` but not `setindex`
    BitSet() = default;
    size_t static constexpr numElementsNeeded(size_t N) {
        return (N + 63) >> 6;
    }
    BitSet(size_t N)
        : data(llvm::SmallVector<uint64_t>(numElementsNeeded(N))) {}
    static BitSet dense(size_t N) {
        BitSet b;
        b.data.resize(numElementsNeeded(N),
                      std::numeric_limits<uint64_t>::max());
        if (size_t rem = N & 63)
            b.data.back() = (size_t(1) << rem) - 1;
        return b;
    }
    size_t maxValue() const {
        size_t N = data.size();
        return N ? (64 * N - std::countl_zero(data[N - 1])) : 0;
    }
    struct Iterator {
        llvm::SmallVectorTemplateCommon<uint64_t>::const_iterator it;
        llvm::SmallVectorTemplateCommon<uint64_t>::const_iterator end;
        uint64_t istate;
        size_t cstate0{std::numeric_limits<size_t>::max()};
        size_t cstate1{0};
        size_t operator*() const { return cstate0 + cstate1; }
        Iterator &operator++() {
            while (istate == 0) {
                ++it;
                if (it == end)
                    return *this;
                istate = *it;
                cstate0 = std::numeric_limits<size_t>::max();
                cstate1 += 64;
            }
            size_t tzp1 = std::countr_zero(istate) + 1;
            cstate0 += tzp1;
            istate >>= tzp1;
            return *this;
        }
        Iterator operator++(int) {
            Iterator temp = *this;
            ++*this;
            return temp;
        }
        struct End {
            ptrdiff_t operator-(Iterator it) {
                ptrdiff_t i = 0;
                for (; it != End{}; ++it, ++i) {
                }
                return i;
            }
            constexpr bool operator==(End) const { return true; }
        };
        bool operator==(End) const { return it == end && (istate == 0); }
        bool operator!=(End) const { return it != end || (istate != 0); }
        bool operator==(Iterator j) const {
            return (it == j.it) && (istate == j.istate);
        }
    };
    // BitSet::Iterator(std::vector<std::uint64_t> &seta)
    //     : set(seta), didx(0), offset(0), state(seta[0]), count(0) {};
    Iterator begin() const {
        auto b{data.begin()};
        auto e{data.end()};
        if (b == e)
            return Iterator{b, e, 0};
        Iterator it{b, e, *b};
        return ++it;
    }
    Iterator::End end() const { return Iterator::End{}; };
    size_t front() const {
        for (size_t i = 0; i < data.size(); ++i)
            if (data[i])
                return 64 * i + std::countr_zero(data[i]);
        return std::numeric_limits<size_t>::max();
    }
    static uint64_t contains(llvm::ArrayRef<uint64_t> data, size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        return (data[d] & (mask));
    }
    uint64_t contains(size_t i) const { return contains(data, i); }

    bool insert(size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        if (d >= data.size())
            data.resize(d + 1);
        bool contained = ((data[d] & mask) != 0);
        if (!contained)
            data[d] |= (mask);
        return contained;
    }
    void uncheckedInsert(size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        if (d >= data.size())
            data.resize(d + 1);
        data[d] |= (mask);
    }

    bool remove(size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        bool contained = ((data[d] & mask) != 0);
        if (contained)
            data[d] &= (~mask);
        return contained;
    }
    static void set(llvm::MutableArrayRef<uint64_t> data, size_t x, bool b) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        uint64_t dd = data[d];
        if (b == ((dd & mask) != 0))
            return;
        if (b) {
            data[d] = dd | mask;
        } else {
            data[d] = dd & (~mask);
        }
    }

    struct Reference {
        llvm::MutableArrayRef<uint64_t> data;
        size_t i;
        operator bool() const { return contains(data, i); }
        void operator=(bool b) {
            BitSet::set(data, i, b);
            return;
        }
    };

    bool operator[](size_t i) const { return contains(data, i); }
    Reference operator[](size_t i) {
        return Reference{llvm::MutableArrayRef<uint64_t>(data), i};
    }
    size_t size() const {
        size_t s = 0;
        for (auto u : data)
            s += std::popcount(u);
        return s;
    }
    bool any() const {
        for (auto u : data)
            if (u)
                return true;
        return false;
    }
    void setUnion(const BitSet &bs) {
        size_t O = bs.data.size(), T = data.size();
        if (O > T)
            data.resize(O);
        for (size_t i = 0; i < O; ++i) {
            uint64_t d = data[i] | bs.data[i];
            data[i] = d;
        }
    }
    BitSet &operator&=(const BitSet &bs) {
        if (bs.data.size() < data.size())
            data.resize(bs.data.size());
        for (size_t i = 0; i < data.size(); ++i)
            data[i] &= bs.data[i];
        return *this;
    }
    // &!
    BitSet &operator-=(const BitSet &bs) {
        if (bs.data.size() < data.size())
            data.resize(bs.data.size());
        for (size_t i = 0; i < data.size(); ++i)
            data[i] &= (~bs.data[i]);
        return *this;
    }
    BitSet &operator|=(const BitSet &bs) {
        if (bs.data.size() > data.size())
            data.resize(bs.data.size());
        for (size_t i = 0; i < bs.data.size(); ++i)
            data[i] |= bs.data[i];
        return *this;
    }
    BitSet operator&(const BitSet &bs) const {
        BitSet r = *this;
        return r &= bs;
    }
    BitSet operator|(const BitSet &bs) const {
        BitSet r = *this;
        return r |= bs;
    }
    bool operator==(const BitSet &bs) const { return data == bs.data; }
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, BitSet const &x) {
    os << "BitSet[";
    auto it = x.begin();
    BitSet::Iterator::End e = x.end();
    if (it != e) {
        os << *(it++);
        for (; it != e; ++it)
            os << ", " << *it;
    }
    os << "]";
    return os;
}

// BitSet with length 64
struct BitSet64 {
    uint64_t u;
    BitSet64() : u(0) {}
    BitSet64(uint64_t u) : u(u) {}
    struct Reference {
        uint64_t &u;
        size_t i;
        operator bool() const { return (u >> i) != 0; }
        void operator=(bool b) {
            uint64_t flag = uint64_t(1) << i;
            if (b) {
                u |= flag;
            } else {
                u &= ~flag;
            }
            return;
        }
    };
    bool operator[](size_t i) { return Reference{u, i}; }
    bool operator[](size_t i) const { return (u >> i) != 0; }
    struct Iterator {
        uint64_t u;
        size_t i{0};
        struct End {};
        size_t operator++() {
            auto tz = std::countr_zero(u);
            i += ++tz;
            u >>= tz;
            return i;
        }
        size_t operator++(int) {
            size_t ii = i;
            auto tz = std::countr_zero(u);
            i += ++tz;
            u >>= tz;
            return ii;
        }
        size_t operator*() { return i; }
        bool operator==(End) { return !u; }
    };
    Iterator begin() const { return Iterator{u}; }
    Iterator::End end() const { return Iterator::End{}; }
    struct ReverseIterator {
        uint64_t u;
        size_t i;
        bool operator==(Iterator::End) { return !u; }
    };
    ReverseIterator rbegin() const {
        return ReverseIterator{u, size_t(64) - size_t(std::countl_zero(u))};
    }
    Iterator::End rend() const { return Iterator::End{}; }
    void set(size_t i) {
        u |= (uint64_t(1) << i);
        return;
    }
    void pushFirst(bool b) { u = (u << 1) | b; }
    void erase(size_t i) { // erase `i` (0-indexed) and shift all remaining
        // `i = 5`, then `mLower = 31` (`000...011111`)
        uint64_t mLower = (uint64_t(1) << i) - 1;
        uint64_t mUpper = ~mLower; // (`111...100000`)
        u = (u & mLower) | ((u + mUpper) >> 1);
    }
};

template <typename T> struct BitSliceView {
    llvm::MutableArrayRef<T> a;
    const BitSet &i;
    struct Iterator {
        llvm::MutableArrayRef<T> a;
        BitSet::Iterator it;
        bool operator==(BitSet::Iterator::End) const {
            return it == BitSet::Iterator::End{};
        }
        Iterator &operator++() {
            ++it;
            return *this;
        }
        Iterator operator++(int) {
            Iterator temp = *this;
            ++it;
            return temp;
        }
        T &operator*() { return a[*it]; }
        const T &operator*() const { return a[*it]; }
        T *operator->() { return &a[*it]; }
        const T *operator->() const { return &a[*it]; }
    };
    Iterator begin() { return {a, i.begin()}; }
    struct ConstIterator {
        llvm::ArrayRef<T> a;
        BitSet::Iterator it;
        bool operator==(BitSet::Iterator::End) const {
            return it == BitSet::Iterator::End{};
        }
        bool operator==(ConstIterator c) const {
            return (it == c.it) && (a.data() == c.a.data());
        }
        ConstIterator &operator++() {
            ++it;
            return *this;
        }
        ConstIterator operator++(int) {
            ConstIterator temp = *this;
            ++it;
            return temp;
        }
        const T &operator*() const { return a[*it]; }
        const T *operator->() const { return &a[*it]; }
    };
    ConstIterator begin() const { return {a, i.begin()}; }
    BitSet::Iterator::End end() const { return {}; }
    size_t size() const { return i.size(); }
};
ptrdiff_t operator-(BitSet::Iterator::End, BitSliceView<int64_t>::Iterator v) {
    return BitSet::Iterator::End{} - v.it;
}
ptrdiff_t operator-(BitSet::Iterator::End,
                    BitSliceView<int64_t>::ConstIterator v) {
    return BitSet::Iterator::End{} - v.it;
}

template <> struct std::iterator_traits<BitSet::Iterator> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = size_t;
    using reference_type = size_t &;
    using pointer_type = size_t *;
};
template <> struct std::iterator_traits<BitSliceView<int64_t>::Iterator> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = int64_t;
    using reference_type = int64_t &;
    using pointer_type = int64_t *;
};
template <> struct std::iterator_traits<BitSliceView<int64_t>::ConstIterator> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = int64_t;
    using reference_type = int64_t &;
    using pointer_type = int64_t *;
};
struct ScheduledNode;
template <> struct std::iterator_traits<BitSliceView<ScheduledNode>::Iterator> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = ScheduledNode;
    using reference_type = ScheduledNode &;
    using pointer_type = ScheduledNode *;
};
template <>
struct std::iterator_traits<BitSliceView<ScheduledNode>::ConstIterator> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = ScheduledNode;
    using reference_type = ScheduledNode &;
    using pointer_type = ScheduledNode *;
};

// typedef
// std::iterator_traits<BitSliceView<int64_t>::Iterator>::iterator_category;

static_assert(std::movable<BitSliceView<int64_t>::Iterator>);
static_assert(std::movable<BitSliceView<int64_t>::ConstIterator>);

static_assert(std::weakly_incrementable<BitSliceView<int64_t>::Iterator>);
static_assert(std::weakly_incrementable<BitSliceView<int64_t>::ConstIterator>);
static_assert(std::input_or_output_iterator<BitSliceView<int64_t>::Iterator>);
static_assert(
    std::input_or_output_iterator<BitSliceView<int64_t>::ConstIterator>);
// static_assert(std::indirectly_readable<BitSliceView<int64_t>::Iterator>);
static_assert(std::indirectly_readable<BitSliceView<int64_t>::ConstIterator>);
// static_assert(std::input_iterator<BitSliceView<int64_t>::Iterator>);
static_assert(std::input_iterator<BitSliceView<int64_t>::ConstIterator>);
static_assert(std::ranges::range<BitSliceView<int64_t>>);
static_assert(std::ranges::range<const BitSliceView<int64_t>>);
// static_assert(std::ranges::forward_range<BitSliceView<int64_t>>);
static_assert(std::ranges::forward_range<const BitSliceView<int64_t>>);

static_assert(std::ranges::range<BitSet>);
