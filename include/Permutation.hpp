#pragma once
#include "./Math.hpp"

struct Permutation {
    Matrix<unsigned, 2, 0> data;

    Permutation(size_t nloops) : data(Matrix<unsigned, 2, 0>(nloops)) {
        // assert(nloops <= MAX_NUM_LOOPS);
        init();
    };

    unsigned &operator()(size_t i) { return data(0, i); }
    unsigned operator()(size_t i) const { return data(0, i); }
    bool operator==(Permutation y) {
        return data.getRow(0) == y.data.getRow(0);
    }
    size_t getNumLoops() const { return data.numCol(); }
    size_t length() const { return data.length(); }

    // llvm::MutableArrayRef<unsigned> inv() { return data.getCol(1); }
    llvm::ArrayRef<unsigned> inv() { return data.getRow(1); }

    unsigned &inv(size_t j) { return data(1, j); }
    auto begin() { return data.begin(); }
    auto end() { return data.begin() + data.numCol(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.begin() + data.numCol(); }

    void init() {
        size_t numloops = getNumLoops();
        for (size_t n = 0; n < numloops; n++) {
            data(0, n) = n;
            data(1, n) = n;
        }
    }
    void swap(size_t i, size_t j) {
        size_t xi = data(0, i);
        size_t xj = data(0, j);
        data(0, i) = xj;
        data(0, j) = xi;
        data(1, xj) = i;
        data(1, xi) = j;
    }

    struct Original {
        size_t i;
        operator size_t() { return i; }
    };
    struct Permuted {
        size_t i;
        operator size_t() { return i; }
    };
    unsigned &operator()(Original i) { return data(0, i); }
    unsigned &operator()(Permuted i) { return data(1, i); }
    unsigned operator()(Original i) const { return data(0, i); }
    unsigned operator()(Permuted i) const { return data(1, i); }
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
