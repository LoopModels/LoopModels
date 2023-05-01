#pragma once

#include "BitSets.hpp"
#include "Loops.hpp"
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
/// \boldsymbol{\Phi}_*\in\mathbb{R}^{N \times N} &=& \textbf{E}\boldsymbol{\Phi}\\ %
/// \boldsymbol{\omega}\in\mathbb{R}^N &=& \text{the offset vector}\\ %
/// \textbf{c}\in\mathbb{R}^{N} &=& \text{the constant offset vector}\\ %
/// \textbf{C}\in\mathbb{R}^{N \times V} &=& \text{runtime variable coefficient matrix}\\ %
/// \textbf{s}\in\mathbb{R}^V &=& \text{the symbolic runtime variables}\\ %
/// \f}
/// 
/// Where \f$\textbf{E}\f$ is an [exchange matrix](https://en.wikipedia.org/wiki/Exchange_matrix).
/// The rows of \f$\boldsymbol{\Phi}\f$ are sorted from the outermost loop to
/// the innermost loop, the opposite ordering used elsewhere. \f$\boldsymbol{\Phi}_*\f$
/// corrects this. 
/// We have
/// \f{eqnarray*}{
/// \textbf{j} &=& \boldsymbol{\Phi}_*\textbf{i} + \boldsymbol{\omega}\\ %
/// \textbf{i} &=& \boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right)\\ %
/// \textbf{x} &=& \textbf{M}'\textbf{i} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{M}'_* &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\\ %
/// \textbf{x} &=& \textbf{M}'_*\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'_*j - \textbf{M}'_*\boldsymbol{\omega} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{c}_* &=& \textbf{c} - \textbf{M}'_*\boldsymbol{\omega} \\ %
/// \textbf{x} &=& \textbf{M}'_*j + \textbf{c}_* + \textbf{Cs} \\ %
/// \f}
/// Therefore, to update the memory accesses from the old induction variables $i$
/// to the new variables $j$, we must simply compute the updated
/// \f$\textbf{c}_*\f$ and \f$\textbf{M}'_*\f$.
/// We can also test for the case where \f$\boldsymbol{\Phi} = \textbf{E}\f$, or equivalently that $\textbf{E}\boldsymbol{\Phi} = \boldsymbol{\Phi}_* = \textbf{I}$.
/// Note that to get the new AffineLoopNest, we call
/// `oldLoop->rotate(PhiInv)`
// clang-format on
class Address {
  using BitSet = ::MemoryAccess::BitSet;
  /// Original (untransformed) memory access
  NotNull<MemoryAccess> oldMemAccess;
  /// transformed loop
  NotNull<AffineLoopNest<false>> loop;
  CostModeling::LoopTreeSchedule *node{nullptr};
  [[no_unique_address]] char *addr_{nullptr};
  [[no_unique_address]] unsigned numMemInputs;
  [[no_unique_address]] unsigned numDirectEdges;
  [[no_unique_address]] unsigned numMemOutputs;
  [[no_unique_address]] unsigned index_;
  [[no_unique_address]] unsigned lowLink_;
  [[no_unique_address]] uint8_t dim;
  [[no_unique_address]] uint8_t depth;
  // may be `false` while `oldMemAccess->isStore()==true`
  // which indicates a reload from this address.
  [[no_unique_address]] uint8_t visited{0};
  [[no_unique_address]] bool isStoreFlag;
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  alignas(int64_t) int64_t mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  constexpr Address(NotNull<AffineLoopNest<false>> explicitLoop,
                    NotNull<MemoryAccess> ma, SquarePtrMatrix<int64_t> Pinv,
                    int64_t denom, PtrVector<int64_t> omega, bool isStr)
    : oldMemAccess(ma), loop(explicitLoop), isStoreFlag(isStr) {
    PtrMatrix<int64_t> M = oldMemAccess->indexMatrix();
    dim = size_t(M.numCol());
    depth = size_t(M.numRow());
    MutPtrMatrix<int64_t> mStar{indexMatrix()};
    mStar << (M.transpose() * Pinv).transpose();
    getDenominator() = denom;
    getOffsetOmega() << ma->offsetMatrix()(_, 0) - mStar * omega;
  }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return const_cast<int64_t *>(mem);
  }
  [[nodiscard]] constexpr auto getAddrMemory() const -> Address ** {
    const void *m = addr_;
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (Address **)p;
  }
  [[nodiscard]] constexpr auto getDDepthMemory() const -> uint8_t * {
    const void *m = addr_ + (numMemInputs + numDirectEdges + numMemOutputs) *
                              sizeof(Address *);
    void *p = const_cast<void *>(static_cast<const void *>(m));
    return (uint8_t *)p;
  }

