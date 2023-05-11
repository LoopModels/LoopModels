#pragma once
#include "Loops.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/Support/raw_ostream.h>

class Address;

class ArrayIndex {
  static constexpr auto memoryOmegaOffset(size_t arrayDim, size_t numLoops,
                                          size_t numSymbols) -> size_t {
    // arrayDim * numLoops from indexMatrix
    // arrayDim * numSymbols from offsetMatrix
    return arrayDim * (numLoops + numSymbols);
  }
  static constexpr auto memoryIntsRequired(size_t arrayDim, size_t numLoops,
                                           size_t numSymbols) -> size_t {
    // numLoops + 1 from getFusionOmega
    return memoryOmegaOffset(arrayDim, numLoops, numSymbols) + numLoops + 1;
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
  unsigned numDim{}, numDynSym{};
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  alignas(int64_t) char mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  // schedule indicated by `1` top bit, remainder indicates loop
  [[nodiscard]] constexpr auto data() -> NotNull<int64_t> {
    void *ptr = mem + sizeof(const llvm::SCEV *const *) * (numDim + numDynSym);
    return static_cast<int64_t *>(ptr);
  }
  [[nodiscard]] constexpr auto data() const -> NotNull<int64_t> {
    const void *ptr =
      mem + sizeof(const llvm::SCEV *const *) * (numDim + numDynSym);
    return static_cast<int64_t *>(const_cast<void *>(ptr));
  }
  [[nodiscard]] constexpr auto scevPtr() -> const llvm::SCEV ** {
    void *ptr = mem;
    return static_cast<const llvm::SCEV **>(ptr);
  }
  [[nodiscard]] constexpr auto scevPtr() const -> const llvm::SCEV *const * {
    const void *ptr = mem;
    return static_cast<const llvm::SCEV *const *>(ptr);
  }
  [[nodiscard]] constexpr auto omegaOffset() const -> size_t {
    return memoryOmegaOffset(getArrayDim(), getNumLoops(), getNumSymbols());
  }

public:
  // auto beginAddr() -> Address ** {
  //   return (addrReplications) ? addrss : &addrs;
  // }
  // auto endAddr() -> Address ** {
  //   return (addrReplications) ? addrss + numAddr : (&addrs) + 1;
  // }
  explicit constexpr ArrayIndex(const llvm::SCEVUnknown *arrayPtr,
                                AffineLoopNest<true> &loopRef,
                                llvm::Instruction *user,
                                std::array<unsigned, 2> dimOff)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user),
      numDim(dimOff[0]), numDynSym(dimOff[1]){};
  explicit constexpr ArrayIndex(const llvm::SCEVUnknown *arrayPtr,
                                AffineLoopNest<true> &loopRef,
                                llvm::Instruction *user)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user){};
  /// Constructor for 0 dimensional memory access
  static auto construct(BumpAlloc<> &alloc,
                        const llvm::SCEVUnknown *arrayPointer,
                        AffineLoopNest<true> &loopRef, llvm::Instruction *user,
                        PtrVector<unsigned> o) -> NotNull<ArrayIndex> {
    unsigned numLoops = loopRef.getNumLoops();
    invariant(o.size(), numLoops + 1);
    size_t memNeeded = numLoops;
    auto *mem = (ArrayIndex *)alloc.allocate(
      sizeof(ArrayIndex) + memNeeded * sizeof(int64_t), 8);
    auto *ma = std::construct_at(mem, arrayPointer, loopRef, user);
    ma->getFusionOmega() << o;
    return ma;
  }
  /// Constructor for regular indexing
  static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest<true> &loopRef, llvm::Instruction *user,
            PtrMatrix<int64_t> indMatT,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrMatrix<int64_t> offsets, PtrVector<unsigned> o)
    -> NotNull<ArrayIndex> {
    // we don't want to hold any other pointers that may need freeing
    unsigned arrayDim = szOff[0].size(), nOff = szOff[1].size();
    // auto sz = copyRef(alloc, llvm::ArrayRef<const llvm::SCEV *>{szOff[0]});
    // auto off = copyRef(alloc, llvm::ArrayRef<const llvm::SCEV *>{szOff[1]});
    unsigned numLoops = loopRef.getNumLoops();
    invariant(o.size(), numLoops + 1);
    size_t numSymbols = size_t(offsets.numCol());
    size_t memNeeded = memoryIntsRequired(arrayDim, numLoops, numSymbols);
    auto *mem = (ArrayIndex *)alloc.allocate(
      sizeof(ArrayIndex) + memNeeded * sizeof(int64_t) +
        (arrayDim + nOff) * sizeof(const llvm::SCEV *const *),
      alignof(ArrayIndex));
    auto *ma = std::construct_at(mem, arrayPtr, loopRef, user,
                                 std::array<unsigned, 2>{arrayDim, nOff});
    std::copy_n(szOff[0].begin(), arrayDim, ma->getSizes().begin());
    std::copy_n(szOff[1].begin(), nOff, ma->getSymbolicOffsets().begin());
    ma->indexMatrix() << indMatT.transpose();
    ma->offsetMatrix() << offsets;
    ma->getFusionOmega() << o;
    return ma;
  }
  /// omegas order is [outer <-> inner]
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return {data() + omegaOffset(), unsigned(getNumLoops()) + 1};
  }
  /// omegas order is [outer <-> inner]
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return {data() + omegaOffset(), unsigned(getNumLoops()) + 1};
  }
  [[nodiscard]] constexpr auto getLoop() const -> NotNull<AffineLoopNest<>> {
    return loop;
  }
  [[nodiscard]] inline auto getSizes()
    -> llvm::MutableArrayRef<const llvm::SCEV *> {
    return {scevPtr(), size_t(numDim)};
  }
  [[nodiscard]] inline auto getSymbolicOffsets()
    -> llvm::MutableArrayRef<const llvm::SCEV *> {
    return {scevPtr() + numDim, size_t(numDynSym)};
  }
  [[nodiscard]] inline auto getSizes() const
    -> llvm::ArrayRef<const llvm::SCEV *> {
    return {scevPtr(), size_t(numDim)};
  }
  [[nodiscard]] inline auto getSymbolicOffsets() const
    -> llvm::ArrayRef<const llvm::SCEV *> {
    return {scevPtr() + numDim, size_t(numDynSym)};
  }
  [[nodiscard]] auto isStore() const -> bool {
    return loadOrStore.isa<llvm::StoreInst>();
  }
  [[nodiscard]] auto isLoad() const -> bool { return !isStore(); }
  // TODO: `constexpr` once `llvm::SmallVector` supports it
  [[nodiscard]] constexpr auto getArrayDim() const -> size_t { return numDim; }
  [[nodiscard]] constexpr auto getNumSymbols() const -> size_t {
    return 1 + numDynSym;
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> size_t {
    return loop->getNumLoops();
  }

  [[nodiscard]] auto getAlign() const -> llvm::Align {
    if (auto *l = loadOrStore.dyn_cast<llvm::LoadInst>()) return l->getAlign();
    return loadOrStore.cast<llvm::StoreInst>()->getAlign();
  }
  /// indexMatrix() -> getNumLoops() x arrayDim()
  /// loops are in [outermost <-> innermost] order
  /// Maps loop indVars to array indices
  /// Letting `i` be the indVars and `d` the indices:
  /// indexMatrix()' * i == d
  /// e.g. `indVars = [i, j]` and indexMatrix = [ 1 1; 0 1]
  /// corresponds to A[i, i + j]
  /// Note that `[i, j]` refers to loops in
  /// innermost -> outermost order, i.e.
  /// for (i : I)
  ///   for (j : J)
  ///      A[i, i + j]
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    return {data(), DenseDims{getNumLoops(), d}};
  }
  /// indexMatrix() -> getNumLoops() x arrayDim()
  /// loops are in [innermost -> outermost] order
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    return {data(), DenseDims{getNumLoops(), d}};
  }
  [[nodiscard]] constexpr auto offsetMatrix() -> MutDensePtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return {data() + getNumLoops() * d, DenseDims{d, numSymbols}};
  }
  [[nodiscard]] constexpr auto offsetMatrix() const -> DensePtrMatrix<int64_t> {
    const size_t d = getArrayDim();
    const size_t numSymbols = getNumSymbols();
    return {data() + getNumLoops() * d, DenseDims{d, numSymbols}};
  }
  [[nodiscard]] constexpr auto getInstruction() -> NotNull<llvm::Instruction> {
    return loadOrStore;
  }
  [[nodiscard]] constexpr auto getInstruction() const
    -> NotNull<const llvm::Instruction> {
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
    if (const auto *C = llvm::dyn_cast<llvm::SCEVConstant>(S))
      return llvm::Align(C->getAPInt().getZExtValue());
    return llvm::Align{1};
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> const llvm::SCEV * {
    return basePointer;
  }
  // inline void addEdgeIn(NotNull<Dependence> i) { edgesIn.push_back(i); }
  // inline void addEdgeOut(NotNull<Dependence> i) { edgesOut.push_back(i); }

  // inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
  // inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }

  // size_t getNumLoops() const { return ref->getNumLoops(); }
  // size_t getNumAxes() const { return ref->axes.size(); }
  // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
  auto fusedThrough(ArrayIndex &other) -> bool {
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
    invariant(numToPeel > 0);
    invariant(numToPeel <= getNumLoops());
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
    // When copying overlapping ranges, std::copy is appropriate when copying
    // to the left (beginning of the destination range is outside the source
    // range) while std::copy_backward is appropriate when copying to the
    // right (end of the destination range is outside the source range).
    // https://en.cppreference.com/w/cpp/algorithm/copy
    int64_t *offOld = p + getArrayDim() * getNumLoops();
    int64_t *fusOld = offOld + getArrayDim() * getNumSymbols();
    int64_t *offNew = p + getArrayDim() * (getNumLoops() - numToPeel);
    int64_t *fusNew = offNew + getArrayDim() * getNumSymbols();
    std::copy(offOld, fusOld, offNew);
    std::copy(fusOld + numToPeel, fusOld + getNumLoops() + 1, fusNew);
  }
  [[nodiscard]] constexpr auto allConstantIndices() const -> bool {
    return numDynSym == 0;
  }
  // Assumes strides and offsets are sorted
  [[nodiscard]] auto sizesMatch(NotNull<const ArrayIndex> x) const -> bool {
    auto thisSizes = getSizes(), xSizes = x->getSizes();
    return std::equal(thisSizes.begin(), thisSizes.end(), xSizes.begin(),
                      xSizes.end());
  }
  [[nodiscard]] constexpr static auto gcdKnownIndependent(const ArrayIndex &)
    -> bool {
    // TODO: handle this!
    // consider `x[2i]` vs `x[2i + 1]`, the former
    // will have a stride of `2`, and the latter of `x[2i+1]`
    // Additionally, in the future, we do
    return false;
  }
};
