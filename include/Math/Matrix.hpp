#pragma once
#include "Math/AxisTypes.hpp"
#include "Math/Indexing.hpp"
#include "Math/Vector.hpp"
#include "TypePromotion.hpp"
#include "Utilities/Allocators.hpp"
#include "Utilities/Optional.hpp"
#include "Utilities/Valid.hpp"

namespace LinearAlgebra {
template <typename T>
concept AbstractMatrixCore =
  HasEltype<T> && requires(T t, size_t i) {
                    { t(i, i) } -> std::convertible_to<eltype_t<T>>;
                    { t.numRow() } -> std::same_as<Row>;
                    { t.numCol() } -> std::same_as<Col>;
                    { t.size() } -> std::same_as<std::pair<Row, Col>>;
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
concept AbstractRowMajorMatrix =
  AbstractMatrix<T> && requires(T t) {
                         { t.rowStride() } -> std::same_as<RowStride>;
                       };

constexpr auto isSquare(const AbstractMatrix auto &A) -> bool {
  return A.numRow() == A.numCol();
}
template <typename T> struct PtrMatrix;
template <typename T> struct MutPtrMatrix;
template <typename T> struct Transpose;

template <typename T>
constexpr auto diag(MutPtrMatrix<T> A) -> MutStridedVector<T> {
  return MutStridedVector<T>{A.data(), A.minRowCol(), A.rowStride() + 1};
}
template <typename T> constexpr auto diag(PtrMatrix<T> A) -> StridedVector<T> {
  return StridedVector<T>{A.data(), A.minRowCol(), A.rowStride() + 1};
}
template <typename T>
constexpr auto antiDiag(MutPtrMatrix<T> A) -> MutStridedVector<T> {
  return MutStridedVector<T>{A.data() + size_t(A.numCol()) - 1, A.minRowCol(),
                             (A.rowStride() - 1)};
}
template <typename T>
constexpr auto antiDiag(PtrMatrix<T> A) -> StridedVector<T> {
  return StridedVector<T>{A.data() + size_t(A.numCol()) - 1, A.minRowCol(),
                          (A.rowStride() - 1)};
}

/// A CRTP type defining const methods for matrices.
template <typename T, typename A> struct ConstMatrixCore {
  [[nodiscard]] constexpr auto data() const -> NotNull<const T> {
    return static_cast<const A *>(this)->data();
  }
  [[nodiscard]] constexpr auto numRow() const -> Row {
    return static_cast<const A *>(this)->numRow();
  }
  [[nodiscard]] constexpr auto numCol() const -> Col {
    return static_cast<const A *>(this)->numCol();
  }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return static_cast<const A *>(this)->rowStride();
  }

  [[gnu::flatten]] constexpr auto operator()(auto m, auto n) const
    -> decltype(auto) {
    return matrixGet(data(), numRow(), numCol(), rowStride(), m, n);
  }
  [[nodiscard]] constexpr auto size() const -> std::pair<Row, Col> {
    return std::make_pair(numRow(), numCol());
  }
  [[nodiscard]] constexpr auto diag() const {
    return LinearAlgebra::diag(PtrMatrix<T>(*this));
  }
  [[nodiscard]] constexpr auto antiDiag() const {
    return LinearAlgebra::antiDiag(PtrMatrix<T>(*this));
  }
  [[nodiscard]] constexpr auto minRowCol() const -> size_t {
    return std::min(size_t(numRow()), size_t(numCol()));
  }
  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(numRow()) == size_t(numCol());
  }
  [[nodiscard]] constexpr auto checkSquare() const -> Optional<size_t> {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return {};
    return N;
  }
  constexpr operator PtrMatrix<T>() const {
    const T *ptr = data();
    return PtrMatrix<T>(ptr, numRow(), numCol(), rowStride());
  }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    const T *ptr = data();
    return PtrMatrix<T>(ptr, numRow(), numCol(), rowStride());
  }
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T>> {
    return Transpose<PtrMatrix<eltype_t<A>>>{view()};
  }
  [[nodiscard]] auto isExchangeMatrix() const -> bool {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return false;
    A &M = *static_cast<A *>(this);
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j)
        if (A(i, j) != (i + j == N - 1)) return false;
    }
  }
  [[nodiscard]] auto isDiagonal() const -> bool {
    for (Row r = 0; r < numRow(); ++r)
      for (Col c = 0; c < numCol(); ++c)
        if (r != c && (*this)(r, c) != 0) return false;
    return true;
  }
};
template <typename T> struct SmallSparseMatrix;
template <typename T, typename A> struct MutMatrixCore : ConstMatrixCore<T, A> {
  using CMC = ConstMatrixCore<T, A>;
  using CMC::data, CMC::numRow, CMC::numCol, CMC::rowStride,
    CMC::operator(), CMC::size, CMC::diag, CMC::antiDiag,
    CMC::operator ::LinearAlgebra::PtrMatrix<T>, CMC::view, CMC::transpose,
    CMC::isSquare, CMC::minRowCol;

  constexpr auto data() -> NotNull<T> { return static_cast<A *>(this)->data(); }

  [[gnu::flatten]] constexpr auto operator()(auto m, auto n) -> decltype(auto) {
    return matrixGet(data(), numRow(), numCol(), rowStride(), m, n);
  }
  constexpr auto diag() {
    return LinearAlgebra::diag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  constexpr auto antiDiag() {
    return LinearAlgebra::antiDiag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  constexpr operator MutPtrMatrix<T>() {
    return MutPtrMatrix<T>{data(), numRow(), numCol(), rowStride()};
  }
  [[nodiscard]] constexpr auto view() -> MutPtrMatrix<T> {
    T *ptr = data();
    return MutPtrMatrix<T>{ptr, numRow(), numCol(), rowStride()};
  }

  [[gnu::flatten]] auto operator=(const SmallSparseMatrix<T> &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    T *mem = data();
    size_t k = 0;
    for (size_t i = 0; i < numRow(); ++i) {
      uint32_t m = B.rows[i] & 0x00ffffff;
      size_t j = 0;
      while (m) {
        uint32_t tz = std::countr_zero(m);
        m >>= tz + 1;
        j += tz;
        mem[rowStride() * i + (j++)] = B.nonZeros[k++];
      }
    }
    assert(k == B.nonZeros.size());
    return *this;
  }
  // [[gnu::flatten]] auto operator=(MutPtrMatrix<T> B) -> MutPtrMatrix<T> {
  //   return copyto(*this, PtrMatrix<T>(B));
  // }
  [[gnu::flatten]] auto operator=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    return copyto(*this, B);
  }
  [[gnu::flatten]] auto operator=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) = b;
    return *this;
  }
  [[gnu::flatten]] auto operator+=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] auto operator-=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] auto operator*=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) *= b;
    return *this;
  }
  [[gnu::flatten]] auto operator/=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) /= b;
    return *this;
  }
  [[nodiscard]] constexpr auto transpose() const { return Transpose(view()); }
};

