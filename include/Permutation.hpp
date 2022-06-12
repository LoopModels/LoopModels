#pragma once
#include "./Math.hpp"

struct Permutation {
    Matrix<unsigned, 0, 2> data;

    Permutation(size_t nloops) : data(Matrix<unsigned, 0, 2>(nloops)) {
        assert(nloops <= MAX_NUM_LOOPS);
        init();
    };

    unsigned &operator()(size_t i) { return data(i, 0); }
    unsigned operator()(size_t i) const { return data(i, 0); }
    bool operator==(Permutation y) {
        return data.getCol(0) == y.data.getCol(0);
    }
    size_t getNumLoops() const { return data.size(0); }
    size_t length() const { return data.length(); }

    // llvm::MutableArrayRef<unsigned> inv() { return data.getCol(1); }
    llvm::ArrayRef<unsigned> inv() { return data.getCol(1); }

    unsigned &inv(size_t j) { return data(j, 1); }
    auto begin() { return data.begin(); }
    auto end() { return data.begin() + data.size(0); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.begin() + data.size(0); }

    void init() {
        size_t numloops = getNumLoops();
        for (size_t n = 0; n < numloops; n++) {
            data(n, 0) = n;
            data(n, 1) = n;
        }
    }
    void swap(size_t i, size_t j) {
        size_t xi = data(i, 0);
        size_t xj = data(j, 0);
        data(i, 0) = xj;
        data(j, 0) = xi;
        data(xj, 1) = i;
        data(xi, 1) = j;
    }

    struct Original {
        size_t i;
        operator size_t() { return i; }
    };
    struct Permuted {
        size_t i;
        operator size_t() { return i; }
    };
    unsigned &operator()(Original i) { return data(i, 0); }
    unsigned &operator()(Permuted i) { return data(i, 1); }
    unsigned operator()(Original i) const { return data(i, 0); }
    unsigned operator()(Permuted i) const { return data(i, 1); }
};
std::ostream &operator<<(std::ostream &os, Permutation const &perm) {
    auto numloop = perm.getNumLoops();
    os << "perm: {";
    for (size_t j = 0; j < numloop - 1; j++) {
        os << perm(j) << " ";
    }
    os << perm(numloop - 1) << "}";
    return os;
}
