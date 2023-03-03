#pragma once
#include "Math/AxisTypes.hpp"
#include "Math/MatrixDimensions.hpp"
#include "TypePromotion.hpp"
#include <concepts>
#include <memory>
#include <type_traits>

namespace LinearAlgebra {
template <typename T>
concept AbstractMatrixCore =
  HasEltype<T> && requires(T t, size_t i) {
                    { t(i, i) } -> std::convertible_to<eltype_t<T>>;
                    { t.numRow() } -> std::same_as<Row>;
                    { t.numCol() } -> std::same_as<Col>;
                    { t.size() } -> std::same_as<CartesianIndex<Row, Col>>;
                    { t.dim() } -> std::convertible_to<StridedDims>;
                    // {
                    //     std::remove_reference_t<T>::canResize
                    //     } -> std::same_as<const bool &>;
                    // {t.extendOrAssertSize(i, i)};
                  };
template <typename T>
concept AbstractMatrix = AbstractMatrixCore<T> && requires(T t, size_t i) {
                                                    {
                                                      t.view()
                                                      } -> AbstractMatrixCore;
                                                  };
template <typename T>
concept HasDataPtr = requires(T t) {
                       { t.data() } -> std::same_as<eltype_t<T> *>;
                     };
template <typename T>
concept DataMatrix = AbstractMatrix<T> && HasDataPtr<T>;
template <typename T>
concept TemplateMatrix = AbstractMatrix<T> && (!HasDataPtr<T>);

template <typename T>
concept AbstractRowMajorMatrix =
  AbstractMatrix<T> && requires(T t) {
                         { t.rowStride() } -> std::same_as<RowStride>;
                       };

template <typename A> struct Transpose {
  static_assert(AbstractMatrix<A>, "Argument to transpose is not a matrix.");
  static_assert(std::is_trivially_copyable_v<A>,
                "Argument to transpose is not trivially copyable.");

  using value_type = eltype_t<A>;
  [[no_unique_address]] A a;
  constexpr auto operator()(size_t i, size_t j) const { return a(j, i); }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return Row{size_t{a.numCol()}};
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    return Col{size_t{a.numRow()}};
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
  [[nodiscard]] constexpr auto size() const -> CartesianIndex<Row, Col> {
    return {numRow(), numCol()};
  }
  [[nodiscard]] constexpr auto dim() const -> DenseDims {
    return {numRow(), numCol()};
  }
  constexpr Transpose(A b) : a(b) {}
};
template <typename A> Transpose(A) -> Transpose<A>;

template <typename T> struct SmallSparseMatrix {
  // non-zeros
  [[no_unique_address]] llvm::SmallVector<T> nonZeros{};
  // masks, the upper 8 bits give the number of elements in previous rows
  // the remaining 24 bits are a mask indicating non-zeros within this row
  static constexpr size_t maxElemPerRow = 24;
  [[no_unique_address]] llvm::SmallVector<uint32_t> rows;
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
    : rows{llvm::SmallVector<uint32_t>(size_t(numRows))}, col{numCols} {
    assert(size_t(col) <= maxElemPerRow);
  }
  constexpr auto get(Row i, Col j) const -> T {
    assert(j < col);
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
    assert(j < col);
    llvm::errs() << "inserting " << x << " at " << size_t(i) << ", "
                 << size_t(j) << "; rows.size() = " << rows.size() << "\n";
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
    constexpr void operator=(T x) {
      A->insert(std::move(x), Row{i}, Col{j});
      return;
    }
  };
  constexpr auto operator()(size_t i, size_t j) -> Reference {
    return Reference{this, i, j};
  }
};

} // namespace LinearAlgebra
