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
  constexpr VCycleIterator(const int32_t *data_, int32_t start_) noexcept
    : data(data_), state(start_), start(start_), dobreak(start_ < 0) {}
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
  constexpr VCycleIterator(const VCycleIterator &) noexcept = default;
  constexpr VCycleIterator(VCycleIterator &&) noexcept = default;
  constexpr auto operator=(const VCycleIterator &) noexcept
    -> VCycleIterator & = default;
  constexpr auto operator=(VCycleIterator &&) noexcept
    -> VCycleIterator & = default;
};
static_assert(std::forward_iterator<VCycleIterator>);

class VCycleRange : public std::ranges::view_interface<VCycleRange> {
  const int32_t *data;
  int32_t start;

public:
  constexpr VCycleRange(math::PtrVector<int32_t> data_, int32_t start_) noexcept
    : data(data_.begin()), start(start_) {}
  constexpr VCycleRange(const int32_t *data_, int32_t start_) noexcept
    : data(data_), start(start_) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> VCycleIterator {
    return {data, start};
  }
  static constexpr auto end() noexcept { return End{}; }
};
static_assert(std::ranges::forward_range<VCycleRange>);

/// VForwardIterator is safe with respect to removing the current iteration from
/// the list. However, behavior is undefined if you remove or move the next
/// element.
class VForwardIterator {
  const int32_t *data{nullptr};
  int32_t state{-1};
  int32_t next{-1};

public:
  using value_type = int32_t;
  constexpr VForwardIterator() noexcept = default;
  constexpr VForwardIterator(const int32_t *data_, int32_t start_) noexcept
    : data{data_}, state{start_}, next{start_ < 0 ? start_ : data_[start_]} {}

  constexpr auto operator*() const noexcept -> int32_t { return state; }
  constexpr auto operator++() noexcept -> VForwardIterator & {
    state = next;
    if (next >= 0) next = data[next];
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
  constexpr VForwardIterator(const VForwardIterator &) noexcept = default;
  constexpr VForwardIterator(VForwardIterator &&) noexcept = default;
  constexpr auto operator=(const VForwardIterator &) noexcept
    -> VForwardIterator & = default;
  constexpr auto operator=(VForwardIterator &&) noexcept
    -> VForwardIterator & = default;
};
static_assert(std::forward_iterator<VForwardIterator>);

class VForwardRange : public std::ranges::view_interface<VForwardRange> {
  const int32_t *data;
  int32_t start;

public:
  constexpr VForwardRange(math::PtrVector<int32_t> data_,
                          int32_t start_) noexcept
    : data(data_.begin()), start(start_) {}
  constexpr VForwardRange(const int32_t *data_, int32_t start_) noexcept
    : data(data_), start(start_) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> VForwardIterator {
    return {data, start};
  }
  [[nodiscard]] static constexpr auto end() noexcept { return End{}; }
};

}; // namespace poly::utils
template <>
inline constexpr bool
  std::ranges::enable_borrowed_range<poly::utils::VForwardRange> = true;
template <>
inline constexpr bool
  std::ranges::enable_borrowed_range<poly::utils::VCycleRange> = true;

static_assert(std::ranges::forward_range<poly::utils::VForwardRange>);
static_assert(std::ranges::view<poly::utils::VForwardRange>);
