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

constexpr auto isSquare(const AbstractMatrix auto &A) -> bool {
  return A.numRow() == A.numCol();
}

template <typename T, MatrixDimension D> struct PtrMatrix;
template <typename T, MatrixDimension D> struct MutPtrMatrix;
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

template <typename T, MatrixDimension D>
PtrMatrix(NotNull<T>, D) -> PtrMatrix<T, D>;
template <typename T, MatrixDimension D> PtrMatrix(T *, D) -> PtrMatrix<T, D>;
template <typename T, MatrixDimension D>
PtrMatrix(NotNull<const T>, D) -> PtrMatrix<T, D>;
template <typename T, MatrixDimension D>
PtrMatrix(const T *, D) -> PtrMatrix<T, D>;

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
  [[nodiscard]] constexpr auto dim() const {
    return static_cast<const A *>(this)->dim();
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
    return PtrMatrix<T>(ptr, dim());
  }
  [[nodiscard]] constexpr auto view() const { return PtrMatrix(data(), dim()); }
  [[nodiscard]] constexpr auto transpose() const { return Transpose{view()}; }
  [[nodiscard]] constexpr auto isExchangeMatrix() const -> bool {
    size_t N = size_t(numRow());
    if (N != size_t(numCol())) return false;
    A &M = *static_cast<A *>(this);
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j)
        if (A(i, j) != (i + j == N - 1)) return false;
    }
  }
  [[nodiscard]] constexpr auto isDiagonal() const -> bool {
    for (Row r = 0; r < numRow(); ++r)
      for (Col c = 0; c < numCol(); ++c)
        if (r != c && (*this)(r, c) != 0) return false;
    return true;
  }
};
template <typename T> struct SmallSparseMatrix;
template <typename T, typename A> struct MutMatrixCore : ConstMatrixCore<T, A> {
  using eltype = std::remove_reference_t<T>;
  using CMC = ConstMatrixCore<T, A>;
  using CMC::data, CMC::numRow, CMC::numCol, CMC::rowStride,
    CMC::operator(), CMC::size, CMC::diag, CMC::antiDiag,
    CMC::operator ::LinearAlgebra::PtrMatrix<T>, CMC::view, CMC::transpose,
    CMC::isSquare, CMC::minRowCol;

  constexpr auto data() -> NotNull<T> { return static_cast<A *>(this)->data(); }
  [[nodiscard]] constexpr auto dim() const {
    return static_cast<const A *>(this)->dim();
  }

  [[gnu::flatten]] constexpr auto operator()(auto m, auto n) -> decltype(auto) {
    return matrixGet(data(), numRow(), numCol(), rowStride(), m, n);
  }
  constexpr auto diag() {
    return LinearAlgebra::diag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  constexpr auto antiDiag() {
    return LinearAlgebra::antiDiag(MutPtrMatrix<T>(*static_cast<A *>(this)));
  }
  template <MatrixDimension D> constexpr operator MutPtrMatrix<T, D>() {
    return MutPtrMatrix<T, D>{data(), dim()};
  }

  [[gnu::flatten]] constexpr auto operator<<(const SmallSparseMatrix<T> &B)
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
  // [[gnu::flatten]] auto operator<<(MutPtrMatrix<T> B) -> MutPtrMatrix<T> {
  //   return copyto(*this, PtrMatrix<T>(B));
  // }
  [[gnu::flatten]] constexpr auto operator<<(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    return copyto(*this, B);
  }
  [[gnu::flatten]] constexpr auto operator<<(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) = b;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator+=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) += B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator-=(const AbstractMatrix auto &B)
    -> MutPtrMatrix<T> {
    assert(numRow() == B.numRow());
    assert(numCol() == B.numCol());
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) -= B(r, c);
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator*=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) *= b;
    return *this;
  }
  [[gnu::flatten]] constexpr auto operator/=(const std::integral auto b)
    -> MutPtrMatrix<T> {
    for (size_t r = 0; r < numRow(); ++r)
      for (size_t c = 0; c < numCol(); ++c) (*this)(r, c) /= b;
    return *this;
  }
  [[nodiscard]] constexpr auto transpose() const { return Transpose(view()); }
#ifndef NDEBUG
  void extendOrAssertSize(Row MM, Col NN) const {
    assert(MM == numRow());
    assert(NN == numCol());
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
};

template <typename T, MatrixDimension D>
struct PtrMatrix : ConstMatrixCore<T, PtrMatrix<T, D>> {
  using BaseT = ConstMatrixCore<T, PtrMatrix<T, D>>;
  using BaseT::size, BaseT::diag, BaseT::antiDiag,
    BaseT::operator(), BaseT::isSquare;

