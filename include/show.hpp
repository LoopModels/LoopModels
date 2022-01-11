#pragma once
#include "ir.hpp"
#include "math.hpp"
#include "symbolics.hpp"
#include <string>

template <typename T, size_t M> void show(Vector<T, M> v) {
    for (size_t i = 0; i < length(v); i++) {
        std::cout << v(i) << ", ";
    }
    if (length(v)) {
        std::cout << v(length(v) - 1);
    }
}
template <typename T, size_t M, size_t N> void show(Matrix<T, M, N> A) {
    for (size_t i = 0; i < size(A, 0); i++) {
        for (size_t j = 0; j < size(A, 1); j++) {
            std::printf("%17d", A(i, j));
        }
    }
}
template <typename T> void show(StrideMatrix<T> A) {
    for (size_t i = 0; i < size(A, 0); i++) {
        for (size_t j = 0; j < size(A, 1); j++) {
            std::printf("%17d", A(i, j));
        }
    }
}
void show(Permutation perm) {
    auto numloop = getNLoops(perm);
    std::printf("perm: <");
    for (size_t j = 0; j < numloop - 1; j++)
        std::printf("%ld ", perm(j));
    std::printf("%ld>", perm(numloop - 1));
}

template <typename T> std::string toString(T const &x) {
    return std::to_string(x);
}
std::string toString(intptr_t i) { return std::to_string(i); }

template <typename T> void show(std::vector<T> &x) {
    std::cout << "[";
    for (size_t i = 0; i < x.size() - 1; ++i) {
        std::cout << x[i] << ", ";
    }
    std::cout << last(x) << "]";
}
void show(intptr_t x) { std::cout << x; }
void show(size_t x) { std::cout << x; }

std::string toString(Polynomial::Uninomial const &x) {
    switch (x.exponent) {
    case 0:
        return "1";
    case 1:
        return "x";
    default:
        // std::cout << "x exponent: " << x.exponent << std::endl;
        return "x^" + std::to_string(x.exponent);
    }
}


std::string toString(Rational x) {
    if (x.denominator == 1) {
        return std::to_string(x.numerator);
    } else {
        return std::to_string(x.numerator) + " / " +
               std::to_string(x.denominator);
    }
}

std::string toString(Polynomial::Monomial const &x) {
    size_t numIndex = x.prodIDs.size();
    if (numIndex) {
        if (numIndex != 1) { // not 0 by prev `if`
            std::string poly = "";
            size_t count = 0;
            size_t v = x.prodIDs[0];
            for (auto it = x.begin(); it != x.end(); ++it) {
                if (*it == v) {
                    ++count;
                } else {
                    poly += monomialTermStr(v, count);
                    v = *it;
                    count = 1;
                }
            }
            poly += monomialTermStr(v, count);
            return poly;
        } else { // numIndex == 1
            return programVarName(x.prodIDs[0]);
        }
    } else {
        return "1";
    }
}
template <typename C, typename M>
std::string toString(const Polynomial::Term<C, M> &x) {
    // std::cout << "x.coeficient: " << toString(x.coefficient) << std::endl;
    // std::cout << "x.exponent: " << toString(x.exponent) << std::endl;
    if (isOne(x.coefficient)) {
        return toString(x.exponent);
    } else if (x.isCompileTimeConstant()) {
        return toString(x.coefficient);
    } else {
        return toString(x.coefficient) + " ( " + toString(x.exponent) + " ) ";
    }
}

template <typename C, typename M>
std::string toString(Polynomial::Terms<C, M> const &x) {
    std::string poly = " ( ";
    for (size_t j = 0; j < length(x.terms); ++j) {
        if (j) {
            poly += " + ";
        }
        poly += toString(x.terms[j]);
    }
    return poly + " ) ";
}

void show(Polynomial::Uninomial const &x) { printf("%s", toString(x).c_str()); }
void show(Polynomial::Monomial const &x) { printf("%s", toString(x).c_str()); }
template <typename C, typename M> void show(Polynomial::Term<C, M> const &x) {
    printf("%s", toString(x).c_str());
}
template <typename C, typename M> void show(Polynomial::Terms<C, M> const &x) {
    printf("%s", toString(x).c_str());
}

std::string toString(SourceType s) {
    switch (s) {
    case SourceType::Constant:
        return "Constant";
    case SourceType::LoopInductionVariable:
        return "Induction Variable";
    case SourceType::Memory:
        return "Memory";
    case SourceType::Term:
        return "Term";
        // case WTR:
        //     return "Write then read";
        // case RTW: // dummy SourceType for indicating a relationship; not
        // lowered
        //     return "Read then write";
        // default:
        //     assert("Unreachable reached; invalid SourceType.");
        //     return "";
    }
}

void show(Const c) {
    auto b = c.bits;
    switch (c.type) {
    case Float64:
        std::printf("Float64(%f)", std::bit_cast<double>(b));
        break;
    case Float32:
        std::printf("Float32(%f)", std::bit_cast<float>((uint32_t)b));
        break;
    case Float16:
        std::printf("Float16(%x)", uint16_t(b));
        break;
    case BFloat16:
        std::printf("BFloat16(%x)", uint16_t(b));
        break;
    case Int64:
        std::printf("Int64(%zu)", int64_t(b));
        break;
    case Int32:
        std::printf("Int32(%d)", int32_t(b));
        break;
    case Int16:
        std::printf("Int16(%d)", int16_t(b));
        break;
    case Int8:
        std::printf("Int8(%d)", int8_t(b));
        break;
    case UInt64:
        std::printf("UInt64(%lu)", uint64_t(b));
        break;
    case UInt32:
        std::printf("UInt32(%x)", uint32_t(b));
        break;
    case UInt16:
        std::printf("UInt16(%x)", uint16_t(b));
        break;
    case UInt8:
        std::printf("UInt8(%x)", uint8_t(b));
        break;
    default:
        assert("unreachable");
    }
}

void show(ArrayRef ar) {
    printf("ArrayRef %zu:\n", ar.arrayId);
    for (size_t i = 0; i < length(ar.inds); ++i) {
        auto [ind, src] = ar.inds[i];
        std::string indStr =
            "i_" + std::to_string(src.id) + " (" + toString(src.typ) + ")";
        std::string poly = "(" + toString(ind) + ") " + indStr;
        printf("    %s", poly.c_str());
        if (i + 1 < length(ar.inds)) {
            printf(" +\n");
        } else {
            printf("\n");
        }
    }
}

// `show` doesn't print a new line by convention.
template <typename T> void showln(T &&x) {
    show(x);
    std::printf("\n");
}
