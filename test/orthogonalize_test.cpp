#include "../include/IntermediateRepresentation.hpp"
#include "../include/Math.hpp"
#include "../include/Symbolics.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

TEST(OrthogonalizeTest, BasicAssertions) {
    auto M = Polynomial::Monomial(Polynomial::ID{1});
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto I = Polynomial::Monomial(Polynomial::ID{3});
    auto J = Polynomial::Monomial(Polynomial::ID{4});
    auto Zero = Polynomial::Term{intptr_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{intptr_t(1), Polynomial::Monomial()};
    // for m = 0:M-1, n = 0:N-1, i = 0:I-1, j = 0:J-1
    //   W[m + i, n + j] += C[i,j] * B[m,n]
    //
    // Loops: m, n, i, j
    Matrix<Int, 0, 0, 0> A(4, 8);
    // A = [ 1 -1  0  0  0  0  0  0
    //       0  0  1 -1  0  0  0  0
    //       0  0  0  0  1 -1  0  0
    //       0  0  0  0  0  0  1 -1 ]
    // r = [ M - 1, 0, N - 1, 0, I - 1, 0, J - 1, 0 ]
    // Loop: A'i <= r
    llvm::SmallVector<MPoly, 8> r;
    // m <= M-1
    A(0, 0) = 1;
    r.push_back(M - 1);
    // m >= 0;
    A(0, 1) = -1;
    r.push_back(Zero);
    // n <= N-1
    A(1, 2) = 1;
    r.push_back(N - 1);
    // n >= 0;
    A(1, 3) = -1;
    r.push_back(Zero);
    // i <= I-1
    A(2, 4) = 1;
    r.push_back(I - 1);
    // i >= 0;
    A(2, 5) = -1;
    r.push_back(Zero);
    // j <= J-1
    A(3, 6) = 1;
    r.push_back(J - 1);
    // j >= 0;
    A(3, 7) = -1;
    r.push_back(Zero);
    for (auto &b : r) {
        if (auto c = b.getCompileTimeConstant()) {
            if (c.getValue() == 0) {
                assert(b.terms.size() == 0);
            }
        }
    }
    PartiallyOrderedSet poset;
    AffineLoopNest alnp(A, r, poset);
    EXPECT_FALSE(alnp.isEmpty());

    // we have three array refs
    // W[i+m, j+n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Waxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> ipm;
    ipm.emplace_back(One, VarID(0, VarType::LoopInductionVariable));
    ipm.emplace_back(One, VarID(2, VarType::LoopInductionVariable));
    Waxes.emplace_back(One, ipm);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> jpn;
    jpn.emplace_back(One, VarID(1, VarType::LoopInductionVariable));
    jpn.emplace_back(One, VarID(3, VarType::LoopInductionVariable));
    Waxes.emplace_back(I + M - One, jpn);
    ArrayReference War(0, Waxes); //, axes, indTo
    std::cout << "War = " << War << std::endl;

    // B[i, j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Baxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i{
        std::make_pair(One, VarID(2, VarType::LoopInductionVariable))};
    Baxes.emplace_back(One, i);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j{
        std::make_pair(One, VarID(3, VarType::LoopInductionVariable))};
    Baxes.emplace_back(I, j);
    ArrayReference Bar(1, Baxes); //, axes, indTo
    std::cout << "Bar = " << Bar << std::endl;

    // C[m, n]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Caxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> m{
        std::make_pair(One, VarID(0, VarType::LoopInductionVariable))};
    Caxes.emplace_back(One, m);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> n{
        std::make_pair(One, VarID(1, VarType::LoopInductionVariable))};
    Caxes.emplace_back(M, n);
    ArrayReference Car(2, Caxes); //, axes, indTo
    std::cout << "Car = " << Car << std::endl;

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<
        std::pair<AffineLoopNest, llvm::SmallVector<ArrayReference, 0>>>
        orth(orthogonalize(alnp, ai));

    EXPECT_TRUE(orth.hasValue());

    auto [newAlnp, newArrayRefs] = orth.getValue();
    std::cout << "A=" << newAlnp.A << std::endl;
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp.aln->b);
    EXPECT_EQ(newAlnp.lowerb[0].size(), 1);
    EXPECT_EQ(newAlnp.lowerb[1].size(), 1);
    EXPECT_EQ(newAlnp.lowerb[2].size(), 2);
    EXPECT_EQ(newAlnp.lowerb[3].size(), 2);
    EXPECT_EQ(newAlnp.upperb[0].size(), 1);
    EXPECT_EQ(newAlnp.upperb[1].size(), 1);
    EXPECT_EQ(newAlnp.upperb[2].size(), 2);
    EXPECT_EQ(newAlnp.upperb[3].size(), 2);
    std::cout << "Skewed loop nest:\n" << newAlnp << std::endl;
    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        std::cout << ar << std::endl << std::endl;
    }
}

