#pragma once

// convenient callable structs for functional programming

template <typename T> struct Equals {
    T x;
    constexpr bool operator()(const auto &y) { return x == y; }
};
