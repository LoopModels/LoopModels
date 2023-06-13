#pragma once

#include "Containers/UnrolledList.hpp"
#include "IR/InstructionCost.hpp"
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
using utils::NotNull, utils::invariant, utils::BumpAlloc, containers::UList;
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
///  8.   // VK_Exit
///  9.   y2 = y*y
/// 10.   for j in J // VK_Loop
/// 11.     z = c[j,i]
/// 12.     e = bar(z, y2)
/// 13.     f = a[i]
/// 14.     g = baz(e, f)
/// 15.     a[i] = g
/// 16.   // VK_Exit
/// 17.   z = a[i]
/// 18.   e = p[]
/// 19.   f = z + e
/// 20.   p[] = f
/// 21. // VK_Exit
/// 22. z = p[]
/// 23. e = z*z
/// 24. p[] = z
/// 25. // VK_EXIT
///
/// Hmm, we don't need parent/children to reach everything, e.g. individual
/// addrs
///
///
/// IR types: Loop, Block, Addr, Instr
/// Loop starts a Loop, Block ends it.
/// At a level, first loops:
/// Parents:
/// Loop[0]: [ parentLoop ]
/// for (i : _(0,numSubLoop-1)) Loop[i+1]: [ Exit[i] ]
/// Exit: [ loopStart, contained addrs...] // ptr, linked list
/// Addr: [ Loop or Exit, prevAddrs... ] // actual
/// Instr: [ args... (loads or insts) ] // actual
///
/// For SCC, we initialize parent ptr to nullptr
/// thus, Loop[0] has no parents
/// For each Blck
///
/// Children don't need to be full reverse, as `*next` covers many:
/// Loop's: [ first sub loop (if it has one), matching Exit ]
/// for (i : _(0,numSubLoop-1)) Exit[i]: [ Loop[i+1] ]
/// Addr's: [ nextAddrs... ]
/// Inst's: [ uses... (stows or insts) ]
///
/// To iterate over subloops, SubLoopIterator
///
/// This simplified structure means we can use LLVM-style RTTI
///
/// top level loops can be chained together...
/// top level loop's next can point to another top level loop
class Node {

public:
  enum ValKind {
    VK_Load,
    VK_Stow, // used for ordered comparisons; all `Addr` types <= Stow
    VK_Exit,
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
  Node *componentFwd{nullptr}; // SCC
  Node *componentBwd{nullptr}; // SCC-cycle
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
  unsigned depth{0};

  constexpr Node(ValKind kind) : kind(kind) {}
  constexpr Node(ValKind kind, unsigned d) : kind(kind), depth(d) {}

public:
  [[nodiscard]] constexpr auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] constexpr auto getDepth() const -> unsigned { return depth; }
  [[nodiscard]] constexpr auto getParent() const -> Node * { return parent; }
  [[nodiscard]] constexpr auto getChild() const -> Node * { return child; }
  [[nodiscard]] constexpr auto getPrev() const -> Node * { return prev; }
  [[nodiscard]] constexpr auto getNext() const -> Node * { return next; }
  constexpr void setNext(Node *n) {
    next = n;
    n->prev = this;
  }
  constexpr void setPrev(Node *n) { n->setNext(this); }
  constexpr void setChild(Node *n) { child = n; }
  constexpr void setParent(Node *n) { parent = n; }
  constexpr void setDepth(unsigned d) { depth = d; }
  constexpr void insertAhead(Node *d) {
    d->setNext(this);
    d->setPrev(prev);
    if (prev) prev->setNext(d);
    prev = d;
  }
  constexpr void removeFromList() {
    if (prev) prev->setNext(next);
    if (next) next->setPrev(prev);
    prev = nullptr;
    next = nullptr;
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
    if (llvm::isa<llvm::ConstantInt>(v)) return VK_Cint;
    if (llvm::isa<llvm::ConstantFP>(v)) return VK_Cflt;
    return VK_CVal;
  }
};