  using eltype = std::remove_reference_t<T>;
  static_assert(!std::is_const_v<T>, "const T is redundant");

  [[no_unique_address]] NotNull<const T> mem;
  [[no_unique_address]] D dims;

  [[nodiscard]] constexpr auto data() const -> NotNull<const T> { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{dims}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{dims}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{dims};
  }
  [[nodiscard]] constexpr inline auto view() const -> PtrMatrix<T, D> {
    return *this;
  };
  [[nodiscard]] constexpr auto transpose() const -> Transpose<PtrMatrix<T, D>> {
    return Transpose<PtrMatrix<T, D>>{*this};
  }
  constexpr PtrMatrix(NotNull<const T> pt, D dim) : mem{pt}, dims{dim} {}
  // constexpr PtrMatrix(const T *const pt, const Row M, const Col N,
  //                     const RowStride X)
  //   : mem(pt), dims{M, N, X} {}
#ifndef NDEBUG
  constexpr void extendOrAssertSize(Row M, Col N) const {
    assert(Row{dims} == M);
    assert(Col{dims} == N);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
  [[nodiscard]] constexpr auto dim() const -> D { return dims; }
};
static_assert(std::same_as<PtrMatrix<int64_t>::eltype, int64_t>);
static_assert(HasEltype<PtrMatrix<int64_t>>);
static_assert(
  std::same_as<
    PtrMatrix<int64_t>::eltype,
    std::decay_t<decltype(std::declval<PtrMatrix<int64_t>>()(0, 0))>>);

template <typename T, MatrixDimension D>
struct MutPtrMatrix : MutMatrixCore<T, MutPtrMatrix<T, D>> {
  using eltype = std::remove_reference_t<T>;
  using BaseT = MutMatrixCore<T, MutPtrMatrix<T, D>>;
  using BaseT::diag, BaseT::antiDiag, BaseT::operator(), BaseT::size,
    BaseT::view, BaseT::isSquare, BaseT::transpose,
    BaseT::operator ::LinearAlgebra::PtrMatrix<T>, BaseT::operator<<,
    BaseT::operator+=, BaseT::operator-=, BaseT::operator*=, BaseT::operator/=;
  static_assert(!std::is_const_v<T>, "MutPtrMatrix should never have const T");
  [[no_unique_address]] T *mem;
  [[no_unique_address]] D dims;

  [[nodiscard]] constexpr auto data() -> T * { return mem; }
  [[nodiscard]] constexpr auto data() const -> const T * { return mem; }
  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{dims}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{dims}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{dims};
  }
  [[nodiscard]] constexpr auto view() const -> PtrMatrix<T> {
    return {data(), dims};
  };
  [[nodiscard]] constexpr auto dim() const -> D { return dims; }
  template <typename M> constexpr operator PtrMatrix<T, M>() const {
    return {data(), dims};
  }

