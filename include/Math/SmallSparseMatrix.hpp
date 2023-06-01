#pragma once

#include "Math/Array.hpp"
#include "Math/ArrayOps.hpp"
#include <cstddef>

namespace LinAlg {
// this file is not used at the moment
template <typename T> struct SmallSparseMatrix {
  // non-zeros
  [[no_unique_address]] Vector<T> nonZeros{};
  // masks, the upper 8 bits give the number of elements in previous rows
  // the remaining 24 bits are a mask indicating non-zeros within this row
  static constexpr size_t maxElemPerRow = 24;
  [[no_unique_address]] Vector<uint32_t> rows;
  [[no_unique_address]] Col col;
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return Row{rows.size()};
  }
  [[nodiscard]] constexpr auto numCol() const -> Col { return col; }
  [[nodiscard]] constexpr auto size() const -> CartesianIndex<Row, Col> {
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto dim() const -> DenseDims {
    return {numRow(), numCol()};
  }
  // [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
  constexpr SmallSparseMatrix(Row numRows, Col numCols)
    : rows(unsigned(numRows)), col{numCols} {
    invariant(size_t(col) <= maxElemPerRow);
  }
  constexpr auto get(Row i, Col j) const -> T {
    invariant(j < col);
    uint32_t r(rows[size_t(i)]);
    uint32_t jshift = uint32_t(1) << uint32_t(j);
    if (!(r & jshift)) return T{};
    // offset from previous rows
    uint32_t prevRowOffset = r >> maxElemPerRow;
    uint32_t rowOffset = std::popcount(r & (jshift - 1));
    return nonZeros[rowOffset + prevRowOffset];
  }
  constexpr auto operator()(size_t i, size_t j) const -> T {
    return get(Row{i}, Col{j});
  }
  constexpr void insert(T x, Row i, Col j) {
    invariant(j < col);
    uint32_t r{rows[size_t(i)]};
    uint32_t jshift = uint32_t(1) << size_t(j);
    // offset from previous rows
    uint32_t prevRowOffset = r >> maxElemPerRow;
    uint32_t rowOffset = std::popcount(r & (jshift - 1));
    size_t k = rowOffset + prevRowOffset;
    if (r & jshift) {
      nonZeros[k] = std::move(x);
    } else {
      nonZeros.insert(nonZeros.begin() + k, std::move(x));
      rows[size_t(i)] = r | jshift;
      for (size_t l = size_t(i) + 1; l < rows.size(); ++l)
        rows[l] += uint32_t(1) << maxElemPerRow;
    }
  }

  struct Reference {
    [[no_unique_address]] SmallSparseMatrix<T> *A;
    [[no_unique_address]] size_t i, j;
    constexpr operator T() const { return A->get(Row{i}, Col{j}); }
    constexpr auto operator=(T x) -> Reference & {
      A->insert(std::move(x), Row{i}, Col{j});
      return *this;
    }
  };
  constexpr auto operator()(size_t i, size_t j) -> Reference {
    return Reference{this, i, j};
  }
};

template <class T, class S, class P>
[[gnu::flatten]] constexpr auto
ArrayOps<T, S, P>::operator<<(const SmallSparseMatrix<T> &B) -> P & {
  static_assert(MatrixDimension<S>);
  size_t M = nr(), N = nc(), k = 0;
  invariant(M, size_t(B.numRow()));
  invariant(N, size_t(B.numCol()));
  T *mem = data_();
  for (size_t i = 0; i < M; ++i) {
    uint32_t m = B.rows[i] & 0x00ffffff;
    size_t j = 0, l = rs() * i;
    while (m) {
      uint32_t tz = std::countr_zero(m);
      m >>= tz + 1;
      for (; tz; --tz) mem[l + j++] = T{};
      mem[l + j++] = B.nonZeros[k++];
    }
    for (; j < N; ++j) mem[l + j] = T{};
  }
  assert(k == B.nonZeros.size());
  return *static_cast<P *>(this);
}

}; // namespace LinAlg
