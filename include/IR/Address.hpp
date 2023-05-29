#pragma once

#include "IR/Node.hpp"
#include "Loops.hpp"
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Math.hpp"
#include "MemoryAccess.hpp"
#include "Utilities/Valid.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Allocator.h>

class Dependence;
namespace CostModeling {
class LoopTreeSchedule;
} // namespace CostModeling

/// Represents a memory access that has been rotated according to some affine
/// transform.
// clang-format off
/// Return the memory accesses after applying the Schedule.
/// Let
/// 
/// \f{eqnarray*}{
/// D &=& \text{the dimension of the array}\\ %
/// N &=& \text{depth of the loop nest}\\ %
/// V &=& \text{runtime variables}\\ %
/// \textbf{i}\in\mathbb{R}^N &=& \text{the old index vector}\\ %
/// \textbf{j}\in\mathbb{R}^N &=& \text{the new index vector}\\ %
/// \textbf{x}\in\mathbb{R}^D &=& \text{the indices into the array}\\ %
/// \textbf{M}\in\mathbb{R}^{N \times D} &=& \text{map from loop ind vars to array indices}\\ %
/// \boldsymbol{\Phi}\in\mathbb{R}^{N \times N} &=& \text{the schedule matrix}\\ %
/// \boldsymbol{\omega}\in\mathbb{R}^N &=& \text{the offset vector}\\ %
/// \textbf{c}\in\mathbb{R}^{N} &=& \text{the constant offset vector}\\ %
/// \textbf{C}\in\mathbb{R}^{N \times V} &=& \text{runtime variable coefficient matrix}\\ %
/// \textbf{s}\in\mathbb{R}^V &=& \text{the symbolic runtime variables}\\ %
/// \f}
/// 
/// The rows of \f$\boldsymbol{\Phi}\f$ are sorted from the outermost loop to
/// the innermost loop.
/// We have
/// \f{eqnarray*}{
/// \textbf{j} &=& \boldsymbol{\Phi}\textbf{i} + \boldsymbol{\omega}\\ %
/// \textbf{i} &=& \boldsymbol{\Phi}^{-1}\left(j - \boldsymbol{\omega}\right)\\ %
/// \textbf{x} &=& \textbf{M}'\textbf{i} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'\boldsymbol{\Phi}^{-1}\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{M}'_* &=& \textbf{M}'\boldsymbol{\Phi}^{-1}\\ %
/// \textbf{x} &=& \textbf{M}'_*\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'_*j - \textbf{M}'_*\boldsymbol{\omega} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{c}_* &=& \textbf{c} - \textbf{M}'_*\boldsymbol{\omega} \\ %
/// \textbf{x} &=& \textbf{M}'_*j + \textbf{c}_* + \textbf{Cs} \\ %
/// \f}
/// Therefore, to update the memory accesses from the old induction variables $i$
/// to the new variables $j$, we must simply compute the updated
/// \f$\textbf{c}_*\f$ and \f$\textbf{M}'_*\f$.
/// We can also test for the case where \f$\boldsymbol{\Phi} = \textbf{E}\f$, or equivalently that $\textbf{E}\boldsymbol{\Phi} = \boldsymbol{\Phi} = \textbf{I}$.
/// Note that to get the new AffineLoopNest, we call
/// `oldLoop->rotate(PhiInv)`
// clang-format on
class Addr : public Node {
  using BitSet = MemoryAccess::BitSet;
  [[no_unique_address]] NotNull<const llvm::SCEVUnknown> basePointer;
  [[no_unique_address]] NotNull<AffineLoopNest> loop;
  [[no_unique_address]] llvm::Instruction *instr;
  [[no_unique_address]] Dependence *edgeIn;
  [[no_unique_address]] int64_t *offSym{nullptr};
  [[no_unique_address]] unsigned numDim{0}, numDynSym{0};
  [[no_unique_address]] uint8_t numMemInputs;
  [[no_unique_address]] uint8_t numDirectEdges;
  [[no_unique_address]] uint8_t numMemOutputs;
  [[no_unique_address]] uint8_t index_{0};
  [[no_unique_address]] uint8_t lowLink_{0};
  [[no_unique_address]] uint8_t blckIdx{0};
  [[no_unique_address]] uint8_t bitfield;
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
  explicit constexpr Addr(const llvm::SCEVUnknown *arrayPtr,
                          AffineLoopNest &loopRef, llvm::Instruction *user,
                          int64_t *offsym, std::array<unsigned, 2> dimOff)
    : basePointer(arrayPtr), loop(loopRef), instr(user), offSym(offsym),
      numDim(dimOff[0]), numDynSym(dimOff[1]){};
  explicit constexpr Addr(const llvm::SCEVUnknown *arrayPtr,
                          AffineLoopNest &loopRef, llvm::Instruction *user)
    : basePointer(arrayPtr), loop(loopRef), loadOrStore(user){};
  /// Constructor for 0 dimensional memory access
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPointer,
            AffineLoopNest &loopRef, llvm::Instruction *user,
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
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest &loopRef, llvm::Instruction *user,
            PtrMatrix<int64_t> indMat,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrVector<int64_t> coffsets, int64_t *offsets,
            PtrVector<unsigned> o) -> NotNull<ArrayIndex> {
    // we don't want to hold any other pointers that may need freeing
    unsigned arrayDim = szOff[0].size(), nOff = szOff[1].size();
    unsigned numLoops = loopRef.getNumLoops();
    invariant(o.size(), numLoops + 1);
    size_t memNeeded = memoryIntsRequired(arrayDim, numLoops);
    auto *mem = (ArrayIndex *)alloc.allocate(
      sizeof(ArrayIndex) + memNeeded * sizeof(int64_t) +
        (arrayDim + nOff) * sizeof(const llvm::SCEV *const *),
      alignof(ArrayIndex));
    auto *ma = std::construct_at(mem, arrayPtr, loopRef, user, offsets,
                                 std::array<unsigned, 2>{arrayDim, nOff});
    std::copy_n(szOff[0].begin(), arrayDim, ma->getSizes().begin());
    std::copy_n(szOff[1].begin(), nOff, ma->getSymbolicOffsets().begin());
    ma->indexMatrix() << indMat;
    ma->offsetVector() << coffsets;
    ma->getFusionOmega() << o;
    return ma;
  }

  constexpr Addr(NotNull<AffineLoopNest> explicitLoop, NotNull<MemoryAccess> ma,
                 SquarePtrMatrix<int64_t> Pinv, int64_t denom,
                 PtrVector<int64_t> omega, bool isStr,
                 CostModeling::LoopTreeSchedule *L, uint8_t memInputs,
                 uint8_t directEdges, uint8_t memOutputs, int64_t *offsets)
    : Node(isStr ? VK_Stow : VK_Load, unsigned(Pinv.numCol())),
      arrayRef(ma->getArrayRef()), loop(explicitLoop), node(L),
      numMemInputs(memInputs), numDirectEdges(directEdges),
      numMemOutputs(memOutputs), bitfield(uint8_t(isStr) << 3) {
    PtrMatrix<int64_t> M{arrayRef->indexMatrix()}; // aD x nLma
    MutPtrMatrix<int64_t> mStar{indexMatrix()};    // aD x nLp
    // M is implicitly padded with zeros, nLp >= nLma
    unsigned nLma = ma->getNumLoops();
    invariant(nLma <= depth);
    invariant(size_t(nLma), size_t(M.numRow()));
    mStar << M * Pinv(_(0, nLma), _);
    getDenominator() = denom;
    getOffsetOmega() << ma->offsetMatrix()(_, 0) - mStar * omega;
    if (offsets) getOffsetOmega() -= M * PtrVector<int64_t>{offsets, nLma};
  }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return (int64_t *)const_cast<void *>((const void *)mem);
    // return const_cast<int64_t *>(mem);
  }
  [[nodiscard]] constexpr auto getAddrMemory() const -> Addr ** {
    const void *m =
      mem +
      (1 + getArrayDim() + (getArrayDim() * getNumLoops())) * sizeof(int64_t);
    // const void *m = addr_;
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (Addr **)p;
  }
  [[nodiscard]] constexpr auto getDDepthMemory() const -> uint8_t * {
    const void *m =
      mem +
      (1 + getArrayDim() + (getArrayDim() * getNumLoops())) * sizeof(int64_t) +
      (numMemInputs + numDirectEdges + numMemOutputs) * sizeof(Addr *);
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (uint8_t *)p;
  }

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() <= VK_Stow;
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> const llvm::SCEV * {
    return arrayRef->getArrayPointer();
  }
  [[nodiscard]] constexpr auto dependsOnIndVars(size_t d) -> bool {
    for (size_t i = 0, D = getArrayDim(); i < D; ++i)
      if (anyNEZero(indexMatrix()(i, _(d, end)))) return true;
    return false;
  }
  [[nodiscard]] constexpr auto getLoop() const -> NotNull<AffineLoopNest> {
    return loop;
  }
  [[nodiscard]] constexpr auto getBlockIdx() const -> uint8_t {
    return blckIdx;
  }
  constexpr void setBlockIdx(uint8_t idx) { blckIdx = idx; }
  [[nodiscard]] constexpr auto getLoopTreeSchedule() const
    -> CostModeling::LoopTreeSchedule * {
    return node;
  }
  constexpr void setLoopTreeSchedule(CostModeling::LoopTreeSchedule *L) {
    node = L;
  }
  constexpr void setLoopTreeSchedule(CostModeling::LoopTreeSchedule *L,
                                     unsigned blockIdx) {
    node = L;
    blckIdx = blockIdx;
  }
  // bits: 0 = visited, 1 = on stack, 2 = placed, 4 = visited2, 5 = activeSubset
  constexpr void visit() { bitfield |= 1; }
  constexpr void unVisit() { bitfield &= ~uint8_t(1); }
  [[nodiscard]] constexpr auto wasVisited() const -> bool {
    return bitfield & 1;
  }
  constexpr void visit2() { bitfield |= 16; }
  constexpr void unVisit2() { bitfield &= ~uint8_t(16); }
  [[nodiscard]] constexpr auto wasVisited2() const -> bool {
    return bitfield & 16;
  }
  constexpr void visit3() { bitfield |= 64; }
  constexpr void unVisit3() { bitfield &= ~uint8_t(64); }
  [[nodiscard]] constexpr auto wasVisited3() const -> bool {
    return bitfield & 64;
  }
  // constexpr void unVisit02() { bitfield &= ~uint8_t(17); }
  constexpr void addToSubset() { bitfield |= 32; }
  constexpr void removeFromSubset() { bitfield &= ~uint8_t(32); }
  [[nodiscard]] constexpr auto inActiveSubset() const -> bool {
    return bitfield & 32;
  }
  constexpr void addToStack() { bitfield |= 2; }
  constexpr void removeFromStack() { bitfield &= ~uint8_t(2); }
  // doesn't reset isStore or wasPlaced
  constexpr void resetBitfield() { bitfield &= uint8_t(12); }
  [[nodiscard]] constexpr auto getParents() const -> BitSet { return parents; }
  // [[nodiscard]] constexpr auto getChildren() const -> BitSet {
  //   return children;
  // }
  constexpr auto getAncestors() -> BitSet & { return ancestors; }
  [[nodiscard]] constexpr auto getAncestors() const -> BitSet {
    return ancestors;
  }
  // constexpr auto getDescendants() -> BitSet & { return descendants; }
  // [[nodiscard]] constexpr auto getDescendants() const -> BitSet {
  //   return descendants;
  // }
  constexpr void addParent(size_t i) { parents.insert(i); }
  // constexpr void addChild(size_t i) { children.insert(i); }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto calcAncestors(uint8_t filtd) -> BitSet {
    if (wasVisited()) return ancestors;
    visit();
    ancestors = {};
    parents = {};
    for (auto *e : inNeighbors(filtd)) {
      ancestors |= e->calcAncestors(filtd);
      parents[e->index()] = true;
      // invariant(!parents.insert(e->index()));
    }
    return ancestors |= parents;
  }
  // // NOLINTNEXTLINE(misc-no-recursion)
  // constexpr auto calcDescendants(uint8_t filtd) -> BitSet {
  //   if (wasVisited2()) return descendants;
  //   visit2();
  //   descendants = {};
  //   children = {};
  //   for (auto *e : outNeighbors(filtd)) {
  //     descendants |= e->calcDescendants(filtd);
  //     children[e->index()] = true;
  //     // invariant(!children.insert(e->index()));
  //   }
  //   return descendants |= children;
  // }
  [[nodiscard]] constexpr auto onStack() const -> bool { return bitfield & 2; }
  constexpr void place() { bitfield |= 4; }
  [[nodiscard]] constexpr auto wasPlaced() const -> bool {
    return bitfield & 4;
  }
  /// isStore() is true if the address is a store, false if it is a load
  /// If the memory access is a store, this can still be a reload
  [[nodiscard]] constexpr auto isStore() const -> bool {
    return getKind() == VK_Stow;
  }
  [[nodiscard]] constexpr auto isLoad() const -> bool {
    return getKind() == VK_Load;
  }
  constexpr auto index() -> uint8_t & { return index_; }
  [[nodiscard]] constexpr auto index() const -> unsigned { return index_; }
  constexpr auto lowLink() -> uint8_t & { return lowLink_; }
  [[nodiscard]] constexpr auto lowLink() const -> unsigned { return lowLink_; }

  struct EndSentinel {};
  class ActiveEdgeIterator {
    Addr **p;
    Addr **e;
    uint8_t *d;
    uint8_t filtdepth;

  public:
    constexpr auto operator*() const -> Addr * { return *p; }
    constexpr auto operator->() const -> Addr * { return *p; }
    /// true means skip, false means we evaluate
    /// so *d <= filtdepth means we skip, evaluating only *d > filtdepth
    [[nodiscard]] constexpr auto hasNext() const -> bool {
      return ((p != e) && ((*d <= filtdepth) || (!((*p)->inActiveSubset()))));
    }
    constexpr auto operator++() -> ActiveEdgeIterator & {
      // meaning of filtdepth 127?
      do {
        ++p;
        ++d;
      } while (hasNext());
      return *this;
    }
    constexpr auto operator==(EndSentinel) const -> bool { return p == e; }
    constexpr auto operator!=(EndSentinel) const -> bool { return p != e; }
    constexpr ActiveEdgeIterator(Addr **_p, Addr **_e, uint8_t *_d, uint8_t fd)
      : p(_p), e(_e), d(_d), filtdepth(fd) {
      while (hasNext()) {
        ++p;
        ++d;
      }
    }
    constexpr auto operator++(int) -> ActiveEdgeIterator {
      ActiveEdgeIterator tmp = *this;
      ++*this;
      return tmp;
    }
    [[nodiscard]] constexpr auto begin() const -> ActiveEdgeIterator {
      return *this;
    }
    [[nodiscard]] static constexpr auto end() -> EndSentinel { return {}; }
  };
  [[nodiscard]] constexpr auto numInNeighbors() const -> unsigned {
    return isStore() ? numMemInputs + numDirectEdges : numMemInputs;
  }
  [[nodiscard]] constexpr auto numOutNeighbors() const -> unsigned {
    return isStore() ? numMemOutputs : numDirectEdges + numMemOutputs;
  }
  [[nodiscard]] constexpr auto numNeighbors() const -> unsigned {
    return numMemInputs + numDirectEdges + numMemOutputs;
  }
  [[nodiscard]] auto inNeighbors(uint8_t filtd) -> ActiveEdgeIterator {
    Addr **p = getAddrMemory();
    return {p, p + numInNeighbors(), getDDepthMemory(), filtd};
  }
  [[nodiscard]] auto outNeighbors(uint8_t filtd) -> ActiveEdgeIterator {
    unsigned n = numInNeighbors();
    Addr **p = getAddrMemory() + n;
    return {p, p + numOutNeighbors(), getDDepthMemory() + n, filtd};
  }