TEST(BadMul, BasicAssertions) {
    auto M =
        Polynomial::Term{intptr_t(1), Polynomial::Monomial(Polynomial::ID{1})};
    auto N = Polynomial::Monomial(Polynomial::ID{2});
    auto O = Polynomial::Monomial(Polynomial::ID{3});
    auto Zero = Polynomial::Term{intptr_t(0), Polynomial::Monomial()};
    auto One = Polynomial::Term{intptr_t(1), Polynomial::Monomial()};
    // for i in 0:M+N+O-3, l in max(0,i+1-N):min(M+O-2,i), j in
    // max(0,l+1-O):min(M-1,l)
    //       W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    //
    // Loops: i, l, j
    Matrix<Int, 0, 0, 0> A(3, 10);
    //       0  1  2  3  4  5  6  7  8  9
    // A = [ 1 -1  0  0 -1  1  0  0  0  0
    //       0  0  1 -1  1 -1  0  0 -1  1
    //       0  0  0  0  0  0  1 -1  1 -1 ]
    // 0 r = [ M + N + O - 3,
    // 1       0,
    // 2       M + O - 2,
    // 3       0,
    // 4       0,
    // 5       N-1,
    // 6       M-1,
    // 7       0,
    // 8       0,
    // 9       O-1 ]
    // Loop: A'i <= r
    llvm::SmallVector<MPoly, 8> r;
    // i <= M + N + O - 3
    A(0, 0) = 1;
    r.push_back(M + N + O - 3);
    // i >= 0;
    A(0, 1) = -1;
    r.push_back(Zero);
    // l <= M + O - 2
    A(1, 2) = 1;
    r.push_back(M + O - 2);
    // l >= 0;
    A(1, 3) = -1;
    r.push_back(Zero);
    // l <= i
    A(0, 4) = -1;
    A(1, 4) = 1;
    r.push_back(Zero);
    // l >= i - (N - 1);
    A(0, 5) = 1;
    A(1, 5) = -1;
    r.push_back(N - 1);
    // j <= M - 1
    A(2, 6) = 1;
    r.push_back(M - 1);
    // j >= 0;
    A(2, 7) = -1;
    r.push_back(Zero);
    // j <= l
    A(1, 8) = -1;
    A(2, 8) = 1;
    r.push_back(Zero);
    // j >= l - (O - 1)
    A(1, 9) = 1;
    A(2, 9) = -1;
    r.push_back(O - 1);
    for (auto &b : r) {
        if (auto c = b.getCompileTimeConstant()) {
            if (c.getValue() == 0) {
                assert(b.terms.size() == 0);
            }
        }
    }
    PartiallyOrderedSet poset;
    AffineLoopNest alnp(A, r, poset);
    EXPECT_FALSE(alnp.isEmpty());

    // W[j,i-l] += B[j,l-j]*C[l-j,i-l]
    // 0, 1, 2
    // i, l, j
    // we have three array refs
    // W[j, i - l]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Waxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> ipm;
    ipm.emplace_back(One, VarID(2, VarType::LoopInductionVariable));
    Waxes.emplace_back(One, ipm);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> jpn;
    jpn.emplace_back(One, VarID(0, VarType::LoopInductionVariable));
    jpn.emplace_back(-One, VarID(1, VarType::LoopInductionVariable));
    Waxes.emplace_back(M, jpn);
    ArrayReference War(0, Waxes); //, axes, indTo
    std::cout << "War = " << War << std::endl;

    // B[j, l - j]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Baxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> i{
        std::make_pair(One, VarID(2, VarType::LoopInductionVariable))};
    Baxes.emplace_back(One, i);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> j{
        std::make_pair(One, VarID(1, VarType::LoopInductionVariable)),
        std::make_pair(-One, VarID(2, VarType::LoopInductionVariable))};
    Baxes.emplace_back(O, j);
    ArrayReference Bar(1, Baxes); //, axes, indTo
    std::cout << "Bar = " << Bar << std::endl;

    // C[l-j,i-l]
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> Caxes;
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> m{
        std::make_pair(One, VarID(1, VarType::LoopInductionVariable)),
        std::make_pair(-One, VarID(2, VarType::LoopInductionVariable))};
    Caxes.emplace_back(One, m);
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> n{
        std::make_pair(One, VarID(0, VarType::LoopInductionVariable)),
        std::make_pair(-One, VarID(1, VarType::LoopInductionVariable))};
    Caxes.emplace_back(M, n);
    ArrayReference Car(2, Caxes); //, axes, indTo
    std::cout << "Car = " << Car << std::endl;

    llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
    llvm::SmallVector<ArrayReference *> ai{&allArrayRefs[0], &allArrayRefs[1],
                                           &allArrayRefs[2]};

    llvm::Optional<
        std::pair<AffineLoopNest, llvm::SmallVector<ArrayReference, 0>>>
        orth(orthogonalize(alnp, ai));

    EXPECT_TRUE(orth.hasValue());

    auto [newAlnp, newArrayRefs] = orth.getValue();
    std::cout << "A=" << newAlnp.A << std::endl;
    // std::cout << "b=" << PtrVector<MPoly>(newAlnp.aln->b);
    EXPECT_EQ(newAlnp.lowerb[0].size(), 1);
    EXPECT_EQ(newAlnp.lowerb[1].size(), 1);
    EXPECT_EQ(newAlnp.lowerb[2].size(), 1);
    EXPECT_EQ(newAlnp.upperb[0].size(), 1);
    EXPECT_EQ(newAlnp.upperb[1].size(), 1);
    EXPECT_EQ(newAlnp.upperb[2].size(), 1);
    std::cout << "Skewed loop nest:\n" << newAlnp << std::endl;
    std::cout << "New ArrayReferences:\n";
    for (auto &ar : newArrayRefs) {
        std::cout << ar << std::endl << std::endl;
    }
}
