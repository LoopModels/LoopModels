#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <llvm/Transforms/Utils/ScalarEvolutionExpander.h>
#ifndef USE_MODULE
#include "IR/Array.cxx"
#include "IR/InstructionCost.cxx"
#include "IR/Node.cxx"
#include "IR/OrthogonalAxes.cxx"
#include "IR/Users.cxx"
#include "Math/Array.cxx"
#include "Math/Comparisons.cxx"
#include "Math/Constructors.cxx"
#include "Numbers/Int8.cxx"
#include "Target/Machine.cxx"
#include "Utilities/ListRanges.cxx"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#else
export module IR:Address;
import Array;
import ArrayConstructors;
import Comparisons;
import InstructionCost;
import Int8;
import Invariant;
import ListIterator;
import ListRange;
import OrthogonalAxes;
import STL;
import TargetMachine;
import Valid;
import :Array;
import :Node;
import :Users;
#endif

#ifdef USE_MODULE
export namespace lp {
#else
namespace lp {
#endif
class ScheduledNode;
} // namespace lp
namespace CostModeling {
template <std::floating_point T>
inline auto to(llvm::InstructionCost cost) -> T {
  std::optional<llvm::InstructionCost::CostType> v = cost.getValue();
  return v ? static_cast<T>(*v) : std::numeric_limits<T>::quiet_NaN();
}
template <std::integral T> inline auto to(llvm::InstructionCost cost) -> T {
  std::optional<llvm::InstructionCost::CostType> v = cost.getValue();
  // max should trigger overflow -> ubsan trigger
  return v ? static_cast<T>(*v) : std::numeric_limits<T>::max();
}
}; // namespace CostModeling
#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using math::PtrVector, math::MutPtrVector, math::DensePtrMatrix,
  math::MutDensePtrMatrix, math::SquarePtrMatrix, math::_, math::DenseDims,
  math::PtrMatrix, math::end, utils::ListRange, numbers::u8;

class Cache;
constexpr auto getAlloc(IR::Cache &cache) -> Arena<> *;
constexpr auto getDataLayout(IR::Cache &cache) -> const llvm::DataLayout &;

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
  int32_t edge_in_{-1};
  int32_t edge_out_{-1};
  lp::ScheduledNode *node_;
  Array array_;
  // Valid<Value> base_pointer_;
  poly::Loop *loop_{nullptr};
  llvm::Instruction *instr_{nullptr};
  int64_t *off_sym_{nullptr};
  Value **syms_;
  Value *predicate_{nullptr};
  Addr *orig_next_{nullptr};
  /// We find reductionns during `IROptimizer` initialization
  /// after sorting edges and removing redundant `Addr`
  /// this is because we may have multiple repeat stores to the the same
  /// location, and a reduction would be the closest pair. Thus, we want to have
  /// an ordering.
  uint16_t num_dyn_sym_{0};
  // u8 num_dim_{0};
  u8 align_shift_{};            ///< Alignment of addr, <= that of array
  numbers::Flag8 hoist_mask_{}; ///< Union of hoists in front and behind
  // 4 padding bytes empty...
  int32_t topological_position_;
  OrthogonalAxes axes_; // 4 bytes
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  int64_t mem_[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
  explicit Addr(Array array, llvm::Type *typ, bool isStow,
                int64_t *dynOffsetPtr, Value **s, ptrdiff_t n_dyn_sym,
                unsigned numLoops, int deps, unsigned maxNumLoops,
                poly::Loop *pl, u8 l2_align)
    : Instruction(isStow ? VK_Stow : VK_Load, numLoops, deps, maxNumLoops, typ),
      array_(array), loop_(pl), off_sym_(dynOffsetPtr), syms_(s),
      num_dyn_sym_(n_dyn_sym), align_shift_(l2_align) {
    // Totally insane for it to be anything close...
    // Even 10 is extreme
    invariant(n_dyn_sym <= std::numeric_limits<uint16_t>::max());
  };
  explicit Addr(Array array, llvm::Instruction *user, int64_t *dynOffsetPtr,
                Value **s, ptrdiff_t n_dyn_sym, unsigned numLoops, int deps,
                unsigned maxNumLoops, poly::Loop *pl = nullptr)
    : Addr(array, user->getAccessType(), llvm::isa<llvm::StoreInst>(user),
           dynOffsetPtr, s, n_dyn_sym, numLoops, deps, maxNumLoops, pl,
           getL2Align(user)) {};

  [[nodiscard]] constexpr auto getIntMemory() -> int64_t * { return mem_; }
  [[nodiscard]] constexpr auto getIntMemory() const -> int64_t * {
    return const_cast<int64_t *>(mem_);
  }
  // memory layout:
  // 0: denominator
  // 1: offset omega
  // 2: index matrix
  // 3: fusion omega
  constexpr auto getOffSym() -> int64_t * { return off_sym_; }
  [[nodiscard]] static auto
  allocate(Arena<> *alloc, Array array, llvm::Type *typ, ptrdiff_t arrayDim,
           ptrdiff_t numLoops, unsigned nOff, int64_t *dynOffsetPtr,
           unsigned maxNumLoops, bool isStow, int deps,
           poly::Loop *pl = nullptr) -> Valid<Addr> {
    size_t mem_needed = intMemNeeded(maxNumLoops, arrayDim);
    auto *mem = static_cast<Addr *>(
      alloc->allocate(sizeof(Addr) + mem_needed * sizeof(int64_t)));
    // over alloc by numLoops - 1, in case we remove loops
    auto **syms = alloc->allocate<IR::Value *>(nOff + numLoops - 1);
    return new (mem) Addr(array, typ, isStow, dynOffsetPtr, syms, nOff,
                          numLoops, deps, maxNumLoops, pl, getL2Align(typ));
  }

public:
  [[nodiscard]] constexpr auto indMatPtr() const -> int64_t * {
    return getIntMemory() + 1 + numDim();
  }
  [[nodiscard]] constexpr auto offsetMatrix() -> MutDensePtrMatrix<int64_t> {
    return {off_sym_,
            DenseDims<>{math::row(numDim()), math::col(num_dyn_sym_)}};
  }
  [[nodiscard]] constexpr auto getOrthAxes() const -> OrthogonalAxes {
    return axes_;
  }
  constexpr void hoistedInFront() { hoist_mask_ |= numbers::Flag8(1); }
  constexpr void hoistedBehind() { hoist_mask_ |= numbers::Flag8(2); }
  /// The hoist flag indicates whether an `Addr` was hoisted in front of and/or
  /// behind loop(s) to which it originally belonged. This is used for cache
  /// optimization, to assign an addr to the original `DepSummary`s to which it
  /// belongs. If an `Addr` in a valley doesn't have a set hoist flag, it is
  /// currently assigned to the preceding `DepSummary`.
  /// `1` indicates hoisted in front
  /// `2` indicates hoisted behind
  constexpr auto getHoistFlag() -> numbers::Flag8 { return hoist_mask_; }
  constexpr auto fromBehind() -> bool {
    // if it was hoisted in front, it is from behind
    return bool(hoist_mask_ & numbers::Flag8(1));
  }
  constexpr auto fromFront() -> bool {
    // if it was hoisted behind, it is from the front
    return bool(hoist_mask_ & numbers::Flag8(2));
  }
  constexpr void mergeHoistFlag(IR::Addr *other) {
    hoist_mask_ |= other->hoist_mask_;
  }
  constexpr auto calcOrthAxes(ptrdiff_t depth1) -> OrthogonalAxes {
    invariant((depth1 <= 24) && (depth1 >= 0));
    invariant(currentDepth1 >= depth1);
    currentDepth1 = depth1;
    bool conv_dims = false;
    /// indexMatrix() -> arrayDim() x getNumLoops()
    DensePtrMatrix<int64_t> inds{indexMatrix()};
    // whatif small constant int?
    bool lastDimContig = isConstantOneInt(getSizes().back());
    ptrdiff_t D = ptrdiff_t(inds.numRow()) - lastDimContig;
    uint_fast16_t noncontigdeps = 0;
    for (ptrdiff_t d = 0; d < D; ++d) {
      uint32_t nzc = 0;
      for (ptrdiff_t l = 0; l < inds.numCol(); ++l) {
        if (!inds[d, l]) continue;
        noncontigdeps |= (1 << l);
        if (nzc++) conv_dims = true;
      }
    }
    uint32_t contig{0};
    if (lastDimContig) {
      uint32_t nzc = 0;
      for (ptrdiff_t l = 0; l < inds.numCol(); ++l) {
        if (!inds[D, l]) continue;
        // TODO: handle non-1 strides here
        if ((((noncontigdeps >> l) & 1) == 0) && (inds[D, l] == 1))
          contig |= uint32_t(1) << l;
        if (nzc++) conv_dims = true;
      }
    }
    axes_ = {.contig_ = contig, .conv_axes_ = conv_dims, .dep_ = loopdeps};
    return axes_;
  }
  [[nodiscard]] constexpr auto isDropped() const -> bool {
    return (getNext() == nullptr) && (getPrev() == nullptr);
  }
  constexpr void setTopPosition(int32_t pos) { topological_position_ = pos; }
  [[nodiscard]] constexpr auto getTopPosition() const -> int32_t {
    return topological_position_;
  }

  /// Constructor for 0 dimensional memory access
  /// public for use with `std::construct_at`
  /// Perhaps it should use a passkey?
  explicit Addr(Array array, llvm::Instruction *user, unsigned numLoops)
    : Instruction(llvm::isa<llvm::StoreInst>(user) ? VK_Stow : VK_Load,
                  numLoops, user->getAccessType()),
      array_(array), instr_(user), align_shift_(getL2Align(user)) {};

  /// This gets called to rotate so that we can make direct comparisons down the
  /// road without needing rotations.
  constexpr void rotate(Arena<> alloc, Valid<poly::Loop> explicitLoop,
                        SquarePtrMatrix<int64_t> Pinv, int64_t denom,
                        PtrVector<int64_t> omega, int64_t *offsets) {
    loop_ = explicitLoop;
    // we are updating in place; we may now have more loops than we did before
    unsigned old_nat_depth = getNaturalDepth();
    MutDensePtrMatrix<int64_t> M{indexMatrix()}; // aD x nLma
    MutPtrVector<int64_t> offset_omega{getOffsetOmega()};
    // MutDensePtrMatrix<int64_t> mStar{indexMatrix()};
    MutDensePtrMatrix<int64_t> m_star{
      math::matrix<int64_t>(&alloc, M.numRow(), Pinv.numCol())};
    // M is implicitly padded with zeros, newNumLoops >= oldNumLoops
    invariant(maxDepth >= old_nat_depth);
    invariant(ptrdiff_t(old_nat_depth), ptrdiff_t(M.numCol()));
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
      offset_omega -=
        PtrVector<int64_t>{offsets, math::length(old_nat_depth)} * M.t();
    // update `M` into `mStar`
    // mStar << M * Pinv[_(0, oldNumLoops), _];
    // MutPtrVector<int64_t> buff{getFusionOmega()[_(0, math::last)]};
    // invariant(buff.size(), ptrdiff_t(depth));
    m_star << M * Pinv[_(0, old_nat_depth), _];
    loopdeps = calcLoopDepMask(m_star);
    // use `mStar` to update offsetOmega`
    offset_omega -= omega * m_star.t();
    indexMatrix() << m_star[_, _(0, getNaturalDepth())];
    // MutDensePtrMatrix<int64_t> indMat{indexMatrix()};
    // for (ptrdiff_t d = 1; d < numDim(); ++d)
    //   indMat[d, _] << mStar[d, _(0, newNatDepth)];
  }
  // NOTE: this requires `nodeOrDepth` to be set to innmost loop depth
  [[nodiscard]] constexpr auto indexedByInnermostLoop() -> bool {
    return currentDepth1 == getNaturalDepth();
  }
  [[nodiscard]] constexpr auto eachAddr() {
    return ListRange{this, [](Addr *a) -> Addr * { return a->getNextAddr(); }};
  }
  constexpr auto getNextAddr() -> Addr * { return orig_next_; }
  [[nodiscard]] constexpr auto getNextAddr() const -> const Addr * {
    return orig_next_;
  }
  // a -> b -> c
  constexpr auto prependOrigAddr(Addr *a) -> Addr * {
    invariant(orig_next_ == nullptr);
    orig_next_ = a;
    return this;
  }
  /// This inserts `origNext`!
  /// x -> b -> y -> z
  /// m -> a -> n -> o
  /// b->insertNextAddr(a);
  /// x-> b -> a -> y -> z
  constexpr auto insertNextAddr(Addr *a) -> Addr * {
    if (a) a->orig_next_ = orig_next_;
    orig_next_ = a;
    return this;
  }
  /// This sets `origNext`!
  /// x -> b -> y -> z
  /// m -> a -> n -> o
  /// b->setNextAddr(a);
  /// x-> b -> a -> n -> o
  constexpr auto setNextAddr(Addr *a) -> Addr * {
    orig_next_ = a;
    return this;
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
  constexpr void setEdgeIn(int32_t id) { edge_in_ = id; }
  constexpr void setEdgeOut(int32_t id) { edge_out_ = id; }

  [[nodiscard]] constexpr auto getEdgeIn() const -> int32_t { return edge_in_; }
  [[nodiscard]] constexpr auto getEdgeOut() const -> int32_t {
    return edge_out_;
  }
  constexpr void setLoopNest(poly::Loop *L) { loop_ = L; }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] constexpr auto getNode() -> lp::ScheduledNode * {
    return node_;
  }
  [[nodiscard]] constexpr auto getNode() const -> const lp::ScheduledNode * {
    return node_;
  }
  constexpr void setNode(lp::ScheduledNode *n) { node_ = n; }

  [[nodiscard]] static auto zeroDim(Arena<> *alloc, Array array,
                                    llvm::Instruction *loadOrStore,
                                    unsigned numLoops) {
    return alloc->create<Addr>(array, loadOrStore, numLoops);
  }
  /// Constructor for regular indexing
  /// indMat is dim x numLoops
  [[nodiscard]] static auto
  construct(Arena<> *alloc, Array array, llvm::Instruction *user,
            PtrMatrix<int64_t> indMat, unsigned nOff,
            PtrVector<int64_t> constOffsets, int64_t *dynOffsetPtr,
            unsigned maxNumLoops, poly::Loop *pl = nullptr) -> Valid<Addr> {
    Addr *ma = construct(alloc, array, user->getAccessType(), indMat, nOff,
                         constOffsets, dynOffsetPtr, maxNumLoops,
                         llvm::isa<llvm::StoreInst>(user), pl);
    ma->instr_ = user;
    ma->align_shift_ = getL2Align(user);
    return ma;
  }
  [[nodiscard]] static auto
  construct(Arena<> *alloc, Array array, llvm::Type *elt,
            PtrMatrix<int64_t> indMat, unsigned nOff,
            PtrVector<int64_t> constOffsets, int64_t *dynOffsetPtr,
            unsigned maxNumLoops, bool isStow, poly::Loop *pl = nullptr)
    -> Valid<Addr> {
    // we don't want to hold any other pointers that may need freeing
    auto [arrayDim, numLoops] = math::shape(indMat);
    Addr *ma =
      allocate(alloc, array, elt, arrayDim, numLoops, nOff, dynOffsetPtr,
               maxNumLoops, isStow, calcLoopDepMask(indMat), pl);
    ma->indexMatrix() << indMat[_, _(0, ma->getNaturalDepth())]; // naturalDepth
    ma->getOffsetOmega() << constOffsets;
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
    size_t mem_needed = intMemNeeded(maxDepth, numDim());
    void *p = alloc->allocate(sizeof(Addr) + mem_needed * sizeof(int64_t));
    *static_cast<ValKind *>(p) = VK_Load;
    // we don't need to copy fusion omega; only needed for initial
    // dependence analysis
    std::memcpy(static_cast<char *>(p) + sizeof(VK_Load),
                reinterpret_cast<char *>(this) + sizeof(VK_Load),
                sizeof(Addr) - sizeof(VK_Load) +
                  intMemNeededFuseFree(getNaturalDepth(), numDim()) *
                    sizeof(int64_t));
    auto *r = static_cast<Addr *>(p);
    r->edge_in_ = -1;
    r->edge_out_ = -1;
    return r;
  }
  [[nodiscard]] constexpr auto getSizes() const -> PtrVector<Value *> {
    return array_.getSizes();
  }
  [[nodiscard]] constexpr auto getSymbolicOffsets() const
    -> PtrVector<Value *> {
    return {syms_, math::length(num_dyn_sym_)};
  }
  // last dim is (perhaps?) contiguous
  // The `i`th stride is the product of `getSizes()[_(i,end)]`.
  // [[nodiscard]] constexpr auto getSizes() -> MutPtrVector<Value *> {
  //   return array_.getSizes();
  // }
  [[nodiscard]] constexpr auto getSymbolicOffsets() -> MutPtrVector<Value *> {
    return {syms_, math::length(num_dyn_sym_)};
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() <= VK_Stow;
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> Valid<Value> {
    return array_.basePointer();
  }
  [[nodiscard]] constexpr auto dependsOnIndVars(size_t d) -> bool {
    for (ptrdiff_t i = 0, D = numDim(); i < D; ++i)
      if (anyNEZero(indexMatrix()[i, _(d, end)])) return true;
    return false;
  }
  [[nodiscard]] constexpr auto getAffLoop() const -> Valid<poly::Loop> {
    return loop_;
  }
  // goes [innermost, ..., outermost]
  // which is the usual outer <-> inner order when you conside that
  // bits are indexed/read from right to left.
  static constexpr auto calcLoopDepMask(PtrMatrix<int64_t> inds) -> int {
    // TODO: optimize me
    int loopdeps{0};
    for (auto v : inds.eachCol() | std::views::reverse)
      loopdeps = (loopdeps << 1) | math::anyNEZero(v);
    return loopdeps;
  }
  /// indexMatrix, and depth indexing, goes outer <-> inner
  /// The bits of the mask go
  /// [0,...,inner,...,outer]
  /// so the bits should be read from right to left, which
  /// is the natural way to iterate over them.
  /// This also keeps masks in alignment with one another.
  [[nodiscard]] constexpr auto loopMask() -> int {
    assert(calcLoopDepMask(indexMatrix()) == loopdeps);
    return loopdeps;
    // if (loopdeps >= 0) return loopdeps;
    // return loopdeps = calcLoopDepMask(indexMatrix());
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
  constexpr void setVal(Arena<> *alloc, Value *n) {
    invariant(isStore());
    invariant(Value::classof(n));
    users.setVal(n);
    n->addUser(alloc, this);
  }
  [[nodiscard]] constexpr auto getPredicate() const -> Value * {
    return predicate_;
  }
  constexpr void setPredicate(Node *n) {
    invariant(Value::classof(n));
    predicate_ = static_cast<Value *>(n);
  }
  /// Get the users of this load.
  /// invariant: this instruction must only be called if `Addr` is a load!
  /// For a store, use `getStoredVal()` to get the stored value.
  /// Returns the children.
  /// Otherwise, like `static_cast<Value*>(this)->getUsers()`
  [[nodiscard]] constexpr auto getUsers() -> Users & {
    invariant(isLoad());
    return users;
  }
  constexpr auto getArray() const -> Array { return array_; }
  [[nodiscard]] constexpr auto numDim() const -> ptrdiff_t {
    return ptrdiff_t(array_.getDim());
  }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * { return instr_; }
  [[nodiscard]] auto getBasicBlock() -> llvm::BasicBlock * {
    return instr_ ? instr_->getParent() : nullptr;
  }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return instr_;
  }
  [[nodiscard]] static auto getAlign(llvm::Instruction *instr) -> llvm::Align {
    if (auto *l = llvm::dyn_cast<llvm::LoadInst>(instr)) return l->getAlign();
    return llvm::cast<llvm::StoreInst>(instr)->getAlign();
  }
  [[nodiscard]] static auto getL2Align(llvm::Instruction *I) -> u8 {
    return getL2Align(getAlign(I));
  }
  [[nodiscard]] static auto getL2Align(llvm::Align a) -> u8 {
    return u8(std::countr_zero(a.value()));
  }
  [[nodiscard]] static auto getL2Align(llvm::Type *T) -> u8 {
    return u8(std::countr_zero(T->getScalarSizeInBits() / 8));
  }
  [[nodiscard]] auto getAlign() const -> llvm::Align {
    return llvm::Align{uint64_t(1) << uint64_t(align_shift_)};
    // if (!instr) return llvm::Align{getType()->getScalarSizeInBits() / 8};
    // return getAlign(instr);
  }
  constexpr void setL2Alignment(u8 l2_align_) { align_shift_ = l2_align_; }
  [[nodiscard]] constexpr auto getDenominator() -> int64_t & {
    return getIntMemory()[0];
  }
  [[nodiscard]] constexpr auto getDenominator() const -> int64_t {
    return getIntMemory()[0];
  }
  // offset omega are per-array dim offsets to the indices
  [[nodiscard]] constexpr auto getOffsetOmega() -> MutPtrVector<int64_t> {
    return {getIntMemory() + 1, math::length(numDim())};
  }
  [[nodiscard]] constexpr auto getOffsetOmega() const -> PtrVector<int64_t> {
    return {getIntMemory() + 1, math::length(numDim())};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  /// First dimension is contiguous
  [[nodiscard]] constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return {indMatPtr(),
            DenseDims<>{math::row(numDim()), math::col(getNaturalDepth())}};
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  /// First dimension is contiguous
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return {indMatPtr(),
            DenseDims<>{math::row(numDim()), math::col(getNaturalDepth())}};
  }
  /// there are `getCurrentDepth() + 1` fusion omegas, representing the
  /// lexicographical position of the address within the loop nest.
  [[nodiscard]] constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    unsigned L = getCurrentDepth() + 1;
    invariant(getCurrentDepth() >= getNaturalDepth());
    size_t off = 1 + numDim() * (getNaturalDepth() + 1);
    return {getIntMemory() + off, math::length(L)};
  }
  /// there are `getCurrentDepth() + 1` fusion omegas, representing the
  /// lexicographical position of the address within the loop nest.
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    unsigned L = getCurrentDepth() + 1;
    invariant(getCurrentDepth() >= getNaturalDepth());
    size_t off = 1 + numDim() * (getNaturalDepth() + 1);
    return {getIntMemory() + off, math::length(L)};
  }
  [[nodiscard]] constexpr auto offsetMatrix() const -> DensePtrMatrix<int64_t> {
    invariant(off_sym_ != nullptr || num_dyn_sym_ == 0);
    return {off_sym_,
            DenseDims<>{math::row(numDim()), math::col(num_dyn_sym_)}};
  }
  [[nodiscard]] constexpr auto getAffineLoop() -> Valid<poly::Loop> {
    return loop_;
  }
  [[nodiscard]] constexpr auto sizesMatch(Valid<const Addr> x) const -> bool {
    auto this_sizes = getSizes(), x_sizes = x->getSizes();
    return std::equal(this_sizes.begin(), this_sizes.end(), x_sizes.begin(),
                      x_sizes.end());
  }
  template <size_t N, bool TTI>
  auto calculateCostContiguousLoadStore(target::Machine<TTI> target,
                                        unsigned vectorWidth,
                                        std::array<CostKind, N> costKinds) const
    -> std::array<llvm::InstructionCost, N> {
    static constexpr unsigned int addr_space = 0;
    llvm::Type *T = cost::getType(getType(), vectorWidth);
    llvm::Align align = getAlign();
    std::array<llvm::InstructionCost, N> ret;
    if (!predicate_) {
      llvm::Intrinsic::ID id =
        isLoad() ? llvm::Instruction::Load : llvm::Instruction::Store;
      for (size_t n; n < N; ++n)
        ret[n] = target.getMemoryOpCost(id, T, align, addr_space, costKinds[n]);
    } else {
      llvm::Intrinsic::ID id =
        isLoad() ? llvm::Intrinsic::masked_load : llvm::Intrinsic::masked_store;
      for (size_t n; n < N; ++n)
        ret[n] =
          target.getMaskedMemoryOpCost(id, T, align, addr_space, costKinds[n]);
    }
    return ret;
  }