public:
  constexpr void visit() { visited |= 1; }
  constexpr void unVisit() { visited &= ~uint8_t(1); }
  [[nodiscard]] constexpr auto wasVisited() const -> bool {
    return visited & 1;
  }
  constexpr void addToStack() { visited |= 2; }
  constexpr void removeFromStack() { visited &= ~uint8_t(2); }
  [[nodiscard]] constexpr auto onStack() const -> bool { return visited & 2; }
  constexpr auto index() -> unsigned & { return index_; }
  [[nodiscard]] constexpr auto index() const -> unsigned { return index_; }
  constexpr auto lowLink() -> unsigned & { return lowLink_; }
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
    constexpr auto operator++() -> ActiveEdgeIterator & {
      do {
        ++p;
        ++d;
      } while ((*d < filtdepth) && (p != e));
      return *this;
    }
    constexpr auto operator==(EndSentinel) const -> bool { return p == e; }
    constexpr auto operator!=(EndSentinel) const -> bool { return p != e; }
    constexpr ActiveEdgeIterator(Address **_p, Address **_e, uint8_t *_d,
                                 uint8_t fd)
      : p(_p), e(_e), d(_d), filtdepth(fd) {
      while ((*d < filtdepth) && (p != e)) {
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
    return isStoreFlag ? numMemInputs + numDirectEdges : numMemInputs;
  }
  [[nodiscard]] constexpr auto numOutNeighbors() const -> unsigned {
    return isStoreFlag ? numMemOutputs : numDirectEdges + numMemOutputs;
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

  [[nodiscard]] auto inNeighbors() const -> PtrVector<Address *> {
    return PtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  }
  [[nodiscard]] auto outNeighbors() const -> PtrVector<Address *> {
    return PtrVector<Address *>{getAddrMemory() + numInNeighbors(),
                                numOutNeighbors()};
  }
  [[nodiscard]] auto inNeighbors() -> MutPtrVector<Address *> {
    return MutPtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  }
  [[nodiscard]] auto outNeighbors() -> MutPtrVector<Address *> {
    return MutPtrVector<Address *>{getAddrMemory() + numInNeighbors(),
                                   numOutNeighbors()};
  }
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, NotNull<AffineLoopNest<false>> explicitLoop,
            NotNull<MemoryAccess> ma, bool isStr, SquarePtrMatrix<int64_t> Pinv,
            int64_t denom, PtrVector<int64_t> omega) -> NotNull<Address> {

    size_t memSz = ma->getNumLoops() * (1 + ma->getArrayDim());
    auto *pt = alloc.allocate(sizeof(Address) + memSz * sizeof(int64_t), 8);
    // we could use the passkey idiom to make the constructor public yet
    // un-callable so that we can use std::construct_at (which requires a public
    // constructor) or, we can just use placement new and not mark this function
    // which will never be constant evaluated anyway constexpr (main benefit of
    // constexpr is UB is not allowed, so we get more warnings).
    // return std::construct_at((Address *)pt, explicitLoop, ma, Pinv, denom,
    // omega, isStr);
    return new (pt) Address(explicitLoop, ma, Pinv, denom, omega, isStr);
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> size_t { return depth; }
  [[nodiscard]] constexpr auto getArrayDim() const -> size_t { return dim; }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * {
    return oldMemAccess->getInstruction();
  }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return oldMemAccess->getInstruction();
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    return oldMemAccess->getAlign();
  }
  [[nodiscard]] constexpr auto getDenominator() -> int64_t & {
    return getIntMemory()[0];
  }
  [[nodiscard]] constexpr auto getDenominator() const -> int64_t {
    return getIntMemory()[0];
  }
  // constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
  //   return {mem + 1, getNumLoops()+1};
  // }
  // [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
  //   return {mem + 1, getNumLoops()+1};
  // }
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {getIntMemory() + 1, unsigned(getNumLoops())};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {getIntMemory() + 1, unsigned(getNumLoops())};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return {getIntMemory() + 1 + getNumLoops(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {getIntMemory() + 1 + getNumLoops(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  [[nodiscard]] auto isStore() const -> bool { return isStoreFlag; }
  [[nodiscard]] auto getLoop() -> NotNull<AffineLoopNest<false>> {
    return loop;
  }
};
