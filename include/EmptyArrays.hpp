#pragma once
#include "./Math.hpp"
#include "Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

struct EmptyMatrix {};
constexpr EmptyMatrix matmul(EmptyMatrix, PtrMatrix<const int64_t>) {
    return EmptyMatrix{};
}
constexpr EmptyMatrix matmul(PtrMatrix<const int64_t>, EmptyMatrix) {
    return EmptyMatrix{};
}

template <typename T>
concept MaybeMatrix =
    std::is_same_v<T, IntMatrix> || std::is_same_v<T, EmptyMatrix>;

template <typename T> struct EmptyVector {
    static constexpr size_t size() { return 0; };
    static constexpr T *begin() { return nullptr; }
    static constexpr T *end() { return nullptr; }
};

template <typename T>
concept MaybeVector = std::is_same_v<T, EmptyVector<Polynomial::Monomial>> ||
    std::is_same_v<T, llvm::SmallVector<Polynomial::Monomial>>;
