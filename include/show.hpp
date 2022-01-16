#pragma once
#include "llvm/ADT/SmallVector.h"
#include <iostream>
#include <vector>

template <typename T> void show_vector(T const &x) {
    std::cout << "std::vector, size = " << x.size() << std::endl;
    if (x.size()) {
        std::cout << "[";
        auto it = x.begin();
        std::cout << *it;
        ++it;
        for (; it != x.end(); ++it) {
            std::cout << ", " << *it;
        }
        std::cout << "]";
    }
}

template <typename T> void show(std::vector<T> const &x) { show_vector(x); }
template <typename T> void show(llvm::SmallVector<T> const &x) {
    show_vector(x);
}
template <typename T, unsigned N> void show(llvm::SmallVector<T,N> const &x) {
    show_vector(x);
}
void show(auto x) { std::cout << x; }
void showln(auto x) {
    show(x);
    std::cout << std::endl;
}
