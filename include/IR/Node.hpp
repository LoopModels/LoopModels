#pragma once

#include "Alloc/Arena.hpp"
#include "Containers/UnrolledList.hpp"
#include "IR/InstructionCost.hpp"
#include "IR/Users.hpp"
#include "Polyhedra/Loops.hpp"
#include "Support/Iterators.hpp"
#include "Utilities/ListRanges.hpp"
#include <Math/Array.hpp>
#include <cstdint>
#include <limits>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>

namespace poly::poly {
class Dependencies;
} // namespace poly::poly
namespace poly::IR {
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
/// Same level -> means getNext()
/// Sub-level \-> means getChild()
/// We have
/// 0. -> 1. -> 2. -> 21. -> 22 -> 23
///             \-> 3 -> 4 -> 8-> 9 -> 10 -> 11 -> 17 -> 18 -> 19 -> 20
///                       \-> 5 -> 6 -> 7     \-> 12 -> 13 -> 14 -> 15 -> 16
/// For a `Loop`, `getChild()` returns the first contained instruction, and
/// For `Instruction`s, `getChild()` returns the first sub-loop.
/// `getParent()` returns the enclosing (outer) loop.
/// This, for example, we can iterate over all sub-loops of `L` via
/// ```c++
/// Node* C = getChild();
/// C = llvm::isa<Loop>(C) ? C : C->getChild();
/// while (C){
///   // do stuff with `C`
///   C = C->getNext()
///   C = (C || llvm::isa<Loop>(C)) ? C : C->getChild();
/// }
/// ```
/// IR types: Loop, Block, Addr, Instr, Consts
class Node {

public:
  enum ValKind : uint8_t {
    VK_Load,
    VK_Stow, // used for ordered comparisons; all `Addr` types <= Stow
    VK_Loop,
    VK_Exit,
    VK_CVal,
    VK_Cint,
    VK_Bint,
    VK_Cflt,
    VK_Bflt,
    VK_Func, // used for ordered comparisons; all `Inst` types >= Func
    VK_Call,
    VK_Oprn,
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
  uint8_t currentDepth{0}; // current depth
  uint8_t naturalDepth{0}; // original, or, for Addr, `indMat.numCol()`
  uint8_t visitDepth{255};
  uint8_t maxDepth; // memory allocated to support up to this depth
  bool dependsOnParentLoop_{false};
  // 7 bytes; we have 1 left!
  // uint16_t index_;
  // uint16_t lowLink_;
  // uint16_t bitfield;

  constexpr Node(ValKind kind_) : kind(kind_) {}
  constexpr Node(ValKind kind_, unsigned depth)
    : kind(kind_), currentDepth(depth), naturalDepth(depth) {}
  constexpr Node(ValKind kind_, unsigned curDepth, unsigned natDepth)
    : kind(kind_), currentDepth(curDepth), naturalDepth(natDepth) {}
  constexpr Node(ValKind kind_, unsigned curDepth, unsigned natDepth,
                 unsigned maxDepth_)
    : kind(kind_), currentDepth(curDepth), naturalDepth(natDepth),
      maxDepth(maxDepth_) {}

private:
  Node *prev{nullptr};
  Node *next{nullptr};
  Node *parent{nullptr};
  Node *child{nullptr};

public:
  constexpr void visit(uint8_t d) { visitDepth = d; }
  [[nodiscard]] constexpr auto getVisitDepth() const -> uint8_t {
    return visitDepth;
  }
  constexpr void clearVisited() { visitDepth = 255; }
  [[nodiscard]] constexpr auto wasVisited(uint8_t d) const -> bool {
    return visitDepth == d;
  }
  constexpr void setDependsOnParentLoop() { dependsOnParentLoop_ = true; }
  [[nodiscard]] constexpr auto dependsOnParentLoop() const -> bool {
    return dependsOnParentLoop_;
  }
  [[nodiscard]] constexpr auto sameBlock(const Node *other) const -> bool {
    return other && other->parent == parent && other->child == child;
  }

