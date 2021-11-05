#include <cstdio>
#include <cstddef>
#include <array>
#include <fmt/format.h>
#include "../include/matrix.hpp"

int main() {
    const std::array<size_t, 2> dims{ {2, 2} };
    int* f = nullptr;
    Matrix<int, 0, 0> M(f, dims);
    fmt::print("dims: {}, {}\n",  M.dims[0], M.dims[1]);
    return 0;
}