#ifndef NDEBUG
  [[gnu::used, nodiscard]] constexpr auto directEdges()
    -> MutPtrVector<Addr *> {
    Addr **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[gnu::used, nodiscard]] constexpr auto directEdges() const
    -> PtrVector<Addr *> {
    Addr **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[gnu::used, nodiscard]] constexpr auto inDepSat() const
    -> PtrVector<uint8_t> {
    return {getDDepthMemory(), numInNeighbors()};
  }
  [[gnu::used, nodiscard]] constexpr auto outDepSat() const
    -> PtrVector<uint8_t> {
    return {getDDepthMemory() + numInNeighbors(), numOutNeighbors()};
  }
  [[gnu::used, nodiscard]] constexpr auto inNeighbors() const
    -> PtrVector<Addr *> {
    return PtrVector<Addr *>{getAddrMemory(), numInNeighbors()};
  }
  [[gnu::used, nodiscard]] constexpr auto outNeighbors() const
    -> PtrVector<Addr *> {
    return PtrVector<Addr *>{getAddrMemory() + numInNeighbors(),
                             numOutNeighbors()};
  }
  [[gnu::used, nodiscard]] constexpr auto inNeighbors()
    -> MutPtrVector<Addr *> {
    return MutPtrVector<Addr *>{getAddrMemory(), numInNeighbors()};
  }
  [[gnu::used, nodiscard]] constexpr auto outNeighbors()
    -> MutPtrVector<Addr *> {
    return MutPtrVector<Addr *>{getAddrMemory() + numInNeighbors(),
                                numOutNeighbors()};
  }