  /// RecipThroughput, but unnormalized by width.
  /// E.g., golden cove can do 3 scalar loads/cycle,
  /// but scalar throughput is still `1`.
  /// This scalar cost will be normalized in actual
  /// cost computation, within
  /// `CostModeling::Cost::Cost::reduce`.
  /// `scalar` should thus always be 1-per.
  /// `contiguous` can differ, because
  /// FIXME: have more fine-grained costs,
  /// e.g. vector vs scalar throughput,
  /// as well as potentially separate
  /// addition and multiplication/fma units.
  /// An intention of this would be to remove the need for contiguous vs
  /// discontiguous here; we'd instead have `count_` and `discontiguous_`.
  /// How to handle discontiguous?
  /// Rely on LLVM for gather/scatter costs?
  ///
  /// `bitmax_` is used for interleave
  struct Costs {
    double scalar_{0}, contig_{0}, noncon_{0};
    // , bitmax_ : 3 {0}, bitcnt_ : 29 {0};
    constexpr auto operator+=(Costs c) -> Costs & {
      scalar_ += c.scalar_;
      contig_ += c.contig_;
      noncon_ += c.noncon_;
      // bitcnt_ += c.bitcnt_;
      // bitmax_ = bitmax_ > c.bitmax_ ? bitmax_ : c.bitmax_;
      return *this;
    }
    // constexpr auto operator*(int32_t tr) const -> Costs {
    //   return {scalar_ * tr, contig_ * tr, noncon_ * tr, bitcnt_ * tr};
    // }
    // constexpr auto operator*=(int32_t tr) -> Costs & {
    //   scalar_ *= tr;
    //   contig_ *= tr;
    //   noncon_ *= tr;
    //   bitcnt_ *= tr;
    //   return *this;
    // }
  };
  template <bool TTI>
  auto calcCostContigDiscontig(target::Machine<TTI> target, int vector_width,
                               int cacheline_bits) -> Costs {
    static constexpr unsigned int addr_space = 0;
    static constexpr CostKind RT =
      llvm::TargetTransformInfo::TCK_RecipThroughput;
    llvm::Type *T = getType();
    llvm::FixedVectorType *VT = llvm::FixedVectorType::get(T, vector_width);
    llvm::Align align = getAlign();

    llvm::Intrinsic::ID id =
      isLoad() ? llvm::Instruction::Load : llvm::Instruction::Store;

    // TODO: PR LLVM to add API that doesn't require `llvm::Value* Ptr`
    llvm::InstructionCost gsc = target.getGatherScatterOpCost(
                            id, VT, predicate_ != nullptr, align, RT),
                          contig =
                            predicate_
                              ? target.getMaskedMemoryOpCost(
                                  isLoad() ? llvm::Intrinsic::masked_load
                                           : llvm::Intrinsic::masked_store,
                                  VT, align, addr_space, RT)
                              : target.getMemoryOpCost(id, VT, align,
                                                       addr_space, RT),
                          scalar = target.getMemoryOpCost(id, T, align,
                                                          addr_space, RT);
    // Heuristically, we add a penalty to `contig`, corresponding to
    // vector_width * element_types / cacheline_bits This corresponds to
    // alignment penalty (if we can't pack to align it), or increased need to
    // prefetch.
    double contig_penalty =
      double(vector_width) * T->getScalarSizeInBits() / cacheline_bits;
    return {.scalar_ = CostModeling::to<double>(scalar),
            .contig_ = CostModeling::to<double>(contig) + contig_penalty,
            .noncon_ = CostModeling::to<double>(gsc)};
  }
  constexpr void incrementNumDynSym(ptrdiff_t numToPeel) {
    num_dyn_sym_ += numToPeel;
  }
  constexpr void setOffSym(int64_t *off_sym) { off_sym_ = off_sym; }
  // [[nodiscard]] constexpr auto getReducingInstruction() const -> Compute *;

}; // class Addr

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
  [[nodiscard]] constexpr auto getNext() const -> Node * {
    return addr->getNext();
  }
  [[nodiscard]] constexpr auto getPrev() const -> Node * {
    return addr->getPrev();
  }
  constexpr void setChild(Node *n) { addr->setChild(n); }
  constexpr void setParent(Node *n) { addr->setParent(n); }
  constexpr void insertChild(Node *n) { addr->insertChild(n); }
  constexpr void insertParent(Node *n) { addr->insertParent(n); }
  constexpr void insertAfter(Node *n) { addr->insertAfter(n); }
  constexpr void insertAhead(Node *n) { addr->insertAhead(n); }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> int {
    return addr->getCurrentDepth();
  }
  [[nodiscard]] constexpr auto getNaturalDepth() const -> int {
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
  constexpr void setVal(Arena<> *alloc, Value *n) {
    return addr->setVal(alloc, n);
  }
};

} // namespace IR
