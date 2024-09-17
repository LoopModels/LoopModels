#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <array>
#include <bit>
#include <boost/container_hash/hash.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/Hashing.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <ostream>
#include <type_traits>
#include <utility>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/UnrolledList.cxx"
#include "IR/Users.cxx"
#include "Math/Array.cxx"
#include "Optimize/Legality.cxx"
#include "Support/Iterators.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/ListRanges.cxx"
#include "Utilities/Valid.cxx"
#else
export module IR:Node;
import Arena;
import Array;
import Invariant;
import Legality;
import ListIterator;
import ListRange;
import TargetMachine;
import UnrolledList;
import Valid;
import :Users;
#endif

#ifdef USE_MODULE
export namespace poly {
#else
namespace poly {
#endif
class Loop;
// class Dependencies;
} // namespace poly
#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
inline constexpr int MAX_SUPPORTED_DEPTH = 15;
using utils::Valid, utils::invariant, alloc::Arena, containers::UList;
class Loop;
/// We take an approach similar to LLVM's RTTI
/// however, we want to take advantage of FAMs while having a "hieararchy"
/// we accomplish this via a base class, and then wrapper classes that simply
/// hold the `Val*`.
///
/// `Val` has a base memory layout which can be used for iterating over the IR
///
/// The IR forms a graph with many links.
/// Linear links let us follow the flat structure that mirrors code we would
/// generate. We additionally have links that let us view it as a tree
/// structure.
///
/// For example, we may have
///
///  0. // VK_Loop // toplevel
///  1. x = load(p) // VK_Load
///  2. for i in I  // VK_Loop
///  3.   y = a[i]
///  4.   for j in J // VK_Loop
///  5.     z = b[j]
///  6.     e = foo(x, y, z)
///  7.     c[j,i] = e // VK_Exit
///  8.   q = 3y - c[i,i]
///  9.   y2 = y*y
/// 10.   w = y2 - q
/// 11.   for j in J // VK_Loop
/// 12.     z = c[j,i]
/// 13.     e = bar(z, y2)
/// 14.     f = a[i]
/// 15.     g = baz(e, f, w)
/// 16.     a[i] = g // VK_Exit
/// 17.   z = a[i]
/// 18.   e = p[]
/// 19.   f = z + e
/// 20.   p[] = f // VK_Exit
/// 21. z = p[]
/// 22. e = z*z
/// 23. p[] = z // VK_Exit
///
/// Start and end of a level are given by `nullptr`.
///
/// Same level -> means getNext()
/// Sub-level \-> means getChild()
/// We have
/// 0. -> 1. -> 2. -> 21. -> 22 -> 23
///             \-> 3 -> 4 -> 8-> 9 -> 10 -> 11 -> 17 -> 18 -> 19 -> 20
///                       \-> 5 -> 6 -> 7     \-> 12 -> 13 -> 14 -> 15 -> 16
/// For a `Loop`, `getChild()` returns the first contained instruction, and
/// For `Instruction`s, `getChild()` returns the first sub-loop.
/// `getParent()` returns the enclosing (outer) loop.
/// Thus, for example, we can iterate over all sub-loops of `L` via
///
///     Node* C = getChild();
///     C = llvm::isa<Loop>(C) ? C : C->getChild();
///     while (C){
///       // do stuff with `C`
///       C = C->getNext()
///       C = (!C || llvm::isa<Loop>(C)) ? C : C->getChild();
///     }
///
/// IR types: Loop, Block, Addr, Instr, Consts
class Node {

public:
  enum ValKind : uint8_t {
    VK_Load,
    VK_Stow, // used for ordered comparisons; all `Addr` types <= Stow
    VK_Loop,
    VK_Exit,
    VK_FArg,
    VK_CVal,
    VK_Cint, // C stands for const
    VK_Bint, // B stands for big
    VK_Cflt, // C stands for const
    VK_Bflt, // B stands for big
    VK_PhiN,
    VK_Func, // used for ordered comparisons; all `Inst` types >= Func
    VK_Call, // LLVM calls are either `VK_Func` or `VK_Call`. Intrin are call.
    VK_Oprn, // ops are like +, -, *
  };