  // rule of 5 requires...
  constexpr MutPtrMatrix(const MutPtrMatrix &A) = default;
  constexpr MutPtrMatrix(T *pt, D dim) : mem{pt}, dims{dim} {}
  constexpr MutPtrMatrix(T *pt, Row M, Col N)
    : mem(pt), dims{M, N, RowStride{size_t(N)}} {};
  constexpr MutPtrMatrix(T *pt, Row M, Col N, RowStride X)
    : mem(pt), dims{M, N, X} {};
  // constexpr MutPtrMatrix(DataMatrix auto &A) : mem(A.data()), dims(A.dim())
  // {}
  constexpr auto operator=(const MutPtrMatrix &A)
    -> MutPtrMatrix<T, D> & = default;

#ifndef NDEBUG
  constexpr void extendOrAssertSize(Row MM, Col NN) const {
    assert(numRow() == MM);
    assert(numCol() == NN);
  }
#else
  static constexpr void extendOrAssertSize(Row, Col) {}
#endif
  [[nodiscard]] constexpr auto truncate(Row r) {
    return MutPtrMatrix(data(), dims.truncate(r));
  }
  [[nodiscard]] constexpr auto truncate(Col c) {
    return MutPtrMatrix(data(), dims.truncate(c));
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

template <typename T, MatrixDimension D, size_t S = 64>
struct Matrix : public MutMatrixCore<T, Matrix<T, D, S>> {
  using Base = MutMatrixCore<T, Matrix<T, D, S>>;
  using Base::diag, Base::antiDiag,
    Base::operator(), Base::size, Base::view, Base::isSquare, Base::transpose,
    Base::operator ::LinearAlgebra::PtrMatrix<T>,
    Base::operator ::LinearAlgebra::MutPtrMatrix<T>, Base::operator<<,
    Base::operator+=, Base::operator-=, Base::operator*=, Base::operator/=;
  using eltype = std::remove_reference_t<T>;

  [[no_unique_address]] Buffer<T, 64, D, std::allocator<T>> buf;

  constexpr auto data() -> T * { return buf.data(); }
  [[nodiscard]] constexpr auto data() const -> const T * { return buf.data(); }
  // Matrix(llvm::SmallVector<T, S> content, Row MM, Col NN)
  //   : mem(std::move(content)), M(MM), N(NN), X(RowStride(*NN)){};
  template <typename L>
  constexpr Matrix(Buffer<T, S, L, std::allocator<T>> &&b, Row M, Col N)
    : buf(std::move(b), DenseDims{M, N}) {}

  constexpr Matrix(D d) : buf(d) {}
  constexpr Matrix(D d, T init) : buf(d, init) {}
  constexpr Matrix(Row M, Col N) : buf(DenseDims{M, N}) {}
  constexpr Matrix(Row M, Col N, T init) : buf(DenseDims{M, N}, init) {}
  constexpr Matrix() = default;
  template <typename M, size_t L>
  constexpr Matrix(Matrix<T, M, L> &&A) : buf(std::move(A.buf)) {}

  template <typename M, size_t L>
  constexpr Matrix(const Matrix<T, M, L> &A) : buf(A.buf) {}
  template <typename Y, typename M, size_t L>
  constexpr Matrix(const Matrix<Y, M, L> &A) : buf(A.buf) {}
  [[gnu::flatten]] constexpr Matrix(const AbstractMatrix auto &A)
    : buf(A.size()) {
    for (size_t m = 0; m < numRow(); ++m)
      for (size_t n = 0; n < numCol(); ++n)
        buf[size_t(rowStride() * m + n)] = A(m, n);
  }
  template <typename M, size_t L>
  constexpr auto operator=(Matrix<T, M, L> &&A) -> Matrix & {
    buf = std::move(A.buf);
    return *this;
  }
  [[nodiscard]] constexpr auto begin() -> T * { return buf.begin(); }
  [[nodiscard]] constexpr auto end() -> T * {
    return buf.begin() + rowStride() * numRow();
  }
  [[nodiscard]] constexpr auto begin() const -> const T * {
    return buf.begin();
  }
  [[nodiscard]] constexpr auto end() const -> const T * {
    return buf.begin() + rowStride() * numRow();
  }
  [[nodiscard]] constexpr auto numRow() const -> Row { return Row{buf.size()}; }
  [[nodiscard]] constexpr auto numCol() const -> Col { return Col{buf.size()}; }
  [[nodiscard]] constexpr auto rowStride() const -> RowStride {
    return RowStride{buf.size()};
  }

  [[nodiscard]] static constexpr auto uninitialized(Row MM, Col NN)
    -> Matrix<T, D, S> {
    Matrix<T, D, S> A(0, 0);
    A.M = MM;
    A.X = A.N = NN;
    A.mem.resize_for_overwrite(MM * NN);
    return A;
  }
  [[nodiscard]] static constexpr auto identity(unsigned M) -> Matrix<T, D, S> {
    Matrix<T, D, S> A(SquareDims{M}, T{0});
    A.diag() << 1;
    return A;
  }
  [[nodiscard]] static constexpr auto identity(Row N) -> Matrix<T, D, S> {
    return identity(unsigned(N));
  }
  static constexpr auto identity(Col N) -> Matrix<T, D, S> {
    return identity(unsigned(N));
  }
  constexpr void clear() { buf.clear(); }

  constexpr void resize(D newDims) { buf.resize(newDims); }
  constexpr void resizeForOverwrite(D d) { buf.resizeForOverwrite(d); }
  // set size and 0.
  constexpr void setSize(Row r, Col c) {
    buf.resizeForOverwrite({r, c});
    buf.fill(0);
  }
  constexpr void resize(Row MM, Col NN) { resize(DenseDims{MM, NN}); }
  constexpr void reserve(Row M, Col N) {
    if constexpr (std::is_same_v<D, StridedDims>)
      buf.reserve(StridedDims{M, N, max(N, RowStride{buf.size()})});
    else if constexpr (std::is_same_v<D, SquareDims>)
      buf.reserve(SquareDims{unsigned(std::max(*M, *N))});
    else buf.reserve(DenseDims{M, N});
  }
  constexpr void reserve(Row M, RowStride X) {
    if constexpr (std::is_same_v<D, StridedDims>)
      buf.reserve(StridedDims{*M, *X, *X});
    else if constexpr (std::is_same_v<D, SquareDims>)
      buf.reserve(SquareDims{unsigned(std::max(*M, *X))});
    else buf.reserve(DenseDims{*M, *X});
  }
  constexpr void clearReserve(Row M, Col N) {
    clear();
    reserve(M, N);
  }
  constexpr void clearReserve(Row M, RowStride X) {
    clear();
    reserve(M, X);
  }
  constexpr void resizeForOverwrite(Row M, Col N, RowStride X) {
    invariant(X >= N);
    if constexpr (std::is_same_v<D, StridedDims>) resizeForOverwrite({M, N, X});
    else if constexpr (std::is_same_v<D, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }
  constexpr void resizeForOverwrite(Row M, Col N) {
    if constexpr (std::is_same_v<D, StridedDims>)
      resizeForOverwrite({M, N, *N});
    else if constexpr (std::is_same_v<D, SquareDims>) {
      invariant(*M == *N);
      resizeForOverwrite({*M});
    } else resizeForOverwrite({*M, *N});
  }

  constexpr void resize(Row r) { buf.resize(r); }
  constexpr void resizeForOverwrite(Row r) { buf.resizeForOverwrite(r); }
  constexpr void resize(Col c) { buf.resize(c); }
  constexpr void resizeForOverwrite(Col c) { buf.resizeForOverwrite(c); }

  constexpr void extendOrAssertSize(Row R, Col C) { resizeForOverwrite(R, C); }
  constexpr void erase(Col c) { buf.erase(c); }
  constexpr void erase(Row r) { buf.erase(r); }
  constexpr void truncate(Col c) { buf.truncate(c); }
  constexpr void truncate(Row r) { buf.truncate(r); }
  constexpr void moveLast(Col j) {
    if (j == numCol()) return;
    Col Nm1 = numCol() - 1;
    for (size_t m = 0; m < numRow(); ++m) {
      auto x = (*this)(m, j);
      for (Col n = j; n < Nm1;) {
        Col o = n++;
        (*this)(m, o) = (*this)(m, n);
      }
      (*this)(m, Nm1) = x;
    }
  }
  [[nodiscard]] constexpr auto deleteCol(size_t c) const -> Matrix<T, D, S> {
    auto newDim = buf.size().similar(numRow() - 1);
    Matrix<T, decltype(newDim), S> A(newDim);
    for (size_t m = 0; m < numRow(); ++m) {
      A(m, _(0, c)) = (*this)(m, _(0, c));
      A(m, _(c, LinearAlgebra::end)) = (*this)(m, _(c + 1, LinearAlgebra::end));
    }
    return A;
  }
  [[nodiscard]] constexpr auto dim() const -> D { return buf.size(); }
};
using IntMatrix = Matrix<int64_t, StridedDims, 64>;
static_assert(std::same_as<IntMatrix::eltype, int64_t>);
static_assert(AbstractMatrix<IntMatrix>);
static_assert(std::copyable<IntMatrix>);
static_assert(std::same_as<eltype_t<Matrix<int64_t, StridedDims>>, int64_t>);

template <typename T>
inline auto matrix(std::allocator<T>, size_t M, size_t N)
  -> Matrix<T, DenseDims> {
  return Matrix<T, DenseDims, 64>::undef(M, N);
}
template <typename T>
constexpr auto matrix(WBumpAlloc<T> alloc, size_t M, size_t N)
  -> MutPtrMatrix<T, DenseDims> {
  return {alloc.allocate(M * N), M, N};
}
template <typename T>
constexpr auto matrix(BumpAlloc<> &alloc, size_t M, size_t N)
  -> MutPtrMatrix<T, DenseDims> {
  return {alloc.allocate<T>(M * N), M, N};
}
template <typename T>
inline auto identity(std::allocator<T>, size_t M) -> Matrix<T, SquareDims> {
  return Matrix<T, SquareDims>::identity(M);
}
template <typename T>
constexpr auto identity(WBumpAlloc<T> alloc, size_t M)
  -> MutPtrMatrix<T, SquareDims> {
  MutPtrMatrix<T, SquareDims> A = {alloc.allocate(M * M), M};
  A << 0;
  A.diag() << 1;
  return A;
}
template <typename T>
constexpr auto identity(BumpAlloc<> &alloc, size_t M)
  -> MutPtrMatrix<T, SquareDims> {
  return identity(WBumpAlloc<T>(alloc), M);
}

} // namespace LinearAlgebra
