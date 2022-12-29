#pragma once
#include "./Math.hpp"
#include <cstddef>
#include <llvm/ADT/SmallVector.h>

template <typename T> struct EmptyMatrix {
  using eltype = T;
  static constexpr auto begin() -> T * { return nullptr; }
  static constexpr auto end() -> T * { return nullptr; }

  static constexpr auto numRow() -> Row { return Row{0}; }
  static constexpr auto numCol() -> Col { return Col{0}; }
  static constexpr auto rowStride() -> LinearAlgebra::RowStride {
    return LinearAlgebra::RowStride{0};
  }
  static constexpr auto getConstCol() -> size_t { return 0; }

  static constexpr auto data() -> T * { return nullptr; }
  constexpr auto operator()(size_t, size_t) -> T { return 0; }
  static constexpr auto size() -> std::pair<Row, Col> {
    return std::make_pair(Row{0}, Col{0});
  }
  static constexpr auto view() -> EmptyMatrix<T> { return EmptyMatrix<T>{}; }
};

static_assert(AbstractMatrix<EmptyMatrix<ptrdiff_t>>);

template <typename T>
constexpr auto matmul(EmptyMatrix<T>, PtrMatrix<const T>) -> EmptyMatrix<T> {
  return EmptyMatrix<T>{};
}
template <typename T>
constexpr auto matmul(PtrMatrix<const T>, EmptyMatrix<T>) -> EmptyMatrix<T> {
  return EmptyMatrix<T>{};
}

template <typename T, typename S>
concept MaybeMatrix =
  std::is_same_v<T, Matrix<S>> || std::is_same_v<T, EmptyMatrix<S>>;

template <typename T> struct EmptyVector {
  static constexpr auto size() -> size_t { return 0; };
  static constexpr auto begin() -> T * { return nullptr; }
  static constexpr auto end() -> T * { return nullptr; }
};

template <typename T, typename S>
concept MaybeVector =
  std::is_same_v<T, EmptyVector<S>> || std::is_same_v<T, PtrVector<S>> ||
  std::is_same_v<T, MutPtrVector<S>> || std::is_same_v<T, StridedVector<S>> ||
  std::is_same_v<T, MutStridedVector<S>> || std::is_same_v<T, Vector<S>> ||
  std::is_same_v<T, llvm::SmallVector<S>>;