template <typename T> struct PtrMatrix : ConstMatrixCore<T, PtrMatrix<T>> {
  using ConstMatrixCore<T, PtrMatrix<T>>::size,
    ConstMatrixCore<T, PtrMatrix<T>>::diag,
    ConstMatrixCore<T, PtrMatrix<T>>::antiDiag,
    ConstMatrixCore<T, PtrMatrix<T>>::operator();

  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "const T is redundant");

  [[no_unique_address]] NotNull<const T> mem;
  [[no_unique_address]] unsigned int M, N, X;
  // [[no_unique_address]] Row M;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  [[nodiscard]] constexpr auto data() const -> NotNull<const T> { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }
  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(M) == size_t(N);
  }
  [[nodiscard]] constexpr inline auto view() const -> PtrMatrix<T> {
    return *this;
  };
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T>> {
    return Transpose<PtrMatrix<T>>{*this};
  }
  constexpr PtrMatrix(const T *const pt, const Row MM, const Col NN,
                      const RowStride XX)
    : mem(pt), M(MM), N(NN), X(XX) {}
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(MM == M);
    assert(NN == N);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
};
static_assert(std::same_as<PtrMatrix<int64_t>::eltype, int64_t>);
static_assert(HasEltype<PtrMatrix<int64_t>>);
static_assert(
  std::same_as<
    PtrMatrix<int64_t>::eltype,
    std::decay_t<decltype(std::declval<PtrMatrix<int64_t>>()(0, 0))>>);

