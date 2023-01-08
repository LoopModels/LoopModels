#pragma once
#include "./BitSets.hpp"
#include "./Loops.hpp"
#include "Math.hpp"
#include "Utilities.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/raw_ostream.h>

struct Dependence;

// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
private:
  static constexpr auto memoryOmegaOffset(size_t arrayDim, size_t numLoops,
                                          size_t numSymbols) -> size_t {
    return arrayDim * numLoops + arrayDim * numSymbols;
  }
  static constexpr auto memoryTotalRequired(size_t arrayDim, size_t numLoops,
                                            size_t numSymbols) -> size_t {
    return arrayDim * numLoops + numLoops + arrayDim * numSymbols + 1;
  }

  static constexpr size_t stackArrayDims = 3;
  static constexpr size_t stackNumLoops = 4;
  static constexpr size_t stackNumSymbols = 1;
  NotNull<const llvm::SCEVUnknown> basePointer;
  NotNull<AffineLoopNest<>> loop;
  // This field will store either the loaded instruction, or the store
  // instruction. This means that we can check if this MemoryAccess is a load
  // via checking `!loadOrStore.isa<llvm::StoreInst>()`.
  // This allows us to create dummy loads (i.e. reloads of stores) via assigning
  // the stored value to `loadOrStore`. In the common case that we do have an
  // actual load instruction, that instruction is equivalent to the loaded
  // value, meaning that is the stored instruction, and thus we still have
  // access to it when it is available.
  NotNull<llvm::Instruction> loadOrStore;
  llvm::SmallVector<const llvm::SCEV *, stackArrayDims> sizes;
  llvm::SmallVector<const llvm::SCEV *, stackArrayDims> symbolicOffsets;
  // omegas order is [outer <-> inner]
  llvm::SmallVector<unsigned, 8> omegas;
  BitSet<> edgesIn;
  BitSet<> edgesOut;
  BitSet<> nodeIndex;
  // `struct hack` - we can over allocate on construction of the MemoryAccess
  // and then access the extra memory without need for `reinterpret_cast`
  // through this pointer.
  // https://www.geeksforgeeks.org/struct-hack/
  // I am not sure to what extent this is kosher.
  int64_t mem[0]; // NOLINT(modernize-avoid-c-arrays)
  //
  // unsigned (instead of ptr) as we build up edges
  // and I don't want to relocate pointers when resizing vector
  // schedule indicated by `1` top bit, remainder indicates loop
  [[nodiscard]] constexpr auto data() -> NotNull<int64_t> { return mem; }
  [[nodiscard]] constexpr auto data() const -> NotNull<const int64_t> {
    return mem;
  }
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

