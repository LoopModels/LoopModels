#pragma once

#include <Math/Array.hpp>
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

class Val {
public:
  enum ValKind {
    VK_Load,
    VK_Stow,
    VK_Exit,
    VK_Loop,
    VK_CVal,
    VK_Intr,
    VK_Func
  };

private:
  Val *prev{nullptr};
  Val *next{nullptr};
  Val *parent{nullptr};
  Val *child{nullptr};
  unsigned depth{0};
  const ValKind kind;

protected:
  constexpr Val(ValKind kind) : kind(kind) {}

public:
  [[nodiscard]] constexpr auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] constexpr auto getDepth() const -> unsigned { return depth; }
  [[nodiscard]] constexpr auto getParent() const -> Val * { return parent; }
  [[nodiscard]] constexpr auto getChild() const -> Val * { return child; }
  [[nodiscard]] constexpr auto getPrev() const -> Val * { return prev; }
  [[nodiscard]] constexpr auto getNext() const -> Val * { return next; }
};

class Loop : public Val {
public:
  Loop() : Val(VK_Loop) {}
  static constexpr auto classof(const Val *v) -> bool {
    return v->getKind() == VK_Loop;
  }
};
class Exit : public Val {
public:
  Exit() : Val(VK_Exit) {}
  static constexpr auto classof(const Val *v) -> bool {
    return v->getKind() == VK_Exit;
  }
};
/// CVal
/// A constant value w/ respect to the loopnest.
class CVal : public Val {
  llvm::Value *val;

public:
  constexpr CVal(llvm::Value *v) : Val(VK_CVal), val(v) {}
  static constexpr auto classof(const Val *v) -> bool {
    return v->getKind() == VK_CVal;
  }

  [[nodiscard]] constexpr auto getVal() const -> llvm::Value * { return val; }
};
