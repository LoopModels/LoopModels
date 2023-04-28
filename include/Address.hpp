#pragma once
#include "Loops.hpp"
#include "Math/Math.hpp"
#include "MemoryAccess.hpp"
#include "Utilities/Valid.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/Support/Allocator.h>

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
struct Address {
private:
  /// Original (untransformed) memory access
  NotNull<MemoryAccess> oldMemAccess;
  /// transformed loop
  NotNull<AffineLoopNest<false>> loop;
  [[no_unique_address]] uint8_t dim;
  [[no_unique_address]] uint8_t depth;
  // may be `false` while `oldMemAccess->isStore()==true`
  // which indicates a reload from this address.
  [[no_unique_address]] bool isStoreFlag;
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  alignas(int64_t *) char mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  Address(NotNull<AffineLoopNest<false>> explicitLoop, NotNull<MemoryAccess> ma,
          SquarePtrMatrix<int64_t> Pinv, int64_t denom,
          PtrVector<int64_t> omega, bool isStr)
    : oldMemAccess(ma), loop(explicitLoop), isStoreFlag(isStr) {
    PtrMatrix<int64_t> M = oldMemAccess->indexMatrix();
    dim = size_t(M.numCol());
    depth = size_t(M.numRow());
    MutPtrMatrix<int64_t> mStar{indexMatrix()};
    mStar << (M.transpose() * Pinv).transpose();
    getDenominator() = denom;
    getOffsetOmega() << ma->offsetMatrix()(_, 0) - mStar * omega;
  }
  [[nodiscard]] constexpr auto getMemory() const -> int64_t * {
    void *p = const_cast<void *>(static_cast<const void *>(mem));
    return static_cast<int64_t *>(p);
  }

public:
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
  [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
    return oldMemAccess->getInstruction();
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    return oldMemAccess->getAlign();
  }
  [[nodiscard]] constexpr auto getDenominator() -> int64_t & {
    return getMemory()[0];
  }
  [[nodiscard]] constexpr auto getDenominator() const -> int64_t {
    return getMemory()[0];
  }
  // constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
  //   return {mem + 1, getNumLoops()+1};
  // }
  // [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
  //   return {mem + 1, getNumLoops()+1};
  // }
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {getMemory() + 1, unsigned(getNumLoops())};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {getMemory() + 1, unsigned(getNumLoops())};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return {getMemory() + 1 + getNumLoops(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {getMemory() + 1 + getNumLoops(),
            DenseDims{getArrayDim(), getNumLoops()}};
  }
  [[nodiscard]] auto isStore() const -> bool { return isStoreFlag; }
  [[nodiscard]] auto getLoop() -> NotNull<AffineLoopNest<false>> {
    return loop;
  }
};
