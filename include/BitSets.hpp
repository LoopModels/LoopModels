#pragma once
#include "./Math.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <ostream>
#include <string>

// A set of `size_t` elements.
// Initially constructed
struct BitSet {
    llvm::SmallVector<uint64_t> data;
    size_t length;
    // size_t operator[](size_t i) const {
    //     return data[i];
    // } // allow `getindex` but not `setindex`

    BitSet(size_t N) : length(0) {
        size_t len = (N + 63) >> 6;
        data.resize(len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = 0;
        }
        // data = std::vector<std::uint64_t>(0, (N + 63) >> 6);
    }
    struct Iterator {
        // TODO: is this safe?
        llvm::ArrayRef<uint64_t> set;
        size_t didx;
        uint64_t offset; // offset with 64 bit block
        uint64_t state;
        size_t count;

        size_t operator*() { return offset + 64 * didx; }
        BitSet::Iterator &operator++() {
            ++count;
            while (state == 0) {
                ++didx;
                if (didx >= set.size())
                    return *this;
                state = set[didx];
                offset = std::numeric_limits<uint64_t>::max();
                // offset = 0xffffffffffffffff;
            }
            size_t tzp1 = std::countr_zero(state) + 1;
            offset += tzp1;
            state >>= tzp1;
            return *this;
        }
        bool operator!=(size_t x) { return count != x; }
        bool operator==(size_t x) { return count == x; }
        bool operator!=(BitSet::Iterator x) { return count != x.count; }
        bool operator==(BitSet::Iterator x) { return count == x.count; }
    };
    // BitSet::Iterator(std::vector<std::uint64_t> &seta)
    //     : set(seta), didx(0), offset(0), state(seta[0]), count(0) {};
    static Iterator construct(llvm::ArrayRef<uint64_t> const &seta) {
        return Iterator{seta, 0, std::numeric_limits<uint64_t>::max(), seta[0],
                        std::numeric_limits<uint64_t>::max()};
    }

    Iterator begin() const { return ++construct(this->data); }
    size_t end() const { return length; };

    static uint64_t contains(llvm::ArrayRef<uint64_t> data, size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        return (data[d] & (mask));
    }

    bool push(size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        bool contained = ((data[d] & mask) != 0);
        if (!contained) {
            data[d] |= (mask);
            ++(length);
        }
        return contained;
    }

    bool remove(size_t x) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        bool contained = ((data[d] & mask) != 0);
        if (contained) {
            data[d] &= (~mask);
            --(length);
        }
        return contained;
    }
    static void set(llvm::MutableArrayRef<uint64_t> data, size_t x, bool b,
                    size_t &length) {
        size_t d = x >> size_t(6);
        uint64_t r = uint64_t(x) & uint64_t(63);
        uint64_t mask = uint64_t(1) << r;
        uint64_t dd = data[d];
        if (b == ((dd & mask) != 0))
            return;
        if (b) {
            data[d] = dd | mask;
            ++length;
        } else {
            data[d] = dd & (~mask);
            --(length);
        }
    }

    struct Reference {
        llvm::MutableArrayRef<uint64_t> data;
        size_t i;
        size_t &length;
        operator bool() const { return contains(data, i); }
        void operator=(bool b) {
            BitSet::set(data, i, b, length);
            return;
        }
    };

    bool operator[](size_t i) const { return contains(data, i); }
    Reference operator[](size_t i) {
        return Reference{llvm::MutableArrayRef<uint64_t>(data), i, length};
    }
    size_t size() const { return length; }
};

std::ostream &operator<<(std::ostream &os, BitSet const &x) {
    os << "BitSet[";
    if (x.length) {
        auto it = x.begin();
        os << std::to_string(*it);
        ++it;
        for (; it != x.end(); ++it) {
            os << ", " << *it;
        }
    }
    os << "]";
    return os;
}

// BitSet with length 64
struct BitSet64 {
    uint64_t u;
    BitSet64() : u(0) {}
    BitSet64(uint64_t u) : u(u) {}
    bool operator[](size_t i) { return (u >> i) != 0; }
    void set(size_t i) {
        u |= (uint64_t(1) << i);
        return;
    }
    void erase(size_t i) { // erase `i` (0-indexed) and shift all remaining
        // `i = 5`, then `mLower = 31` (`000...011111`)
        uint64_t mLower = (uint64_t(1) << i) - 1;
        uint64_t mUpper = ~mLower; // (`111...100000`)
        u = (u & mLower) | ((u + mUpper) >> 1);
    }
};
