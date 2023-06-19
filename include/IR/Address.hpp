#pragma once

#include "IR/InstructionCost.hpp"
#include "IR/Node.hpp"
#include "Polyhedra/Loops.hpp"
#include "Support/OStream.hpp"
#include <Containers/UnrolledList.hpp>
#include <Math/Array.hpp>
#include <Math/Comparisons.hpp>
#include <Math/Math.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Valid.hpp>
#include <cstddef>
#include <cstdint>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Casting.h>

namespace poly {
namespace poly {
class Dependence;
} // namespace poly
class ScheduledNode;
namespace CostModeling {
class LoopTreeSchedule;
} // namespace CostModeling
namespace IR {
using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::_, math::DenseDims,
  math::PtrMatrix, math::end;

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
/// Note that to get the new poly::Loop, we call
/// `oldLoop->rotate(PhiInv)`
// clang-format on
class Addr : public Instruction {
  [[no_unique_address]] poly::Dependence *edgeIn{nullptr};
  [[no_unique_address]] union {
    ScheduledNode *node{nullptr};
    size_t maxDepth;
  } nodeOrDepth; // both are used at different times
  [[no_unique_address]] NotNull<const llvm::SCEVUnknown> basePointer;
  [[no_unique_address]] poly::Loop *loop{nullptr};
  [[no_unique_address]] llvm::Instruction *instr;
  [[no_unique_address]] int64_t *offSym{nullptr};
  [[no_unique_address]] const llvm::SCEV **syms;
  [[no_unique_address]] Value *predicate{nullptr};
  [[no_unique_address]] unsigned numDim{0}, numDynSym{0};
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  int64_t mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  [[nodiscard]] static constexpr auto intMemNeeded(size_t numLoops, size_t dim)
    -> size_t {
    // 1 for denom
    // dim for OffsetOmega
    // dim*numLoops for indexMatrix
    // numLoops for FusionOmega
    // 1 + dim + dim*numLoops + numLoops == 1 + (dim + 1)*(numLoops + 1)
    return 1 + (numLoops + 1) * (dim + 1);
  }
  // this is a reload
  explicit Addr(Addr *other)
    : Instruction(VK_Stow, other->depth), basePointer(other->basePointer),
      loop(other->loop), instr(other->instr), offSym(other->offSym),
      syms(other->syms), numDim(other->numDim), numDynSym(other->numDynSym) {
    std::memcpy(mem, other->mem,
                intMemNeeded(getNumLoops(), numDim) * sizeof(int64_t));
  }
  explicit Addr(const llvm::SCEVUnknown *arrayPtr, llvm::Instruction *user,
                int64_t *offsym, const llvm::SCEV **s,
                std::array<unsigned, 2> dimOff, unsigned numLoops)
    : Instruction(llvm::isa<llvm::StoreInst>(user) ? VK_Stow : VK_Load,
                  numLoops),
      basePointer(arrayPtr), instr(user), offSym(offsym), syms(s),
      numDim(dimOff[0]), numDynSym(dimOff[1]){};
  explicit Addr(const llvm::SCEVUnknown *arrayPtr, llvm::Instruction *user,
                unsigned numLoops)
    : Instruction(llvm::isa<llvm::StoreInst>(user) ? VK_Stow : VK_Load,
                  numLoops),
      basePointer(arrayPtr), instr(user){};
  /// Constructor for 0 dimensional memory access