class Loop;
/// Exit
/// child is next loop at same level
/// parent is the loop the exit closes
/// subExit is the exit of the last subloop
class Exit : public Node {
  Exit *subExit;

public:
  Exit(unsigned d) : Node(VK_Exit, d) {}
  [[nodiscard]] constexpr auto getLoop() const -> Loop *;
  [[nodiscard]] constexpr auto getNextLoop() const -> Loop *;
  [[nodiscard]] constexpr auto getSubExit() const -> Exit * { return subExit; }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Exit;
  }
  constexpr void setSubExit(Exit *e) { subExit = e; }
  constexpr void setNextLoop(Loop *);
};
/// Loop
/// parent: outer loop
/// child: inner (sub) loop
/// exit is the associated exit block
class Loop : public Node {
  Exit *exit;
  llvm::Loop *llvmLoop{nullptr};
  poly::Loop *affineLoop{nullptr};

public:
  Loop(Exit *e, unsigned d, llvm::Loop *LL, poly::Loop *AL)
    : Node(VK_Loop, d), llvmLoop(LL), affineLoop(AL) {
    exit = e;
    e->setParent(this);
    // we also initialize prev/next, adding instrs will push them
    e->setPrev(this);
    setNext(e);
  }
  [[nodiscard]] constexpr auto getExit() const -> Exit * { return exit; }
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
    return getExit()->getNextLoop();
  }
  [[nodiscard]] constexpr auto getNextOrChild() const -> Loop * {
    Loop *L = getSubLoop();
    return L ? L : getNextLoop();
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
  constexpr void forEachLoopAndExit(const auto &f) {
    for (auto *c = this; c; c = c->getNextLoop()) {
      f(c);
      if (auto *s = c->getSubLoop()) s->forEachLoopAndExit(f);
      f(c->getExit());
    }
  }
  static constexpr auto create(BumpAlloc<> &alloc, llvm::Loop *LL,
                               poly::Loop *AL, size_t depth) -> Loop * {
    auto *E = alloc.create<Exit>(depth);
    auto *L = alloc.create<Loop>(E, depth, LL, AL);
    return L;
  }
  constexpr auto addSubLoop(BumpAlloc<> &alloc, llvm::Loop *LL) -> Loop * {
    auto d = getDepth() + 1;
    auto *E = alloc.create<Exit>(d);
    auto *L = alloc.create<Loop>(E, d, LL, nullptr);
    L->setParent(this);
    if (auto *eOld = getExit()->getSubExit()) {
      invariant(getChild() != nullptr);
      eOld->setNextLoop(L);
    } else setChild(L);
    getExit()->setSubExit(E);
    return L;
  }
  constexpr auto addOuterLoop(BumpAlloc<> &alloc) -> Loop * {
    // NOTE: we need to correctly set depths later
    auto *E = alloc.create<Exit>(0);
    auto *L =
      alloc.create<Loop>(E, 0, llvmLoop->getParentLoop(), getAffineLoop());
    setParent(L);
    invariant(getNextLoop() == nullptr);
    E->setSubExit(getExit());
    return L;
  }
  // NOLINTNEXTLINE(readability-make-member-function-const)
  constexpr void addNextLoop(Loop *L) {
    invariant(getExit()->getNextLoop() == nullptr);
    invariant(getOuterLoop() == nullptr);
    getExit()->setNextLoop(L);
  }
  constexpr auto addNextLoop(BumpAlloc<> &alloc, llvm::Loop *LL) -> Loop * {
    auto d = getDepth() + 1;
    auto *E = alloc.create<Exit>(d);
    auto *L = alloc.create<Loop>(E, d, LL, nullptr);
    invariant(getExit()->getNextLoop() == nullptr);
    getExit()->setNextLoop(L);
    if (auto *p = getOuterLoop()) {
      invariant(p->getChild() == this);
      L->setParent(p);
      p->getExit()->setSubExit(E);
    }
    return L;
  }
  [[nodiscard]] constexpr auto getLLVMLoop() const -> llvm::Loop * {
    return llvmLoop;
  }
  constexpr void truncate() {
    // TODO: we use the current loop depth, and set the poly::Loop
    // and all ArrayRefs accordingly.
  }
  [[nodiscard]] constexpr auto getAffineLoop() const -> poly::Loop * {
    return affineLoop;
  }
};
constexpr void Exit::setNextLoop(Loop *L) { setChild(L); }
constexpr auto Exit::getLoop() const -> Loop * {
  return static_cast<Loop *>(getParent());
}
constexpr auto Exit::getNextLoop() const -> Loop * {
  return static_cast<Loop *>(getChild());
}

