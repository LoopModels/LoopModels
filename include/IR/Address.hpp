#pragma once

#include "IR/InstructionCost.hpp"
#include "IR/Node.hpp"
#include "IR/OrthogonalAxes.hpp"
#include "IR/Users.hpp"
#include "Polyhedra/Loops.hpp"
#include "Support/OStream.hpp"
#include "Utilities/ListRanges.hpp"
#include <Alloc/Arena.hpp>
#include <Containers/UnrolledList.hpp>
#include <Math/Array.hpp>
#include <Math/Comparisons.hpp>
#include <Math/Math.hpp>
#include <Utilities/Valid.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Casting.h>

namespace poly {
namespace lp {
class ScheduledNode;
} // namespace lp
namespace poly {
struct Dependence;
class Dependencies;
} // namespace poly
namespace IR {
using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::_, math::DenseDims,
  math::PtrMatrix, math::end, poly::Dependence, poly::Dependencies;

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
  int32_t edgeIn{-1};
  int32_t edgeOut{-1};
  lp::ScheduledNode *node;
  Valid<const llvm::SCEVUnknown> basePointer;
  poly::Loop *loop{nullptr};
  llvm::Instruction *instr;
  int64_t *offSym{nullptr};
  const llvm::SCEV **syms;
  Value *predicate{nullptr};
  Addr *origNext{nullptr};
  /// We find reductionns during `IROptimizer` initialization
  /// after sorting edges and removing redundant `Addr`
  /// this is because we may have multiple repeat stores to the the same
  /// location, and a reduction would be the closest pair. Thus, we want to have
  /// an ordering.
  Addr *reassociableReduction{nullptr}; // if reduction, corresponding addr
  uint16_t numDim{0}, numDynSym{0};
  int32_t topologicalPosition;
  OrthogonalAxes axes;
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
  explicit Addr(const llvm::SCEVUnknown *arrayPtr, llvm::Instruction *user,
                int64_t *offsym, const llvm::SCEV **s,
                std::array<unsigned, 2> dimOff, unsigned numLoops,
                unsigned natDepth, unsigned maxNumLoops)
    : Instruction(llvm::isa<llvm::StoreInst>(user) ? VK_Stow : VK_Load,
                  numLoops, natDepth, maxNumLoops),
      basePointer(arrayPtr), instr(user), offSym(offsym), syms(s),
      numDim(dimOff[0]), numDynSym(dimOff[1]){};

  [[nodiscard]] constexpr auto getIntMemory() -> int64_t * { return mem; }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return const_cast<int64_t *>(mem);
  }
  // memory layout:
  // 0: denominator
  // 1: offset omega
  // 2: index matrix
  // 3: fusion omega
  constexpr auto getOffSym() -> int64_t * { return offSym; }
  [[nodiscard]] constexpr auto indMatPtr() const -> int64_t * {
    return getIntMemory() + 1 + getArrayDim();
  }
  [[nodiscard]] constexpr auto getSymbolicOffsets()
    -> MutPtrVector<const llvm::SCEV *> {
    return {syms + numDim, numDynSym};
  }
  [[nodiscard]] constexpr auto offsetMatrix() -> MutDensePtrMatrix<int64_t> {
    return {offSym, DenseDims<>{{getArrayDim()}, {numDynSym}}};
  }
  /// recursive reassociability search

public:
  [[nodiscard]] constexpr auto getOrthAxes() const -> OrthogonalAxes {
    return axes;
  }
  constexpr auto calcOrthAxes(ptrdiff_t depth) -> OrthogonalAxes {
    invariant((depth <= 24) && (depth >= 0));
    invariant(depth >= naturalDepth);
    invariant(currentDepth >= depth);
    currentDepth = depth;
    bool indepAxes = true;
    uint32_t contig{0}, indep{(uint32_t(1) << depth) - 1};
    /// indexMatrix() -> arrayDim() x getNumLoops()
    DensePtrMatrix<int64_t> inds{indexMatrix()};
    for (ptrdiff_t l = 0; l < inds.numCol(); ++l) {
      if (!inds[0, l]) continue;
      contig |= uint32_t(1) << l;
      indep &= ~(uint32_t(1) << l);
    }
    for (ptrdiff_t d = 1; d < inds.numRow(); ++d) {
      for (ptrdiff_t l = 0; l < inds.numCol(); ++l) {
        if (!inds[d, l]) continue;
        if (!(indep & (uint32_t(1) << l))) indepAxes = false;
        indep &= ~(uint32_t(1) << l);
      }
    }
    axes = {indepAxes, contig, indep};
    return axes;
  }
  [[nodiscard]] constexpr auto isDropped() const -> bool {
    return (getNext() == nullptr) && (getPrev() == nullptr);
  }
  constexpr void setTopPosition(int32_t pos) { topologicalPosition = pos; }
  [[nodiscard]] constexpr auto getTopPosition() const -> int32_t {
    return topologicalPosition;
  }

