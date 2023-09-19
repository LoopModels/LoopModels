#pragma once

#include <Math/Array.hpp>
#include <llvm/Support/raw_ostream.h>
#include <sstream>

namespace poly {
namespace math {

// we go through ostringstream to adapt `std::ostream` print methods to
// `llvm::raw_ostream`
template <typename T>
inline auto operator<<(llvm::raw_ostream &os, PtrVector<T> const &A)
  -> llvm::raw_ostream & {
  std::ostringstream sos;
  printVector(sos, A);
  return os << sos.str();
}
inline auto operator<<(llvm::raw_ostream &os, const AbstractVector auto &A)
  -> llvm::raw_ostream & {
  Vector<utils::eltype_t<decltype(A)>> B(A.size());
  B << A;
  return os << B;
}
template <typename T>
inline auto operator<<(llvm::raw_ostream &os, PtrMatrix<T> A)
  -> llvm::raw_ostream & {
  std::ostringstream sos;
  printMatrix(sos, A);
  return os << sos.str();
}
template <typename T>
inline auto operator<<(llvm::raw_ostream &os, Array<T, SquareDims> A)
  -> llvm::raw_ostream & {
  return os << PtrMatrix<T>{A};
}
template <typename T>
inline auto operator<<(llvm::raw_ostream &os, Array<T, DenseDims> A)
  -> llvm::raw_ostream & {
  return os << PtrMatrix<T>{A};
}
} // namespace math
namespace utils {
inline void llvmOStreamPrint(std::ostream &os, const auto &x) {
  llvm::SmallVector<char> buff;
  llvm::raw_svector_ostream llos{buff};
  llos << x;
  os << std::string_view(llos.str());
}

} // namespace utils
} // namespace poly