  // we have a private pointer so different types can share
  // in manner not exacctly congruent with type hierarchy
  // in particular, `Inst` and `Load` want `User` lists
  // while `Stow`s do not.
  // `Addr` is the common load/store subtype
  // So in some sense, we want both `Load` and `Store` to inherit from `Addr`,
  // but only load to inherit 'hasUsers' and only store to inherit the operand.
  // `Inst` would also inherit 'hasUsers', but would want a different operands
  // type.
  // Addr has a FAM, so multiple inheritance isn't an option for `Load`/`Stow`,
  // and we want a common base that we can query to avoid monomorphization.
protected:
  const ValKind kind;
  /// The current position, `0` means top level, 1 inside a single loop
  uint8_t currentDepth1 : 4 {0}; // current depth
  uint8_t maxDepth : 4 {0};      // memory allocated to support up to this depth
  /// For an `Addr`, this is the "natural depth" where it would be
  /// placed in a loop without dependencies, i.e., the inner mostindex
  /// `0` means top level, `1` inside a single loop, etc
  uint8_t usedByLoop : 1 {0};
  uint8_t visitDepth0 : 7 {127};
  uint8_t visitDepth1{255};
  /// Mask indicating dependencies.
  /// The bits of the mask go
  /// [0,...,inner,...,outer]
  /// This is in constrast to indexMatrix, and depth indexing, which are
  /// [outer,...,inner]
  /// Thus, the bits of `loopdeps` should be read from right to left, which is
  /// the natural way to iterate over bits anyway. This also keeps masks in
  /// alignment with one another. This is noteworthy, because while collections
  /// such as arrays are naturally FIFO, bits are more naturally FILO.
  uint16_t loopdeps{std::numeric_limits<uint16_t>::max()};

  constexpr Node(ValKind kind_) : kind(kind_) {}
  constexpr Node(ValKind kind_, unsigned depth)
    : kind(kind_), currentDepth1(depth) {}
  constexpr Node(ValKind kind_, unsigned curDepth, uint16_t deps)
    : kind(kind_), currentDepth1(curDepth), loopdeps(deps) {}
  constexpr Node(ValKind kind_, unsigned curDepth, uint16_t deps,
                 unsigned maxDepth_)
    : kind(kind_), currentDepth1(curDepth), maxDepth(maxDepth_),
      loopdeps(deps) {}

private:
  Node *prev_{nullptr};
  Node *next_{nullptr};
  Node *parent_{nullptr};
  Node *child_{nullptr};

public:
  constexpr void setUsedByInner() { usedByLoop = true; }
  [[nodiscard]] constexpr auto checkUsedByInner() const -> bool {
    return usedByLoop;
  }
  [[nodiscard]] constexpr auto loopMask() const -> int {
    invariant(loopdeps != std::numeric_limits<uint16_t>::max());
    return loopdeps;
  }
  constexpr auto peelLoops(ptrdiff_t numToPeel) -> ptrdiff_t {
    loopdeps >>= int(numToPeel);
    return currentDepth1 -= numToPeel;
  }
  // constexpr void setDependsOnLoop(int depth) { loopdeps |= (1 << depth); }
  // returns true if `Node` depends on loops at depth `>=depth`,
  // where `depth=0` refers to the outermost loop.
  [[nodiscard]] constexpr auto checkDependsOnLoop(int depth) -> bool;
  constexpr void visit0(uint8_t d) {
    usedByLoop = false;
    visitDepth0 = d;
  }
  [[nodiscard]] constexpr auto getVisitDepth0() const -> uint8_t {
    return visitDepth0;
  }
  constexpr void clearVisited0() { visitDepth0 = 127; }
  /// bool visited(uint8_t d) { return visitDepth == d; }
  [[nodiscard]] constexpr auto visited0(uint8_t d) const -> bool {
    return visitDepth0 == d;
  }
  constexpr void visit1(uint8_t d) { visitDepth1 = d; }
  [[nodiscard]] constexpr auto getVisitDepth1() const -> uint8_t {
    return visitDepth1;
  }
  constexpr void clearVisited1() { visitDepth1 = 255; }
  /// bool visited(uint8_t d) { return visitDepth == d; }
  [[nodiscard]] constexpr auto visited1(uint8_t d) const -> bool {
    return visitDepth1 == d;
  }
  [[nodiscard]] constexpr auto sameBlock(const Node *other) const -> bool {
    return other && other->parent_ == parent_ && other->child_ == child_;
  }
  [[nodiscard]] constexpr auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> int {
    return currentDepth1;
  }
  [[nodiscard]] constexpr auto getMaxDepth() const -> int { return maxDepth; }
  [[nodiscard]] constexpr auto getNaturalDepth() const -> int {
    invariant(loopdeps != std::numeric_limits<uint16_t>::max());
    return 8 * int(sizeof(int)) - std::countl_zero(unsigned(loopdeps));
  }