  constexpr Addr(NotNull<poly::Loop> explicitLoop, NotNull<Addr> ma,
                 SquarePtrMatrix<int64_t> Pinv, int64_t denom,
                 PtrVector<int64_t> omega, bool isStr, int64_t *offsets)
    : Instruction(isStr ? VK_Stow : VK_Load, unsigned(Pinv.numCol())),
      basePointer(ma->getArrayPointer()), loop(explicitLoop),
      instr(ma->getInstruction()), offSym(ma->getOffSym()) {
    DensePtrMatrix<int64_t> M{ma->indexMatrix()};    // aD x nLma
    MutDensePtrMatrix<int64_t> mStar{indexMatrix()}; // aD x nLp
    // M is implicitly padded with zeros, nLp >= nLma
    unsigned nLma = ma->getNumLoops();
    invariant(nLma <= depth);
    invariant(size_t(nLma), size_t(M.numRow()));
    mStar << M * Pinv(_(0, nLma), _);
    getDenominator() = denom;
    getOffsetOmega() << ma->getOffsetOmega() - mStar * omega;
    if (offsets) getOffsetOmega() -= M * PtrVector<int64_t>{offsets, nLma};
  }
  [[nodiscard]] constexpr auto getIntMemory() -> int64_t * { return mem; }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return const_cast<int64_t *>(mem);
  }
  // [[nodiscard]] constexpr auto getAddrMemory() const -> Addr ** {
  //   const void *m =
  //     mem +
  //     (1 + getArrayDim() + (getArrayDim() * getNumLoops())) *
  //     sizeof(int64_t);
  //   // const void *m = addr_;
  //   void *p = const_cast<void *>(static_cast<const void *>(m));
  //   return (Addr **)p;
  // }
  // [[nodiscard]] constexpr auto getDDepthMemory() const -> uint8_t * {
  //   const void *m =
  //     mem +
  //     (1 + getArrayDim() + (getArrayDim() * getNumLoops())) * sizeof(int64_t)
  //     + (numMemInputs + numDirectEdges + numMemOutputs) * sizeof(Addr *);
  //   void *p = const_cast<void *>(static_cast<const void *>(m));
  //   return (uint8_t *)p;
  // }
  constexpr auto getOffSym() -> int64_t * { return offSym; }
  [[nodiscard]] constexpr auto indMatPtr() const -> int64_t * {
    return getIntMemory() + 1 + getArrayDim();
  }
  [[nodiscard]] auto getSymbolicOffsets() -> MutPtrVector<const llvm::SCEV *> {
    return {syms + numDim, numDynSym};
  }
  [[nodiscard]] constexpr auto offsetMatrix() -> MutDensePtrMatrix<int64_t> {
    return {offSym, DenseDims{getArrayDim(), numDynSym}};
  }

public:
  constexpr void setLoopNest(poly::Loop *L) { loop = L; }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getNode() -> ScheduledNode * {
    return nodeOrDepth.node;
  }
  [[nodiscard]] constexpr auto getNode() const -> const ScheduledNode * {
    return nodeOrDepth.node;
  }
  constexpr void setNode(ScheduledNode *n) { nodeOrDepth.node = n; }
  constexpr void forEachInput(const auto &f);

