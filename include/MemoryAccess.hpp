#pragma once
#include "./BitSets.hpp"
#include "./Loops.hpp"
#include <cstddef>
#include <cstring>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

static constexpr auto memoryAccessRequiredIndexSize(size_t arrayDim,
                                                    size_t numLoops,
                                                    size_t numSymbols)
  -> size_t {
  return arrayDim * numLoops + arrayDim * numSymbols;
}
// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
  static constexpr size_t stackArrayDims = 3;
  static constexpr size_t stackNumLoops = 4;
  static constexpr size_t stackNumSymbols = 1;
  [[no_unique_address]] llvm::SmallVector<
    int64_t, memoryAccessRequiredIndexSize(stackArrayDims, stackNumLoops,
                                           stackNumSymbols)>
    indices;
  [[no_unique_address]] NotNull<const llvm::SCEVUnknown> basePointer;
  [[no_unique_address]] NotNull<AffineLoopNest<>> loop;
  [[no_unique_address]] NotNull<llvm::Instruction> loadOrStore;
  [[no_unique_address]] llvm::SmallVector<const llvm::SCEV *, stackArrayDims>
    sizes;
  [[no_unique_address]] llvm::SmallVector<const llvm::SCEV *, stackArrayDims>
    symbolicOffsets;
  // omegas order is [outer <-> inner]
  [[no_unique_address]] llvm::SmallVector<unsigned, 8> omegas;
  [[no_unique_address]] llvm::SmallVector<unsigned> edgesIn;
  [[no_unique_address]] llvm::SmallVector<unsigned> edgesOut;
  [[no_unique_address]] BitSet<> nodeIndex;
  // unsigned (instead of ptr) as we build up edges
  // and I don't want to relocate pointers when resizing vector
  // schedule indicated by `1` top bit, remainder indicates loop
  [[nodiscard]] auto isLoad() const -> bool {
    return loadOrStore.isa<llvm::LoadInst>();
  }
  // TODO: `constexpr` once `llvm::SmallVector` supports it
  [[nodiscard]] auto getArrayDim() const -> size_t { return sizes.size(); }
  [[nodiscard]] auto getNumSymbols() const -> size_t {
    return 1 + symbolicOffsets.size();
  }
  [[nodiscard]] auto getNumLoops() const -> size_t {
    assert(loop->getNumLoops() + 1 == omegas.size());
    return loop->getNumLoops();
  }

  [[nodiscard]] auto getAlign() const -> llvm::Align {
    if (auto l = loadOrStore.dyn_cast<llvm::LoadInst>()) return l->getAlign();
    else if (auto s = loadOrStore.cast<llvm::StoreInst>()) return s->getAlign();
// note cast not dyn_cast for store
// not a load or store
#if __cplusplus >= 202202L
    std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
    __builtin_unreachable();
#endif
#endif
#endif
    return llvm::Align(1);
  }
  // static inline size_t requiredData(size_t dim, size_t numLoops){
  // 	return dim*numLoops +
  // }
  /// indexMatrix() -> getNumLoops() x arrayDim()
  /// Maps loop indVars to array indices
  /// Letting `i` be the indVars and `d` the indices:
  /// indexMatrix()' * i == d
  /// e.g. `indVars = [i, j]` and indexMatrix = [ 1 1; 0 1]
  /// corresponds to A[i, i + j]
  /// Note that `[i, j]` refers to loops in
  /// innermost -> outermost order, i.e.
  /// for (j : J)
  ///   for (i : I)
  ///      A[i, i + j]
  [[nodiscard]] auto indexMatrix() -> MutPtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    return MutPtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
  }
  [[nodiscard]] auto indexMatrix() const -> PtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    return PtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
  }
  [[nodiscard]] auto offsetMatrix() -> MutPtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return MutPtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d,
                                 numSymbols, numSymbols};
  }
  [[nodiscard]] auto offsetMatrix() const -> PtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return PtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d, numSymbols,
                              numSymbols};
  }
  [[nodiscard]] auto getInstruction() -> NotNull<llvm::Instruction> {
    return loadOrStore;
  }
  [[nodiscard]] auto getInstruction() const -> NotNull<llvm::Instruction> {
    return loadOrStore;
  }
  [[nodiscard]] auto getLoad() -> llvm::LoadInst * {
    return loadOrStore.dyn_cast<llvm::LoadInst>();
  }
  [[nodiscard]] auto getStore() -> llvm::StoreInst * {
    return loadOrStore.dyn_cast<llvm::StoreInst>();
  }
  /// initialize alignment from an elSize SCEV.
  static auto typeAlignment(const llvm::SCEV *S) -> llvm::Align {
    if (auto *C = llvm::dyn_cast<llvm::SCEVConstant>(S))
      return llvm::Align(C->getAPInt().getZExtValue());
    return llvm::Align{1};
  }
  void resize(size_t d) {
    sizes.resize(d);
    indices.resize(
      memoryAccessRequiredIndexSize(d, getNumLoops(), getNumSymbols()));
  }

  inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
  inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
  /// add a node index
  inline void addNodeIndex(unsigned i) { nodeIndex.insert(i); }
  MemoryAccess(const llvm::SCEVUnknown *arrayPtr, AffineLoopNest<true> &loopRef,
               llvm::Instruction *user,
               llvm::SmallVector<const llvm::SCEV *, 3> sz,
               llvm::SmallVector<const llvm::SCEV *, 3> off,
               llvm::SmallVector<unsigned, 8> o)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user),
      sizes(std::move(sz)), symbolicOffsets(std::move(off)),
      omegas(std::move(o)){};
  MemoryAccess(const llvm::SCEVUnknown *arrayPtr, AffineLoopNest<true> &loopRef,
               llvm::Instruction *user,
               llvm::SmallVector<const llvm::SCEV *, 3> sz,
               llvm::SmallVector<const llvm::SCEV *, 3> off,
               llvm::ArrayRef<unsigned> o)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user),
      sizes(std::move(sz)), symbolicOffsets(std::move(off)),
      omegas(o.begin(), o.end()){};

  // inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
  // inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }

  // size_t getNumLoops() const { return ref->getNumLoops(); }
  // size_t getNumAxes() const { return ref->axes.size(); }
  // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
  inline auto fusedThrough(MemoryAccess &x) -> bool {
    bool allEqual = true;
    size_t numLoopsCommon = std::min(getNumLoops(), x.getNumLoops());
    for (size_t n = 0; n < numLoopsCommon; ++n)
      allEqual &= (omegas[n] == x.omegas[n]);
    return allEqual;
  }
  // note returns true if unset
  // inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
  [[nodiscard]] inline auto getFusionOmega() const -> PtrVector<unsigned> {
    return PtrVector<unsigned>{omegas.data(), omegas.size()};
  }
  // inline PtrVector<int64_t> getSchedule(size_t loop) const {
  //     return schedule.getPhi()(loop, _);
  // }
  inline auto truncateSchedule() -> MemoryAccess * {
    // we're truncating down to `ref.getNumLoops()`, discarding outer most
    size_t dropCount = omegas.size() - (getNumLoops() + 1);
    if (dropCount) omegas.erase(omegas.begin(), omegas.begin() + dropCount);
    return this;
  }
  [[nodiscard]] auto isLoopIndependent() const -> bool {
    return LinearAlgebra::allZero(indices);
  }
  [[nodiscard]] auto allConstantIndices() const -> bool {
    return symbolicOffsets.size() == 0;
  }
  // Assumes strides and offsets are sorted
  [[nodiscard]] auto sizesMatch(const MemoryAccess &x) const -> bool {
    if (getArrayDim() != x.getArrayDim()) return false;
    for (size_t i = 0; i < getArrayDim(); ++i)
      if (sizes[i] != x.sizes[i]) return false;
    return true;
  }
  [[nodiscard]] constexpr static auto gcdKnownIndependent(const MemoryAccess &)
    -> bool {
    // TODO: handle this!
    // consider `x[2i]` vs `x[2i + 1]`, the former
    // will have a stride of `2`, and the latter of `x[2i+1]`
    // Additionally, in the future, we do
    return false;
  }
};

