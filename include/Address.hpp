#pragma once

#include "Loops.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "MemoryAccess.hpp"
#include "Utilities/Valid.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Allocator.h>

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
class Address {
  using BitSet = MemoryAccess::BitSet;
  /// Original (untransformed) memory access
  [[no_unique_address]] MemoryAccess oldMemAccess;
  /// transformed loop
  [[no_unique_address]] NotNull<AffineLoopNest<false>> loop;
  [[no_unique_address]] CostModeling::LoopTreeSchedule *node{nullptr};
  [[no_unique_address]] BitSet parents;
  [[no_unique_address]] BitSet children;
  [[no_unique_address]] BitSet ancestors;
  [[no_unique_address]] BitSet descendants;
  [[no_unique_address]] uint8_t numMemInputs;
  [[no_unique_address]] uint8_t numDirectEdges;
  [[no_unique_address]] uint8_t numMemOutputs;
  [[no_unique_address]] uint8_t index_{0};
  [[no_unique_address]] uint8_t lowLink_{0};
  [[no_unique_address]] uint8_t dim;
  [[no_unique_address]] uint8_t depth;
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
  constexpr Address(NotNull<AffineLoopNest<false>> explicitLoop,
                    MemoryAccess ma, SquarePtrMatrix<int64_t> Pinv,
                    int64_t denom, PtrVector<int64_t> omega, bool isStr,
                    CostModeling::LoopTreeSchedule *L, uint8_t memInputs,
                    uint8_t directEdges, uint8_t memOutputs)
    : oldMemAccess(ma), loop(explicitLoop), node(L), numMemInputs(memInputs),
      numDirectEdges(directEdges), numMemOutputs(memOutputs),
      dim(ma.getArrayDim()), depth(uint8_t(Pinv.numCol())),
      bitfield(uint8_t(isStr) << 3) {
    PtrMatrix<int64_t> M = oldMemAccess.indexMatrix(); // nLma x aD
    MutPtrMatrix<int64_t> mStar{indexMatrix()};        // aD x nLp
    // M is implicitly padded with zeros, nLp >= nLma
    size_t nLma = ma.getNumLoops();
    invariant(nLma <= depth);
    invariant(nLma, size_t(M.numRow()));
    mStar << M.transpose() * Pinv(_(0, nLma), _);
    getDenominator() = denom;
    getOffsetOmega() << ma.offsetMatrix()(_, 0) - mStar * omega;
  }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return (int64_t *)const_cast<void *>((const void *)mem);
    // return const_cast<int64_t *>(mem);
  }
  [[nodiscard]] constexpr auto getAddrMemory() const -> Address ** {
    const void *m =
      mem +
      (1 + getArrayDim() + (getArrayDim() * getNumLoops())) * sizeof(int64_t);
    // const void *m = addr_;
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (Address **)p;
  }
  [[nodiscard]] constexpr auto getDDepthMemory() const -> uint8_t * {
    const void *m =
      mem +
      (1 + getArrayDim() + (getArrayDim() * getNumLoops())) * sizeof(int64_t) +
      (numMemInputs + numDirectEdges + numMemOutputs) * sizeof(Address *);
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (uint8_t *)p;
  }

