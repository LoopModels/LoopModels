#pragma once
#include "./LinearDiophantine.hpp"
#include "./Math.hpp"
#include "./NormalForm.hpp"
#include <cstddef>
#include <cstdint>

// function unimod_hnf(A)
//     H, U = Matrix.(hnf_with_transform(MatrixSpace(ZZ, size(A')...)(A')))
//     (isdiag(H) && all(isone, @views H[diagind(H)])) || return nothing
//     [A; Int.(inv(U' .// 1))[size(A, 1)+1:end, :]]
// end

// if `A` can be unimodularized, returns the inverse of the unimodularized `A`
llvm::Optional<SquareMatrix<int64_t>> unimodularize(IntMatrix A) {
    llvm::Optional<std::pair<IntMatrix, SquareMatrix<int64_t>>> OHNF =
        NormalForm::hermite(std::move(A));
    if (!OHNF.hasValue()) {
        return {};
    }
    auto &[H, U] = OHNF.getValue();
    for (size_t m = 0; m < H.numCol(); ++m) {
        if (H(m, m) != 1) {
            // unimodularization was not succesful
            return {};
        }
    }
    return std::move(U);
}
/*

llvm::Optional<std::pair<size_t, size_t>>
searchPivot(const SquareMatrix<int64_t> &A, size_t k, size_t originalRows) {
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
void swapRowCol(SquareMatrix<int64_t> &A, Permutation &permCol, size_t k,
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
// void swapCol(SquareMatrix<int64_t> &A, size_t i,
//                 size_t j0, size_t j1) {
//     if (j0 != j1){
// 	std::swap(A(i,j0),A(i,j1));
//     }
// }

// solve c == a*x + b*y, returning g, x, y?

llvm::Optional<std::tuple<int64_t, int64_t, int64_t>>
unimodularize2x3(int64_t A00, int64_t A01, int64_t A02, int64_t A10,
                 int64_t A11, int64_t A12) {
    int64_t c = A00 * A11 - A01 * A10;
    int64_t b = A02 * A10 - A00 * A12;
    int64_t a = A01 * A12 - A02 * A11;
    return linearDiophantine(1, std::make_tuple(a, b, c));
}

llvm::Optional<std::pair<std::tuple<int64_t, int64_t, int64_t>,
                         std::tuple<int64_t, int64_t, int64_t>>>
unimodularize1x3(int64_t a, int64_t b, int64_t c) {
    // we need to find a second row, we let x = 1
    // we need
    // 1 == gcd(b - a*y, c - a*z)
    // int64_t args[3];
    if (gcd(b, c) == 1) {
        auto t1 = std::make_tuple(int64_t(1), int64_t(0), int64_t(0));
        auto t2 = unimodularize2x3(a, b, c, 1, 0, 0);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    if (gcd(a, c) == 1) {
        auto t1 = std::make_tuple(int64_t(0), int64_t(1), int64_t(0));
        auto t2 = unimodularize2x3(a, b, c, 0, 1, 0);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    if (gcd(a, b) == 1) {
        auto t1 = std::make_tuple(int64_t(0), int64_t(0), int64_t(1));
        auto t2 = unimodularize2x3(a, b, c, 0, 0, 1);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    // just try [1, 0, 1], [1, 1, 0], and [0, 1, 1] as solutions
    for (size_t i = 0; i < 3; ++i) {
        auto t1 = std::make_tuple(int64_t(i != 0), int64_t(i != 1),
                                  int64_t(i != 2));
        auto t2 = unimodularize2x3(a, b, c, i != 0, i != 1, i != 2);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    // can we solve b - a*y == 1?
    auto opt0 = linearDiophantine(b - 1, std::make_tuple(a));
    if (opt0.hasValue()) {
        int64_t y = std::get<0>(opt0.getValue());
        auto t1 = std::make_tuple(int64_t(1), y, int64_t(0));
        auto t2 = unimodularize2x3(a, b, c, 1, y, 0);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    // can we solve c - a*z == 1?
    auto opt1 = linearDiophantine(c - 1, std::make_tuple(a));
    if (opt1.hasValue()) {
        int64_t z = std::get<0>(opt1.getValue());
        auto t1 = std::make_tuple(int64_t(1), int64_t(0), z);
        auto t2 = unimodularize2x3(a, b, c, 1, 0, z);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    // can we solve (b - a*y) - (c - a*z) == 1?
    //              c - b + 1 == a*(z-y)
    auto opt2 = linearDiophantine(c - b + 1, std::make_tuple(a));
    if (opt2.hasValue()) {
        int64_t z = std::get<0>(opt2.getValue()); // let y = 0, so z-y == z
        auto t1 = std::make_tuple(int64_t(1), int64_t(0), z);
        auto t2 = unimodularize2x3(a, b, c, 1, 0, z);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    // can we solve (c - a*z) - (b - a*y) == 1?
    //              b - c + 1 == a*(y-z)
    auto opt3 = linearDiophantine(c - b + 1, std::make_tuple(a));
    if (opt3.hasValue()) {
        int64_t y = std::get<0>(opt3.getValue()); // let z = 0, so y-z == y
        auto t1 = std::make_tuple(int64_t(1), y, int64_t(0));
        auto t2 = unimodularize2x3(a, b, c, 1, y, 0);
        if (t2.hasValue()) {
            return std::make_pair(t1, t2.getValue());
        }
    }
    return {};
}

llvm::Optional<std::tuple<int64_t, int64_t>> unimodularize1x2(int64_t A00,
                                                                int64_t A01) {
    return linearDiophantine(1, A00, A01);
}

llvm::Optional<SquareMatrix<int64_t>>
unimodularization(const SquareMatrix<int64_t> &Aorig, size_t originalRows) {

    size_t N = Aorig.size(0);
    // special cases
    if (N == originalRows) {
        return Aorig;
    } else if (originalRows == 0) {
        SquareMatrix<int64_t> A(N);
        for (size_t r = 0; r < N; ++r) {
            for (size_t c = 0; c < N; ++c) {
                A(c, r) = r == c;
            }
        }
    } else if (N == 2) {
        // originalRows == 1
        auto cd = unimodularize1x2(Aorig(0, 0), Aorig(0, 1));
        if (cd.hasValue()) {
            SquareMatrix<int64_t> A = Aorig;
            auto [c, d] = cd.getValue();
            A(1, 0) = c;
            A(1, 1) = d;
        } else {
            return {};
        }
    } else if (N == 3) {
        if (originalRows == 1) {
            auto r1r2 = unimodularize1x3(Aorig(0, 0), Aorig(0, 1), Aorig(0, 2));
            if (r1r2.hasValue()) {
                SquareMatrix<int64_t> A = Aorig;
                auto [r1, r2] = r1r2.getValue();
                auto [x1, y1, z1] = r1;
                A(1, 0) = x1;
                A(1, 1) = y1;
                A(1, 2) = z1;
                auto [x2, y2, z2] = r2;
                A(2, 0) = x2;
                A(2, 1) = y2;
                A(2, 2) = z2;
            } else {
                return {};
            }
        } else {
            // originalRows == 2
            auto r2 = unimodularize2x3(Aorig(0, 0), Aorig(0, 1), Aorig(0, 2),
                                       Aorig(1, 0), Aorig(1, 1), Aorig(1, 2));
            if (r2.hasValue()) {
                SquareMatrix<int64_t> A = Aorig;
                auto [x, y, z] = r2.getValue();
                A(2, 0) = x;
                A(2, 1) = y;
                A(2, 2) = z;
            } else {
                return {};
            }
        }
    }
    SquareMatrix<int64_t> A = Aorig;
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
        int64_t Akk = A(k, k); // 1 or -1
        for (size_t i = k + 1; i < originalRows; ++i) {
            int64_t scale = A(i, k) * Akk;
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
*/
