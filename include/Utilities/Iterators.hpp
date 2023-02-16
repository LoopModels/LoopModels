#pragma once

#include <type_traits>
template <typename B, typename E> struct Range {
  [[no_unique_address]] B b;
  [[no_unique_address]] E e;
  [[nodiscard]] constexpr auto begin() const -> B { return b; }
  [[nodiscard]] constexpr auto end() const -> E { return e; }
};
template <std::integral B, std::integral E> struct Range<B, E> {
  [[no_unique_address]] B b;
  [[no_unique_address]] E e;
  // wrapper that allows dereferencing
  struct Iterator {
    [[no_unique_address]] B i;
    constexpr auto operator==(E other) -> bool { return i == other; }
    auto operator++() -> Iterator & {
      ++i;
      return *this;
    }
    auto operator++(int) -> Iterator { return Iterator{i++}; }
    auto operator--() -> Iterator & {
      --i;
      return *this;
    }
    auto operator--(int) -> Iterator { return Iterator{i--}; }
    auto operator*() -> B { return i; }
  };
  [[nodiscard]] constexpr auto begin() const -> Iterator { return Iterator{b}; }
  [[nodiscard]] constexpr auto end() const -> E { return e; }
  [[nodiscard]] constexpr auto rbegin() const -> Iterator {
    return std::reverse_iterator{end()};
  }
  [[nodiscard]] constexpr auto rend() const -> E {
    return std::reverse_iterator{begin()};
  }
  [[nodiscard]] constexpr auto size() const { return e - b; }
  friend inline auto operator<<(llvm::raw_ostream &os, Range<B, E> r)
    -> llvm::raw_ostream & {
    return os << "[" << r.b << ":" << r.e << ")";
  }
  template <std::integral BB, std::integral EE>
  constexpr operator Range<BB, EE>() const {
    return Range<BB, EE>{static_cast<BB>(b), static_cast<EE>(e)};
  }
};
constexpr auto standardizeRangeBound(auto x) { return x; }
constexpr auto standardizeRangeBound(std::unsigned_integral auto x) {
  return size_t(x);
}
constexpr auto standardizeRangeBound(std::signed_integral auto x) {
  return ptrdiff_t(x);
}

template <typename B, typename E>
Range(B b, E e) -> Range<decltype(standardizeRangeBound(b)),
                         decltype(standardizeRangeBound(e))>;

template <typename B, typename E>
constexpr auto operator+(Range<B, E> r, size_t x) {
  return Range{r.b + x, r.e + x};
}
template <typename B, typename E>
constexpr auto operator-(Range<B, E> r, size_t x) {
  return Range{r.b - x, r.e - x};
}
constexpr auto skipFirst(const auto &x) {
  auto b = x.begin();
  return Range{++b, x.end()};
}

template <typename T> struct StridedIterator {
  using value_type = std::remove_cvref_t<T>;
  T *ptr;
  size_t stride;
  constexpr auto operator==(const StridedIterator &other) const -> bool {
    return ptr == other.ptr;
  }
  constexpr auto operator!=(const StridedIterator &other) const -> bool {
    return ptr != other.ptr;
  }
  constexpr auto operator<(const StridedIterator &other) const -> bool {
    return ptr < other.ptr;
  }
  constexpr auto operator>(const StridedIterator &other) const -> bool {
    return ptr > other.ptr;
  }
  constexpr auto operator<=(const StridedIterator &other) const -> bool {
    return ptr <= other.ptr;
  }
  constexpr auto operator>=(const StridedIterator &other) const -> bool {
    return ptr >= other.ptr;
  }
  constexpr auto operator*() const -> T & { return *ptr; }
  constexpr auto operator->() const -> T * { return ptr; }
  constexpr auto operator++() -> StridedIterator & {
    ptr += stride;
    return *this;
  }
  constexpr auto operator++(int) -> StridedIterator {
    auto tmp = *this;
    ptr += stride;
    return tmp;
  }
  constexpr auto operator--() -> StridedIterator & {
    ptr -= stride;
    return *this;
  }
  constexpr auto operator--(int) -> StridedIterator {
    auto tmp = *this;
    ptr -= stride;
    return tmp;
  }
  constexpr auto operator+(size_t x) const -> StridedIterator {
    return StridedIterator{ptr + x * stride, stride};
  }
  constexpr auto operator-(size_t x) const -> StridedIterator {
    return StridedIterator{ptr - x * stride, stride};
  }
  constexpr auto operator+=(size_t x) -> StridedIterator & {
    ptr += x * stride;
    return *this;
  }
  constexpr auto operator-=(size_t x) -> StridedIterator & {
    ptr -= x * stride;
    return *this;
  }
  constexpr auto operator-(const StridedIterator &other) const -> ptrdiff_t {
    invariant(stride == other.stride);
    return (ptr - other.ptr) / stride;
  }
  constexpr auto operator+(const StridedIterator &other) const -> ptrdiff_t {
    invariant(stride == other.stride);
    return (ptr + other.ptr) / stride;
  }
  constexpr auto operator[](size_t x) const -> T & { return ptr[x * stride]; }
  friend constexpr auto operator+(size_t x, const StridedIterator &it)
    -> StridedIterator {
    return it + x;
  }
};
template <class T> StridedIterator(T *, size_t) -> StridedIterator<T>;
static_assert(std::weakly_incrementable<StridedIterator<int64_t>>);
static_assert(std::input_or_output_iterator<StridedIterator<int64_t>>);
static_assert(std::indirectly_readable<StridedIterator<int64_t>>,
              "failed indirectly readable");
static_assert(std::indirectly_readable<StridedIterator<int64_t>>,
              "failed indirectly readable");
static_assert(std::output_iterator<StridedIterator<int64_t>, size_t>,
              "failed output iterator");
static_assert(std::forward_iterator<StridedIterator<int64_t>>,
              "failed forward iterator");
static_assert(std::input_iterator<StridedIterator<int64_t>>,
              "failed input iterator");
static_assert(std::bidirectional_iterator<StridedIterator<int64_t>>,
              "failed bidirectional iterator");

static_assert(std::totally_ordered<StridedIterator<int64_t>>,
              "failed random access iterator");
static_assert(std::random_access_iterator<StridedIterator<int64_t>>,
              "failed random access iterator");