  [[nodiscard]] constexpr auto getParent() const -> Node * {
    // invariant(next != this);
    return parent_;
  }
  [[nodiscard]] constexpr auto getChild() const -> Node * {
    // invariant(next != this);
    return child_;
  }
  [[nodiscard]] constexpr auto getPrev() const -> Node * {
    invariant(next_ != this);
    return prev_;
  }
  [[nodiscard]] constexpr auto getNext() const -> Node * {
    invariant(next_ != this);
    return next_;
  }
  void verify() {
    invariant(prev_ != this);
    invariant(next_ != this);
    invariant(prev_ == nullptr || (prev_ != next_));
  }
  constexpr auto setNext(Node *n) -> Node * {
    verify();
    next_ = n;
    if (n) n->prev_ = this;
    verify();
    return this;
  }
  constexpr auto setPrev(Node *n) -> Node * {
    verify();
    prev_ = n;
    if (n) n->next_ = this;
    verify();
    return this;
  }
  /// Currently, `this == node` is allowed because `AddrChain` uses it.
  /// This sets child to `n`, and the child's parent to `this`
  /// To additionally set `n->child` to `this->child`,
  /// and `child->parent=n`, use `insertChild`.
  /// The effective difference is
  /// `setChild` loses the place in a parent/child chain,
  /// as the original `this->child` is lost, and its parent isn't updated
  /// either. Thus, `setChild` is only really appropriate when pushing `*this`,
  /// or not building a chain.
  constexpr auto setChild(Node *n) -> Node * {
    child_ = n;
    if (n) n->parent_ = this;
    return this;
  }
  constexpr auto setParent(Node *n) -> Node * {
    parent_ = n;
    if (n) n->child_ = this;
    return this;
  }
  constexpr void setParentLoop(IR::Node *L) {
    invariant(L->kind, VK_Loop);
    currentDepth1 = L->getCurrentDepth() + (kind == VK_Loop);
    parent_ = L;
  }
  constexpr void setSubLoop(IR::Node *L) {
    invariant(kind != VK_Loop);
    invariant(!L || (L->kind == VK_Loop));
    invariant(!L || (L->getCurrentDepth() == currentDepth1 + 1));
    child_ = L;
  }
  constexpr void setCurrentDepth(int d) {
    invariant(d >= 0);
    invariant(d <= std::numeric_limits<decltype(currentDepth1)>::max());
    invariant(d >= getNaturalDepth());
    currentDepth1 = d;
  }
  /// insert `n` ahead of `this`
  /// `prev->this->next`
  /// bcomes
  /// `prev->n->this->next`
  constexpr void insertAhead(Node *n) {
    invariant(n != prev_);
    invariant(n != this);
    invariant(n != next_);
    n->prev_ = prev_;
    if (prev_) prev_->next_ = n;
    n->next_ = this;
    prev_ = n;
  }
  /// insert `n` after `this`:
  /// `prev->this->next`
  /// bcomes
  /// `prev->this->n->next`
  constexpr void insertAfter(Node *n) {
    verify();
    n->prev_ = this;
    n->next_ = next_;
    if (next_) next_->prev_ = n;
    next_ = n;
    verify();
  }
  constexpr void clearPrevNext() {
    prev_ = nullptr;
    next_ = nullptr;
  }
  [[nodiscard]] constexpr auto wasDropped() const -> bool {
    return (prev_ == nullptr) && (next_ == nullptr);
  }
  constexpr auto removeFromList() -> Node * {
    verify();
    if (prev_) prev_->next_ = next_;
    if (next_) next_->prev_ = prev_;
    clearPrevNext();
    return this;
  }
  constexpr void insertChild(Valid<Node> n) {
    n->parent_ = this;
    n->child_ = child_;
    if (child_) child_->parent_ = n;
    child_ = n;
  }
  constexpr void insertParent(Valid<Node> n) {
    n->child_ = this;
    n->parent_ = parent_;
    if (parent_) parent_->child_ = n;
    parent_ = n;
  }
  constexpr void forEach(const auto &f) {
    for (Node *n = this; n; n = n->getNext()) f(n);
  }
  static auto getInstKind(llvm::Instruction *v) -> ValKind {
    if (auto *c = llvm::dyn_cast<llvm::CallInst>(v))
      return c->getIntrinsicID() == llvm::Intrinsic::not_intrinsic ? VK_Func
                                                                   : VK_Call;
    return VK_Oprn;
  }
  static auto getKind(llvm::Value *v) -> ValKind {
    if (llvm::isa<llvm::LoadInst>(v)) return VK_Load;
    if (llvm::isa<llvm::StoreInst>(v)) return VK_Stow;
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(v)) return getInstKind(I);
    if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(v))
      return (C->getBitWidth() > 64) ? VK_Bint : VK_Cint;
    if (llvm::isa<llvm::ConstantFP>(v)) return VK_Bflt;
    if (llvm::isa<llvm::Argument>(v)) return VK_FArg;
    return VK_CVal;
  }
  /// Iterate through all instructions
  [[nodiscard]] constexpr auto nodes() noexcept
    -> utils::ListRange<Node, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }
  [[nodiscard]] constexpr auto nodes() const noexcept
    -> utils::ListRange<const Node, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }

  [[nodiscard]] constexpr auto getLoop() const noexcept -> Loop *;
  constexpr auto calcLoopMask() -> uint16_t;
  // Get the next loop of the same level
  [[nodiscard]] constexpr auto getSubLoop() const noexcept -> Loop *;
  constexpr void hoist(IR::Loop *P, int depth, IR::Loop *S);
};
static_assert(sizeof(Node) == 4 * sizeof(Node *) + 8);

