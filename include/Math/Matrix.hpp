#pragma once
#include "Math/AxisTypes.hpp"
#include "Math/MatrixDimensions.hpp"
#include "TypePromotion.hpp"
#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace LinAlg {

template <typename T>
concept AbstractMatrixCore = HasEltype<T> && requires(T t, size_t i) {
  { t(i, i) } -> std::convertible_to<eltype_t<T>>;
  { t.numRow() } -> SameOrBroadcast<Row>;
  { t.numCol() } -> SameOrBroadcast<Col>;
  { t.size() } -> SameOrBroadcast<CartesianIndex<Row, Col>>;
  { t.dim() } -> std::convertible_to<StridedDims>;
  // {
  //     std::remove_reference_t<T>::canResize
  //     } -> std::same_as<const bool &>;
  // {t.extendOrAssertSize(i, i)};
};
template <typename T>
concept AbstractMatrix = AbstractMatrixCore<T> && requires(T t, size_t i) {
  { t.view() } -> AbstractMatrixCore;
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
concept AbstractRowMajorMatrix = AbstractMatrix<T> && requires(T t) {
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

template <class T> struct UniformScaling {
  using value_type = T;
  T value;
  constexpr UniformScaling(T x) : value(x) {}
  constexpr auto operator()(Row r, Col c) const -> T {
    return r == c ? value : T{};
  }
  static constexpr auto numRow() -> Row { return 0; }
  static constexpr auto numCol() -> Col { return 0; }
  static constexpr auto size() -> CartesianIndex<Row, Col> { return {0, 0}; }
  static constexpr auto dim() -> DenseDims { return {0, 0}; }
  [[nodiscard]] constexpr auto view() const -> auto { return *this; };
  template <class U> constexpr auto operator*(const U &x) const {
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::true_type>)
      return UniformScaling<U>{x};
    else return UniformScaling<U>{value * x};
  }
  constexpr auto operator==(const AbstractMatrix auto &A) const -> bool {
    auto R = size_t(A.numRow());
    if (R != A.numCol()) return false;
    for (size_t r = 0; r < R; ++r)
      for (size_t c = 0; c < R; ++c)
        if (A(r, c) != (r == c ? value : T{})) return false;
    return true;
  }
};
template <class T>
constexpr auto operator==(const AbstractMatrix auto &A,
                          const UniformScaling<T> &B) -> bool {
  return B == A;
}
template <class T, class U>
constexpr auto operator*(const U &x, UniformScaling<T> d) {
  if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::true_type>)
    return UniformScaling<U>{x};
  else return UniformScaling<U>{d.value * x};
}

static constexpr inline UniformScaling<std::true_type> I{
  std::true_type{}}; // identity

template <class T> UniformScaling(T) -> UniformScaling<T>;
static_assert(AbstractMatrix<UniformScaling<int64_t>>);

} // namespace LinAlg

using LinAlg::I;
