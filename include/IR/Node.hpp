#pragma once

#include "Utilities/Allocators.hpp"
#include <Math/Array.hpp>
#include <cstdint>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>

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
    VK_Stow,
    VK_Exit,
    VK_Loop,
    VK_CVal,
    VK_Cint,
    VK_Cflt,
    VK_Intr,
    VK_Func
  };

private:
  Node *prev{nullptr};
  Node *next{nullptr};
  Node *parent{nullptr};
  Node *child{nullptr};
  const ValKind kind;

protected:
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
  constexpr void setNext(Node *n) { next = n; }
  constexpr void setPrev(Node *n) { prev = n; }
  constexpr void setChild(Node *n) { child = n; }
  constexpr void setParent(Node *n) { parent = n; }
  constexpr void setDepth(unsigned d) { depth = d; }
};

class Loop;
/// Exit
/// child is next loop at same level
/// parent is the loop the exit closes
/// subExit is the exit of the last subloop
class Exit : public Node {
  Exit *subExit{nullptr};

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

public:
  Loop(Exit *e, unsigned d) : Node(VK_Loop, d), exit(e) {
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
    return exit->getNextLoop();
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
  static constexpr auto createTopLevel(BumpAlloc<> &alloc) -> Loop * {
    auto *E = alloc.create<Exit>(0);
    auto *L = alloc.create<Loop>(E, 0);
    return L;
  }
  constexpr auto addSubLoop(BumpAlloc<> &alloc) -> Loop * {
    auto d = getDepth() + 1;
    auto *E = alloc.create<Exit>(d);
    auto *L = alloc.create<Loop>(E, d);
    L->setParent(this);
    if (auto *eOld = exit->getSubExit()) {
      invariant(getChild() != nullptr);
      eOld->setNextLoop(L);
    } else setChild(L);
    exit->setSubExit(E);
    return L;
  }
  constexpr auto addOuterLoop(BumpAlloc<> &alloc) -> Loop * {
    // NOTE: we need to correctly set depths later
    auto *E = alloc.create<Exit>(0);
    auto *L = alloc.create<Loop>(E, 0);
    setParent(L);
    invariant(getNextLoop() == nullptr);
    E->setSubExit(exit);
    return L;
  }
  constexpr auto addNextLoop(BumpAlloc<> &alloc) -> Loop * {
    auto d = getDepth() + 1;
    auto *E = alloc.create<Exit>(d);
    auto *L = alloc.create<Loop>(E, d);
    invariant(exit->getNextLoop() == nullptr);
    exit->setNextLoop(L);
    if (auto *p = getOuterLoop()) {
      invariant(p->getChild() == this);
      L->setParent(p);
      p->getExit()->setSubExit(E);
    }
    return L;
  }
};
constexpr void Exit::setNextLoop(Loop *L) { setChild(L); }
constexpr auto Exit::getLoop() const -> Loop * {
  return static_cast<Loop *>(getParent());
}
constexpr auto Exit::getNextLoop() const -> Loop * {
  return static_cast<Loop *>(getChild());
}

/// CVal
/// A constant value w/ respect to the loopnest.
class CVal : public Node {
  llvm::Value *val;

public:
  constexpr CVal(llvm::Value *v) : Node(VK_CVal), val(v) {}
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_CVal;
  }

  [[nodiscard]] constexpr auto getVal() const -> llvm::Value * { return val; }
};
/// Cnst
class Cnst : public Node {

  llvm::Type *ty;

protected:
  constexpr Cnst(ValKind kind, llvm::Type *t) : Node(kind), ty(t) {}

public:
  static constexpr auto classof(const Node *v) -> bool {
    return v->getKind() == VK_Cint || v->getKind() == VK_Cflt;
  }
  [[nodiscard]] constexpr auto getTy() const -> llvm::Type * { return ty; }
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
