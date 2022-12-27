
#pragma once
#include "../include/Loops.hpp"
#include "../include/MemoryAccess.hpp"
#include <cstdint>

struct ArrayReference {
  const llvm::SCEVUnknown *basePointer;
  AffineLoopNest<> *loop;
  IntMatrix indMat;
  IntMatrix offMat;
  llvm::SmallVector<const llvm::SCEV *, 3> sizes;
  ArrayReference(const llvm::SCEVUnknown *p, AffineLoopNest<> &l, size_t dim)
    : basePointer(p), loop(&l), indMat(loop->getNumLoops(), dim),
      offMat(dim, 1), sizes(dim) {}
  ArrayReference(const ArrayReference &other, AffineLoopNest<> *al,
                 PtrMatrix<int64_t> iM)
    : basePointer(other.basePointer), loop(al), indMat(iM),
      offMat(other.offMat), sizes(other.sizes) {}
  auto indexMatrix() -> MutPtrMatrix<int64_t> { return indMat; }
  auto offsetMatrix() -> MutPtrMatrix<int64_t> { return offMat; }
  [[nodiscard]] auto getArrayDim() const -> size_t {
    return size_t(offMat.numRow());
  }
};
inline auto createMemAccess(ArrayReference &ar, llvm::Instruction *I,
                            llvm::ArrayRef<unsigned> omegas) -> MemoryAccess {
  MemoryAccess mem{ar.basePointer, *ar.loop, I, ar.sizes, {}, omegas};
  mem.resize(size_t(ar.offsetMatrix().numRow()));
  mem.indexMatrix() = ar.indexMatrix();
  mem.offsetMatrix() = ar.offsetMatrix();
  return mem;
}
