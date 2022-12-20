#pragma once

// convenient callable structs for functional programming

template <typename T> struct Equals {
  T x;
  constexpr auto operator()(const auto &y) -> bool { return x == y; }
};
