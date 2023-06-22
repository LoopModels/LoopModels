
#pragma once
#include "IR/Address.hpp"
#include "Math/Math.hpp"
#include "Polyhedra/Loops.hpp"
#include <cstdint>
#include <llvm/Support/Allocator.h>

struct ArrayReference {
  const llvm::SCEVUnknown *basePointer;
  poly::Loop *loop;
  DenseMatrix<int64_t> indMat;
  DenseMatrix<int64_t> offMat;
  llvm::SmallVector<const llvm::SCEV *, 3> sizes;
  ArrayReference(const llvm::SCEVUnknown *p, poly::Loop *l, size_t dim)
    : basePointer(p), loop(l), indMat(DenseDims{loop->getNumLoops(), dim}),
      offMat(DenseDims{dim, 1}), sizes(dim) {
    indexMatrix() << 0;
    offsetMatrix() << 0;
  }
  ArrayReference(const ArrayReference &other, poly::Loop *al,
                 PtrMatrix<int64_t> iM)
    : basePointer(other.basePointer), loop(al), indMat(iM),
      offMat(other.offMat), sizes(other.sizes) {}
  auto indexMatrix() -> MutPtrMatrix<int64_t> { return indMat; }
  auto offsetMatrix() -> MutPtrMatrix<int64_t> { return offMat; }
  [[nodiscard]] auto getArrayDim() const -> size_t {
    return size_t(offMat.numRow());
  }
};
inline auto createMemAccess(Arena<> *alloc, ArrayReference &ar,
                            llvm::Instruction *IC, PtrVector<unsigned> omegas)
  -> NotNull<MemoryAccess> {

  IntMatrix indMatT(ar.indMat.transpose());
  return MemoryAccess::construct(alloc, ar.basePointer, *ar.loop, IC, indMatT,
                                 {ar.sizes, {}}, ar.offsetMatrix(), omegas);
}