  [[nodiscard]] static auto construct(BumpAlloc<> &alloc,
                                      const llvm::SCEVUnknown *ptr,
                                      llvm::Instruction *user,
                                      unsigned numLoops) -> NotNull<Addr> {
    auto *mem = (Addr *)alloc.allocate(
      sizeof(Addr) + numLoops * sizeof(int64_t), alignof(Addr));
    auto *ma = new (mem) Addr(ptr, user, numLoops);
    return ma;
  }
  /// Constructor for regular indexing
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPtr,
            llvm::Instruction *user, PtrMatrix<int64_t> indMat,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrVector<int64_t> coffsets, int64_t *offsets, unsigned numLoops)
    -> NotNull<Addr> {
    // we don't want to hold any other pointers that may need freeing
    unsigned arrayDim = szOff[0].size(), nOff = szOff[1].size();
    size_t memNeeded = intMemNeeded(numLoops, arrayDim);
    auto *mem = (Addr *)alloc.allocate(
      sizeof(Addr) + memNeeded * sizeof(int64_t), alignof(Addr));
    const auto **syms = // over alloc by numLoops - 1, in case we remove
      alloc.allocate<const llvm::SCEV *>(arrayDim + nOff + numLoops - 1);
    auto *ma =
      new (mem) Addr(arrayPtr, user, offsets, syms,
                     std::array<unsigned, 2>{arrayDim, nOff}, numLoops);
    std::copy_n(szOff[0].begin(), arrayDim, syms);
    std::copy_n(szOff[1].begin(), nOff, syms + arrayDim);
    ma->indexMatrix() << indMat;
    ma->getOffsetOmega() << coffsets;
    return ma;
  }
  /// copies `o` and decrements the last element
  /// it decrements, as we iterate in reverse order
  constexpr void setFusionOmega(MutPtrVector<int> o) {
    invariant(o.size(), getNumLoops() + 1);
    std::copy_n(o.begin(), getNumLoops(), getFusionOmega().begin());
    getFusionOmega().back() = o.back()--;
  }
  [[nodiscard]] auto reload(BumpAlloc<> &alloc) -> NotNull<Addr> {
    size_t memNeeded = intMemNeeded(getNumLoops(), numDim);
    auto *p = (Addr *)alloc.allocate(sizeof(Addr) + memNeeded * sizeof(int64_t),
                                     alignof(Addr));
    return new (p) Addr(*this);
  }
  [[nodiscard]] auto getSizes() const -> PtrVector<const llvm::SCEV *> {
    return {syms, numDim};
  }
  [[nodiscard]] auto getSymbolicOffsets() const
    -> PtrVector<const llvm::SCEV *> {
    return {syms + numDim, numDynSym};
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() <= VK_Stow;
  }
  [[nodiscard]] constexpr auto getArrayPointer() const
    -> NotNull<const llvm::SCEVUnknown> {
    return basePointer;
  }
  [[nodiscard]] auto getType() const -> llvm::Type * {
    return basePointer->getType();
  }
  [[nodiscard]] constexpr auto dependsOnIndVars(size_t d) -> bool {
    for (size_t i = 0, D = getArrayDim(); i < D; ++i)
      if (anyNEZero(indexMatrix()(i, _(d, end)))) return true;
    return false;
  }
  [[nodiscard]] constexpr auto getLoop() const -> NotNull<poly::Loop> {
    return loop;
  }
  /*
  [[nodiscard]] constexpr auto getBlockIdx() const -> uint8_t {
    return blckIdx;
  }
  constexpr void setBlockIdx(uint8_t idx) { blckIdx = idx; }
  // constexpr void setLoopTreeSchedule(CostModeling::LoopTreeSchedule *L,
  //                                    unsigned blockIdx) {
  //   node = L;
  //   blckIdx = blockIdx;
  // }
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
  [[nodiscard]] constexpr auto onStack() const -> bool { return bitfield & 2; }
  constexpr void place() { bitfield |= 4; }
  [[nodiscard]] constexpr auto wasPlaced() const -> bool {
    return bitfield & 4;
  }
  constexpr auto index() -> uint8_t & { return index_; }
  [[nodiscard]] constexpr auto index() const -> unsigned { return index_; }
  constexpr auto lowLink() -> uint8_t & { return lowLink_; }
  [[nodiscard]] constexpr auto lowLink() const -> unsigned { return lowLink_; }
  */
  [[nodiscard]] constexpr auto getStoredVal() const -> Value * {
    invariant(isStore());
    return unionPtr.node;
  }
  // doesn't add users
  constexpr void setVal(Value *n) {
    invariant(isStore());
    invariant(Value::classof(n));
    unionPtr.node = n;
  }
  [[nodiscard]] constexpr auto getPredicate() const -> Value * {
    return predicate;
  }
  constexpr void setPredicate(Node *n) {
    invariant(Value::classof(n));
    predicate = static_cast<Value *>(n);
  }
  [[nodiscard]] constexpr auto getUsers() const
    -> containers::UList<Instruction *> * {
    invariant(isLoad());
    return unionPtr.users;
  }
  /// extend number of Cols, copying A[_(0,R),_] into dest, filling new cols
  /// with 0
  // L is the inner most loop being removed
  void updateOffsMat(BumpAlloc<> &alloc, size_t numToPeel, llvm::Loop *L,
                     llvm::ScalarEvolution *SE) {
    invariant(numToPeel > 0);
    // need to condition on loop
    // remove the numExtraLoopsToPeel from Rt
    // that is, we want to move Rt(_,_(end-numExtraLoopsToPeel,end))
    // would this code below actually be expected to boost
    // performance? if
    // (Bt.numCol()+numExtraLoopsToPeel>Bt.rowStride())
    // 	Bt.resize(Bt.numRow(),Bt.numCol(),Bt.numCol()+numExtraLoopsToPeel);
    // order of loops in Rt is outermost->innermost
    DensePtrMatrix<int64_t> oldOffsMat{offsetMatrix()}, Rt{indexMatrix()};
    size_t dynSymInd = numDynSym;
    numDynSym += numToPeel;
    MutPtrVector<const llvm::SCEV *> sym{getSymbolicOffsets()};
    offSym = alloc.allocate<int64_t>(size_t(numDynSym) * numDim);
    MutDensePtrMatrix<int64_t> offsMat{offsetMatrix()};
    if (dynSymInd) offsMat(_, _(0, dynSymInd)) << oldOffsMat;
    for (size_t i = numToPeel; i;) {
      L = L->getParentLoop();
      if (allZero(Rt(_, --i))) continue;
      // push the SCEV
      auto *iTyp = L->getInductionVariable(*SE)->getType();
      const llvm::SCEV *S = SE->getAddRecExpr(
        SE->getZero(iTyp), SE->getOne(iTyp), L, llvm::SCEV::NoWrapMask);
      if (const llvm::SCEV **j = std::ranges::find(sym, S); j != sym.end()) {
        --numDynSym;
        offsMat(_, std::distance(sym.begin(), j)) += Rt(_, i);
      } else {
        offsMat(_, dynSymInd) << Rt(_, i);
        sym[dynSymInd++] = S;
      }
    }
  }
  void peelLoops(BumpAlloc<> &alloc, size_t numToPeel, llvm::Loop *L,
                 llvm::ScalarEvolution *SE) {
    invariant(numToPeel > 0);
    size_t maxDepth = nodeOrDepth.maxDepth, numLoops = getNumLoops();
    invariant(numToPeel <= maxDepth);
    // we need to compare numToPeel with actual depth
    // because we might have peeled some loops already
    invariant(numLoops <= maxDepth);
    numToPeel -= maxDepth - numLoops;
    if (numToPeel == 0) return;
    // we're dropping the outer-most `numToPeel` loops
    // first, we update offsMat
    updateOffsMat(alloc, numToPeel, L, SE);
    // current memory layout (outer <-> inner):
    // - denom (1)
    // - offsetOmega (arrayDim)
    // - indexMatrix (arrayDim x numLoops)
    // - fusionOmegas (numLoops+1)
    int64_t *dst = indMatPtr(), *src = dst + numToPeel;
    depth = numLoops - numToPeel;
    size_t dim = getArrayDim();
    invariant(depth < numLoops);
    // we want d < dim for indexMatrix, and then == dim for fusion omega
    for (size_t d = 0; d <= dim; ++d) {
      std::copy_n(src, depth + (d == dim), dst);
      src += numLoops;
      dst += depth;
    }
  }

