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
///  8.   // VK_Block
///  9.   y2 = y*y
/// 10.   for j in J // VK_Loop
/// 11.     z = c[j,i]
/// 12.     e = bar(z, y2)
/// 13.     f = a[i]
/// 14.     g = baz(e, f)
/// 15.     a[i] = g
/// 16.   // VK_block
/// 17.   z = a[i]
/// 18.   e = p[]
/// 19.   f = z + e
/// 20.   p[] = f
/// 21. // VK_block
/// 22. z = p[]
/// 23. e = z*z
/// 24. p[] = z
///
/// At a level, first loops:
/// parents: [ parentLoop ] (depth differs)
/// Second loop:
/// parents [ prev end blck ]
///
/// VK_Block's parents: [ LoopStart, all addr within the loop ]
///
/// Addr's parents (function of level): [ parent loop, loads that come first]
/// Instr's parents: [ addrs... ]
///
/// Children are reverse:
/// Loop's: [ first sub loop (if it has one), matching blck ]
/// Blck's: [ nnext sub loop (if it has one), outer subloop ]
/// Addr's: [ instrs... ]
/// Inst's: [ instrs... ]
///
/// To iterate over subloops, SubLoopIterator
class Val {
public:
  enum ValKind { VK_Load, VK_Store, VK_Instr, VK_Block, VK_Loop };

private:
  ValKind kind;
  unsigned numParents;
  unsigned numChildren;
  unsigned depth;
  Val *next;

#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  alignas(int64_t) char mem[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif
public:
  [[nodiscard]] auto getKind() const -> ValKind { return kind; }
  [[nodiscard]] auto parents() -> MutPtrVector<Val *> {
    void *p = mem;
    return {(Val **)p, numParents};
  }
  [[nodiscard]] auto parents() const -> PtrVector<Val *> {
    const void *p = mem;
    return {(Val **)p, numParents};
  }
  [[nodiscard]] auto children() -> MutPtrVector<Val *> {
    void *p = mem;
    return {((Val **)p) + numParents, numChildren};
  }
  [[nodiscard]] auto children() const -> PtrVector<Val *> {
    const void *p = mem;
    return {((Val **)p) + numParents, numChildren};
  }
};