  // [[nodiscard]] constexpr auto wasVisited() const -> bool {
  //   return bitfield & 0x1;
  // }
  // constexpr void setVisited() { bitfield |= 0x1; }
  // constexpr void clearVisited() { bitfield &= ~0x1; }
  // [[nodiscard]] constexpr auto isOnStack() const -> bool {
  //   return bitfield & 0x2;
  // }
  // constexpr void setOnStack() { bitfield |= 0x2; }
  // constexpr void removeFromStack() { bitfield &= ~0x2; }
  // constexpr void setDependsOnParentLoop() { bitfield |= 0x4; }
  // [[nodiscard]] constexpr auto lowLink() -> uint16_t & { return lowLink_; }
  // [[nodiscard]] constexpr auto lowLink() const -> unsigned { return lowLink_;
  // } constexpr void setLowLink(unsigned l) { lowLink_ = l; }
  // [[nodiscard]] constexpr auto index() -> uint16_t & { return index_; }
  // [[nodiscard]] constexpr auto getIndex() const -> unsigned { return index_;
  // } constexpr void setIndex(unsigned i) { index_ = i; }
  [[nodiscard]] constexpr auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] constexpr auto getCurrentDepth() const -> unsigned {
    return currentDepth;
  }
  [[nodiscard]] constexpr auto getNaturalDepth() const -> unsigned {
    return naturalDepth;
  }

  [[nodiscard]] constexpr auto getParent() const -> Node * { return parent; }
  [[nodiscard]] constexpr auto getChild() const -> Node * { return child; }
  [[nodiscard]] constexpr auto getPrev() const -> Node * { return prev; }
  [[nodiscard]] constexpr auto getNext() const -> Node * { return next; }
  constexpr auto setNext(Node *n) -> Node * {
    next = n;
    if (n) n->prev = this;
    return this;
  }
  constexpr auto setPrev(Node *n) -> Node * {
    prev = n;
    if (n) n->next = this;
    return this;
  }
  constexpr auto setChild(Node *n) -> Node * {
    child = n;
    if (n) n->parent = this;
    return this;
  }
  constexpr auto setParent(Node *n) -> Node * {
    parent = n;
    if (n) n->child = this;
    return this;
  }
  constexpr void setCurrentDepth(unsigned d) {
    invariant(d <= std::numeric_limits<decltype(currentDepth)>::max());
    currentDepth = d;
  }
  /// insert `d` ahead of `this`
  constexpr void insertAhead(Node *d) {
    d->setNext(this);
    d->setPrev(prev);
    if (prev) prev->setNext(d);
    prev = d;
  }
  /// insert `d` after `this`
  constexpr void insertAfter(Node *d) {
    d->setPrev(this);
    d->setNext(next);
    if (next) next->setPrev(d);
    next = d;
  }
  constexpr void clearPrevNext() {
    prev = nullptr;
    next = nullptr;
  }
  constexpr void removeFromList() {
    if (prev) prev->setNext(next);
    if (next) next->setPrev(prev);
    clearPrevNext();
  }
  constexpr void insertChild(Node *d) {
    d->setParent(this);
    d->setChild(child);
    if (child) child->setParent(d);
    child = d;
  }
  constexpr void insertParent(Node *d) {
    d->setChild(this);
    d->setParent(parent);
    if (parent) parent->setChild(d);
    parent = d;
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
    return VK_CVal;
  }
  [[nodiscard]] constexpr auto nodes() noexcept
    -> utils::ListRange<Node, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }
  [[nodiscard]] constexpr auto nodes() const noexcept
    -> utils::ListRange<const Node, utils::GetNext, utils::Identity> {
    return utils::ListRange{this, utils::GetNext{}};
  }
  [[nodiscard]] inline constexpr auto getLoop() const noexcept -> Loop *;
};
static_assert(sizeof(Node) == 4 * sizeof(Node *) + 8);