template <typename T>
struct MutPtrMatrix : public MutMatrixCore<T, MutPtrMatrix<T>> {
  using eltype = std::remove_reference_t<T>;
  using Base = MutMatrixCore<T, MutPtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;
  static_assert(!std::is_const_v<T>, "MutPtrMatrix should never have const T");
  [[no_unique_address]] T *mem;
  [[no_unique_address]] unsigned int M, N, X;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;
  [[gnu::flatten]] auto operator=(MutPtrMatrix<T> B) -> MutPtrMatrix<T> {
    mem = B.mem;
    M = B.M;
    N = B.N;
    X = B.X;
    return *this;
    // return copyto(*this, PtrMatrix<T>(B));
  }
  // [[gnu::flatten]] auto operator=(AbstractMatrix auto B) -> MutPtrMatrix<T> {
  //   return copyto(*this, PtrMatrix<T>(B));
  // }

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    return {data(), numRow(), numCol(), rowStride()};
  };
  constexpr operator PtrMatrix<T>() const {
    return {data(), numRow(), numCol(), rowStride()};
  }

  // rule of 5 requires...
  constexpr MutPtrMatrix(const MutPtrMatrix<T> &A) = default;
  constexpr MutPtrMatrix(T *pt, Row MM, Col NN)
    : mem(pt), M(MM), N(NN), X(NN){};
  constexpr MutPtrMatrix(T *pt, Row MM, Col NN, RowStride XX)
    : mem(pt), M(MM), N(NN), X(XX){};
  template <typename ARM>
  constexpr MutPtrMatrix(ARM &A)
    : mem(A.data()), M(A.numRow()), N(A.numCol()), X(A.rowStride()) {}

  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(M) == size_t(N);
  }
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(numRow() == MM);
    assert(numCol() == NN);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
  constexpr void truncate(Row r) {
    assert(r <= M);
    M = unsigned(r);
  }
  constexpr void truncate(Col c) {
    assert(c <= N);
    N = unsigned(c);
  }
};
template <typename T>
struct DenseMutPtrMatrix : public MutMatrixCore<T, DenseMutPtrMatrix<T>> {
  using eltype = std::remove_reference_t<T>;
  using Base = MutMatrixCore<T, DenseMutPtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;
  static_assert(!std::is_const_v<T>, "MutPtrMatrix should never have const T");
  [[no_unique_address]] T *const mem;
  [[no_unique_address]] unsigned int M, N;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return N; }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    return {data(), numRow(), numCol(), rowStride()};
  };
  constexpr operator PtrMatrix<T>() const {
    return {data(), numRow(), numCol(), rowStride()};
  }

  // rule of 5 requires...
  constexpr DenseMutPtrMatrix(const DenseMutPtrMatrix<T> &A) = default;
  constexpr DenseMutPtrMatrix(T *pt, Row MM, Col NN) : mem(pt), M(MM), N(NN){};
  template <typename ARM>
  constexpr DenseMutPtrMatrix(ARM &A)
    : mem(A.data()), M(A.numRow()), N(A.numCol()) {
    assert(A.numCol() == A.rowStride());
  }

  [[nodiscard]] constexpr auto isSquare() const -> bool {
    return size_t(M) == size_t(N);
  }
  [[nodiscard]] constexpr auto begin() -> T * { return mem; }
  [[nodiscard]] constexpr auto end() -> T * { return mem + M * N; }
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(numRow() == MM);
    assert(numCol() == NN);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
  constexpr void truncate(Row r) {
    assert(r <= M);
    M = unsigned(r);
  }
};
template <typename T> constexpr auto ptrVector(T *p, size_t M) {
  if constexpr (std::is_const_v<T>)
    return PtrVector<std::remove_const_t<T>>{p, M};
  else return MutPtrVector<T>{p, M};
}

template <typename T> PtrMatrix(T *, size_t, size_t) -> PtrMatrix<T>;
template <typename T> MutPtrMatrix(T *, size_t, size_t) -> MutPtrMatrix<T>;
template <typename T> PtrMatrix(T *, size_t, size_t, size_t) -> PtrMatrix<T>;
template <typename T>
MutPtrMatrix(T *, size_t, size_t, size_t) -> MutPtrMatrix<T>;

template <AbstractRowMajorMatrix T> PtrMatrix(T &A) -> PtrMatrix<eltype_t<T>>;
template <AbstractRowMajorMatrix T>
MutPtrMatrix(T &A) -> MutPtrMatrix<eltype_t<T>>;

