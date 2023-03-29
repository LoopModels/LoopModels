#pragma once
#include "Math/Array.hpp"

namespace LinAlg {

static_assert(
  AbstractSimilar<PtrVector<int64_t>, std::integral_constant<unsigned int, 4>>);

// TODO: add MatrixDimension support
template <typename T>
concept StaticSize = StaticInt<T>;

template <class T, StaticSize S>
class StaticArray : public ArrayOps<T, S, StaticArray<T, S>> {
  static constexpr size_t capacity = size_t{S{}};
  std::array<T, capacity> memory{};

public:
  using value_type = T;
  using reference = T &;
  using const_reference = const T &;
  using size_type = unsigned;
  using difference_type = int;
  using iterator = T *;
  using const_iterator = const T *;
  using pointer = T *;
  using const_pointer = const T *;
  using concrete = std::true_type;
  constexpr StaticArray() = default;
  constexpr StaticArray(const T &x) noexcept {
    std::fill_n(data(), capacity, x);
  }
  constexpr StaticArray(StaticArray const &) = default;
  constexpr StaticArray(StaticArray &&) noexcept = default;
  constexpr StaticArray(const std::initializer_list<T> &list) {
    invariant(list.size(), capacity);
    std::copy(list.begin(), list.end(), data());
  }
  template <AbstractSimilar<S> V> constexpr StaticArray(const V &b) noexcept {
    (*this) << b;
  }
  [[nodiscard]] constexpr auto data() const noexcept -> const T * {
    return memory.data();
  }
  constexpr auto data() noexcept -> T * { return memory.data(); }

  constexpr auto operator=(StaticArray const &) -> StaticArray & = default;
  constexpr auto operator=(StaticArray &&) noexcept -> StaticArray & = default;