/// Loop
/// parent: outer loop
/// child: inner (sub) loop
/// exit is the associated exit block
class Loop : public Node {
  enum LegalTransforms {
    Unknown = 0,
    DependenceFree = 1,
    IndexMismatch = 2,
    None = 3
  };
  poly::Loop *affineLoop{nullptr};
  Node *last{nullptr};
  /// loopMeta's leading 2 bits give `LegalTransforms`
  /// remaining 30 bits give an ID to the loop.
  /// IDs are in topologically sorted order.
  uint32_t loopMeta;
  int32_t edgeId{-1};
  // LegalTransforms legal{Unknown};
  // while `child` points to the first contained instruction,
  // `last` points to the last contained instruction,
  // and can be used for backwards iteration over the graph.

public:
  constexpr void setMeta(uint32_t m) { loopMeta = m; }
  [[nodiscard]] constexpr auto getID() const -> uint32_t {
    return loopMeta & 0x3FFFFFFF;
  }
  [[nodiscard]] constexpr auto getLegal() const -> LegalTransforms {
    return static_cast<LegalTransforms>(loopMeta >> 30);
  }
  constexpr auto setLegal(LegalTransforms l) -> LegalTransforms {
    loopMeta = (loopMeta & 0x3FFFFFFF) | (static_cast<uint32_t>(l) << 30);
    return l;
  }
  [[nodiscard]] constexpr auto edges(poly::PtrVector<int32_t> edges) const
    -> utils::VForwardRange {
    return utils::VForwardRange{edges, edgeId};
  }
  constexpr Loop(unsigned d)
    : Node{VK_Loop, d}, loopMeta{std::numeric_limits<uint32_t>::max()} {}
  constexpr Loop(unsigned d, poly::Loop *AL)
    : Node{VK_Loop, d}, affineLoop{AL} {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Loop;
  }
  /// Get the first subloop.
  [[nodiscard]] constexpr auto getSubLoop() const -> Loop * {
    Node *C = getChild();
    C = (C || llvm::isa<Loop>(C)) ? C : C->getChild();
    return static_cast<Loop *>(C);
  }
  /// Return the enclosing, parent loop.
  [[nodiscard]] constexpr auto getOuterLoop() const -> Loop * {
    return static_cast<Loop *>(getParent());
  }
  /// Returns the next loop at the same level
  [[nodiscard]] constexpr auto getNextLoop() const -> Loop * {
    Node *N = getNext();
    if (!N) return nullptr;
    if (!llvm::isa<Loop>(N)) N = N->getChild();
    return static_cast<Loop *>(N);
  }
  [[nodiscard]] constexpr auto subLoops() const {
    return utils::ListRange{getSubLoop(),
                            [](Loop *L) -> Loop * { return L->getNextLoop(); }};
  }
  /// getLast()
  /// Get the last node in the loop.
  /// Useful for iterating backwards.
  [[nodiscard]] constexpr auto getLast() const -> Node * { return last; }
  constexpr void setLast(Node *n) { last = n; }
  [[nodiscard]] constexpr auto getLLVMLoop() const -> llvm::Loop * {
    return affineLoop->getLLVMLoop();
  }
  [[nodiscard]] constexpr auto getAffineLoop() const -> poly::Loop * {
    return affineLoop;
  }
  [[nodiscard]] constexpr auto contains(IR::Node *N) const -> bool {
    for (Loop *L = N->getLoop(); L; L = L->getLoop())
      if (L == this) return true;
    return false;
  }
  // get the outermost subloop of `this` to which `N` belongs
  [[nodiscard]] constexpr auto getSubloop(IR::Node *N) -> Loop * {
    Loop *L = N->getLoop();
    if (L == this) return this;
    for (; L;) {
      Loop *O = L->getOuterLoop();
      if (O == this) return L;
      L = O;
    }
    return nullptr;
  }
  [[nodiscard]] constexpr auto getEdge() const -> int32_t { return edgeId; }
  constexpr void addEdge(math::MutPtrVector<int32_t> deps, int32_t d) {
    invariant(d >= 0);
    // [ -1, -1, -1, -1, -1 ] // d = 2, edgeId = -1
    // [  2, -1, -1, -1, -1 ] // d = 0, edgeId = 2
    // [  2, -1, -1, -1,  0 ] // d = 4, edgeId = 0
    // now edgeId = 4, and we can follow path 4->0->2
    deps[d] = edgeId;
    edgeId = d;
  }
  constexpr auto getLoopAtDepth(uint8_t d) -> Loop * {
    Loop *L = this;
    for (uint8_t currDepth = this->currentDepth; currDepth > d; --currDepth)
      L = L->getOuterLoop();
    return L;
  }
  inline auto getLegality(poly::Dependencies, math::PtrVector<int32_t>)
    -> LegalTransforms;
};
[[nodiscard]] inline constexpr auto Node::getLoop() const noexcept -> Loop * {
  if (!parent) return nullptr;
  if (parent->kind != VK_Loop) return nullptr;
  return static_cast<Loop *>(parent);
}