/// Loop
/// parent: outer loop
/// child: inner (sub) loop
/// last is the last instruction in the body
class Loop : public Node {
  poly::Loop *affine_loop_{nullptr};
  Node *last_{nullptr};
  /// IDs are in topologically sorted order.
  CostModeling::Legality legality_;
  int32_t edge_id_{-1}; // edge cycle id
  // while `child` points to the first contained instruction,
  // `last` points to the last contained instruction,
  // and can be used for backwards iteration over the graph.

public:
  /// Get the IDs for the Dependencies carried by this loop
  [[nodiscard]] constexpr auto edges(math::PtrVector<int32_t> edges) const
    -> utils::VForwardRange {
    return utils::VForwardRange{edges, edge_id_};
  }
  constexpr Loop(unsigned depth1)
    : Node{VK_Loop, depth1, uint16_t(depth1 ? 1 << (depth1 - 1) : 0)} {}
  constexpr Loop(unsigned depth1, poly::Loop *AL)
    : Node{VK_Loop, depth1, uint16_t(1 << (depth1 - 1))}, affine_loop_{AL} {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Loop;
  }
  /// Get the first subloop.
  [[nodiscard]] constexpr auto getSubLoop() const -> Loop * {
    Node *C = getChild();
    C = (!C || llvm::isa<Loop>(C)) ? C : C->getChild();
    return llvm::cast_or_null<Loop>(C);
  }
  /// Return the enclosing, parent loop.
  [[nodiscard]] constexpr auto getOuterLoop() const -> Loop * {
    return llvm::cast_or_null<Loop>(getParent());
  }
  /// Returns the next loop at the same level
  [[nodiscard]] constexpr auto getNextLoop() const -> Loop * {
    Node *N = getNext();
    if (!N) return nullptr;
    if (!llvm::isa<Loop>(N)) N = N->getChild();
    return llvm::cast_or_null<Loop>(N);
    // return static_cast<Loop *>(N);
  }
  [[nodiscard]] constexpr auto subLoops() const {
    return utils::ListRange{getSubLoop(),
                            [](Loop *L) -> Loop * { return L->getNextLoop(); }};
  }
  [[nodiscard]] constexpr auto getNumLoops() const -> int {
    return getNaturalDepth();
  }
  /// getLast()
  /// Get the last node in the loop.
  /// Useful for iterating backwards.
  [[nodiscard]] constexpr auto getLast() const -> Node * { return last_; }
  constexpr void setLast(Node *n) { last_ = n; }
  [[nodiscard]] constexpr auto getAffineLoop() const -> poly::Loop * {
    return affine_loop_;
  }
  constexpr void setAffineLoop(poly::Loop *L) { affine_loop_ = L; }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr void setAffineLoop() {
    if (affine_loop_) return;
    for (Loop *SL : subLoops()) SL->setAffineLoop();
    if (currentDepth1) affine_loop_ = getSubLoop()->getAffineLoop();
  }
  /// Note `!L->contains(L)`
  [[nodiscard]] constexpr auto contains(IR::Node *N) const -> bool {
    for (Loop *L = N->getLoop(); L; L = L->getLoop())
      if (L == this) return true;
    return false;
  }
  // get the outermost subloop of `this` to which `N` belongs
  [[nodiscard]] constexpr auto getSubloop(IR::Node *N) -> Loop * {
    Loop *L = N->getLoop(), *O;
    if (L == this) return nullptr;
    for (; L; L = O) {
      O = L->getOuterLoop();
      if (O == this) {
        invariant(1 + currentDepth1 == L->currentDepth1);
        return L;
      }
    }
    return nullptr;
  }
  [[nodiscard]] constexpr auto getEdge() const -> int32_t { return edge_id_; }
  constexpr void setEdge(int32_t edge_id) { edge_id_ = edge_id; }
  constexpr void addEdge(math::MutPtrVector<int32_t> deps, int32_t d) {
    invariant(d >= 0);
    // [ -1, -1, -1, -1, -1 ] // d = 2, edgeId = -1
    // [  2, -1, -1, -1, -1 ] // d = 0, edgeId = 2
    // [  2, -1, -1, -1,  0 ] // d = 4, edgeId = 0
    // now edgeId = 4, and we can follow path 4->0->2
    deps[d] = std::exchange(edge_id_, d);
  }
  constexpr auto getLoopAtDepth(uint8_t depth1) -> Loop * {
    Loop *L = this;
    for (int curr_depth = this->currentDepth1; curr_depth > depth1;
         --curr_depth)
      L = L->getOuterLoop();
    invariant(L->getCurrentDepth() == depth1);
    return L;
  }
  // Returns flag of all loops that must have iterations peeled
  // when equal to this loop after offsetting (must check dependencies for
  // associated arrays and offsets).
  constexpr auto getLegality() -> CostModeling::Legality { return legality_; }
  constexpr void setLegality(CostModeling::Legality legality) {
    legality_ = legality;
  }
  constexpr auto calcLoopMask() -> int {
    invariant(currentDepth1 <= MAX_SUPPORTED_DEPTH);
    return loopdeps = (1 << currentDepth1);
  }
  [[nodiscard]] constexpr auto revNodes() noexcept
    -> utils::ListRange<Node, utils::GetPrev, utils::Identity> {
    return utils::ListRange{this->last_, utils::GetPrev{}};
  }
  [[nodiscard]] constexpr auto revNodes() const noexcept
    -> utils::ListRange<const Node, utils::GetPrev, utils::Identity> {
    return utils::ListRange{static_cast<const Node *>(this->last_),
                            utils::GetPrev{}};
  }
  [[nodiscard]] constexpr auto getNumBBs() const -> int;
};