  [[nodiscard]] constexpr auto begin() const noexcept {
    if constexpr (std::is_same_v<S, StridedRange>)
      return StridedIterator{data(), S{}.stride};
    else return data();
  }
  [[nodiscard]] constexpr auto end() const noexcept {
    return begin() + capacity;
  }
  [[nodiscard]] constexpr auto rbegin() const noexcept {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() const noexcept {
    return std::reverse_iterator(begin());
  }
  [[nodiscard]] constexpr auto front() const noexcept -> const T & {
    return *begin();
  }
  [[nodiscard]] constexpr auto back() const noexcept -> const T & {
    return *(end() - 1);
  }
  // indexing has two components:
  // 1. offsetting the pointer
  // 2. calculating new dim
  // static constexpr auto slice(NotNull<T>, Index<S> auto i){
  //   auto
  // }
  constexpr auto operator[](Index<S> auto i) const noexcept -> decltype(auto) {
    auto offset = calcOffset(S{}, i);
    auto newDim = calcNewDim(S{}, i);
    auto ptr = data();
    invariant(ptr != nullptr);
    if constexpr (std::is_same_v<decltype(newDim), Empty>) return ptr[offset];
    else return Array<T, decltype(newDim)>{ptr + offset, newDim};
  }
  // TODO: switch to operator[] when we enable c++23
  // for vectors, we just drop the column, essentially broadcasting
  template <class R, class C>
  constexpr auto operator()(R r, C c) const noexcept -> decltype(auto) {
    if constexpr (MatrixDimension<S>)
      return (*this)[CartesianIndex<R, C>{r, c}];
    else return (*this)[size_t(r)];
  }
  [[nodiscard]] constexpr auto minRowCol() const -> size_t {
    return std::min(size_t(numRow()), size_t(numCol()));
  }

  [[nodiscard]] constexpr auto diag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{S{}}) + 1};
    auto ptr = data();
    invariant(ptr != nullptr);
    return Array<T, StridedRange>{ptr, r};
  }
  [[nodiscard]] constexpr auto antiDiag() const noexcept {
    StridedRange r{minRowCol(), unsigned(RowStride{S{}}) - 1};
    auto ptr = data();
    invariant(ptr != nullptr);
    return Array<T, StridedRange>{ptr + size_t(Col{S{}}) - 1, r};
  }
  [[nodiscard]] static constexpr auto isSquare() noexcept -> bool {
    return Row{S{}} == Col{S{}};
  }
  [[nodiscard]] constexpr auto checkSquare() const -> Optional<size_t> {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return {};
    return N;
  }

  [[nodiscard]] constexpr auto numRow() const noexcept -> Row {
    return Row{S{}};
  }
  [[nodiscard]] constexpr auto numCol() const noexcept -> Col {
    return Col{S{}};
  }
  [[nodiscard]] constexpr auto rowStride() const noexcept -> RowStride {
    return RowStride{S{}};
  }
  [[nodiscard]] static constexpr auto empty() -> bool { return capacity == 0; }
  [[nodiscard]] static constexpr auto size() noexcept {
    if constexpr (StaticInt<S>) return S{};
    else if constexpr (std::is_same_v<S, StridedRange>) return capacity;
    else return CartesianIndex{Row{S{}}, Col{S{}}};
  }
  [[nodiscard]] static constexpr auto dim() noexcept -> S { return S{}; }
  [[nodiscard]] constexpr auto transpose() const { return Transpose{*this}; }
  [[nodiscard]] constexpr auto isExchangeMatrix() const -> bool {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return false;
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j)
        if ((*this)(i, j) != (i + j == N - 1)) return false;
    }
  }
  [[nodiscard]] constexpr auto isDiagonal() const -> bool {
    for (Row r = 0; r < numRow(); ++r)
      for (Col c = 0; c < numCol(); ++c)
        if (r != c && (*this)(r, c) != 0) return false;
    return true;
  }
  [[nodiscard]] constexpr auto view() const noexcept -> Array<T, S> {
    auto ptr = data();
    invariant(ptr != nullptr);
    return Array<T, S>{const_cast<T *>(ptr), S{}};
  }

  [[nodiscard]] constexpr auto begin() noexcept {
    if constexpr (std::is_same_v<S, StridedRange>)
      return StridedIterator{data(), S{}.stride};
    else return data();
  }
  [[nodiscard]] constexpr auto end() noexcept { return begin() + capacity; }
  [[nodiscard]] constexpr auto rbegin() noexcept {
    return std::reverse_iterator(end());
  }
  [[nodiscard]] constexpr auto rend() noexcept {
    return std::reverse_iterator(begin());
  }
  constexpr auto front() noexcept -> T & { return *begin(); }
  constexpr auto back() noexcept -> T & { return *(end() - 1); }
  constexpr auto operator[](Index<S> auto i) noexcept -> decltype(auto) {
    auto offset = calcOffset(S{}, i);
    auto newDim = calcNewDim(S{}, i);
    if constexpr (std::is_same_v<decltype(newDim), Empty>)
      return data()[offset];
    else return MutArray<T, decltype(newDim)>{data() + offset, newDim};
  }
  // TODO: switch to operator[] when we enable c++23
  template <class R, class C>
  constexpr auto operator()(R r, C c) noexcept -> decltype(auto) {
    if constexpr (MatrixDimension<S>)
      return (*this)[CartesianIndex<R, C>{r, c}];
    else return (*this)[size_t(r)];
  }
  constexpr void fill(T value) {
    std::fill_n((T *)(data()), size_t(this->dim()), value);
  }
  [[nodiscard]] constexpr auto diag() noexcept {
    StridedRange r{unsigned(min(Row{S{}}, Col{S{}})),
                   unsigned(RowStride{S{}}) + 1};
    return MutArray<T, StridedRange>{data(), r};
  }
  [[nodiscard]] constexpr auto antiDiag() noexcept {
    Col c = Col{S{}};
    StridedRange r{unsigned(min(Row{S{}}, c)), unsigned(RowStride{S{}}) - 1};
    return MutArray<T, StridedRange>{data() + size_t(c) - 1, r};
  }
};

template <class T, size_t N>
using SVector = StaticArray<T, std::integral_constant<size_t, N>>;

static_assert(AbstractVector<SVector<int64_t, 3>>);
}; // namespace LinAlg
using LinAlg::SVector;