struct Exit : Node {
  Exit() : Node(VK_Exit) {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Exit;
  }
};

class Instruction;

class Value : public Node {
protected:
  constexpr Value(ValKind kind_) : Node(kind_) {}
  constexpr Value(ValKind kind_, unsigned depth) : Node(kind_, depth) {}
  constexpr Value(ValKind kind_, unsigned curDepth, unsigned natDepth)
    : Node(kind_, curDepth, natDepth) {}
  constexpr Value(ValKind kind_, unsigned curDepth, unsigned natDepth,
                  unsigned maxDepth_)
    : Node(kind_, curDepth, natDepth, maxDepth_) {}

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

  [[nodiscard]] inline auto getFastMathFlags() const -> llvm::FastMathFlags;
  /// these methods are overloaded for specific subtypes
  inline auto getCost(const llvm::TargetTransformInfo &TTI, cost::VectorWidth W)
    -> cost::RecipThroughputLatency;
  [[nodiscard]] inline auto getValue() -> llvm::Value *;
  [[nodiscard]] inline auto getValue() const -> const llvm::Value *;
  [[nodiscard]] inline auto getInstruction() -> llvm::Instruction *;
  [[nodiscard]] inline auto getInstruction() const -> const llvm::Instruction *;

  [[nodiscard]] inline auto getType() const -> llvm::Type *;
  [[nodiscard]] inline auto getType(unsigned) const -> llvm::Type *;
  [[nodiscard]] inline auto getOperands() -> math::PtrVector<Value *>;
  [[nodiscard]] inline auto getNumOperands() const -> unsigned;
  [[nodiscard]] inline auto getOperand(unsigned) -> Value *;
  [[nodiscard]] inline auto getOperand(unsigned) const -> const Value *;
  [[nodiscard]] inline auto associativeOperandsFlag() const -> uint8_t;
  [[nodiscard]] inline auto getNumScalarBits() const -> unsigned;
  [[nodiscard]] inline auto getNumScalarBytes() const -> unsigned;
  [[nodiscard]] inline auto getBasicBlock() -> llvm::BasicBlock *;
  [[nodiscard]] inline auto getBasicBlock() const -> const llvm::BasicBlock *;
};

class Instruction : public Value {
  /// For use with control flow merging
  /// same operation on same type with disparate branches can be merged
  /// This only identifies instructions
protected:
  constexpr Instruction(ValKind kind_) : Value(kind_) {}
  constexpr Instruction(ValKind kind_, unsigned depth) : Value(kind_, depth) {}
  constexpr Instruction(ValKind kind_, unsigned curDepth, unsigned natDepth)
    : Value(kind_, curDepth, natDepth) {}
  constexpr Instruction(ValKind kind_, unsigned curDepth, unsigned natDepth,
                        unsigned maxDepth_)
    : Value(kind_, curDepth, natDepth, maxDepth_) {}

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_Func || v->getKind() <= VK_Stow;
  }
  struct Identifier {
    llvm::Intrinsic::ID ID;
    Node::ValKind kind;
    llvm::Type *type;
    constexpr auto operator==(const Identifier &other) const -> bool = default;
  };
  // declarations
  [[nodiscard]] auto getIdentifier() const -> Identifier;
  inline void setOperands(Arena<> *alloc, math::PtrVector<Value *>);
};
static_assert(std::is_copy_assignable_v<Instruction::Identifier>); 