inline auto operator<<(llvm::raw_ostream &os, const MemoryAccess &m)
  -> llvm::raw_ostream & {
  if (m.isLoad()) os << "Load: ";
  else os << "Store: ";
  if (auto instr = m.getInstruction()) os << *instr;
  os << "\nArrayReference " << *m.basePointer << " (dim = " << m.getArrayDim()
     << ", num loops: " << m.getNumLoops();
  if (m.sizes.size()) os << ", element size: " << *m.sizes.back();
  os << "):\n";
  PtrMatrix<int64_t> A{m.indexMatrix()};
  os << "Sizes: [";
  if (m.sizes.size()) {
    os << " unknown";
    for (ptrdiff_t i = 0; i < ptrdiff_t(A.numCol()) - 1; ++i)
      os << ", " << *m.sizes[i];
  }
  os << " ]\nSubscripts: [ ";
  size_t numLoops = size_t(A.numRow());
  for (size_t i = 0; i < A.numCol(); ++i) {
    if (i) os << ", ";
    bool printPlus = false;
    for (size_t j = numLoops; j-- > 0;) {
      if (int64_t Aji = A(j, i)) {
        if (printPlus) {
          if (Aji <= 0) {
            Aji *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (Aji != 1) os << Aji << '*';
        os << "i_" << numLoops - j - 1 << " ";
        printPlus = true;
      }
    }
    PtrMatrix<int64_t> offs = m.offsetMatrix();
    for (size_t j = 0; j < offs.numCol(); ++j) {
      if (int64_t offij = offs(i, j)) {
        if (printPlus) {
          if (offij <= 0) {
            offij *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (j) {
          if (offij != 1) os << offij << '*';
          os << *m.loop->S[j - 1];
        } else os << offij;
        printPlus = true;
      }
    }
  }
  return os << "]\nSchedule Omega: " << m.getFusionOmega()
            << "\nAffineLoopNest:\n"
            << *m.loop;
}
