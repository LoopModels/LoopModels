#pragma once
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

struct EndSentinel {
  [[nodiscard]] constexpr auto operator-(auto it) -> ptrdiff_t {
    ptrdiff_t i = 0;
    for (; it != EndSentinel{}; ++it, ++i) {
    }
    return i;
  }
  // overloaded operator== cannot be a static member function
  constexpr auto operator==(EndSentinel) const -> bool { return true; }
};
struct BitSetIterator {
  [[no_unique_address]] llvm::SmallVectorTemplateCommon<
    uint64_t>::const_iterator it;
  [[no_unique_address]] llvm::SmallVectorTemplateCommon<
    uint64_t>::const_iterator end;
  [[no_unique_address]] uint64_t istate;
  [[no_unique_address]] size_t cstate0{std::numeric_limits<size_t>::max()};
  [[no_unique_address]] size_t cstate1{0};
  constexpr auto operator*() const -> size_t { return cstate0 + cstate1; }
  constexpr auto operator++() -> BitSetIterator & {
    while (istate == 0) {
      ++it;
      if (it == end)
        return *this;
      istate = *it;
      cstate0 = std::numeric_limits<size_t>::max();
      cstate1 += 64;
    }
    size_t tzp1 = std::countr_zero(istate) + 1;
    cstate0 += tzp1;
    istate >>= tzp1;
    return *this;
  }
  constexpr auto operator++(int) -> BitSetIterator {
    BitSetIterator temp = *this;
    ++*this;
    return temp;
  }
  constexpr auto operator==(EndSentinel) const -> bool {
    return it == end && (istate == 0);
  }
  constexpr auto operator!=(EndSentinel) const -> bool {
    return it != end || (istate != 0);
  }
  constexpr auto operator==(BitSetIterator j) const -> bool {
    return (it == j.it) && (istate == j.istate);
  }
};