  /// Constructor for 0 dimensional memory access
  /// public for use with `std::construct_at`
  /// Perhaps it should use a passkey?
  explicit Addr(const llvm::SCEVUnknown *arrayPtr, llvm::Instruction *user,
                unsigned numLoops)
    : Instruction(llvm::isa<llvm::StoreInst>(user) ? VK_Stow : VK_Load,
                  numLoops),
      basePointer(arrayPtr), instr(user){};

  constexpr void rotate(Valid<poly::Loop> explicitLoop,
                        SquarePtrMatrix<int64_t> Pinv, int64_t denom,
                        PtrVector<int64_t> omega, int64_t *offsets) {
    loop = explicitLoop;
    // we are updating in place; we may now have more loops than we did before
    unsigned oldNatDepth = getNaturalDepth();
    DensePtrMatrix<int64_t> M{indexMatrix()}; // aD x nLma
    MutPtrVector<int64_t> offsetOmega{getOffsetOmega()};
    unsigned depth = this->naturalDepth = uint8_t(ptrdiff_t(Pinv.numCol()));
    MutDensePtrMatrix<int64_t> mStar{indexMatrix()};
    // M is implicitly padded with zeros, newNumLoops >= oldNumLoops
    invariant(maxDepth >= naturalDepth);
    invariant(oldNatDepth <= naturalDepth);
    invariant(ptrdiff_t(oldNatDepth), ptrdiff_t(M.numRow()));
    getDenominator() = denom;
    // layout goes offsetOmega, indexMatrix, fusionOmega
    // When we call `rotate`, we don't need fusionOmega anymore, because
    // placement represented via the `ScheduledNode` and then IR graph
    // Thus, we only need to update indexMatrix and offsetOmega
    // offsetOmegas exactly alias, so we have no worries there.
    // For `indexMatrix`, we use the unused `fusionOmega` space
    // as a temporary, to avoid the aliasing problem.
    //
    // Use `M` before updating it, to update `offsetOmega`
    if (offsets)
      offsetOmega -= PtrVector<int64_t>{offsets, oldNatDepth} * M.t();
    // update `M` into `mStar`
    // mStar << M * Pinv(_(0, oldNumLoops), _);
    MutPtrVector<int64_t> buff{getFusionOmega()[_(0, math::last)]};
    invariant(buff.size(), ptrdiff_t(depth));
    unsigned newNatDepth = 0;
    for (ptrdiff_t d = getArrayDim(); d--;) {
      buff << 0;
      for (ptrdiff_t k = 0; k < oldNatDepth; ++k) buff += M[d, k] * Pinv[k, _];
      mStar[d, _] << buff;
      if (newNatDepth == depth) continue;
      // find last
      auto range = std::ranges::reverse_view{buff[_(newNatDepth, depth)]};
      auto m = std::ranges::find_if(range, [](int64_t i) { return i != 0; });
      if (m == range.end()) continue;
      newNatDepth = depth - std::distance(range.begin(), m);
    }
    // use `mStar` to update offsetOmega`
    offsetOmega -= omega * mStar.t();
    if (newNatDepth == depth) return;
    invariant(newNatDepth < depth);
    this->naturalDepth = newNatDepth;
    MutDensePtrMatrix<int64_t> indMat{this->indexMatrix()};
    for (ptrdiff_t d = 1; d < getArrayDim(); ++d)
      indMat[d, _] << mStar[d, _(0, newNatDepth)];
    this->naturalDepth = newNatDepth;
  }
  // NOTE: this requires `nodeOrDepth` to be set to innmost loop depth
  [[nodiscard]] constexpr auto indexedByInnermostLoop() -> bool {
    bool ret = currentDepth == naturalDepth;
    if (ret) setDependsOnParentLoop();
    return ret;
  }
  [[nodiscard]] constexpr auto eachAddr() {
    return utils::ListRange{this, [](Addr *a) { return a->getNextAddr(); }};
  }
  constexpr auto getNextAddr() -> Addr * { return origNext; }
  [[nodiscard]] constexpr auto getNextAddr() const -> const Addr * {
    return origNext;
  }
  constexpr auto insertNextAddr(Addr *a) -> Addr * {
    if (a) a->origNext = origNext;
    origNext = a;
    return this;
  }
  constexpr auto setNextAddr(Addr *a) -> Addr * {
    origNext = a;
    return this;
  }
  // Called from IROptimizer
  // In a reduction, `in` must be a load and `out` a store
  // This should only be called once, between nearest load/store pair
  // as it doesn't store detecting invalidity.
  // It checks for invalidity, in which case it doesn't set the reassociable
  // reduction.
  constexpr inline void maybeReassociableReduction(Dependencies);
  constexpr auto reassociableReductionPair() -> Addr * {
    return reassociableReduction;
  }

