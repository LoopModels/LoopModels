#include <cstddef>
#include <cstdio>
#include <array>

template <class T, size_t M, size_t N>
struct Matrix {
    static constexpr size_t D = (!M + !N);

    T* content;
    const std::array<size_t, D> dims;

    Matrix(T* content, const std::array<size_t, D> dims) : content(content), dims(dims) { };

    size_t getSize(size_t i) {
        if (i == 0) {
            return (M != 0) ? M : dims[0];
        } else {
            return (N != 0) ? N : dims[D-1];
        }
    }

    T& operator()(size_t i, size_t j) {
        return content[i + j * getSize(0)];
    }

    void show() {
        for (size_t i = 0; i < getSize(0); i++) {
            for (size_t j = 0; j < getSize(1); j++) {
                std::printf("%17d", (*this)(i, j));
            }
            std::printf("\n");
        }
    }
};