/// A set of `size_t` elements.
/// Initially constructed
template <typename T = llvm::SmallVector<uint64_t, 1>> struct BitSet {
  [[no_unique_address]] T data;
  // size_t operator[](size_t i) const {
  //     return data[i];
  // } // allow `getindex` but not `setindex`
  BitSet() = default;
  static constexpr auto numElementsNeeded(size_t N) -> size_t {
    return (N + 63) >> 6;
  }
  BitSet(size_t N) : data(numElementsNeeded(N)) {}

  static auto dense(size_t N) -> BitSet {
    BitSet b;
    b.data.resize(numElementsNeeded(N), std::numeric_limits<uint64_t>::max());
    if (size_t rem = N & 63)
      b.data.back() = (size_t(1) << rem) - 1;
    return b;
  }
  [[nodiscard]] auto maxValue() const -> size_t {
    size_t N = data.size();
    return N ? (64 * N - std::countl_zero(data[N - 1])) : 0;
  }
  // BitSet::Iterator(std::vector<std::uint64_t> &seta)
  //     : set(seta), didx(0), offset(0), state(seta[0]), count(0) {};
  [[nodiscard]] constexpr auto begin() const -> BitSetIterator {
    auto b{data.begin()};
    auto e{data.end()};
    if (b == e)
      return BitSetIterator{b, e, 0};
    BitSetIterator it{b, e, *b};
    return ++it;
  }
  [[nodiscard]] constexpr static auto end() -> EndSentinel {
    return EndSentinel{};
  };
  [[nodiscard]] inline auto front() const -> size_t {
    for (size_t i = 0; i < data.size(); ++i)
      if (data[i])
        return 64 * i + std::countr_zero(data[i]);
    return std::numeric_limits<size_t>::max();
  }
  static inline auto contains(llvm::ArrayRef<uint64_t> data, size_t x)
    -> uint64_t {
    if (data.empty())
      return 0;
    size_t d = x >> size_t(6);
    uint64_t r = uint64_t(x) & uint64_t(63);
    uint64_t mask = uint64_t(1) << r;
    return (data[d] & (mask));
  }
  [[nodiscard]] auto contains(size_t i) const -> uint64_t {
    return contains(data, i);
  }

  auto insert(size_t x) -> bool {
    size_t d = x >> size_t(6);
    uint64_t r = uint64_t(x) & uint64_t(63);
    uint64_t mask = uint64_t(1) << r;
    if (d >= data.size())
      data.resize(d + 1);
    bool contained = ((data[d] & mask) != 0);
    if (!contained)
      data[d] |= (mask);
    return contained;
  }
  void uncheckedInsert(size_t x) {
    size_t d = x >> size_t(6);
    uint64_t r = uint64_t(x) & uint64_t(63);
    uint64_t mask = uint64_t(1) << r;
    if (d >= data.size())
      data.resize(d + 1);
    data[d] |= (mask);
  }

  auto remove(size_t x) -> bool {
    size_t d = x >> size_t(6);
    uint64_t r = uint64_t(x) & uint64_t(63);
    uint64_t mask = uint64_t(1) << r;
    bool contained = ((data[d] & mask) != 0);
    if (contained)
      data[d] &= (~mask);
    return contained;
  }
  static void set(uint64_t &d, size_t r, bool b) {
    uint64_t mask = uint64_t(1) << r;
    if (b == ((d & mask) != 0))
      return;
    if (b)
      d |= mask;
    else
      d &= (~mask);
  }
  static void set(llvm::MutableArrayRef<uint64_t> data, size_t x, bool b) {
    size_t d = x >> size_t(6);
    uint64_t r = uint64_t(x) & uint64_t(63);
    set(data[d], r, b);
  }

  struct Reference {
    [[no_unique_address]] llvm::MutableArrayRef<uint64_t> data;
    [[no_unique_address]] size_t i;
    operator bool() const { return contains(data, i); }
    void operator=(bool b) {
      BitSet::set(data, i, b);
      return;
    }
  };

  auto operator[](size_t i) const -> bool { return contains(data, i); }
  auto operator[](size_t i) -> Reference {
    return Reference{llvm::MutableArrayRef<uint64_t>(data), i};
  }
  [[nodiscard]] auto size() const -> size_t {
    size_t s = 0;
    for (auto u : data)
      s += std::popcount(u);
    return s;
  }
  [[nodiscard]] auto any() const -> bool {
    for (auto u : data)
      if (u)
        return true;
    return false;
  }
  void setUnion(const BitSet &bs) {
    size_t O = bs.data.size(), N = data.size();
    if (O > N)
      data.resize(O);
    for (size_t i = 0; i < O; ++i) {
      uint64_t d = data[i] | bs.data[i];
      data[i] = d;
    }
  }
  auto operator&=(const BitSet &bs) -> BitSet & {
    if (bs.data.size() < data.size())
      data.resize(bs.data.size());
    for (size_t i = 0; i < data.size(); ++i)
      data[i] &= bs.data[i];
    return *this;
  }
  // &!
  auto operator-=(const BitSet &bs) -> BitSet & {
    if (bs.data.size() < data.size())
      data.resize(bs.data.size());
    for (size_t i = 0; i < data.size(); ++i)
      data[i] &= (~bs.data[i]);
    return *this;
  }
  auto operator|=(const BitSet &bs) -> BitSet & {
    if (bs.data.size() > data.size())
      data.resize(bs.data.size());
    for (size_t i = 0; i < bs.data.size(); ++i)
      data[i] |= bs.data[i];
    return *this;
  }
  auto operator&(const BitSet &bs) const -> BitSet {
    BitSet r = *this;
    return r &= bs;
  }
  auto operator|(const BitSet &bs) const -> BitSet {
    BitSet r = *this;
    return r |= bs;
  }
  auto operator==(const BitSet &bs) const -> bool { return data == bs.data; }

  friend inline auto operator<<(llvm::raw_ostream &os, BitSet const &x)
    -> llvm::raw_ostream & {
    os << "BitSet[";
    auto it = x.begin();
    constexpr EndSentinel e = BitSet::end();
    if (it != e) {
      os << *(it++);
      for (; it != e; ++it)
        os << ", " << *it;
    }
    os << "]";
    return os;
  }
  [[nodiscard]] auto isEmpty() const -> bool {
    for (auto u : data)
      if (u)
        return false;
    return true;
  }
};

template <unsigned N> using FixedSizeBitSet = BitSet<std::array<uint64_t, N>>;
// BitSet with length 64
using BitSet64 = FixedSizeBitSet<1>;