public:
  [[nodiscard]] constexpr auto getLoop() const
    -> NotNull<AffineLoopNest<false>> {
    return loop;
  }
  [[nodiscard]] constexpr auto getLoopTreeSchedule() const
    -> CostModeling::LoopTreeSchedule * {
    return node;
  }
  constexpr void setLoopTreeSchedule(CostModeling::LoopTreeSchedule *L) {
    node = L;
  }
  // bits: 0 = visited, 1 = on stack, 2 = placed
  // 3 = isStore, 4 = visited2, 5 = activeSubset
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
  [[nodiscard]] constexpr auto getChildren() const -> BitSet {
    return children;
  }
  constexpr auto getAncestors() -> BitSet & { return ancestors; }
  [[nodiscard]] constexpr auto getAncestors() const -> BitSet {
    return ancestors;
  }
  constexpr auto getDescendants() -> BitSet & { return descendants; }
  [[nodiscard]] constexpr auto getDescendants() const -> BitSet {
    return descendants;
  }
  constexpr void addParent(size_t i) { parents.insert(i); }
  constexpr void addChild(size_t i) { children.insert(i); }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto calcAncestors(uint8_t filtd) -> BitSet {
    if (wasVisited()) return ancestors;
    visit();
    ancestors = {};
    parents = {};
    for (auto *e : inNeighbors(filtd)) {
      ancestors |= e->calcAncestors(filtd);
      parents.insert(e->index());
    }
    return ancestors |= parents;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto calcDescendants(uint8_t filtd) -> BitSet {
    if (wasVisited2()) return descendants;
    visit2();
    descendants = {};
    children = {};
    for (auto *e : outNeighbors(filtd)) {
      descendants |= e->calcDescendants(filtd);
      children.insert(e->index());
    }
    return descendants |= children;
  }
  [[nodiscard]] constexpr auto onStack() const -> bool { return bitfield & 2; }
  constexpr void place() { bitfield |= 4; }
  [[nodiscard]] constexpr auto wasPlaced() const -> bool {
    return bitfield & 4;
  }
  /// isStore() is true if the address is a store, false if it is a load
  /// If the memory access is a store, this can still be a reload
  [[nodiscard]] constexpr auto isStore() const -> bool { return bitfield & 8; }
  [[nodiscard]] constexpr auto isLoad() const -> bool { return !isStore(); }
  constexpr auto index() -> uint8_t & { return index_; }
  [[nodiscard]] constexpr auto index() const -> unsigned { return index_; }
  constexpr auto lowLink() -> uint8_t & { return lowLink_; }
  [[nodiscard]] constexpr auto lowLink() const -> unsigned { return lowLink_; }

  struct EndSentinel {};
  class ActiveEdgeIterator {
    Address **p;
    Address **e;
    uint8_t *d;
    uint8_t filtdepth;

  public:
    constexpr auto operator*() const -> Address * { return *p; }
    constexpr auto operator->() const -> Address * { return *p; }
    [[nodiscard]] constexpr auto hasNext() const -> bool {
      return ((p != e) && ((*d < filtdepth) || (!((*p)->inActiveSubset()))));
    }
    constexpr auto operator++() -> ActiveEdgeIterator & {
      // meaning of filtdepth 255?
      do {
        ++p;
        ++d;
      } while (hasNext());
      return *this;
    }
    constexpr auto operator==(EndSentinel) const -> bool { return p == e; }
    constexpr auto operator!=(EndSentinel) const -> bool { return p != e; }
    constexpr ActiveEdgeIterator(Address **_p, Address **_e, uint8_t *_d,
                                 uint8_t fd)
      : p(_p), e(_e), d(_d), filtdepth(fd) {
      while (hasNext()) {
        ++p;
        ++d;
      }
    }
    constexpr auto operator++(int) -> ActiveEdgeIterator {
      auto tmp = *this;
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
    Address **p = getAddrMemory();
    return {p, p + numInNeighbors(), getDDepthMemory(), filtd};
  }
  [[nodiscard]] auto outNeighbors(uint8_t filtd) -> ActiveEdgeIterator {
    unsigned n = numInNeighbors();
    Address **p = getAddrMemory() + n;
    return {p, p + numOutNeighbors(), getDDepthMemory() + n, filtd};
  }
  [[nodiscard]] constexpr auto directEdges() -> MutPtrVector<Address *> {
    Address **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[nodiscard]] constexpr auto directEdges() const -> PtrVector<Address *> {
    Address **p = getAddrMemory() + numMemInputs;
    return {p, numDirectEdges};
  }
  [[nodiscard]] constexpr auto outDepSat() -> PtrVector<uint8_t> {
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
  constexpr void indirectInNeighbor(Address *other, size_t i, uint8_t d) {
    getAddrMemory()[i] = other;
    getDDepthMemory()[i] = d;
  }
  constexpr void indirectOutNeighbor(Address *other, size_t i, uint8_t d) {
    getAddrMemory()[numMemInputs + numDirectEdges + i] = other;
    getDDepthMemory()[numMemInputs + numDirectEdges + i] = d;
  }

  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, NotNull<AffineLoopNest<false>> explicitLoop,
            MemoryAccess ma, bool isStr, SquarePtrMatrix<int64_t> Pinv,
            int64_t denom, PtrVector<int64_t> omega,
            CostModeling::LoopTreeSchedule *L, unsigned inputEdges,
            unsigned directEdges, unsigned outputEdges) -> NotNull<Address> {

    size_t numLoops = size_t(Pinv.numCol()), arrayDim = ma.getArrayDim(),
           memSz = (1 + arrayDim + (arrayDim * numLoops)) * sizeof(int64_t) +
                   (inputEdges + directEdges + outputEdges) *
                     (sizeof(Address *) + sizeof(uint8_t)) +
                   sizeof(Address);
    // size_t memSz = ma->getNumLoops() * (1 + ma->getArrayDim());
    auto *pt = alloc.allocate(memSz, 8);
    // we could use the passkey idiom to make the constructor public yet
    // un-callable so that we can use std::construct_at (which requires a public
    // constructor) or, we can just use placement new and not mark this function
    // which will never be constant evaluated anyway constexpr (main benefit of
    // constexpr is UB is not allowed, so we get more warnings).
    // return std::construct_at((Address *)pt, explicitLoop, ma, Pinv, denom,
    // omega, isStr);
    return new (pt) Address(explicitLoop, ma, Pinv, denom, omega, isStr, L,
                            inputEdges, directEdges, outputEdges);
  }
  constexpr void addDirectConnection(Address *store, size_t loadEdge) {
    assert(isLoad() && store->isStore());
    directEdges().front() = store;
    store->directEdges()[loadEdge] = this;
    // never ignored
    getDDepthMemory()[numMemInputs] = 255;
    store->getDDepthMemory()[numMemInputs + loadEdge] = 255;
  }
  constexpr void addOut(Address *child, uint8_t d) {
    // we hijack index_ and lowLink_ before they're used for SCC
    size_t inInd = index_++, outInd = child->lowLink_++;
    indirectOutNeighbor(child, inInd, d);
    child->indirectInNeighbor(this, outInd, d);
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> size_t { return depth; }
  [[nodiscard]] constexpr auto getArrayDim() const -> size_t { return dim; }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * {
    return oldMemAccess.getInstruction();
  }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return oldMemAccess.getInstruction();
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    return oldMemAccess.getArrayRef()->getAlign();
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
  [[nodiscard]] constexpr auto getLoop() -> NotNull<AffineLoopNest<false>> {
    return loop;
  }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> unsigned;
  void printDotName(llvm::raw_ostream &os) const {
    if (isLoad()) os << "... = ";
    os << *oldMemAccess.getArrayRef()->getArrayPointer() << "[";
    DensePtrMatrix<int64_t> A{indexMatrix()};
    DensePtrMatrix<int64_t> B{oldMemAccess.offsetMatrix()};
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
            os << *oldMemAccess.getLoop()->getSyms()[j - 1];
          } else os << offij;
          printPlus = true;
        }
      }
    }
    os << "]";
    if (isStore()) os << " = ...";
  }
};