public:
  static auto
  construct(llvm::BumpPtrAllocator &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest<true> &loopRef, llvm::Instruction *user,
            PtrMatrix<int64_t> indMatT,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrMatrix<int64_t> offsets, llvm::ArrayRef<unsigned> o)
    -> NotNull<MemoryAccess> {
    auto [sz, off] = std::move(szOff);
    size_t arrayDim = sz.size();
    size_t numLoops = loopRef.getNumLoops();
    size_t numSymbols = size_t(offsets.numCol());
    size_t memNeeded = memoryTotalRequired(arrayDim, numLoops, numSymbols);
    auto *mem =
      alloc.Allocate<char>(sizeof(MemoryAccess) + memNeeded * sizeof(int64_t));
    auto *ma = new (mem)
      MemoryAccess(arrayPtr, loopRef, user, std::move(sz), std::move(off), o);
    return ma;
  }
  [[nodiscard]] constexpr auto inputEdges() const -> const BitSet<> & {
    return edgesIn;
  }
  [[nodiscard]] constexpr auto outputEdges() const -> const BitSet<> & {
    return edgesOut;
  }
  [[nodiscard]] constexpr auto getNodeIndex() const -> const BitSet<> & {
    return nodeIndex;
  }
  [[nodiscard]] constexpr auto getLoop() const -> NotNull<AffineLoopNest<>> {
    return loop;
  }
  [[nodiscard]] constexpr auto getNodes() -> BitSet<> & { return nodeIndex; }
  [[nodiscard]] constexpr auto getNodes() const -> const BitSet<> & {
    return nodeIndex;
  }
  [[nodiscard]] auto getSizes() const
    -> const llvm::SmallVector<const llvm::SCEV *, stackArrayDims> & {
    return sizes;
  }
  [[nodiscard]] auto getSymbolicOffsets() const
    -> const llvm::SmallVector<const llvm::SCEV *, stackArrayDims> & {
    return symbolicOffsets;
  }
  [[nodiscard]] auto isStore() const -> bool {
    return loadOrStore.isa<llvm::StoreInst>();
  }
  [[nodiscard]] auto isLoad() const -> bool { return !isStore(); }
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
    else return loadOrStore.cast<llvm::StoreInst>()->getAlign();
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
    return MutPtrMatrix<int64_t>{data(), getNumLoops(), d, d};
  }
  /// indexMatrix() -> getNumLoops() x arrayDim()
  [[nodiscard]] auto indexMatrix() const -> PtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    return PtrMatrix<int64_t>{data(), getNumLoops(), d, d};
  }
  [[nodiscard]] auto offsetMatrix() -> MutPtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return MutPtrMatrix<int64_t>{data() + getNumLoops() * d, d, numSymbols,
                                 numSymbols};
  }
  [[nodiscard]] auto offsetMatrix() const -> PtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return PtrMatrix<int64_t>{data() + getNumLoops() * d, d, numSymbols,
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
    // sizes.resize(d);
    // indices.resize(
    //   memoryAccessRequiredIndexSize(d, getNumLoops(), getNumSymbols()));
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> const llvm::SCEV * {
    return basePointer;
  }
  // inline void addEdgeIn(NotNull<Dependence> i) { edgesIn.push_back(i); }
  // inline void addEdgeOut(NotNull<Dependence> i) { edgesOut.push_back(i); }
  inline void addEdgeIn(size_t i) { edgesIn.insert(i); }
  inline void addEdgeOut(size_t i) { edgesOut.insert(i); }
  /// add a node index
  inline void addNodeIndex(unsigned i) { nodeIndex.insert(i); }

  // inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
  // inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }

  // size_t getNumLoops() const { return ref->getNumLoops(); }
  // size_t getNumAxes() const { return ref->axes.size(); }
  // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
  auto fusedThrough(MemoryAccess &x) -> bool {
    size_t numLoopsCommon = std::min(getNumLoops(), x.getNumLoops());
    return std::equal(omegas.begin(), omegas.begin() + numLoopsCommon,
                      x.omegas.begin());
  }
  // note returns true if unset
  // inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
  [[nodiscard]] inline auto getFusionOmega() const -> PtrVector<unsigned> {
    return PtrVector<unsigned>{omegas.data(), omegas.size()};
  }
  // inline PtrVector<int64_t> getSchedule(size_t loop) const {
  //     return schedule.getPhi()(loop, _);
  // }
  // FIXME: needs to truncate indexMatrix, offsetVector, etc
  inline auto truncateSchedule() -> MemoryAccess * {
    // we're truncating down to `ref.getNumLoops()`, discarding outer most
    size_t dropCount = omegas.size() - (getNumLoops() + 1);
    if (dropCount) omegas.erase(omegas.begin(), omegas.begin() + dropCount);
    return this;
  }
  [[nodiscard]] auto allConstantIndices() const -> bool {
    return symbolicOffsets.size() == 0;
  }
  // Assumes strides and offsets are sorted
  [[nodiscard]] auto sizesMatch(const MemoryAccess &x) const -> bool {
    return std::equal(sizes.begin(), sizes.end(), x.sizes.begin(),
                      x.sizes.end());
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
  os << *m.getInstruction();
  os << "\nArrayReference " << *m.getArrayPointer()
     << " (dim = " << m.getArrayDim() << ", num loops: " << m.getNumLoops();
  if (m.getArrayDim()) os << ", element size: " << *m.getSizes().back();
  os << "):\n";
  PtrMatrix<int64_t> A{m.indexMatrix()};
  os << "Sizes: [";
  if (m.getArrayDim()) {
    os << " unknown";
    for (ptrdiff_t i = 0; i < ptrdiff_t(A.numCol()) - 1; ++i)
      os << ", " << *m.getSizes()[i];
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
          os << *m.getLoop()->S[j - 1];
        } else os << offij;
        printPlus = true;
      }
    }
  }
  return os << "]\nSchedule Omega: " << m.getFusionOmega()
            << "\nAffineLoopNest:\n"
            << *m.getLoop();
}
