#pragma once
#include "./Math.hpp"
#include "./LinearDiophantine.hpp"

llvm::Optional<std::pair<size_t, size_t>>
searchPivot(const SquareMatrix<intptr_t> &A, size_t k, size_t originalRows) {
    size_t N = A.size(0);
    for (size_t r = k; r < originalRows; ++r) {
        for (size_t c = k; c < N; ++c) {
            if (std::abs(A(r, c)) == 1) {
                return std::make_pair(r, c);
            }
        }
    }
    return llvm::Optional<std::pair<size_t, size_t>>();
}
void swapRowCol(SquareMatrix<intptr_t> &A, Permutation &permCol, size_t k,
                size_t i, size_t j, size_t originalRows) {
    size_t N = A.size(0);
    if (k != j) {
        for (size_t r = k; r < originalRows; ++r) {
            std::swap(A(r, j), A(r, k));
        }
        permCol.swap(k, j);
    }
    if (k != i) {
        for (size_t c = k; c < N; ++c) {
            std::swap(A(i, c), A(k, c));
        }
    }
}
// void swapCol(SquareMatrix<intptr_t> &A, size_t i,
//                 size_t j0, size_t j1) {
//     if (j0 != j1){
// 	std::swap(A(i,j0),A(i,j1));
//     }
// }

// solve c == a*x + b*y, returning g, x, y?

llvm::Optional<std::tuple<intptr_t, intptr_t, intptr_t>>
unimodularize2x3(intptr_t A00, intptr_t A01, intptr_t A02, intptr_t A10,
                 intptr_t A11, intptr_t A12) {
    intptr_t c = A00 * A11 - A01 * A10;
    intptr_t b = A02 * A10 - A00 * A12;
    intptr_t a = A01 * A12 - A02 * A11;
    return linearDiophantine(1, std::make_tuple(a, b, c));
}
llvm::Optional<std::tuple<intptr_t, intptr_t>> unimodularize1x2(intptr_t A00,
                                                                intptr_t A01) {
    return linearDiophantine(1, A00, A01);
}

SquareMatrix<intptr_t> unimodularization(const SquareMatrix<intptr_t> &Aorig,
                                         size_t originalRows) {
    SquareMatrix<intptr_t> A = Aorig;
    size_t N = A.size(0);
    // [1 x x x x]
    // [0 1 x x x]
    // [0 0 1 x x]
    // [0 0 0 x x] the last originalRow
    // [0 0 0 a b] solve the diophantine equation for `a` and `b`
    // all but last row
    Permutation permCol(N);
    // Permutation permRow(N);
    // We don't need to keep track of row permutations, as we only permute the
    // original rows. Same goes for columns, but this means unpermuting would
    // permute the added rows.
    // Plan A: permute rows and columns to get leading ones (which then zero
    // out the rest of the row before we search for the next 1).
    // for (size_t r = 0; r < originalRows; ++r){
    size_t k = 0;
    for (; k < originalRows; ++k) {
        auto maybePivot = searchPivot(A, k, originalRows);
        std::cout << "k: " << k << "; pivot: " << maybePivot.hasValue()
                  << std::endl;
        if (maybePivot.hasValue()) {
            auto [i, j] = maybePivot.getValue();
            std::cout << "pivot: (" << i << ", " << j << ")\n";
            swapRowCol(A, permCol, k, i, j, originalRows);
            std::cout << "A:\n" << A << std::endl;
        } else {
            // TODO: gcdx backup plan to find combination that produces 0
            // Then, given that fails, all hope is not lost, as we could add a
            // row that solves our problems.
            break; // assert(false && "Not implemented");
        }
        // now reduce
        intptr_t Akk = A(k, k); // 1 or -1
        for (size_t i = k + 1; i < originalRows; ++i) {
            intptr_t scale = A(i, k) * Akk;
            A(i, k) = 0;
            for (size_t j = k + 1; j < N; ++j) {
                A(i, j) -= scale * A(k, j);
            }
        }
    }
    for (size_t i = originalRows; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) {
            A(i, j) = i == j;
        }
    }
    if (originalRows < N - 1) {
        // second last row: 0 0 1 0
        // means we need:   0 0 0 1
        A(N - 1, N - 1) = 1;
    } else if (originalRows == N) {
        // we confirmed it is unimodular...perhaps not the cheapest means
    } else {
        // solve linear diophantine for last row
        // second last row: 0 0 1 x
        //                      a b
        // Must solve for a, b
        // loop from k+1 to N, solving 2x2 diagonal blocks for
        // abs(det(...)) == 1.
        // If remainder is odd, 1 at last position
        A(N - 1, N - 1) = 1;
    }
    std::cout << "A:\n" << A << std::endl;
    for (size_t i = 0; i < originalRows; ++i) {
        for (size_t j = 0; j < N; ++j) {
            A(i, j) = Aorig(i, j);
        }
    }
    for (size_t j = 0; j < N; ++j) {
        size_t jNew = permCol.inv(j);
        if (jNew != j) {
            for (size_t i = originalRows; i < N; ++i) {
                std::swap(A(i, j), A(i, jNew));
            }
        }
    }
    return A;
}


