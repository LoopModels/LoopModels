#include <cstddef>
#include <array>

template <class T, size_t M, size_t N>
struct Matrix {
    static constexpr size_t D = (!M + !N);

    T* content;
    const std::array<size_t, D> dims;

    Matrix(T* content, const std::array<size_t, D> dims) : content(content), dims(dims) { };
};