  [[nodiscard]] static constexpr auto intMemNeeded(size_t numLoops, size_t dim)
    -> size_t {
    // d = dim, l = numLoops
    // Memory layout: offset, size
    // 0,1 for denom
    // 1,d for offsetOmega
    // 1 + d, d*l for indexMatrix
    // 1 + d + d*l, l+1 for fusionOmega
    // 1 + d + d*l + l + 1 == 1 + (d + 1)*(l + 1)
    return 1 + (numLoops + 1) * (dim + 1);
  }
  [[nodiscard]] static constexpr auto intMemNeededFuseFree(size_t numLoops,
                                                           size_t dim)
    -> size_t {
    // d = dim, l = numLoops
    // Memory layout: offset, size
    // 0,1 for denom
    // 1,d for offsetOmega
    // 1 + d, d*l for indexMatrix
    // 1 + d + d*l == 1 + d*(1+l)
    return 1 + (numLoops + 1) * dim;
  }
  Addr(const Addr &) = delete;
  constexpr void setEdgeIn(int32_t id) { edgeIn = id; }
  constexpr void setEdgeOut(int32_t id) { edgeOut = id; }

  [[nodiscard]] constexpr auto getEdgeIn() const -> int32_t { return edgeIn; }
  [[nodiscard]] constexpr auto getEdgeOut() const -> int32_t { return edgeOut; }
  constexpr void setLoopNest(poly::Loop *L) { loop = L; }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getNode() -> lp::ScheduledNode * { return node; }
  [[nodiscard]] constexpr auto getNode() const -> const lp::ScheduledNode * {
    return node;
  }
  constexpr void setNode(lp::ScheduledNode *n) { node = n; }
  [[nodiscard]] inline auto inputAddrs(Dependencies) const;
  [[nodiscard]] inline auto outputAddrs(Dependencies) const;
  [[nodiscard]] inline auto inputAddrs(Dependencies, unsigned depth) const;
  [[nodiscard]] inline auto outputAddrs(Dependencies, unsigned depth) const;
  [[nodiscard]] inline auto inputEdges(Dependencies) const;
  [[nodiscard]] inline auto outputEdges(Dependencies) const;
  [[nodiscard]] inline auto inputEdges(Dependencies, unsigned depth) const;
  [[nodiscard]] inline auto outputEdges(Dependencies, unsigned depth) const;
  [[nodiscard]] inline auto inputEdgeIDs(Dependencies) const;
  [[nodiscard]] inline auto outputEdgeIDs(Dependencies) const;
  [[nodiscard]] inline auto inputEdgeIDs(Dependencies, unsigned depth) const;
  [[nodiscard]] inline auto outputEdgeIDs(Dependencies, unsigned depth) const;

