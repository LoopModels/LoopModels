#pragma once
#include "./BitSets.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Utilities.hpp"
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
#include <strings.h>

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
    return arrayDim * numLoops + numLoops + arrayDim * numSymbols;
  }

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
  llvm::SmallVector<const llvm::SCEV *, 3> sizes;
  llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
  // unsigned (instead of ptr) as we build up edges
  // and I don't want to relocate pointers when resizing vector
  BitSet<> edgesIn;
  BitSet<> edgesOut;
  BitSet<> nodeIndex;
  // This is a flexible length array, declared as a length-1 array
  // I wish there were some way to opt into "I'm using a c99 extension"
  // so that I could use `mem[]` or `mem[0]` instead of `mem[1]`. See:
  // https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
  // https://developers.redhat.com/articles/2022/09/29/benefits-limitations-flexible-array-members#flexible_array_members_vs__pointer_implementation
  int64_t mem[1]; // NOLINT(modernize-avoid-c-arrays)
  // schedule indicated by `1` top bit, remainder indicates loop
  [[nodiscard]] constexpr auto data() -> NotNull<int64_t> { return mem; }
  [[nodiscard]] constexpr auto data() const -> NotNull<const int64_t> {
    return mem;
  }
  [[nodiscard]] auto omegaOffset() const -> size_t {
    return memoryOmegaOffset(getArrayDim(), getNumLoops(), getNumSymbols());
  }
  MemoryAccess(const llvm::SCEVUnknown *arrayPtr, AffineLoopNest<true> &loopRef,
               llvm::Instruction *user,
               llvm::SmallVector<const llvm::SCEV *, 3> sz,
               llvm::SmallVector<const llvm::SCEV *, 3> off)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user),
      sizes(std::move(sz)), symbolicOffsets(std::move(off)){};
  MemoryAccess(const llvm::SCEVUnknown *arrayPtr, AffineLoopNest<true> &loopRef,
               llvm::Instruction *user)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user){};

public:
  static auto construct(llvm::BumpPtrAllocator &alloc,
                        const llvm::SCEVUnknown *arrayPointer,
                        AffineLoopNest<true> &loopRef, llvm::Instruction *user,
                        PtrVector<unsigned> o) -> NotNull<MemoryAccess> {
    size_t numLoops = loopRef.getNumLoops();
    assert(o.size() == numLoops + 1);
    size_t memNeeded = numLoops;
    auto *mem =
      alloc.Allocate<char>(sizeof(MemoryAccess) + memNeeded * sizeof(int64_t));
    auto *ma = new (mem) MemoryAccess(arrayPointer, loopRef, user);
    ma->getFusionOmega() = o;
    return ma;
  }
  static auto
  construct(llvm::BumpPtrAllocator &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest<true> &loopRef, llvm::Instruction *user,
            PtrMatrix<int64_t> indMatT,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrMatrix<int64_t> offsets, PtrVector<unsigned> o)
    -> NotNull<MemoryAccess> {
    auto [sz, off] = std::move(szOff);
    size_t arrayDim = sz.size();
    size_t numLoops = loopRef.getNumLoops();
    assert(o.size() == numLoops + 1);
    size_t numSymbols = size_t(offsets.numCol());
    size_t memNeeded = memoryTotalRequired(arrayDim, numLoops, numSymbols);
    auto *mem =
      alloc.Allocate<char>(sizeof(MemoryAccess) + memNeeded * sizeof(int64_t));
    auto *ma = new (mem)
      MemoryAccess(arrayPtr, loopRef, user, std::move(sz), std::move(off));
    ma->indexMatrix() = indMatT.transpose();
    ma->offsetMatrix() = offsets;
    ma->getFusionOmega() = o;

    return ma;
  }
  /// omegas order is [outer <-> inner]
  [[nodiscard]] auto getFusionOmega() -> MutPtrVector<int64_t> {
    return {data() + omegaOffset(), getNumLoops() + 1};
  }
  /// omegas order is [outer <-> inner]
  [[nodiscard]] auto getFusionOmega() const -> PtrVector<int64_t> {
    return {data() + omegaOffset(), getNumLoops() + 1};
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
    -> const llvm::SmallVector<const llvm::SCEV *, 3> & {
    return sizes;
  }
  [[nodiscard]] auto getSymbolicOffsets() const
    -> const llvm::SmallVector<const llvm::SCEV *, 3> & {
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
  /// loops are in [innermost -> outermost] order
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
  /// loops are in [innermost -> outermost] order
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
  auto fusedThrough(MemoryAccess &other) -> bool {
    size_t numLoopsCommon = std::min(getNumLoops(), other.getNumLoops());
    auto thisOmega = getFusionOmega();
    auto otherOmega = other.getFusionOmega();
    return std::equal(thisOmega.begin(), thisOmega.begin() + numLoopsCommon,
                      otherOmega.begin());
  }
  // note returns true if unset
  // inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
  // inline PtrVector<int64_t> getSchedule(size_t loop) const {
  //     return schedule.getPhi()(loop, _);
  // }
  // FIXME: needs to truncate indexMatrix, offsetVector, etc
  void peelLoops(size_t numToPeel) {
    assert(numToPeel > 0 && "Shouldn't be peeling 0 loops");
    assert(numToPeel <= getNumLoops() && "Cannot peel more loops than exist");
    // we're dropping the outer-most `numToPeel` loops
    // current memory layout:
    // - indexMatrix (getNumLoops() x getArrayDim())
    // - offsetMatrix (getArrayDim() x getNumSymbols())
    // - fusionOmegas (getNumLoops()+1)
    //
    // indexMatrix rows are in innermost -> outermost order
    // fusionOmegas are in outer<->inner order
    // so we copy `offsetMatrix` `numToPeel * getArrayDim()` elements earlier
    int64_t *p = data();
    // When copying overlapping ranges, std::copy is appropriate when copying to
    // the left (beginning of the destination range is outside the source range)
    // while std::copy_backward is appropriate when copying to the right (end of
    // the destination range is outside the source range).
    // https://en.cppreference.com/w/cpp/algorithm/copy
    int64_t *offOld = p + getArrayDim() * getNumLoops();
    int64_t *fusOld = offOld + getArrayDim() * getNumSymbols();
    int64_t *offNew = p + getArrayDim() * (getNumLoops() - numToPeel);
    int64_t *fusNew = offNew + getArrayDim() * getNumSymbols();
    std::copy(offOld, fusOld, offNew);
    std::copy(fusOld + numToPeel, fusOld + getNumLoops() + 1, fusNew);
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
