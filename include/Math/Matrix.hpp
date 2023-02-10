#pragma once
#include "Math/AxisTypes.hpp"
#include "Math/Indexing.hpp"
#include "Math/MatrixDimensions.hpp"
#include "Math/Vector.hpp"
#include "TypePromotion.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Invariant.hpp"
#include "Utilities/Optional.hpp"
#include "Utilities/Valid.hpp"
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
                    { t.size() } -> std::same_as<std::pair<Row, Col>>;
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
  auto operator()(size_t i, size_t j) const { return a(j, i); }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return Row{size_t{a.numCol()}};
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    return Col{size_t{a.numRow()}};
  }
  [[nodiscard]] constexpr auto view() const -> auto & { return *this; };
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  [[nodiscard]] constexpr auto dim() const -> DenseDims {
    return {numRow(), numCol()};
  }
  Transpose(A b) : a(b) {}
};
template <typename A> Transpose(A) -> Transpose<A>;

} // namespace LinearAlgebra