  [[nodiscard]] static auto zeroDim(Arena<> *alloc,
                                    llvm::SCEVUnknown const *arrayPtr,
                                    llvm::Instruction *loadOrStore,
                                    unsigned numLoops) {
    return alloc->create<Addr>(arrayPtr, loadOrStore, numLoops);
  }
  /// Constructor for regular indexing
  [[nodiscard]] static auto
  construct(Arena<> *alloc, const llvm::SCEVUnknown *arrayPtr,
            llvm::Instruction *user, PtrMatrix<int64_t> indMat,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrVector<int64_t> coffsets, int64_t *offsets, unsigned numLoops,
            unsigned maxNumLoops) -> Valid<Addr> {
    // we don't want to hold any other pointers that may need freeing
    unsigned arrayDim = szOff[0].size(), nOff = szOff[1].size();
    size_t memNeeded = intMemNeeded(maxNumLoops, arrayDim);
    auto *mem = static_cast<Addr *>(
      alloc->allocate(sizeof(Addr) + memNeeded * sizeof(int64_t)));
    const auto **syms = // over alloc by numLoops - 1, in case we remove
      alloc->allocate<const llvm::SCEV *>(arrayDim + nOff + numLoops - 1);
    unsigned natDepth = numLoops;
    for (; natDepth; --natDepth)
      if (math::anyNEZero(indMat[_, natDepth - 1])) break;
    auto *ma = new (mem) Addr(arrayPtr, user, offsets, syms,
                              std::array<unsigned, 2>{arrayDim, nOff}, numLoops,
                              natDepth, maxNumLoops);
    std::copy_n(szOff[0].begin(), arrayDim, syms);
    std::copy_n(szOff[1].begin(), nOff, syms + arrayDim);
    ma->indexMatrix() << indMat[_, _(0, natDepth)]; // naturalDepth
    ma->getOffsetOmega() << coffsets;
    return ma;
  }
  /// copies `o` and decrements the last element
  /// it decrements, as we iterate in reverse order
  constexpr void setFusionOmega(MutPtrVector<int> o) {
    invariant(o.size(), ptrdiff_t(getCurrentDepth()) + 1);
    std::copy_n(o.begin(), getCurrentDepth(), getFusionOmega().begin());
    getFusionOmega().back() = o.back()--;
  }
  [[nodiscard]] auto reload(Arena<> *alloc) -> Valid<Addr> {
    size_t memNeeded = intMemNeeded(maxDepth, numDim);
    void *p = alloc->allocate(sizeof(Addr) + memNeeded * sizeof(int64_t));
    *static_cast<ValKind *>(p) = VK_Load;
    // we don't need to copy fusion omega; only needed for initial
    // dependence analysis
    std::memcpy(static_cast<char *>(p) + sizeof(VK_Load),
                static_cast<char *>(static_cast<void *>(this)) +
                  sizeof(VK_Load),
                sizeof(Addr) - sizeof(VK_Load) +
                  intMemNeededFuseFree(naturalDepth, numDim) * sizeof(int64_t));
    auto *r = static_cast<Addr *>(p);
    r->edgeIn = -1;
    r->edgeOut = -1;
    return r;
  }
  [[nodiscard]] constexpr auto getSizes() const
    -> PtrVector<const llvm::SCEV *> {
    return {syms, numDim};
  }
  [[nodiscard]] constexpr auto getSymbolicOffsets() const
    -> PtrVector<const llvm::SCEV *> {
    return {syms + numDim, numDynSym};
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() <= VK_Stow;
  }
  [[nodiscard]] constexpr auto getArrayPointer() const
    -> Valid<const llvm::SCEVUnknown> {
    return basePointer;
  }
  [[nodiscard]] auto getType() const -> llvm::Type * {
    return basePointer->getType();
  }
  [[nodiscard]] constexpr auto dependsOnIndVars(size_t d) -> bool {
    for (size_t i = 0, D = getArrayDim(); i < D; ++i)
      if (anyNEZero(indexMatrix()[i, _(d, end)])) return true;
    return false;
  }
  [[nodiscard]] constexpr auto getAffLoop() const -> Valid<poly::Loop> {
    return loop;
  }
  /// Get the value stored by this instruction.
  /// invariant: this instruction must only be called if `Addr` is a store!
  /// For a load, use `getUsers()` to get a range of the users.
  /// Returns the parent (other than predicates).
  [[nodiscard]] constexpr auto getStoredVal() const -> Value * {
    invariant(isStore());
    return users.getVal();
  }
  [[nodiscard]] constexpr auto getStoredValPtr() -> Value ** {
    invariant(isStore());
    return users.getValPtr();
  }
  // doesn't add users
  constexpr void setVal(Value *n) {
    invariant(isStore());
    invariant(Value::classof(n));
    users.setVal(n);
  }
  [[nodiscard]] constexpr auto getPredicate() const -> Value * {
    return predicate;
  }
  constexpr void setPredicate(Node *n) {
    invariant(Value::classof(n));
    predicate = static_cast<Value *>(n);
  }
  /// Get the users of this load.
  /// invariant: this instruction must only be called if `Addr` is a load!
  /// For a store, use `getStoredVal()` to get the stored value.
  /// Returns the children.
  [[nodiscard]] constexpr auto getUsers() -> Users & {
    invariant(isLoad());
    return users;
  }
  /// extend number of Cols, copying A[_(0,R),_] into dest, filling new cols
  /// with 0
  // L is the inner most loop being removed
  void updateOffsMat(Arena<> *alloc, size_t numToPeel,
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
    offSym = alloc->allocate<int64_t>(size_t(numDynSym) * numDim);
    MutDensePtrMatrix<int64_t> offsMat{offsetMatrix()};
    if (dynSymInd) offsMat[_, _(0, dynSymInd)] << oldOffsMat;
    llvm::Loop *L = loop->getLLVMLoop();
    for (unsigned d = loop->getNumLoops() - numToPeel; d--;)
      L = L->getParentLoop();
    for (size_t i = numToPeel; i;) {
      L = L->getParentLoop();
      if (allZero(Rt[_, --i])) continue;
      // push the SCEV
      auto *iTyp = L->getInductionVariable(*SE)->getType();
      const llvm::SCEV *S = SE->getAddRecExpr(
        SE->getZero(iTyp), SE->getOne(iTyp), L, llvm::SCEV::NoWrapMask);
      if (const llvm::SCEV **j = std::ranges::find(sym, S); j != sym.end()) {
        --numDynSym;
        offsMat[_, std::distance(sym.begin(), j)] += Rt[_, i];
      } else {
        offsMat[_, dynSymInd] << Rt[_, i];
        sym[dynSymInd++] = S;
      }
    }
  }
  // we peel off the outer `numToPeel` loops
  // however, we may have already peeled off some number of loops
  // we check how many have already been peeled via...
  void peelLoops(Arena<> *alloc, ptrdiff_t numToPeel,
                 llvm::ScalarEvolution *SE) {
    invariant(numToPeel > 0);
    loop->removeOuterMost(numToPeel, SE);
    ptrdiff_t numLoops = getCurrentDepth();
    invariant(numToPeel <= maxDepth);
    // we need to compare numToPeel with actual depth
    // because we might have peeled some loops already
    invariant(numLoops <= maxDepth);
    numToPeel -= maxDepth - numLoops;
    if (numToPeel == 0) return;
    // we're dropping the outer-most `numToPeel` loops
    // first, we update offsMat
    updateOffsMat(alloc, numToPeel, SE);
    // current memory layout (outer <-> inner):
    // - denom (1)
    // - offsetOmega (arrayDim)
    // - indexMatrix (arrayDim x numLoops)
    // - fusionOmegas (numLoops+1)
    int64_t *dst = indMatPtr(), *src = dst + numToPeel;
    ptrdiff_t dim = getArrayDim(), oldNatDepth = this->naturalDepth;
    this->currentDepth = numLoops - numToPeel;
    this->naturalDepth = oldNatDepth - numToPeel;
    invariant(currentDepth < numLoops);
    // we want d < dim for indexMatrix, and then == dim for fusion omega
    for (ptrdiff_t d = dim;;) {
      std::copy_n(src, (d) ? naturalDepth : (currentDepth + 1), dst);
      if (!d--) break;
      src += oldNatDepth;
      dst += naturalDepth;
    }
  }
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
    return {getIntMemory() + 1, getArrayDim()};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {getIntMemory() + 1, getArrayDim()};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  /// First dimension is contiguous
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return {indMatPtr(), DenseDims<>{{getArrayDim()}, {getNaturalDepth()}}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  /// First dimension is contiguous
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {indMatPtr(), DenseDims<>{{getArrayDim()}, {getNaturalDepth()}}};
  }
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    unsigned L = getCurrentDepth() + 1;
    // L + 1 means we add the extra array dim for `offsetOmega`
    size_t d = getArrayDim(), off = 1 + d * L;
    return {getIntMemory() + off, L};
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    unsigned L = getCurrentDepth() + 1;
    invariant(getCurrentDepth() >= getNaturalDepth());
    size_t off = 1 + getArrayDim() * (getNaturalDepth() + 1);
    return {getIntMemory() + off, L};
  }
  [[nodiscard]] constexpr auto offsetMatrix() const -> DensePtrMatrix<int64_t> {
    invariant(offSym != nullptr || numDynSym == 0);
    return {offSym, DenseDims<>{{getArrayDim()}, {numDynSym}}};
  }
  [[nodiscard]] constexpr auto getAffineLoop() -> Valid<poly::Loop> {
    return loop;
  }
  [[nodiscard]] constexpr auto sizesMatch(Valid<const Addr> x) const -> bool {
    auto thisSizes = getSizes(), xSizes = x->getSizes();
    return std::equal(thisSizes.begin(), thisSizes.end(), xSizes.begin(),
                      xSizes.end());
  }
  auto calculateCostContiguousLoadStore(const llvm::TargetTransformInfo &TTI,
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

  /// RecipThroughput
  struct Costs {
    double contiguous;
    double discontiguous;
    double scalar;
    constexpr auto operator+=(Costs c) -> Costs & {
      contiguous += c.contiguous;
      discontiguous += c.discontiguous;
      scalar += c.scalar;
      return *this;
    }
  };
  auto calcCostContigDiscontig(const llvm::TargetTransformInfo &TTI,
                               unsigned int vectorWidth) -> Costs {
    constexpr unsigned int addrSpace = 0;
    llvm::Type *T = cost::getType(getType(), vectorWidth);
    llvm::Align alignment = getAlign();

    llvm::Intrinsic::ID id =
      isLoad() ? llvm::Instruction::Load : llvm::Instruction::Store;

    llvm::InstructionCost gsc{TTI.getGatherScatterOpCost(
      id, T, basePointer->getValue(), predicate, alignment,
      llvm::TargetTransformInfo::TCK_RecipThroughput)},
      contig, scalar;

    if (!predicate) {
      contig =
        TTI.getMemoryOpCost(id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_RecipThroughput);
      scalar =
        TTI.getMemoryOpCost(id, T, alignment, addrSpace,
                            llvm::TargetTransformInfo::TCK_RecipThroughput);
    } else {
      llvm::Intrinsic::ID mid =
        isLoad() ? llvm::Intrinsic::masked_load : llvm::Intrinsic::masked_store;
      contig = TTI.getMaskedMemoryOpCost(
        mid, T, alignment, addrSpace,
        llvm::TargetTransformInfo::TCK_RecipThroughput);
      scalar = TTI.getMaskedMemoryOpCost(
        mid, T, alignment, addrSpace,
        llvm::TargetTransformInfo::TCK_RecipThroughput);
    }
    double dc{NAN}, dd{NAN}, ds{NAN};
    if (std::optional<double> o = contig.getValue()) dc = *o;
    if (std::optional<double> o = gsc.getValue()) dc = *o;
    if (std::optional<double> o = scalar.getValue()) dc = *o;
    return {dc, dd, ds};
  }

  /// drop `this` and remove it from `Dependencies`
  inline void drop(Dependencies);

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
        if (int64_t Aji = A[i, j]) {
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
        if (int64_t offij = j ? B[i, j] : b[i]) {
          if (printPlus) {
            if (offij <= 0) {
              offij *= -1;
              os << " - ";
            } else os << " + ";
          }
          if (j) {
            if (offij != 1) os << offij << '*';
            os << *getAffLoop()->getSyms()[j - 1];
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
     << ", natural depth: " << m.getNaturalDepth();
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
      if (int64_t Aji = A[i, j]) {
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
      if (int64_t offij = offs[i, j]) {
        if (printPlus) {
          if (offij <= 0) {
            offij *= -1;
            os << " - ";
          } else os << " + ";
        }
        if (j) {
          if (offij != 1) os << offij << '*';
          os << *m.getAffLoop()->getSyms()[j - 1];
        } else os << offij;
        printPlus = true;
      }
    }
  }
  return os << "]\nInitial Fusion Omega: " << m.getFusionOmega()
            << "\npoly::Loop:" << *m.getAffLoop();
}
class AddrWrapper {

protected:
  Addr *addr;
  constexpr AddrWrapper(Addr *a) : addr(a) {}

public:
  constexpr explicit operator bool() { return addr != nullptr; }
  [[nodiscard]] constexpr auto getChild() const -> Node * {
    return addr->getChild();
  }
  [[nodiscard]] constexpr auto getParent() const -> Node * {
    return addr->getParent();
  }
  constexpr void setChild(Node *n) { addr->setChild(n); }
  constexpr void setParent(Node *n) { addr->setParent(n); }
  constexpr void insertChild(Node *n) { addr->insertChild(n); }
  constexpr void insertParent(Node *n) { addr->insertParent(n); }
  constexpr void insertAfter(Node *n) { addr->insertAfter(n); }
  constexpr void insertAhead(Node *n) { addr->insertAhead(n); }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> unsigned {
    return addr->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getNaturalDepth() const -> unsigned {
    return addr->getNaturalDepth();
  }
  constexpr auto operator==(const AddrWrapper &other) const -> bool {
    return addr == other.addr;
  }
  [[nodiscard]] constexpr auto getLoop() const -> poly::Loop * {
    return addr->getAffineLoop();
  }
  constexpr operator Addr *() { return addr; }
};

class Load : public AddrWrapper {

public:
  Load(Addr *a) : AddrWrapper(a->getKind() == Node::VK_Load ? a : nullptr) {}
  Load(Node *a)
    : AddrWrapper(a->getKind() == Node::VK_Load ? static_cast<Addr *>(a)
                                                : nullptr) {}
  [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
    // could be load or store
    return llvm::cast<llvm::Instruction>(this->addr->getInstruction());
  }
};
class Stow : public AddrWrapper {

public:
  Stow(Addr *a) : AddrWrapper(a->getKind() == Node::VK_Stow ? a : nullptr) {}
  Stow(Node *a)
    : AddrWrapper(a->getKind() == Node::VK_Stow ? static_cast<Addr *>(a)
                                                : nullptr) {}
  [[nodiscard]] auto getInstruction() const -> llvm::StoreInst * {
    // must be store
    return llvm::cast<llvm::StoreInst>(this->addr->getInstruction());
  }

  [[nodiscard]] constexpr auto getStoredVal() const -> Value * {
    return this->addr->getStoredVal();
  }
  [[nodiscard]] constexpr auto getStoredValPtr() -> Value ** {
    return this->addr->getStoredValPtr();
  }
  constexpr void setVal(Value *n) { return addr->setVal(n); }
};

} // namespace IR
} // namespace poly
