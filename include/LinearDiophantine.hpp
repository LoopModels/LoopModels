#pragma once
#include "./Math.hpp"
#include <tuple>

llvm::Optional<std::tuple<int64_t, int64_t>>
linearDiophantine(int64_t c, int64_t a, int64_t b) {
    if (c == 0) {
        return std::make_tuple(int64_t(0), int64_t(0));
    } else if ((a | b) == 0) {
        return {};
    }
    auto [g, x, y] = gcdx(a, b);
    int64_t cDivG = g == 1 ? c : c / g;
    if (cDivG * g == c) {
        // g = a*x + b*y;
        int64_t cDivG = c / g;
        return std::make_tuple(x * cDivG, y * cDivG);
    } else {
        return {};
    }
}

// d = a*x; x = d/a
llvm::Optional<std::tuple<int64_t>> linearDiophantine(int64_t d,
                                                       std::tuple<int64_t> a) {
    int64_t a0 = std::get<0>(a);
    if (d == 0) {
        return std::make_tuple(int64_t(0));
    } else if (a0 == 0) {
        return {};
    }
    int64_t x = d / a0;
    if (a0 * x == d) {
        return std::make_tuple(x);
    } else {
        return {};
    }
}
// d = a[0]*x + a[1]*y;
llvm::Optional<std::tuple<int64_t, int64_t>>
linearDiophantine(int64_t d, std::tuple<int64_t, int64_t> a) {
    return linearDiophantine(d, std::get<0>(a), std::get<1>(a));
}

template <size_t N> struct Val {};
template <typename Tuple, std::size_t... Is, size_t N>
auto pop_front_impl(const Tuple &tuple, std::index_sequence<Is...>, Val<N>) {
    return std::make_tuple(std::get<N + Is>(tuple)...);
}

template <typename Tuple, size_t N> auto pop_front(const Tuple &tuple, Val<N>) {
    return pop_front_impl(
        tuple, std::make_index_sequence<std::tuple_size<Tuple>::value - N>(),
        Val<N>());
}

template <typename Tuple>
llvm::Optional<Tuple> linearDiophantine(int64_t d, Tuple a) {
    int64_t a0 = std::get<0>(a);
    int64_t a1 = std::get<1>(a);
    auto aRem = pop_front(a, Val<2>());
    if ((a0 | a1) == 0) {
        auto opt = linearDiophantine(d, aRem);
        if (opt.hasValue()) {
            return std::tuple_cat(std::make_tuple(int64_t(0), int64_t(0)),
                                  opt.getValue());
        } else {
            return {};
        }
    }
    int64_t q = gcd(a0, a1);
    // d == q*((a/q)*x + (b/q)*y) + ... == q*w + ...
    // solve the rest
    auto dio_dqc =
        linearDiophantine(d, std::tuple_cat(std::make_tuple(q), aRem));
    if (!dio_dqc.hasValue()) {
        return {};
    }
    auto t = dio_dqc.getValue();
    int64_t w = std::get<0>(t);
    // w == ((a0/q)*x + (a1/q)*y)
    auto o = linearDiophantine(w, a0 / q, a1 / q);
    if (!o.hasValue()) {
        return {};
    }
    auto [x, y] = o.getValue();
    return std::tuple_cat(std::make_tuple(x, y), pop_front(t, Val<1>()));
}
