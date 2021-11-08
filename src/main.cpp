#include <cstdio>
#include <cstddef>
#include <array>
#include <fmt/format.h>
#include "../include/math.hpp"

int main() {
    int f[4];
    // int* f = new int[4];
    // for (size_t i=0; i<4; i++) f[i] = i + 1;
    Matrix<int, 0, 0> M(f, 2, 2);
    for (int m = 0; m < 2; m++){
        for (int n = 0; n < 2; n++){
            M(n,m) = 10*n+m;
        }
    }
    fmt::print("dims: {}, {}\n",  M.dims[0], M.dims[1]);
    fmt::print("A[0, 1]: {}\n",  M(0, 1));
    showln(M);
    // delete[] f;
    return 0;
}