static_assert(sizeof(PtrMatrix<int64_t>) <=
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(sizeof(MutPtrMatrix<int64_t>) <=
              4 * sizeof(unsigned int) + sizeof(int64_t *));
static_assert(std::is_trivially_copyable_v<Row>);
static_assert(std::is_trivially_copyable_v<Col>);
static_assert(std::is_trivially_copyable_v<RowStride>);
static_assert(std::is_trivially_copyable_v<const Row>);
static_assert(std::is_trivially_copyable_v<const Col>);
static_assert(std::is_trivially_copyable_v<const RowStride>);
static_assert(std::is_trivially_copyable_v<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> is not trivially copyable!");
static_assert(std::is_trivially_copyable_v<PtrVector<int64_t>>,
              "PtrVector<int64_t,0> is not trivially copyable!");
// static_assert(std::is_trivially_copyable_v<MutPtrMatrix<int64_t>>,
//               "MutPtrMatrix<int64_t> is not trivially copyable!");

static_assert(!AbstractVector<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");
static_assert(!AbstractVector<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractVector succeeded");

static_assert(AbstractMatrix<PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(
  std::same_as<std::remove_reference_t<decltype(MutPtrMatrix<int64_t>(
                 nullptr, Row{0}, Col{0})(size_t(0), size_t(0)))>,
               int64_t>);

static_assert(AbstractMatrix<MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const PtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");
static_assert(AbstractMatrix<const MutPtrMatrix<int64_t>>,
              "PtrMatrix<int64_t> isa AbstractMatrix failed");

static_assert(AbstractVector<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");
static_assert(AbstractVector<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");
static_assert(AbstractVector<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractVector failed");

static_assert(AbstractVector<Vector<int64_t>>,
              "PtrVector<int64_t> isa AbstractVector failed");

static_assert(!AbstractMatrix<MutPtrVector<int64_t>>,
              "PtrVector<int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const PtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");
static_assert(!AbstractMatrix<const MutPtrVector<int64_t>>,
              "PtrVector<const int64_t> isa AbstractMatrix succeeded");

//
// Matrix
//
template <typename T, size_t M = 0, size_t N = 0, size_t S = 64>
struct Matrix : public MutMatrixCore<T, Matrix<T, M, N, S>> {
  using Base = MutMatrixCore<T, Matrix<T, M, N, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;

  // using eltype = std::remove_cv_t<T>;
  using eltype = std::remove_reference_t<T>;
  // static_assert(M * N == S,
  //               "if specifying non-zero M and N, we should have M*N == S");
  T mem[S]; // NOLINT(*-avoid-c-arrays)
  static constexpr auto numRow() -> Row { return Row{M}; }
  static constexpr auto numCol() -> Col { return Col{N}; }
  static constexpr auto rowStride() -> RowStride { return RowStride{N}; }

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }

  static constexpr auto getConstCol() -> size_t { return N; }
};

template <typename T, size_t M, size_t S>
struct Matrix<T, M, 0, S> : public MutMatrixCore<T, Matrix<T, M, 0, S>> {
  using Base = MutMatrixCore<T, Matrix<T, M, 0, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;

  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] size_t N, X;

  Matrix(size_t n) : mem(llvm::SmallVector<T, S>(M * n)), N(n), X(n){};

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{N}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{X};
  }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
  void resizeForOverwrite(Col NN, RowStride XX) {
    N = size_t(NN);
    X = size_t(XX);
    mem.resize_for_overwrite(XX * M);
  }
  void resizeForOverwrite(Col NN) { resizeForOverwrite(NN, NN); }
};
template <typename T, size_t N, size_t S>
struct Matrix<T, 0, N, S> : public MutMatrixCore<T, Matrix<T, 0, N, S>> {
  using Base = MutMatrixCore<T, Matrix<T, 0, N, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;

  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] size_t M;

  Matrix(size_t m) : mem(llvm::SmallVector<T, S>(m * N)), M(m){};

  [[nodiscard]] constexpr inline auto numRow() const -> Row { return Row{M}; }
  static constexpr auto numCol() -> Col { return Col{N}; }
  static constexpr auto rowStride() -> RowStride { return RowStride{N}; }
  static constexpr auto getConstCol() -> size_t { return N; }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
};

template <typename T>
struct SquarePtrMatrix : ConstMatrixCore<T, SquarePtrMatrix<T>> {
  using Base = ConstMatrixCore<T, SquarePtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>;
  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "const T is redundant");
  [[no_unique_address]] const T *const mem;
  [[no_unique_address]] const size_t M;
  constexpr SquarePtrMatrix(const T *const pt, size_t MM) : mem(pt), M(MM){};

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }
  constexpr auto data() -> const T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
};

template <typename T>
struct MutSquarePtrMatrix : public MutMatrixCore<T, MutSquarePtrMatrix<T>> {
  using Base = MutMatrixCore<T, MutSquarePtrMatrix<T>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;
  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "T should not be const");
  [[no_unique_address]] T *const mem;
  [[no_unique_address]] const size_t M;

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }
  constexpr MutSquarePtrMatrix(T *pt, size_t MM) : mem(pt), M(MM){};
  constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  constexpr operator SquarePtrMatrix<T>() const {
    return SquarePtrMatrix<T>{mem, M};
  }
  constexpr auto begin() -> T * { return mem; }
  constexpr auto end() -> T * { return mem + M * M; }
};

template <typename T, unsigned STORAGE = 8>
struct SquareMatrix : public MutMatrixCore<T, SquareMatrix<T, STORAGE>> {
  using Base = MutMatrixCore<T, SquareMatrix<T, STORAGE>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;

  using eltype = std::remove_reference_t<T>;
  static constexpr unsigned TOTALSTORAGE = STORAGE * STORAGE;
  [[no_unique_address]] llvm::SmallVector<T, TOTALSTORAGE> mem;
  [[no_unique_address]] size_t M;

  SquareMatrix(size_t m)
    : mem(llvm::SmallVector<T, TOTALSTORAGE>(m * m)), M(m){};

  SquareMatrix(AbstractMatrix auto A) {
    M = size_t(A.numRow());
    mem.resize_for_overwrite(M * M);
    copyto(*this, A);
  }

  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{M}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{M}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{M};
  }

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }

  constexpr auto begin() -> T * { return data(); }
  constexpr auto end() -> T * { return data() + M * M; }
  [[nodiscard]] constexpr auto begin() const -> const T * { return data(); }
  [[nodiscard]] constexpr auto end() const -> const T * {
    return data() + M * M;
  }
  auto operator[](size_t i) -> T & { return mem[i]; }
  auto operator[](size_t i) const -> const T & { return mem[i]; }

  static auto identity(size_t N) -> SquareMatrix<T, STORAGE> {
    SquareMatrix<T, STORAGE> A(N);
    for (size_t r = 0; r < N; ++r) A(r, r) = 1;
    return A;
  }
  inline static auto identity(Row N) -> SquareMatrix<T, STORAGE> {
    return identity(size_t(N));
  }
  inline static auto identity(Col N) -> SquareMatrix<T, STORAGE> {
    return identity(size_t(N));
  }
  constexpr operator MutSquarePtrMatrix<T>() {
    return MutSquarePtrMatrix<T>(mem.data(), size_t(M));
  }
  constexpr operator SquarePtrMatrix<T>() const {
    return SquarePtrMatrix<T>(mem.data(), M);
  }