[[nodiscard]] constexpr auto Node::getLoop() const noexcept -> Loop * {
  if (!parent_ || (parent_->kind != VK_Loop)) return nullptr;
  return static_cast<Loop *>(parent_);
}
[[nodiscard]] constexpr auto Node::getSubLoop() const noexcept -> Loop * {
  Node *C = getChild();
  if ((kind == VK_Loop) && C && !(llvm::isa<Loop>(C))) C = C->getChild();
  return llvm::cast_or_null<Loop>(C);
}
/// This is used for convenience in top sort, but our canonical IR
/// does not actually contain Exit nodes!
struct Exit : Node {
  Exit() : Node(VK_Exit) {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Exit;
  }
};

class Instruction;

class Value : public Node {
  llvm::Type *typ_;

protected:
  constexpr Value(ValKind kind_, llvm::Type *t) : Node(kind_), typ_(t) {}
  constexpr Value(ValKind kind_, unsigned depth, llvm::Type *t)
    : Node(kind_, depth), typ_(t) {}
  constexpr Value(ValKind kind_, unsigned curDepth, int deps, llvm::Type *t)
    : Node(kind_, curDepth, deps), typ_(t) {}
  constexpr Value(ValKind kind_, unsigned curDepth, int deps,
                  unsigned maxDepth_, llvm::Type *t)
    : Node(kind_, curDepth, deps, maxDepth_), typ_(t) {}

  Instruction *reduction_dst_{nullptr};
  Users users;