#else
  [[nodiscard]] constexpr auto directEdges() -> MutPtrVector<Address *> {
    Address **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[nodiscard]] constexpr auto directEdges() const -> PtrVector<Address *> {
    Address **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[nodiscard]] constexpr auto inDepSat() const -> PtrVector<uint8_t> {
    return {getDDepthMemory(), numInNeighbors()};
  }
  [[nodiscard]] constexpr auto outDepSat() const -> PtrVector<uint8_t> {
    return {getDDepthMemory() + numInNeighbors(), numOutNeighbors()};
  }
  [[nodiscard]] constexpr auto inNeighbors() const -> PtrVector<Address *> {
    return PtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  }
  [[nodiscard]] constexpr auto outNeighbors() const -> PtrVector<Address *> {
    return PtrVector<Address *>{getAddrMemory() + numInNeighbors(),
                                numOutNeighbors()};
  }
  [[nodiscard]] constexpr auto inNeighbors() -> MutPtrVector<Address *> {
    return MutPtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  }
  [[nodiscard]] constexpr auto outNeighbors() -> MutPtrVector<Address *> {
    return MutPtrVector<Address *>{getAddrMemory() + numInNeighbors(),
                                   numOutNeighbors()};
  }
#endif
  constexpr void indirectInNeighbor(Addr *other, size_t i, uint8_t d) {
    getAddrMemory()[i] = other;
    getDDepthMemory()[i] = d;
  }
  constexpr void indirectOutNeighbor(Addr *other, size_t i, uint8_t d) {
    getAddrMemory()[numMemInputs + numDirectEdges + i] = other;
    getDDepthMemory()[numMemInputs + numDirectEdges + i] = d;
  }
  [[nodiscard]] static auto allocate(BumpAlloc<> &alloc,
                                     NotNull<MemoryAccess> ma,
                                     unsigned inputEdges, unsigned directEdges,
                                     unsigned outputEdges) -> NotNull<Addr> {

    size_t numLoops = ma->getNumLoops(), arrayDim = ma->getArrayDim(),
           memSz = (1 + arrayDim + (arrayDim * numLoops)) * sizeof(int64_t) +
                   (inputEdges + directEdges + outputEdges) *
                     (sizeof(Addr *) + sizeof(uint8_t)) +
                   sizeof(Addr);
    return (Addr *)alloc.allocate(memSz, alignof(Addr));
  }
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, NotNull<AffineLoopNest> explicitLoop,
            NotNull<MemoryAccess> ma, bool isStr, SquarePtrMatrix<int64_t> Pinv,
            int64_t denom, PtrVector<int64_t> omega,
            CostModeling::LoopTreeSchedule *L, unsigned inputEdges,
            unsigned directEdges, unsigned outputEdges, int64_t *offsets)
    -> NotNull<Addr> {

    size_t numLoops = size_t(Pinv.numCol()), arrayDim = ma->getArrayDim(),
           memSz = (1 + arrayDim + (arrayDim * numLoops)) * sizeof(int64_t) +
                   (inputEdges + directEdges + outputEdges) *
                     (sizeof(Addr *) + sizeof(uint8_t)) +
                   sizeof(Addr);
    // size_t memSz = ma->getNumLoops() * (1 + ma->getArrayDim());
    auto *pt = alloc.allocate(memSz, alignof(Addr));
    // we could use the passkey idiom to make the constructor public yet
    // un-callable so that we can use std::construct_at (which requires a
    // public constructor) or, we can just use placement new and not mark this
    // function which will never be constant evaluated anyway constexpr (main
    // benefit of constexpr is UB is not allowed, so we get more warnings).
    // return std::construct_at((Address *)pt, explicitLoop, ma, Pinv, denom,
    // omega, isStr);
    return new (pt) Addr(explicitLoop, ma, Pinv, denom, omega, isStr, L,
                         inputEdges, directEdges, outputEdges, offsets);
  }
  constexpr void addDirectConnection(Addr *store, size_t loadEdge) {
    assert(isLoad() && store->isStore());
    directEdges().front() = store;
    store->directEdges()[loadEdge] = this;
    // never ignored
    getDDepthMemory()[numMemInputs] = 255;
    store->getDDepthMemory()[numMemInputs + loadEdge] = 255;
  }
  constexpr void addOut(Addr *child, uint8_t d) {
    // we hijack index_ and lowLink_ before they're used for SCC
    size_t inInd = index_++, outInd = child->lowLink_++;
    indirectOutNeighbor(child, inInd, d);
    child->indirectInNeighbor(this, outInd, d);
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> size_t { return depth; }
  [[nodiscard]] constexpr auto getArrayDim() const -> size_t {
    return arrayRef->getArrayDim();
  }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * {
    return arrayRef->getInstruction();
  }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return arrayRef->getInstruction();
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    return arrayRef->getAlign();
  }
  [[nodiscard]] constexpr auto getDenominator() -> int64_t & {
    return getIntMemory()[0];
  }
  [[nodiscard]] constexpr auto getDenominator() const -> int64_t {
    return getIntMemory()[0];
  }
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {getIntMemory() + 1, unsigned(getArrayDim())};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {getIntMemory() + 1, unsigned(getArrayDim())};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return {getIntMemory() + 1 + getArrayDim(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {getIntMemory() + 1 + getArrayDim(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  [[nodiscard]] constexpr auto getLoop() -> NotNull<AffineLoopNest> {
    return loop;
  }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> unsigned;
  void printDotName(llvm::raw_ostream &os) const {
    if (isLoad()) os << "... = ";
    os << *arrayRef->getArrayPointer() << "[";
    DensePtrMatrix<int64_t> A{indexMatrix()};
    DensePtrMatrix<int64_t> B{arrayRef->offsetMatrix()};
    PtrVector<int64_t> b{getOffsetOmega()};
    size_t numLoops = size_t(A.numCol());
    for (size_t i = 0; i < A.numRow(); ++i) {
      if (i) os << ", ";
      bool printPlus = false;
      for (size_t j = 0; j < numLoops; ++j) {
        if (int64_t Aji = A(i, j)) {
          if (printPlus) {
            if (Aji <= 0) {
              Aji *= -1;
              os << " - ";
            } else os << " + ";
          }
          if (Aji != 1) os << Aji << '*';
          os << "i_" << j;
          printPlus = true;
        }
      }
      for (size_t j = 0; j < B.numCol(); ++j) {
        if (int64_t offij = j ? B(i, j) : b[i]) {
          if (printPlus) {
            if (offij <= 0) {
              offij *= -1;
              os << " - ";
            } else os << " + ";
          }
          if (j) {
            if (offij != 1) os << offij << '*';
            os << *arrayRef->getLoop()->getSyms()[j - 1];
          } else os << offij;
          printPlus = true;
        }
      }
    }
    os << "]";
    if (isStore()) os << " = ...";
  }
};