#ifndef NDEBUG
  void extendOrAssertSize(Row R, Col C) {
    assert(R == C && "Matrix must be square");
    M = size_t(R);
    mem.resize_for_overwrite(M * M);
  }
#else
  void extendOrAssertSize(Row R, Col) {
    M = size_t(R);
    mem.resize_for_overwrite(M * M);
  }
#endif
};

template <typename T, size_t S>
struct Matrix<T, 0, 0, S> : public MutMatrixCore<T, Matrix<T, 0, 0, S>> {
  using Base = MutMatrixCore<T, Matrix<T, 0, 0, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>,
    Base::operator=, Base::operator+=, Base::operator-=, Base::operator*=,
    Base::operator/=;
  using eltype = std::remove_reference_t<T>;
  [[no_unique_address]] llvm::SmallVector<T, S> mem;
  [[no_unique_address]] unsigned int M = 0, N = 0, X = 0;
  // [[no_unique_address]] Row M;
  // [[no_unique_address]] Col N;
  // [[no_unique_address]] RowStride X;

  constexpr auto data() -> T * { return mem.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem.data(); }
  Matrix(llvm::SmallVector<T, S> content, Row MM, Col NN)
    : mem(std::move(content)), M(MM), N(NN), X(RowStride(*NN)){};

  Matrix(Row MM, Col NN)
    : mem(llvm::SmallVector<T, S>(MM * NN)), M(MM), N(NN), X(RowStride(*NN)){};