  // union {
  //   // UList<Instruction *> *users{nullptr}; // Func, Call, Oprn, Load
  //   // undefined behavior to access wrong one, but we sometimes want to
  //   // reference the user and users together without being particular
  //   // about which, so we use a nested union to do so without undef behavior
  //   union {
  //     Instruction *user;
  //     Instruction **users;
  //   } userPtr;
  //   Value *node;      // Stow
  //   llvm::Type *typ;  // Cint, Cflt, Bint, Bflt
  //   llvm::Value *val; // CVal
  // } unionPtr;

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_CVal || v->getKind() <= VK_Stow;
  }
  inline auto printName(std::ostream &) const -> std::ostream &;
  // user methods
  [[nodiscard]] constexpr auto getUsers() noexcept -> Users & { return users; }
  [[nodiscard]] constexpr auto getUsers() const noexcept -> const Users & {
    return users;
  }
  constexpr void setUsers(const Users &other) noexcept { users = other; }
  constexpr void addUser(Arena<> *alloc, Instruction *I) noexcept {
    users.push_back(alloc, I);
  }
  constexpr void removeFromUsers(Instruction *I) { users.remove(I); }

  /// isStore() is true if the address is a store, false if it is a load
  /// If the memory access is a store, this can still be a reload
  [[nodiscard]] constexpr auto isStore() const -> bool {
    return getKind() == VK_Stow;
  }
  [[nodiscard]] constexpr auto isLoad() const -> bool {
    return getKind() == VK_Load;
  }
  /// Defines a cycle of instructions corresponding to a reduction
  /// e.g.
  /// x = a[i];
  /// y = foo(x);
  /// z = bar(y);
  /// a[i] = z;
  /// The cycle would let us visit `foo(x)` and `bar(y)`.
  [[nodiscard]] constexpr auto getReductionDst() const -> Instruction * {
    return reduction_dst_;
  }
  /// this->reduction_dst_ = op;
  constexpr void linkReductionDst(Instruction *op) { reduction_dst_ = op; }

  /// these methods are overloaded for specific subtypes

  [[nodiscard]] constexpr auto getType() const -> llvm::Type * { return typ_; }
  [[nodiscard]] auto getType(unsigned width) const -> llvm::Type * {
    if (width <= 1) return typ_;
    return llvm::FixedVectorType::get(typ_, width);
  }
  [[nodiscard]] inline auto getNumScalarBits() const -> unsigned {
    return getType()->getScalarSizeInBits();
  }
  [[nodiscard]] inline auto getNumScalarBytes() const -> unsigned {
    return getNumScalarBits() / 8;
  }

private:
  friend auto operator<<(std::ostream &os, const Value &v) -> std::ostream & {
    // TODO: add real printing method
    // preferably, one that is both readable and deterministic!
    os << "%" << &v;
    return os;
  }
};

/// May be an Addr or a Compute
class Instruction : public Value {
  /// For use with control flow merging
  /// same operation on same type with disparate branches can be merged
  /// This only identifies instructions
protected:
  constexpr Instruction(ValKind kind_, llvm::Type *t) : Value(kind_, t) {}
  constexpr Instruction(ValKind kind_, unsigned depth, llvm::Type *t)
    : Value(kind_, depth, t) {}
  constexpr Instruction(ValKind kind_, unsigned curDepth, int deps,
                        llvm::Type *t)
    : Value(kind_, curDepth, deps, t) {}
  constexpr Instruction(ValKind kind_, unsigned curDepth, int deps,
                        unsigned maxDepth_, llvm::Type *t)
    : Value(kind_, curDepth, deps, maxDepth_, t) {}
  int topidx_{-1};
  int blkidx_{-1};

public:
  auto printName(std::ostream &os) const -> std::ostream & {
    if (topidx_ >= 0) os << "%" << topidx_;
    else os << this;
    return os;
  }

  using CostKind = llvm::TargetTransformInfo::TargetCostKind;
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_PhiN || v->getKind() <= VK_Stow;
  }
  /// Gives position within the loop nest; starts at `0`.
  [[nodiscard]] constexpr auto getTopIdx() const -> int { return topidx_; }
  /// Gives the idx of the sorted basic block.
  /// 0 are loop invariant instructions that are hoisted outside and in front of
  /// the root loop.
  [[nodiscard]] constexpr auto getBlkIdx() const -> int { return blkidx_; }
  // constexpr void setTopIdx(int newidx) { topidx = newidx; }
  // constexpr void setBlkIdx(int newidx) { blkidx = newidx; }
  constexpr auto setPosition(std::array<int, 2> newidx) -> std::array<int, 2> {
    topidx_ = newidx[0]++;
    blkidx_ = newidx[1];
    return newidx;
  }
  struct Identifier {
    llvm::Intrinsic::ID ID;
    Node::ValKind kind;
    llvm::Type *type;
    constexpr auto operator==(const Identifier &other) const -> bool = default;

  private:
    [[nodiscard]] friend auto
    hash_value(const Instruction::Identifier &x) noexcept -> size_t {
      auto seed = static_cast<size_t>(x.kind);
      boost::hash_combine(seed, x.type);
      boost::hash_combine(seed, x.ID);
      return seed;
    }
  };
};
static_assert(std::is_trivially_copy_assignable_v<Instruction::Identifier>);

