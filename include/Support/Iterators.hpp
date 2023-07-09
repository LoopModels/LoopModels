#pragma once
#include <Math/Array.hpp>
#include <Utilities/ListRanges.hpp>
#include <bits/iterator_concepts.h>
#include <bits/ranges_base.h>
#include <cstdint>
#include <ranges>

namespace poly::utils {

class VCycleIterator {

  const int32_t *data{nullptr};
  int32_t state{-1};
  int32_t start{-1};
  bool dobreak{true};

public:
  using value_type = int32_t;
  constexpr VCycleIterator() noexcept = default;
  constexpr VCycleIterator(const int32_t *data, int32_t start) noexcept
    : data(data), state(start), start(start), dobreak(start < 0) {}
  constexpr auto operator*() const noexcept -> int32_t { return state; }
  constexpr auto operator++() noexcept -> VCycleIterator & {
    state = data[state];
    dobreak = state == start;
    return *this;
  }
  constexpr auto operator++(int) noexcept -> VCycleIterator {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }
  constexpr auto operator==(const VCycleIterator &other) const noexcept
    -> bool {
    return state == other.state;
  }
  constexpr auto operator!=(const VCycleIterator &other) const noexcept
    -> bool {
    return state != other.state;
  }
  constexpr auto operator==(End) const -> bool { return dobreak; }
  constexpr auto operator-(const VCycleIterator &other) const noexcept
    -> ptrdiff_t {
    ptrdiff_t diff = 0;
    auto it = *this;
    while (it != other) {
      ++it;
      ++diff;
    }
    return diff;
  }
};
static_assert(std::forward_iterator<VCycleIterator>);

class VCycleRange {
  const int32_t *data;
  int32_t start;

public:
  constexpr VCycleRange(math::PtrVector<int32_t> data, int32_t start) noexcept
    : data(data.begin()), start(start) {}
  constexpr VCycleRange(const int32_t *data, int32_t start) noexcept
    : data(data), start(start) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> VCycleIterator {
    return {data, start};
  }
  static constexpr auto end() noexcept { return End{}; }
};
static_assert(std::ranges::forward_range<VCycleRange>);

class VForwardIterator {
  const int32_t *data{nullptr};
  int32_t state{-1};

public:
  using value_type = int32_t;
  constexpr VForwardIterator() noexcept = default;
  constexpr VForwardIterator(const int32_t *data, int32_t start) noexcept
    : data(data), state(start) {}

  constexpr auto operator*() const noexcept -> int32_t { return state; }
  constexpr auto operator++() noexcept -> VForwardIterator & {
    state = data[state];
    return *this;
  }
  constexpr auto operator++(int) noexcept -> VForwardIterator {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }
  constexpr auto operator==(const VForwardIterator &other) const noexcept
    -> bool {
    return state == other.state;
  }
  constexpr auto operator!=(const VForwardIterator &other) const noexcept
    -> bool {
    return state != other.state;
  }
  constexpr auto operator==(End) const -> bool { return state < 0; }
  constexpr auto operator-(const VForwardIterator &other) const noexcept
    -> ptrdiff_t {
    ptrdiff_t diff = 0;
    auto it = *this;
    while (it != other) {
      ++it;
      ++diff;
    }
    return diff;
  }
};
static_assert(std::forward_iterator<VForwardIterator>);

class VForwardRange {
  const int32_t *data;
  int32_t start;

public:
  constexpr VForwardRange(math::PtrVector<int32_t> data, int32_t start) noexcept
    : data(data.begin()), start(start) {}
  constexpr VForwardRange(const int32_t *data, int32_t start) noexcept
    : data(data), start(start) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> VForwardIterator {
    return {data, start};
  }
  [[nodiscard]] static constexpr auto end() noexcept { return End{}; }
};
static_assert(std::ranges::forward_range<VForwardRange>);

}; // namespace poly::utils