  Matrix() = default;
  Matrix(SquareMatrix<T> &&A) : mem(std::move(A.mem)), M(A.M), N(A.M), X(A.M){};
  Matrix(const SquareMatrix<T> &A)
    : mem(A.begin(), A.end()), M(A.M), N(A.M), X(A.M){};
  Matrix(const AbstractMatrix auto &A)
    : mem(llvm::SmallVector<T>{}), M(A.numRow()), N(A.numCol()), X(A.numCol()) {
    mem.resize_for_overwrite(M * N);
    for (size_t m = 0; m < M; ++m)
      for (size_t n = 0; n < N; ++n) mem[size_t(X * m + n)] = A(m, n);
  }
  [[nodiscard]] constexpr auto begin() { return mem.begin(); }
  [[nodiscard]] constexpr auto end() { return mem.begin() + rowStride() * M; }
  [[nodiscard]] constexpr auto begin() const { return mem.begin(); }
  [[nodiscard]] constexpr auto end() const {
    return mem.begin() + rowStride() * M;
  }
  [[nodiscard]] constexpr auto numRow() const -> Row { return M; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return N; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride { return X; }

  static auto uninitialized(Row MM, Col NN) -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(0, 0);
    A.M = MM;
    A.X = A.N = NN;
    A.mem.resize_for_overwrite(MM * NN);
    return A;
  }
  static auto identity(size_t MM) -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(MM, MM);
    A.diag() = 1;
    return A;
  }
  inline static auto identity(Row N) -> Matrix<T, 0, 0, S> {
    return identity(size_t(N));
  }
  inline static auto identity(Col N) -> Matrix<T, 0, 0, S> {
    return identity(size_t(N));
  }
  void clear() {
    M = N = X = 0;
    mem.clear();
  }

  void resize(Row MM, Col NN, RowStride XX) {
    mem.resize(XX * MM);
    size_t minMMM = std::min(size_t(M), size_t(MM));
    if ((XX > X) && M && N)
      // need to copy
      for (size_t m = minMMM - 1; m > 0; --m)
        for (auto n = size_t(N); n-- > 0;)
          mem[size_t(XX * m + n)] = mem[size_t(X * m + n)];
    // zero
    for (size_t m = 0; m < minMMM; ++m)
      for (auto n = size_t(N); n < size_t(NN); ++n) mem[size_t(XX * m + n)] = 0;
    for (size_t m = minMMM; m < size_t(MM); ++m)
      for (size_t n = 0; n < size_t(NN); ++n) mem[size_t(XX * m + n)] = 0;
    X = *XX;
    M = *MM;
    N = *NN;
  }
  void insertZero(Col i) {
    Col NN = N + 1;
    auto XX = RowStride(std::max(size_t(X), size_t(NN)));
    mem.resize(XX * M);
    size_t nLower = (XX > X) ? size_t(0) : size_t(i);
    if (M && N)
      // need to copy
      for (auto m = size_t(M); m-- > 0;)
        for (auto n = size_t(N); n-- > nLower;)
          mem[size_t(XX * m + n) + (n >= size_t(i))] = mem[size_t(X * m + n)];
    // zero
    for (size_t m = 0; m < M; ++m) mem[size_t(XX * m) + size_t(i)] = 0;
    X = *XX;
    N = *NN;
  }
  // set size and 0.
  void setSize(Row r, Col c) {
    M = *r;
    N = *c;
    X = *c;
    mem.clear();
    mem.resize(X * M);
  }
  void resize(Row MM, Col NN) { resize(MM, NN, max(NN, X)); }
  void reserve(Row MM, Col NN) { reserve(MM, max(NN, X)); }
  void reserve(Row MM, RowStride NN) { mem.reserve(NN * MM); }
  void clearReserve(Row MM, Col NN) { clearReserve(MM, RowStride(*NN)); }
  void clearReserve(Row MM, RowStride XX) {
    clear();
    mem.reserve(XX * MM);
  }
  void clearReserve(size_t L) {
    clear();
    mem.reserve(L);
  }
  void resizeForOverwrite(Row MM, Col NN, RowStride XX) {
    assert(XX >= NN);
    M = *MM;
    N = *NN;
    X = *XX;
    if (X * M > mem.size()) mem.resize_for_overwrite(X * M);
  }
  void resizeForOverwrite(Row MM, Col NN) {
    M = *MM;
    N = X = *NN;
    if (X * M > mem.size()) mem.resize_for_overwrite(X * M);
  }

  void resize(Row MM) {
    Row Mold = M;
    M = *MM;
    if (rowStride() * M > mem.size()) mem.resize(X * M);
    if (M > Mold) (*this)(_(Mold, M), _) = 0;
  }
  void resizeForOverwrite(Row MM) {
    if (rowStride() * MM > mem.size()) mem.resize_for_overwrite(X * M);
    M = *MM;
  }
  void resize(Col NN) { resize(M, NN); }
  void resizeForOverwrite(Col NN) {
    if (X < NN) {
      X = *NN;
      mem.resize_for_overwrite(X * M);
    }
    N = *NN;
  }
  void extendOrAssertSize(Row R, Col C) { resizeForOverwrite(R, C); }
  void erase(Col i) {
    assert(i < N);
    for (size_t m = 0; m < M; ++m)
      for (auto n = size_t(i); n < N - 1; ++n)
        (*this)(m, n) = (*this)(m, n + 1);
    --N;
  }
  void erase(Row i) {
    assert(i < M);
    auto it = mem.begin() + X * i;
    mem.erase(it, it + X);
    --M;
  }
  constexpr void truncate(Col NN) {
    assert(NN <= N);
    N = *NN;
  }
  constexpr void truncate(Row MM) {
    assert(MM <= M);
    M = *MM;
  }
  void moveLast(Col j) {
    if (j == N) return;
    for (size_t m = 0; m < M; ++m) {
      auto x = (*this)(m, j);
      for (auto n = size_t(j); n < N - 1;) {
        size_t o = n++;
        (*this)(m, o) = (*this)(m, n);
      }
      (*this)(m, N - 1) = x;
    }
  }
  [[nodiscard]] auto deleteCol(size_t c) const -> Matrix<T, 0, 0, S> {
    Matrix<T, 0, 0, S> A(M, N - 1);
    for (size_t m = 0; m < M; ++m) {
      A(m, _(0, c)) = (*this)(m, _(0, c));
      A(m, _(c, LinearAlgebra::end)) = (*this)(m, _(c + 1, LinearAlgebra::end));
    }
    return A;
  }
};
using IntMatrix = Matrix<int64_t>;
static_assert(std::same_as<IntMatrix::eltype, int64_t>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(std::same_as<eltype_t<Matrix<int64_t>>, int64_t>);

template <typename T>
inline auto matrix(std::allocator<T>, size_t M, size_t N)
  -> Matrix<T, 0, 0, 64> {
  return Matrix<T, 0, 0, 64>::undef(M, N);
}
template <typename T>
constexpr auto matrix(WBumpAlloc<T> alloc, size_t M, size_t N)
  -> DenseMutPtrMatrix<T> {
  return {alloc.allocate(M * N), M, N};
}
template <typename T>
constexpr auto matrix(BumpAlloc<> &alloc, size_t M, size_t N)
  -> DenseMutPtrMatrix<T> {
  return {alloc.allocate<T>(M * N), M, N};
}
template <typename T>
inline auto identity(std::allocator<T>, size_t M) -> Matrix<T, 0, 0, 64> {
  return SquareMatrix<T>::identity(M);
}
template <typename T>
constexpr auto identity(WBumpAlloc<T> alloc, size_t M)
  -> MutSquarePtrMatrix<T> {
  MutSquarePtrMatrix<T> A = {alloc.allocate(M * M), M};
  A = 0;
  A.diag() = 1;
  return A;
}
template <typename T>
constexpr auto identity(BumpAlloc<> &alloc, size_t M) -> MutSquarePtrMatrix<T> {
  return identity(WBumpAlloc<T>(alloc), M);
}

} // namespace LinearAlgebra