/// CVal
/// A constant value w/ respect to the loopnest.
class CVal : public Value {
  llvm::Value *val;

public:
  constexpr CVal(llvm::Value *v) : Value(VK_CVal) { val = v; }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_CVal;
  }

  [[nodiscard]] constexpr auto getValue() const -> llvm::Value * { return val; }
  [[nodiscard]] auto getType() const -> llvm::Type * { return val->getType(); }
};
// constexpr void Value::removeFromUsers(Value *n) {
//   if (auto *I = llvm::dyn_cast<Instruction>(n)) removeFromUsers(I);
// }

/// Cnst
class Cnst : public Value {
  llvm::Type *typ;

protected:
  constexpr Cnst(ValKind knd, llvm::Type *t) : Value(knd) { typ = t; }

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cint || v->getKind() == VK_Cflt;
  }
  [[nodiscard]] constexpr auto getType() const -> llvm::Type * { return typ; }
  struct Identifier {
    ValKind kind;
    llvm::Type *typ;
    union {
      int64_t i;
      double f;
      const llvm::APInt *ci;
      const llvm::APFloat *cf;
    } payload;
    constexpr auto operator==(const Identifier &o) const -> bool {
      if (kind != o.kind || typ != o.typ) return false;
      switch (kind) {
      case VK_Cint: return payload.i == o.payload.i;
      case VK_Cflt: return payload.f == o.payload.f;
      case VK_Bint: return *payload.ci == *o.payload.ci;
      default: invariant(kind == VK_Bflt); return *payload.cf == *o.payload.cf;
      }
    }
    constexpr Identifier(llvm::Type *t, int64_t i)
      : kind(VK_Cint), typ(t), payload(i){};
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
  };
};
/// A constant value w/ respect to the loopnest.
class Cint : public Cnst {
  int64_t val;

public:
  constexpr Cint(int64_t v, llvm::Type *t) : Cnst(VK_Cint, t), val(v) {}
  static constexpr auto create(Arena<> *alloc, int64_t v, llvm::Type *t)
    -> Cint * {
    return alloc->create<Cint>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cint;
  }

  [[nodiscard]] constexpr auto getVal() const -> int64_t { return val; }
};
/// Cnst
/// A constant value w/ respect to the loopnest.
class Cflt : public Cnst {
  double val;

public:
  constexpr Cflt(double v, llvm::Type *t) : Cnst(VK_Cflt, t), val(v) {}
  static constexpr auto create(Arena<> *alloc, double v, llvm::Type *t)
    -> Cflt * {
    return alloc->create<Cflt>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cflt;
  }

  [[nodiscard]] constexpr auto getVal() const -> double { return val; }
};
/// A constant value w/ respect to the loopnest.
class Bint : public Cnst {
  const llvm::APInt &val;

public:
  Bint(llvm::ConstantInt *v, llvm::Type *t)
    : Cnst(VK_Bint, t), val(v->getValue()) {}
  static constexpr auto create(Arena<> *alloc, llvm::ConstantInt *v,
                               llvm::Type *t) -> Bint * {
    return alloc->create<Bint>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Bint;
  }

  [[nodiscard]] constexpr auto getVal() const -> const llvm::APInt & {
    return val;
  }
};
/// Cnst
/// A constant value w/ respect to the loopnest.
class Bflt : public Cnst {
  const llvm::APFloat &val;

public:
  Bflt(llvm::ConstantFP *v, llvm::Type *t)
    : Cnst(VK_Bflt, t), val(v->getValue()) {}
  static constexpr auto create(Arena<> *alloc, llvm::ConstantFP *v,
                               llvm::Type *t) -> Bflt * {
    return alloc->create<Bflt>(v, t);
  }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Bflt;
  }

  [[nodiscard]] constexpr auto getVal() const -> const llvm::APFloat & {
    return val;
  }
};

[[nodiscard]] inline auto isConstantOneInt(Node *n) -> bool {
  if (Cint *c = llvm::dyn_cast<Cint>(n)) return c->getVal() == 1;
  if (Bint *c = llvm::dyn_cast<Bint>(n)) return c->getVal().isOne();
  return false;
}

class Compute;
struct InstByValue {
  Compute *inst;
  inline auto operator==(InstByValue const &other) const -> bool;
};

} // namespace poly::IR