inline auto Value::printName(std::ostream &os) const -> std::ostream & {
  if (const auto *I = llvm::dyn_cast<IR::Instruction>(this))
    return I->printName(os);
  return os << this;
}

constexpr void Node::hoist(IR::Loop *P, int depth0, IR::Loop *S) {
  invariant(P->getCurrentDepth() == depth0);
  invariant(!S || S->getLoop() == P); // P encloses S
  invariant(depth0 >= getNaturalDepth());
  parent_ = P;
  child_ = S;
  setCurrentDepth(depth0);
}
[[nodiscard]] constexpr auto Loop::getNumBBs() const -> int {
  // Should loops just store topidx as well?
  Node *N = getLast();
  for (;;) {
    invariant(N != this);
    if (auto *I = llvm::dyn_cast<Instruction>(N)) return I->getTopIdx();
    if (auto *L = llvm::dyn_cast<Loop>(N)) N = L->getLast();
    else {
      Node *P = N->getPrev();
      N = P ? P : N->getLoop();
    }
  }
}

/// Cnst
/// This is a loop invariant value.
/// In contrast to `CVal`, this holds a type, and should have a subtype
/// (only constructor is protected) to hold a particlar value instance.
class LoopInvariant : public Value {

protected:
  constexpr LoopInvariant(ValKind knd, llvm::Type *t) : Value(knd, 0, 0, t) {}

public:
  struct Argument {
    ptrdiff_t number_;
  };

  static constexpr auto classof(const Node *v) -> bool {
    ValKind k = v->getKind();
    return (k >= VK_FArg) && (k <= VK_Bflt);
  }
  struct Identifier {
    ValKind kind;
    llvm::Type *typ;
    union {
      int64_t i;
      double f;
      const llvm::APInt *ci;
      const llvm::APFloat *cf;
      llvm::Value *val;
    } payload;
    constexpr auto operator==(const Identifier &o) const -> bool {
      if (kind != o.kind || typ != o.typ) return false;
      switch (kind) {
      case VK_FArg: [[fallthrough]];
      case VK_Cint: return payload.i == o.payload.i;
      case VK_Cflt: return payload.f == o.payload.f;
      case VK_CVal: return payload.val == o.payload.val;
      case VK_Bint: return *payload.ci == *o.payload.ci;
      default: invariant(kind == VK_Bflt); return *payload.cf == *o.payload.cf;
      }
    }
    constexpr Identifier(llvm::Type *t, long long i)
      : kind(VK_Cint), typ(t), payload(i) {};
    constexpr Identifier(llvm::Type *t, long i) : Identifier(t, (long long)i) {}
    constexpr Identifier(llvm::Type *t, int i) : Identifier(t, (long long)i) {}
    constexpr Identifier(llvm::Type *t, double f) : kind(VK_Cflt), typ(t) {
      payload.f = f;
    };
    constexpr Identifier(llvm::Type *t, const llvm::APInt &i)
      : kind(VK_Bint), typ(t) {
      payload.ci = &i;
    };
    constexpr Identifier(llvm::Type *t, const llvm::APFloat &f)
      : kind(VK_Bflt), typ(t) {
      payload.cf = &f;
    }
    constexpr Identifier(llvm::Value *v) : kind(VK_CVal), typ(v->getType()) {
      payload.val = v;
    }
    constexpr Identifier(llvm::Type *t, llvm::Value *v)
      : kind(VK_CVal), typ(t) {
      invariant(t == v->getType());
      payload.val = v;
    }
    constexpr Identifier(llvm::Type *t, Argument arg) : kind(VK_FArg), typ(t) {
      payload.i = arg.number_;
    }

