#pragma once
#include <iostream>
#include <llvm/ADT/SmallVector.h>

template <typename T> void show(llvm::SmallVectorImpl<T> const &x) {
    printVector(std::cout, x);
}
template <typename T>
concept LeftLeftPrint = requires(std::ostream &os, const T &a) {
    { os << a };
};
void show(LeftLeftPrint auto x) { std::cout << x; }
void showln(auto x) {
    show(x);
    std::cout << std::endl;
}
