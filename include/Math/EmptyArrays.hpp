#pragma once
#include "Math/Matrix.hpp"
#include "Math/MatrixDimensions.hpp"
#include <cstddef>
#include <llvm/ADT/SmallVector.h>

namespace LinAlg {

template <typename T> struct EmptyMatrix {
  using value_type = T;
  static constexpr auto begin() -> T * { return nullptr; }
  static constexpr auto end() -> T * { return nullptr; }

  static constexpr auto numRow() -> Row { return Row{0}; }
  static constexpr auto numCol() -> Col { return Col{0}; }
  static constexpr auto rowStride() -> RowStride { return RowStride{0}; }
  static constexpr auto getConstCol() -> size_t { return 0; }

  static constexpr auto data() -> T * { return nullptr; }
  constexpr auto operator()(size_t, size_t) -> T { return 0; }
  static constexpr auto size() -> CartesianIndex<Row, Col> {
    return {Row{0}, Col{0}};
  }
  static constexpr auto view() -> EmptyMatrix<T> { return EmptyMatrix<T>{}; }
  static constexpr auto dim() -> SquareDims { return SquareDims{unsigned(0)}; }
};

static_assert(AbstractMatrix<EmptyMatrix<ptrdiff_t>>);

// template <typename T>
// constexpr auto matmul(EmptyMatrix<T>, PtrMatrix<T>) -> EmptyMatrix<T> {
//   return EmptyMatrix<T>{};
// }
// template <typename T>
// constexpr auto matmul(PtrMatrix<T>, EmptyMatrix<T>) -> EmptyMatrix<T> {
//   return EmptyMatrix<T>{};
// }

template <typename T> struct EmptyVector {
  static constexpr auto size() -> size_t { return 0; };
  static constexpr auto begin() -> T * { return nullptr; }
  static constexpr auto end() -> T * { return nullptr; }
};
} // namespace LinAlg
using LinAlg::EmptyMatrix;
