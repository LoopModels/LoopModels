#pragma once

#include "./POSet.hpp"
#include "Symbolics.hpp"
#include <cassert>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

// for non-symbolic Polyhedra
struct LiteralComparator {
    static constexpr size_t numConstantTerms() { return 1; }
    static inline bool greaterEqual(llvm::ArrayRef<int64_t> x,
                                    llvm::ArrayRef<int64_t> y) {
        return x[0] >= y[0];
    }
    static inline bool greater(llvm::ArrayRef<int64_t> x,
                               llvm::ArrayRef<int64_t> y) {
        return x[0] > y[0];
    }
    static inline bool lessEqual(llvm::ArrayRef<int64_t> x,
                                 llvm::ArrayRef<int64_t> y) {
        return x[0] <= y[0];
    }
    static inline bool less(llvm::ArrayRef<int64_t> x,
                            llvm::ArrayRef<int64_t> y) {
        return x[0] < y[0];
    }
    static inline bool equal(llvm::ArrayRef<int64_t> x,
                             llvm::ArrayRef<int64_t> y) {
        return x[0] == y[0];
    }
};
// BaseComparator defines all other comparator methods as a function of
// `greaterEqual`, so that `greaterEqual` is the only one that needs to be
// implemented.
// An assumption is that index `0` is a literal constant, and only indices >0
// are symbolic. Thus, we can shift index-0 to swap between `(>/<)=` and ``>/<`
// comparisons.
template <typename T> struct BaseComparator {
    inline bool greaterEqual(llvm::ArrayRef<int64_t> x,
                             llvm::ArrayRef<int64_t> y) const {
        return static_cast<const T *>(this)->greaterEqual(x, y);
    }
    inline bool less(llvm::ArrayRef<int64_t> x,
                     llvm::ArrayRef<int64_t> y) const {
        return static_cast<const T *>(this)->greaterEqual(y, x);
    }
    inline bool greater(llvm::MutableArrayRef<int64_t> x,
                        llvm::ArrayRef<int64_t> y) const {
        int64_t x0 = x[0]--;
        bool ret = static_cast<const T *>(this)->greaterEqual(x, y);
        x[0] = x0;
        return ret;
    }
    inline bool greater(llvm::ArrayRef<int64_t> x,
                        llvm::ArrayRef<int64_t> y) const {
        // TODO: avoid this needless memcopy and (possible) allocation?
        llvm::SmallVector<int64_t, 8> xm{x.begin(), x.end()};
        return greater(xm, y);
    }
    inline bool lessEqual(llvm::ArrayRef<int64_t> x,
                          llvm::ArrayRef<int64_t> y) const {
        return greater(y, x);
    }
    inline bool lessEqual(llvm::ArrayRef<int64_t> x,
                          llvm::MutableArrayRef<int64_t> y) const {
        return greater(y, x);
    }
    inline bool equal(llvm::ArrayRef<int64_t> x,
                      llvm::ArrayRef<int64_t> y) const {
        return static_cast<const T *>(this)->greaterEqual(x, y) &&
               static_cast<const T *>(this)->greaterEqual(y, x);
    }
};

struct SymbolicComparator : BaseComparator<SymbolicComparator> {
    PartiallyOrderedSet POSet;
    llvm::SmallVector<Polynomial::Monomial> monomials;
    size_t numConstantTerms() const { return monomials.size(); }
    bool greaterEqual(llvm::SmallVector<int64_t> x,
                      llvm::SmallVector<int64_t> y) const {
        MPoly delta;
        assert(x.size());
        assert(x.size() == y.size());
        assert(x.size() == 1 + monomials.size());
        for (size_t i = 0; i < monomials.size(); ++i)
            if (int64_t d = x[i + 1] - y[i + 1])
                delta.terms.emplace_back(d, monomials[i]);
        if (int64_t d = x[0] - y[0])
            delta.terms.emplace_back(d);
        return POSet.knownLessThanZero(delta);
    }
};

template <typename T>
concept Comparator = requires(T t) {
    { t.numConstantTerms() } -> std::convertible_to<size_t>;
}
&&requires(T t, llvm::ArrayRef<int64_t> x) {
    { t.greaterEqual(x, x) } -> std::convertible_to<bool>;
    { t.lessEqual(x, x) } -> std::convertible_to<bool>;
    { t.greater(x, x) } -> std::convertible_to<bool>;
    { t.less(x, x) } -> std::convertible_to<bool>;
    { t.equal(x, x) } -> std::convertible_to<bool>;
};
