#pragma once
#include <algorithm>

constexpr auto allZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a == 0; });
  // return std::ranges::all_of(x, [](auto x) { return x == 0; });
}
constexpr auto allGEZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a >= 0; });
  // return std::ranges::all_of(x, [](auto x) { return x >= 0; });
}
constexpr auto allLEZero(const auto &x) -> bool {
  return std::all_of(x.begin(), x.end(), [](auto a) { return a <= 0; });
  // return std::ranges::all_of(x, [](auto x) { return x <= 0; });
}

constexpr auto countNonZero(const auto &x) -> size_t {
  return std::count_if(x.begin(), x.end(), [](auto a) { return a != 0; });
  // return std::ranges::count_if(x, [](auto x) { return x != 0; });
}