  // struct EndSentinel {};
  // class ActiveEdgeIterator {
  //   Addr **p;
  //   Addr **e;
  //   uint8_t *d;
  //   uint8_t filtdepth;

  // public:
  //   constexpr auto operator*() const -> Addr * { return *p; }
  //   constexpr auto operator->() const -> Addr * { return *p; }
  //   /// true means skip, false means we evaluate
  //   /// so *d <= filtdepth means we skip, evaluating only *d > filtdepth
  //   [[nodiscard]] constexpr auto hasNext() const -> bool {
  //     return ((p != e) && ((*d <= filtdepth) ||
  //     (!((*p)->inActiveSubset()))));
  //   }
  //   constexpr auto operator++() -> ActiveEdgeIterator & {
  //     // meaning of filtdepth 127?
  //     do {
  //       ++p;
  //       ++d;
  //     } while (hasNext());
  //     return *this;
  //   }
  //   constexpr auto operator==(EndSentinel) const -> bool { return p == e; }
  //   constexpr auto operator!=(EndSentinel) const -> bool { return p != e; }
  //   constexpr ActiveEdgeIterator(Addr **_p, Addr **_e, uint8_t *_d, uint8_t
  //   fd)
  //     : p(_p), e(_e), d(_d), filtdepth(fd) {
  //     while (hasNext()) {
  //       ++p;
  //       ++d;
  //     }
  //   }
  //   constexpr auto operator++(int) -> ActiveEdgeIterator {
  //     ActiveEdgeIterator tmp = *this;
  //     ++*this;
  //     return tmp;
  //   }
  //   [[nodiscard]] constexpr auto begin() const -> ActiveEdgeIterator {
  //     return *this;
  //   }
  //   [[nodiscard]] static constexpr auto end() -> EndSentinel { return {}; }
  // };
  // [[nodiscard]] constexpr auto numInNeighbors() const -> unsigned {
  //   return isStore() ? numMemInputs + numDirectEdges : numMemInputs;
  // }
  // [[nodiscard]] constexpr auto numOutNeighbors() const -> unsigned {
  //   return isStore() ? numMemOutputs : numDirectEdges + numMemOutputs;
  // }
  // [[nodiscard]] constexpr auto numNeighbors() const -> unsigned {
  //   return numMemInputs + numDirectEdges + numMemOutputs;
  // }
  // [[nodiscard]] auto inNeighbors(uint8_t filtd) -> ActiveEdgeIterator {
  //   Addr **p = getAddrMemory();
  //   return {p, p + numInNeighbors(), getDDepthMemory(), filtd};
  // }
  // [[nodiscard]] auto outNeighbors(uint8_t filtd) -> ActiveEdgeIterator {
  //   unsigned n = numInNeighbors();
  //   Addr **p = getAddrMemory() + n;
  //   return {p, p + numOutNeighbors(), getDDepthMemory() + n, filtd};
  // }
  // #ifndef NDEBUG
  //   [[gnu::used, nodiscard]] constexpr auto directEdges()
  //     -> MutPtrVector<Addr *> {
  //     Addr **p = getAddrMemory() + numMemInputs;
  //     return {p, numDirectEdges};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto directEdges() const
  //     -> PtrVector<Addr *> {
  //     Addr **p = getAddrMemory() + numMemInputs;
  //     return {p, numDirectEdges};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto inDepSat() const
  //     -> PtrVector<uint8_t> {
  //     return {getDDepthMemory(), numInNeighbors()};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto outDepSat() const
  //     -> PtrVector<uint8_t> {
  //     return {getDDepthMemory() + numInNeighbors(), numOutNeighbors()};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto inNeighbors() const
  //     -> PtrVector<Addr *> {
  //     return PtrVector<Addr *>{getAddrMemory(), numInNeighbors()};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto outNeighbors() const
  //     -> PtrVector<Addr *> {
  //     return PtrVector<Addr *>{getAddrMemory() + numInNeighbors(),
  //                              numOutNeighbors()};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto inNeighbors()
  //     -> MutPtrVector<Addr *> {
  //     return MutPtrVector<Addr *>{getAddrMemory(), numInNeighbors()};
  //   }
  //   [[gnu::used, nodiscard]] constexpr auto outNeighbors()
  //     -> MutPtrVector<Addr *> {
  //     return MutPtrVector<Addr *>{getAddrMemory() + numInNeighbors(),
  //                                 numOutNeighbors()};
  //   }
  // #else
  //   [[nodiscard]] constexpr auto directEdges() -> MutPtrVector<Address *> {
  //     Address **p = getAddrMemory() + numMemInputs;
  //     return {p, numDirectEdges};
  //   }
  //   [[nodiscard]] constexpr auto directEdges() const -> PtrVector<Address *>
  //   {
  //     Address **p = getAddrMemory() + numMemInputs;
  //     return {p, numDirectEdges};
  //   }
  //   [[nodiscard]] constexpr auto inDepSat() const -> PtrVector<uint8_t> {
  //     return {getDDepthMemory(), numInNeighbors()};
  //   }
  //   [[nodiscard]] constexpr auto outDepSat() const -> PtrVector<uint8_t> {
  //     return {getDDepthMemory() + numInNeighbors(), numOutNeighbors()};
  //   }
  //   [[nodiscard]] constexpr auto inNeighbors() const -> PtrVector<Address *>
  //   {
  //     return PtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  //   }
  //   [[nodiscard]] constexpr auto outNeighbors() const -> PtrVector<Address *>
  //   {
  //     return PtrVector<Address *>{getAddrMemory() + numInNeighbors(),
  //                                 numOutNeighbors()};
  //   }
  //   [[nodiscard]] constexpr auto inNeighbors() -> MutPtrVector<Address *> {
  //     return MutPtrVector<Address *>{getAddrMemory(), numInNeighbors()};
  //   }
  //   [[nodiscard]] constexpr auto outNeighbors() -> MutPtrVector<Address *> {
  //     return MutPtrVector<Address *>{getAddrMemory() + numInNeighbors(),
  //                                    numOutNeighbors()};
  //   }
  // #endif
  // constexpr void indirectInNeighbor(Addr *other, size_t i, uint8_t d) {
  //   getAddrMemory()[i] = other;
  //   getDDepthMemory()[i] = d;
  // }
  // constexpr void indirectOutNeighbor(Addr *other, size_t i, uint8_t d) {
  //   getAddrMemory()[numMemInputs + numDirectEdges + i] = other;
  //   getDDepthMemory()[numMemInputs + numDirectEdges + i] = d;
  // }
  [[nodiscard]] static auto allocate(BumpAlloc<> &alloc, NotNull<Addr> ma,
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
  construct(BumpAlloc<> &alloc, NotNull<poly::Loop> explicitLoop,
            NotNull<Addr> ma, bool isStr, SquarePtrMatrix<int64_t> Pinv,
            int64_t denom, PtrVector<int64_t> omega, unsigned inputEdges,
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
    return new (pt) Addr(explicitLoop, ma, Pinv, denom, omega, isStr, offsets);
  }
  // constexpr void addDirectConnection(Addr *store, size_t loadEdge) {
  //   assert(isLoad() && store->isStore());
  //   directEdges().front() = store;
  //   store->directEdges()[loadEdge] = this;
  //   // never ignored
  //   getDDepthMemory()[numMemInputs] = 255;
  //   store->getDDepthMemory()[numMemInputs + loadEdge] = 255;
  // }
  // constexpr void addOut(Addr *child, uint8_t d) {
  //   // we hijack index_ and lowLink_ before they're used for SCC
  //   size_t inInd = index_++, outInd = child->lowLink_++;
  //   indirectOutNeighbor(child, inInd, d);
  //   child->indirectInNeighbor(this, outInd, d);
  // }
  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned { return depth; }
  [[nodiscard]] constexpr auto getArrayDim() const -> unsigned {
    return numDim;
  }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * { return instr; }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return instr;
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    if (auto *l = llvm::dyn_cast<llvm::LoadInst>(instr)) return l->getAlign();
    return llvm::cast<llvm::StoreInst>(instr)->getAlign();
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
    return {indMatPtr(), DenseDims{getArrayDim(), getNumLoops()}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {indMatPtr(), DenseDims{getArrayDim(), getNumLoops()}};
  }
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    unsigned L = getNumLoops() + 1;
    // L + 1 means we add the extra array dim for `offsetOmega`
    size_t d = getArrayDim(), off = 1 + d * L;
    return {getIntMemory() + off, L};
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    unsigned L = getNumLoops() + 1;
    size_t d = getArrayDim(), off = 1 + d * L;
    return {getIntMemory() + off, L};
  }
  [[nodiscard]] constexpr auto offsetMatrix() const -> DensePtrMatrix<int64_t> {
    invariant(offSym != nullptr || numDynSym == 0);
    return {offSym, DenseDims{getArrayDim(), numDynSym}};
  }
  [[nodiscard]] constexpr auto getLoop() -> NotNull<poly::Loop> { return loop; }
  [[nodiscard]] constexpr auto sizesMatch(NotNull<const Addr> x) const -> bool {
    auto thisSizes = getSizes(), xSizes = x->getSizes();
    return std::equal(thisSizes.begin(), thisSizes.end(), xSizes.begin(),
                      xSizes.end());
  }
  auto calculateCostContiguousLoadStore(llvm::TargetTransformInfo &TTI,
                                        unsigned int vectorWidth)
    -> cost::RecipThroughputLatency {
    constexpr unsigned int addrSpace = 0;
    llvm::Type *T = cost::getType(getType(), vectorWidth);
    llvm::Align alignment = getAlign();
    if (!predicate) {
      llvm::Intrinsic::ID id =
        isLoad() ? llvm::Instruction::Load : llvm::Instruction::Store;
      return {
        TTI.getMemoryOpCost(id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_RecipThroughput),
        TTI.getMemoryOpCost(id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_Latency)};
    }
    llvm::Intrinsic::ID id =
      isLoad() ? llvm::Intrinsic::masked_load : llvm::Intrinsic::masked_store;
    return {
      TTI.getMaskedMemoryOpCost(id, T, alignment, addrSpace,
                                llvm::TargetTransformInfo::TCK_RecipThroughput),
      TTI.getMaskedMemoryOpCost(id, T, alignment, addrSpace,
                                llvm::TargetTransformInfo::TCK_Latency)};
  }

