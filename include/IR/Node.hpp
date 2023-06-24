#pragma once

#include "Containers/UnrolledList.hpp"
#include "IR/InstructionCost.hpp"
#include "IR/Users.hpp"
#include "Polyhedra/Loops.hpp"
#include "Utilities/Allocators.hpp"
#include <Math/Array.hpp>
#include <cstdint>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>

namespace poly::IR {
using utils::NotNull, utils::invariant, utils::Arena, containers::UList;
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
///  7.     c[j,i] = e
///  8.   q = 3y - c[i,i]
///  9.   y2 = y*y
/// 10.   w = y2 - q
/// 11.   for j in J // VK_Loop
/// 12.     z = c[j,i]
/// 13.     e = bar(z, y2)
/// 14.     f = a[i]
/// 15.     g = baz(e, f, w)
/// 16.     a[i] = g
/// 17.   z = a[i]
/// 18.   e = p[]
/// 19.   f = z + e
/// 20.   p[] = f
/// 21. z = p[]
/// 22. e = z*z
/// 23. p[] = z
///
/// Same level -> means getNext()
/// Sub-level \-> means getChild()
/// We have
/// 0. -> 1. -> 2. -> 21. -> 22 -> 23
///             \-> 3 -> 4 -> 8-> 9 -> 10 -> 11 -> 17 -> 18 -> 19 -> 20
///                       \-> 5 -> 6 -> 7     \-> 12 -> 13 -> 14 -> 15 -> 16
/// For a `Loop`, `getChild()` returns the first contained instruction, and
/// `getParent()` returns the enclosing (outer) loop.
/// For `Instruction`s, `getChild()` returns the first sub-loop.
/// This, for example, we can iterate over all sub-loops of `L` via
/// ```c++
/// Node* C = getChild();
/// C = llvm::isa<Loop>(C) ? C : C->getChild();
/// while (C){
///   // do stuff with `C`
///   C = C->getNext()
///   C = (C || llvm::isa<Loop>(C)) ? C : C->getChild();
/// }
///
/// IR types: Loop, Block, Addr, Instr, Consts
class Node {

public:
  enum ValKind : uint8_t {
    VK_Load,
    VK_Stow, // used for ordered comparisons; all `Addr` types <= Stow
    VK_Loop,
    VK_CVal,
    VK_Cint,
    VK_Bint,
    VK_Cflt,
    VK_Bflt,
    VK_Func, // used for ordered comparisons; all `Inst` types >= Func
    VK_Call,
    VK_Oprn,
  };

private:
  Node *prev{nullptr};
  Node *next{nullptr};
  Node *parent{nullptr};
  Node *child{nullptr};

  // we have a private pointer so different types can share
  // in manner not exacctly congruent with type hiearchy
  // in particular, `Inst` and `Load` want `User` lists
  // while `Stow`s do not.
  // `Addr` is the common load/store subtype
  // So in some sense, we want both `Load` and `Store` to inherit from `Addr`,
  // but only load to inherit 'hasUsers' and only store to inherit the operand.
  // `Inst` would also inherit 'hasUsers', but would want a different operands
  // type.
  // Addr has a FAM, so multiple inheritence isn't an option for `Load`/`Stow`,
  // and we want a common base that we can query to avoid monomorphization.
protected:
  const ValKind kind;
  uint8_t depth{0};
  uint16_t index_;
  uint16_t lowLink_;
  uint16_t bitfield;

