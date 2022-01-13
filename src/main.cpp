#include <cstdio>
#include <cstddef>
#include <array>
#include "../include/math.hpp"
#include "../include/show.hpp"

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
    showln(M);
    // delete[] f;
    return 0;
}
