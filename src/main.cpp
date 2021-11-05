#include <cstdio>
#include <cstddef>
#include <array>
#include <fmt/format.h>
#include "../include/matrix.hpp"

int main() {
    const std::array<size_t, 2> dims{ {2, 2} };
    int* f = new int[4];
    for (size_t i=0; i<4; i++) f[i] = i + 1;
    Matrix<int, 0, 0> M(f, dims);
    fmt::print("dims: {}, {}\n",  M.dims[0], M.dims[1]);
    fmt::print("A[0, 1]: {}\n",  M(0, 1));
    M.show();
    delete[] f;
    return 0;
}