  constexpr Node(ValKind kind) : kind(kind) {}
  constexpr Node(ValKind kind, unsigned d) : kind(kind), depth(d) {}

public:
  [[nodiscard]] constexpr auto wasVisited() const -> bool {
    return bitfield & 0x1;
  }
  constexpr void setVisited() { bitfield |= 0x1; }
  constexpr void clearVisited() { bitfield &= ~0x1; }
  [[nodiscard]] constexpr auto isOnStack() const -> bool {
    return bitfield & 0x2;
  }
  constexpr void setOnStack() { bitfield |= 0x2; }
  constexpr void removeFromStack() { bitfield &= ~0x2; }
  [[nodiscard]] constexpr auto lowLink() -> uint16_t & { return lowLink_; }
  [[nodiscard]] constexpr auto lowLink() const -> unsigned { return lowLink_; }
  constexpr void setLowLink(unsigned l) { lowLink_ = l; }
  [[nodiscard]] constexpr auto index() -> uint16_t & { return index_; }
  [[nodiscard]] constexpr auto getIndex() const -> unsigned { return index_; }
  constexpr void setIndex(unsigned i) { index_ = i; }
  [[nodiscard]] constexpr auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] constexpr auto getDepth() const -> unsigned { return depth; }
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
  constexpr void setDepth(unsigned d) { depth = d; }
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
  constexpr void removeFromList() {
    if (prev) prev->setNext(next);
    if (next) next->setPrev(prev);
    prev = nullptr;
    next = nullptr;
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
};
static_assert(sizeof(Node) == 4 * sizeof(Node *) + 8);

/// Loop
/// parent: outer loop
/// child: inner (sub) loop
/// exit is the associated exit block
class Loop : public Node {
  poly::Loop *affineLoop{nullptr};

public:
  Loop(unsigned d, poly::Loop *AL) : Node(VK_Loop, d), affineLoop(AL) {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Loop;
  }
  [[nodiscard]] constexpr auto getSubLoop() const -> Loop * {
    return static_cast<Loop *>(getChild());
  }
  [[nodiscard]] constexpr auto getOuterLoop() const -> Loop * {
    return static_cast<Loop *>(getParent());
  }
  [[nodiscard]] constexpr auto getNextLoop() const -> Loop * {
    Node *C = getChild();
    C = (C || llvm::isa<Loop>(C)) ? C : C->getChild();
    return static_cast<Loop *>(C);
  }
  constexpr void forEachSubLoop(const auto &f) {
    for (auto *c = getSubLoop(); c; c = c->getNextLoop()) f(c);
  }
  /// call `f` for this loop train, and all subloops
  ///
  constexpr void forEachLoop(const auto &f) {
    for (auto *c = this; c; c = c->getNextLoop()) {
      f(c);
      if (auto *s = c->getSubLoop()) s->forEachLoop(f);
    }
  }
  static constexpr auto create(Arena<> *alloc, poly::Loop *AL, size_t depth)
    -> Loop * {
    return alloc->create<Loop>(depth, AL);
  }
  [[nodiscard]] constexpr auto getLLVMLoop() const -> llvm::Loop * {
    return affineLoop->getLLVMLoop();
  }
  [[nodiscard]] constexpr auto getAffineLoop() const -> poly::Loop * {
    return affineLoop;
  }
};

class Instruction;

class Value : public Node {
protected:
  constexpr Value(ValKind kind) : Node(kind) {}
  constexpr Value(ValKind kind, unsigned depth) : Node(kind, depth) {}
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
  // unionPtr methods
  // [[nodiscard]] constexpr auto getUsers() const
  //   -> const UList<Instruction *> * {
  //   invariant(kind == VK_Load || kind >= VK_Func);
  //   return unionPtr.users;
  // }
  // [[nodiscard]] constexpr auto getUsers() -> UList<Instruction *> * {
  //   invariant(kind == VK_Load || kind >= VK_Func);
  //   return unionPtr.users;
  // }
  // constexpr void setUsers(UList<Instruction *> *users) {
  //   invariant(kind == VK_Load || kind >= VK_Func);
  //   unionPtr.users = users;
  // }
  // constexpr void addUser(Arena<> *alloc, Instruction *n) {
  //   invariant(kind == VK_Load || kind >= VK_Func);
  //   if (!unionPtr.users)
  //     unionPtr.users = alloc->create<UList<Instruction *>>(n);
  //   else unionPtr.users = unionPtr.users->pushUnique(alloc, n);
  // }
  // constexpr void removeFromUsers(Instruction *n) {
  //   invariant(kind == VK_Load || kind >= VK_Func);
  //   unionPtr.users->eraseUnordered(n);
  // }

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
  inline auto getCost(llvm::TargetTransformInfo &TTI, cost::VectorWidth W)
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
  constexpr Instruction(ValKind kind) : Value(kind) {}
  constexpr Instruction(ValKind kind, unsigned depth) : Value(kind, depth) {}

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_Func || v->getKind() <= VK_Stow;
  }
  struct Identifier {
    llvm::Intrinsic::ID ID;
    Node::ValKind kind;
    llvm::Type *type;
  };
  // declarations
  [[nodiscard]] constexpr auto getIdentifier() const -> Identifier;
  inline void setOperands(Arena<> *alloc, math::PtrVector<Value *>);
};

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
  constexpr Cnst(ValKind kind, llvm::Type *t) : Value(kind) { typ = t; }

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
  constexpr Bint(llvm::ConstantInt *v, llvm::Type *t)
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
  constexpr Bflt(llvm::ConstantFP *v, llvm::Type *t)
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

} // namespace poly::IR