class Value : public Node {
protected:
  union {
    UList<Value *> *users{nullptr}; // Func, Call, Oprn, Load
    Value *node;                    // Stow
    llvm::Type *typ;                // Cint, Cflt, Bint, Bflt
    llvm::Value *val;               // CVal
  } unionPtr;

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() >= VK_CVal || v->getKind() <= VK_Stow;
  }

  constexpr Value(ValKind kind) : Node(kind) {}
  constexpr Value(ValKind kind, unsigned depth) : Node(kind, depth) {}

  // unionPtr methods
  [[nodiscard]] constexpr auto getUsers() const -> const UList<Value *> * {
    invariant(kind == VK_Load || kind >= VK_Func);
    return unionPtr.users;
  }
  [[nodiscard]] constexpr auto getUsers() -> UList<Value *> * {
    invariant(kind == VK_Load || kind >= VK_Func);
    return unionPtr.users;
  }
  constexpr void setUsers(UList<Value *> *users) {
    invariant(kind == VK_Load || kind >= VK_Func);
    unionPtr.users = users;
  }
  constexpr void addUser(BumpAlloc<> &alloc, Value *n) {
    invariant(kind == VK_Load || kind >= VK_Func);
    if (!unionPtr.users) unionPtr.users = alloc.create<UList<Value *>>(n);
    else unionPtr.users = unionPtr.users->push(alloc, n);
  }
  constexpr void removeFromUsers(Value *n) {
    invariant(kind == VK_Load || kind >= VK_Func);
    unionPtr.users->eraseUnordered(n);
  }

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
  [[nodiscard]] inline auto getInstruction() -> llvm::Instruction *;

  [[nodiscard]] inline auto getType() const -> llvm::Type *;
  [[nodiscard]] inline auto getType(unsigned) const -> llvm::Type *;
  [[nodiscard]] inline auto getOperands() -> math::PtrVector<Value *>;
};

/// CVal
/// A constant value w/ respect to the loopnest.
class CVal : public Value {

public:
  constexpr CVal(llvm::Value *v) : Value(VK_CVal) { unionPtr.val = v; }
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_CVal;
  }

  [[nodiscard]] constexpr auto getValue() const -> llvm::Value * {
    return unionPtr.val;
  }
  [[nodiscard]] auto getType() const -> llvm::Type * {
    return getValue()->getType();
  }
};
/// Cnst
class Cnst : public Value {

protected:
  constexpr Cnst(ValKind kind, llvm::Type *t) : Value(kind) {
    unionPtr.typ = t;
  }

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cint || v->getKind() == VK_Cflt;
  }
  [[nodiscard]] constexpr auto getType() const -> llvm::Type * {
    return unionPtr.typ;
  }
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
  static constexpr auto create(BumpAlloc<> &alloc, int64_t v, llvm::Type *t)
    -> Cint * {
    return alloc.create<Cint>(v, t);
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
  static constexpr auto create(BumpAlloc<> &alloc, double v, llvm::Type *t)
    -> Cflt * {
    return alloc.create<Cflt>(v, t);
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
  static constexpr auto create(BumpAlloc<> &alloc, llvm::ConstantInt *v,
                               llvm::Type *t) -> Bint * {
    return alloc.create<Bint>(v, t);
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
  static constexpr auto create(BumpAlloc<> &alloc, llvm::ConstantFP *v,
                               llvm::Type *t) -> Bflt * {
    return alloc.create<Bflt>(v, t);
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
