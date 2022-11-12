#pragma once
#include "./Math.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

template <typename T> struct EmptyMatrix {
    using eltype = T;
    static constexpr bool canResize = false;
    static constexpr bool isMutable = false;
    static constexpr T getLinearElement(size_t) { return 0; }
    static constexpr T *begin() { return nullptr; }
    static constexpr T *end() { return nullptr; }

    static constexpr size_t numRow() { return 0; }
    static constexpr size_t numCol() { return 0; }
    static constexpr size_t rowStride() { return 0; }
    static constexpr size_t colStride() { return 0; }
    static constexpr size_t getConstCol() { return 0; }

    static constexpr T *data() { return nullptr; }
    constexpr T operator()(size_t, size_t) { return 0; }
    static constexpr std::pair<size_t, size_t> size() {
        return std::make_pair(0, 0);
    }
    static constexpr EmptyMatrix<T> view() { return EmptyMatrix<T>{}; }
};

static_assert(AbstractMatrix<EmptyMatrix<int64_t>>);

template <typename T>
constexpr EmptyMatrix<T> matmul(EmptyMatrix<T>, PtrMatrix<const T>) {
    return EmptyMatrix<T>{};
}
template <typename T>
constexpr EmptyMatrix<T> matmul(PtrMatrix<const T>, EmptyMatrix<T>) {
    return EmptyMatrix<T>{};
}

template <typename T, typename S>
concept MaybeMatrix =
    std::is_same_v<T, Matrix<S>> || std::is_same_v<T, EmptyMatrix<S>>;

template <typename T> struct EmptyVector {
    static constexpr size_t size() { return 0; };
    static constexpr T *begin() { return nullptr; }
    static constexpr T *end() { return nullptr; }
};

template <typename T, typename S>
concept MaybeVector = std::is_same_v<T, EmptyVector<S>> ||
                      std::is_same_v<T, llvm::SmallVector<S>>;