template <typename T, typename S = llvm::SmallVector<uint64_t, 1>>
struct BitSliceView {
  [[no_unique_address]] llvm::MutableArrayRef<T> a;
  [[no_unique_address]] const BitSet<S> &i;
  struct Iterator {
    [[no_unique_address]] llvm::MutableArrayRef<T> a;
    [[no_unique_address]] BitSetIterator it;
    constexpr auto operator==(EndSentinel) const -> bool {
      return it == EndSentinel{};
    }
    constexpr auto operator++() -> Iterator & {
      ++it;
      return *this;
    }
    constexpr auto operator++(int) -> Iterator {
      Iterator temp = *this;
      ++it;
      return temp;
    }
    auto operator*() -> T & { return a[*it]; }
    auto operator*() const -> const T & { return a[*it]; }
    auto operator->() -> T * { return &a[*it]; }
    auto operator->() const -> const T * { return &a[*it]; }
  };
  constexpr auto begin() -> Iterator { return {a, i.begin()}; }
  struct ConstIterator {
    [[no_unique_address]] llvm::ArrayRef<T> a;
    [[no_unique_address]] BitSetIterator it;
    constexpr auto operator==(EndSentinel) const -> bool {
      return it == EndSentinel{};
    }
    constexpr auto operator==(ConstIterator c) const -> bool {
      return (it == c.it) && (a.data() == c.a.data());
    }
    constexpr auto operator++() -> ConstIterator & {
      ++it;
      return *this;
    }
    constexpr auto operator++(int) -> ConstIterator {
      ConstIterator temp = *this;
      ++it;
      return temp;
    }
    auto operator*() const -> const T & { return a[*it]; }
    auto operator->() const -> const T * { return &a[*it]; }
  };
  [[nodiscard]] constexpr auto begin() const -> ConstIterator {
    return {a, i.begin()};
  }
  [[nodiscard]] constexpr auto end() const -> EndSentinel { return {}; }
  [[nodiscard]] constexpr auto size() const -> size_t { return i.size(); }
};
[[nodiscard]] constexpr auto operator-(EndSentinel,
                                       BitSliceView<int64_t>::Iterator v)
  -> ptrdiff_t {
  return EndSentinel{} - v.it;
}
[[nodiscard]] constexpr auto operator-(EndSentinel,
                                       BitSliceView<int64_t>::ConstIterator v)
  -> ptrdiff_t {
  return EndSentinel{} - v.it;
}

template <> struct std::iterator_traits<BitSetIterator> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = size_t;
  using reference_type = size_t &;
  using pointer_type = size_t *;
};
template <> struct std::iterator_traits<BitSliceView<int64_t>::Iterator> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = int64_t;
  using reference_type = int64_t &;
  using pointer_type = int64_t *;
};
template <> struct std::iterator_traits<BitSliceView<int64_t>::ConstIterator> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = int64_t;
  using reference_type = int64_t &;
  using pointer_type = int64_t *;
};
struct ScheduledNode;
template <> struct std::iterator_traits<BitSliceView<ScheduledNode>::Iterator> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = ScheduledNode;
  using reference_type = ScheduledNode &;
  using pointer_type = ScheduledNode *;
};
template <>
struct std::iterator_traits<BitSliceView<ScheduledNode>::ConstIterator> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = ScheduledNode;
  using reference_type = ScheduledNode &;
  using pointer_type = ScheduledNode *;
};

// typedef
// std::iterator_traits<BitSliceView<int64_t>::Iterator>::iterator_category;

static_assert(std::movable<BitSliceView<int64_t>::Iterator>);
static_assert(std::movable<BitSliceView<int64_t>::ConstIterator>);

static_assert(std::weakly_incrementable<BitSliceView<int64_t>::Iterator>);
static_assert(std::weakly_incrementable<BitSliceView<int64_t>::ConstIterator>);
static_assert(std::input_or_output_iterator<BitSliceView<int64_t>::Iterator>);
static_assert(
  std::input_or_output_iterator<BitSliceView<int64_t>::ConstIterator>);
// static_assert(std::indirectly_readable<BitSliceView<int64_t>::Iterator>);
static_assert(std::indirectly_readable<BitSliceView<int64_t>::ConstIterator>);
// static_assert(std::input_iterator<BitSliceView<int64_t>::Iterator>);
static_assert(std::input_iterator<BitSliceView<int64_t>::ConstIterator>);
static_assert(std::ranges::range<BitSliceView<int64_t>>);
static_assert(std::ranges::range<const BitSliceView<int64_t>>);
// static_assert(std::ranges::forward_range<BitSliceView<int64_t>>);
static_assert(std::ranges::forward_range<const BitSliceView<int64_t>>);

static_assert(std::ranges::range<BitSet<>>);