  auto getCost(llvm::TargetTransformInfo &TTI, cost::VectorWidth W)
    -> cost::RecipThroughputLatency {
    // TODO: cache?
    return calculateCostContiguousLoadStore(TTI, W.getWidth());
  }

  [[nodiscard]] constexpr auto getCurrentDepth() const -> unsigned;

  void printDotName(llvm::raw_ostream &os) const {
    if (isLoad()) os << "... = ";
    os << *getArrayPointer() << "[";
    DensePtrMatrix<int64_t> A{indexMatrix()};
    DensePtrMatrix<int64_t> B{offsetMatrix()};
    PtrVector<int64_t> b{getOffsetOmega()};
    ptrdiff_t numLoops = ptrdiff_t(A.numCol());
    for (ptrdiff_t i = 0; i < A.numRow(); ++i) {
      if (i) os << ", ";
      bool printPlus = false;
      for (ptrdiff_t j = 0; j < numLoops; ++j) {
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
      for (ptrdiff_t j = 0; j < B.numCol(); ++j) {
        if (int64_t offij = j ? B(i, j) : b[i]) {
          if (printPlus) {
            if (offij <= 0) {
              offij *= -1;
              os << " - ";
            } else os << " + ";
          }
          if (j) {
            if (offij != 1) os << offij << '*';
            os << *getLoop()->getSyms()[j - 1];
          } else os << offij;
          printPlus = true;
        }
      }
    }
    os << "]";
    if (isStore()) os << " = ...";
  }
};
inline auto operator<<(llvm::raw_ostream &os, const Addr &m)
  -> llvm::raw_ostream & {
  if (m.isLoad()) os << "Load: ";
  else os << "Store: ";
  os << *m.getInstruction();
  os << "\nArrayIndex " << *m.getArrayPointer() << " (dim = " << m.getArrayDim()
     << ", num loops: " << m.getNumLoops();
  if (m.getArrayDim()) os << ", element size: " << *m.getSizes().back();
  os << "):\n";
  PtrMatrix<int64_t> A{m.indexMatrix()};
  os << "Sizes: [";
  if (m.getArrayDim()) {
    os << " unknown";
    for (ptrdiff_t i = 0; i < ptrdiff_t(A.numRow()) - 1; ++i)
      os << ", " << *m.getSizes()[i];
  }
  os << " ]\nSubscripts: [ ";
  ptrdiff_t numLoops = ptrdiff_t(A.numCol());
  PtrMatrix<int64_t> offs = m.offsetMatrix();
  for (ptrdiff_t i = 0; i < A.numRow(); ++i) {
    if (i) os << ", ";
    bool printPlus = false;
    for (ptrdiff_t j = 0; j < numLoops; ++j) {
      if (int64_t Aji = A(i, j)) {
        if (printPlus) {
          if (Aji <= 0) {
            Aji *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (Aji != 1) os << Aji << '*';
        os << "i_" << j << " ";
        printPlus = true;
      }
    }
    for (ptrdiff_t j = 0; j < offs.numCol(); ++j) {
      if (int64_t offij = offs(i, j)) {
        if (printPlus) {
          if (offij <= 0) {
            offij *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (j) {
          if (offij != 1) os << offij << '*';
          os << *m.getLoop()->getSyms()[j - 1];
        } else os << offij;
        printPlus = true;
      }
    }
  }
  return os << "]\nInitial Fusion Omega: " << m.getFusionOmega()
            << "\npoly::Loop:" << *m.getLoop();
}
class Load {
  Addr *addr;

public:
  Load(Addr *a) : addr(a->getKind() == Node::VK_Load ? a : nullptr) {}
  Load(Node *a)
    : addr(a->getKind() == Node::VK_Load ? static_cast<Addr *>(a) : nullptr) {}
  constexpr explicit operator bool() { return addr != nullptr; }
  [[nodiscard]] constexpr auto getInstruction() const -> llvm::Instruction * {
    // return llvm::cast<llvm::LoadInst>(addr->getInstruction());
    // load or store (could be reload)
    return addr->getInstruction();
  }
};
class Stow {
  Addr *addr;

public:
  Stow(Addr *a) : addr(a->getKind() == Node::VK_Stow ? a : nullptr) {}
  Stow(Node *a)
    : addr(a->getKind() == Node::VK_Stow ? static_cast<Addr *>(a) : nullptr) {}
  constexpr explicit operator bool() { return addr != nullptr; }
  [[nodiscard]] constexpr auto getInstruction() const -> llvm::StoreInst * {
    return llvm::cast<llvm::StoreInst>(addr->getInstruction());
  }
  [[nodiscard]] constexpr auto getStoredVal() const -> Value * {
    return addr->getStoredVal();
  }
  constexpr void setVal(Value *n) { return addr->setVal(n); }
};

} // namespace IR
} // namespace poly
