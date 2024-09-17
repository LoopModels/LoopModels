#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>

#ifndef USE_MODULE
#include "Utilities/ListRanges.cxx"
#include "Utilities/Invariant.cxx"
#include "Math/Array.cxx"
#else
export module ListIterator;
import Array;
import Invariant;
import ListRange;
#endif

#ifdef USE_MODULE
export namespace utils {
#else
namespace utils {
#endif

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
  constexpr auto operator*() const noexcept -> int32_t {
    invariant(state != data[state]);
    invariant(state >= 0);
    return state;
  }
  constexpr auto operator++() noexcept -> VCycleIterator & {
    invariant(state != data[state]);
    invariant(state >= 0);
    state = data[state];
    dobreak = state == start;
    return *this;
  }
  constexpr auto operator++(int) noexcept -> VCycleIterator {
    auto tmp = *this;
    ++(*this);
    return tmp;
  }
  constexpr auto
  operator==(const VCycleIterator &other) const noexcept -> bool {
    return state == other.state;
  }
  constexpr auto
  operator!=(const VCycleIterator &other) const noexcept -> bool {
    return state != other.state;
  }
  constexpr auto operator==(End) const -> bool { return dobreak; }
  constexpr auto
  operator-(const VCycleIterator &other) const noexcept -> ptrdiff_t {
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
  constexpr auto
  operator=(const VCycleIterator &) noexcept -> VCycleIterator & = default;
  constexpr auto
  operator=(VCycleIterator &&) noexcept -> VCycleIterator & = default;
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
  static constexpr auto end() noexcept -> End { return {}; }
};
static_assert(std::ranges::forward_range<VCycleRange>);

/// VForwardIterator is safe with respect to removing the current iteration from
/// the list. However, behavior is undefined if you remove or move the next
/// element.
class VForwardIterator {
  const int32_t *data_{nullptr};
  int32_t state_{-1};
  int32_t next_{-1};

public:
  using value_type = int32_t;
  constexpr VForwardIterator() noexcept = default;
  constexpr VForwardIterator(const int32_t *data, int32_t start) noexcept
    : data_{data}, state_{start}, next_{start < 0 ? start : data[start]} {}

  constexpr auto operator*() const noexcept -> int32_t {
    invariant(state_ != next_);
    invariant(state_ >= 0);
    return state_;
  }
  constexpr auto operator++() noexcept -> VForwardIterator & {
    invariant(state_ != next_);
    invariant(state_ >= 0);
    state_ = next_;
    if (next_ >= 0) next_ = data_[next_];
    return *this;
  }
  constexpr auto operator++(int) noexcept -> VForwardIterator {
    VForwardIterator tmp = *this;
    ++(*this);
    return tmp;
  }
  constexpr auto
  operator==(const VForwardIterator &other) const noexcept -> bool {
    return state_ == other.state_;
  }
  constexpr auto
  operator!=(const VForwardIterator &other) const noexcept -> bool {
    return state_ != other.state_;
  }
  constexpr auto operator==(End) const -> bool { return state_ < 0; }
  constexpr auto
  operator-(const VForwardIterator &other) const noexcept -> ptrdiff_t {
    ptrdiff_t diff = 0;
    VForwardIterator it = *this;
    while (it != other) {
      ++it;
      ++diff;
    }
    return diff;
  }
  constexpr VForwardIterator(const VForwardIterator &) noexcept = default;
  constexpr VForwardIterator(VForwardIterator &&) noexcept = default;
  constexpr auto
  operator=(const VForwardIterator &) noexcept -> VForwardIterator & = default;
  constexpr auto
  operator=(VForwardIterator &&) noexcept -> VForwardIterator & = default;
};
static_assert(std::forward_iterator<VForwardIterator>);

/// VForwardRange is safe with respect to removing the current iteration from
/// the list. However, behavior is undefined if you remove or move the next
/// element.
class VForwardRange : public std::ranges::view_interface<VForwardRange> {
  const int32_t *data_;
  int32_t start_;

public:
  constexpr VForwardRange() = default;
  constexpr VForwardRange(math::PtrVector<int32_t> data, int32_t start) noexcept
    : data_(data.begin()), start_(start) {}
  constexpr VForwardRange(const int32_t *data, int32_t start) noexcept
    : data_(data), start_(start) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> VForwardIterator {
    return {data_, start_};
  }
  [[nodiscard]] static constexpr auto end() noexcept -> End { return {}; }
};

}; // namespace utils
template <>
inline constexpr bool std::ranges::enable_borrowed_range<utils::VForwardRange> =
  true;
template <>
inline constexpr bool std::ranges::enable_borrowed_range<utils::VCycleRange> =
  true;

static_assert(std::ranges::forward_range<utils::VForwardRange>);
static_assert(std::ranges::view<utils::VForwardRange>);