  private:
    [[nodiscard]] friend constexpr auto
    hash_value(LoopInvariant::Identifier const &x) noexcept -> size_t {
      auto seed = static_cast<size_t>(x.kind);
      boost::hash_combine(seed, x.typ);
      switch (x.kind) {
      case Node::VK_FArg: [[fallthrough]];
      case Node::VK_Cint: boost::hash_combine(seed, x.payload.i); break;
      case Node::VK_Cflt: boost::hash_combine(seed, x.payload.f); break;
      case Node::VK_CVal: boost::hash_combine(seed, x.payload.val); break;
      case Node::VK_Bint:
        boost::hash_combine(seed, llvm::hash_value(*x.payload.ci));
        break;
      default:
        invariant(x.kind == Node::VK_Bint);
        boost::hash_combine(seed, llvm::hash_value(*x.payload.cf));
      }
      return seed;
    }
  };
  static constexpr auto loopMask() -> uint16_t { return 0; }
  static constexpr auto calcLoopMask() -> uint16_t { return 0; }
};

class FunArg : public LoopInvariant {
  int64_t argnum_;

public:
  constexpr FunArg(int64_t arg, llvm::Type *t)
    : LoopInvariant(VK_FArg, t), argnum_(arg) {}
  static constexpr auto create(Arena<> *alloc, int64_t arg, llvm::Type *t)
    -> FunArg * {
    return alloc->create<FunArg>(arg, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_FArg;
  }

  [[nodiscard]] constexpr auto getArgNumber() const -> int64_t {
    return argnum_;
  }
};
/// A constant value w/ respect to the loopnest.
class Cint : public LoopInvariant {
  int64_t val_;

public:
  constexpr Cint(int64_t v, llvm::Type *t)
    : LoopInvariant(VK_Cint, t), val_(v) {}
  static constexpr auto create(Arena<> *alloc, int64_t v, llvm::Type *t)
    -> Cint * {
    return alloc->create<Cint>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cint;
  }

  [[nodiscard]] constexpr auto getVal() const -> int64_t { return val_; }
  bool isOne() const { return val_ == 1; }
};

class CVal : public LoopInvariant {
  llvm::Value *val_;

public:
  constexpr CVal(llvm::Value *v)
    : LoopInvariant(VK_CVal, v->getType()), val_(v) {}
  static constexpr auto create(Arena<> *alloc, llvm::Value *v) -> CVal * {
    return alloc->create<CVal>(v);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_CVal;
  }

  [[nodiscard]] constexpr auto getVal() const -> llvm::Value * { return val_; }
};
/// Cnst
/// A constant value w/ respect to the loopnest.
class Cflt : public LoopInvariant {
  double val_;

public:
  constexpr Cflt(double v, llvm::Type *t)
    : LoopInvariant(VK_Cflt, t), val_(v) {}
  static constexpr auto create(Arena<> *alloc, double v, llvm::Type *t)
    -> Cflt * {
    return alloc->create<Cflt>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cflt;
  }

  [[nodiscard]] constexpr auto getVal() const -> double { return val_; }
};
/// A constant value w/ respect to the loopnest.
class Bint : public LoopInvariant {
  const llvm::APInt &val_;

public:
  Bint(llvm::ConstantInt *v, llvm::Type *t)
    : LoopInvariant(VK_Bint, t), val_(v->getValue()) {}
  static constexpr auto create(Arena<> *alloc, llvm::ConstantInt *v,
                               llvm::Type *t) -> Bint * {
    return alloc->create<Bint>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Bint;
  }

  [[nodiscard]] constexpr auto getVal() const -> const llvm::APInt & {
    return val_;
  }
  bool isOne() const { return val_.isOne(); }
};
/// Cnst
/// A constant value w/ respect to the loopnest.
class Bflt : public LoopInvariant {
  const llvm::APFloat &val_;

public:
  Bflt(llvm::ConstantFP *v, llvm::Type *t)
    : LoopInvariant(VK_Bflt, t), val_(v->getValue()) {}
  static constexpr auto create(Arena<> *alloc, llvm::ConstantFP *v,
                               llvm::Type *t) -> Bflt * {
    return alloc->create<Bflt>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Bflt;
  }

  [[nodiscard]] constexpr auto getVal() const -> const llvm::APFloat & {
    return val_;
  }
};

[[nodiscard]] inline auto isConstantOneInt(Node *n) -> bool {
  if (Cint *c = llvm::dyn_cast<Cint>(n)) return c->getVal() == 1;
  if (Bint *c = llvm::dyn_cast<Bint>(n)) return c->getVal().isOne();
  return false;
}

} // namespace IR
