#pragma once
#include "./Math.hpp"
#include <tuple>

std::optional<std::tuple<int64_t, int64_t>>
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
std::optional<std::tuple<int64_t>> linearDiophantine(int64_t d,
                                                     std::tuple<int64_t> a) {
    int64_t a0 = std::get<0>(a);
    if (d == 0) {
        return std::make_tuple(int64_t(0));
    } else if (a0) {
        int64_t x = d / a0;
        if (a0 * x == d)
            return std::make_tuple(x);
    }
    return {};
}
// d = a[0]*x + a[1]*y;
std::optional<std::tuple<int64_t, int64_t>>
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
std::optional<Tuple> linearDiophantine(int64_t d, Tuple a) {
    int64_t a0 = std::get<0>(a);
    int64_t a1 = std::get<1>(a);
    auto aRem = pop_front(a, Val<2>());
    if ((a0 | a1) == 0) {
        if (auto opt = linearDiophantine(d, aRem))
            return std::tuple_cat(std::make_tuple(int64_t(0), int64_t(0)),
                                  *opt);
        return {};
    }
    int64_t q = gcd(a0, a1);
    // d == q*((a/q)*x + (b/q)*y) + ... == q*w + ...
    // solve the rest
    if (auto dio_dqc =
            linearDiophantine(d, std::tuple_cat(std::make_tuple(q), aRem))) {
        auto t = *dio_dqc;
        int64_t w = std::get<0>(t);
        // w == ((a0/q)*x + (a1/q)*y)
        if (auto o = linearDiophantine(w, a0 / q, a1 / q))
            return std::tuple_cat(*o, pop_front(t, Val<1>()));
    }
    return {};
}
