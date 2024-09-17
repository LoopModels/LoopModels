#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <llvm/Support/Casting.h>

#ifndef USE_MODULE
#include "IR/Node.cxx"
#include "IR/Instruction.cxx"
#include "Dicts/Dict.cxx"
#include "IR/Address.cxx"
#include "Utilities/ListRanges.cxx"
#else
export module IR:TreeResult;
import ListRange;
import :Address;
import :Dict;
import :Instruction;
import :Node;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using dict::map;

/// Uses `origNext` to create a chain
/// `child` and `parent` are used for jumping first/last stow
struct AddrChain {
  /// `Addr`s, sorted `[stow..., load...]`
  /// stow's `getChild()` points to last stow
  /// load's `getChild()` points to last load
  ///
  /// Alternatively, we could consider:
  /// stow's `getChild()` points to last stow or `nullptr`
  /// load's `getChild()` points to last load or `nullptr`
  /// `nullptr` refers to self
  /// Using `nullptr`  instead of referring to self requires
  /// an extra check, but it makes debugging easier
  /// as we can avoid ever storing cycles.
  Addr *addr{nullptr};

  // we accumulate `maxDepth` as we go
  // Newly constructed addrs have enough space for the max depth,
  // so we can resize mostly in place later.
  // we have all addr in a line
  constexpr void addAddr(Addr *A) {
    if (!addr || addr->isLoad()) addr = A->prependOrigAddr(addr);
    else getLastStore()->insertNextAddr(A);
    if (A->isLoad()) {
      Addr *L = A->getNextAddr();
      A->setChild(L ? L->getChild() : A);
    } else addr->setChild(A);
  }
  [[nodiscard]] constexpr auto getAddr() const {
    return utils::ListRange(static_cast<Addr *>(addr), NextAddrChain{});
  }
  [[nodiscard]] constexpr auto getLoads() const {
    return utils::ListRange(getFirstLoad(), NextAddrChain{});
  }
  struct GetStores {
    static constexpr auto operator()(Addr *A) -> Addr * {
      Addr *W = A->getNextAddr();
      return (W && W->isStore()) ? W : nullptr;
    }
  };
  [[nodiscard]] constexpr auto getStores() const {
    Addr *S = (addr && addr->isStore()) ? addr : nullptr;
    return utils::ListRange(S, GetStores{});
    // return utils::ListRange(S, [](Addr *A) -> Addr * {
    //   Addr *W = A->getNextAddr();
    //   return (W && W->isStore()) ? W : nullptr;
    // });
  }
  constexpr auto operator*=(AddrChain other) -> AddrChain & {
    if (other.addr) {
      if (addr && addr->isStore()) {
        // [this_stow..., other..., this_load...]
        Addr *LS = getLastStore(), *FL = LS->getNextAddr();
        LS->setNextAddr(other.addr);
        other.getLastAddr()->setNextAddr(FL);
      } else {
        // [other..., this_load...]
        other.getLastAddr()->setNextAddr(addr);
        addr = other.addr;
      }
    }
    return *this;
  }
  /// Note: this is used at a time where `getLoads()` and `getStores()` are no
  /// longer valid, as we have used `getChild()` for the IR graph structure,
  /// i.e. to point to sub-loops.
  constexpr void removeDropped() {
    Addr *a = addr;
    for (; a && a->wasDropped();) addr = a = a->getNextAddr();
    Addr *b = a->getNextAddr();
    for (; b;) {
      for (; b->wasDropped();) {
        b = b->getNextAddr();
        if (!b) {
          a->setNextAddr(nullptr);
          return;
        }
      }
      a->setNextAddr(b);
      a = b;
      b = a->getNextAddr();
    }
  }

private:
  [[nodiscard]] constexpr auto getFirstStore() const -> Addr * {
    return (addr && addr->isStore()) ? addr : nullptr;
  }
  [[nodiscard]] constexpr auto getLastStore() const -> Addr * {
    if (!addr || addr->isLoad()) return nullptr;
    return llvm::cast<Addr>(addr->getChild());
  }
  [[nodiscard]] constexpr auto getFirstLoad() const -> Addr * {
    if (!addr || addr->isLoad()) return addr;
    return llvm::cast<Addr>(addr->getChild())->getNextAddr();
  }
  [[nodiscard]] constexpr auto getLastLoad() const -> Addr * {
    Addr *L = getFirstLoad();
    return L ? llvm::cast<Addr>(L->getChild()) : nullptr;
  }
  [[nodiscard]] constexpr auto getLastAddr() const -> Addr * {
    if (!addr) return nullptr;
    // if (addr->isLoad()) return llvm::cast<Addr>(addr->getChild());
    Addr *C = llvm::cast<Addr>(addr->getChild());
    if (C->isLoad()) return C;
    Addr *L = C->getNextAddr();
    return L ? llvm::cast<Addr>(L->getChild()) : C;
  }
  struct NextAddrChain {
    constexpr auto operator()(Addr *A) const -> Addr * {
      return A->getNextAddr();
    }
  };
};

/// The `TreeResult` gives the result of parsing a loop tree.
/// The purpose of the `TreeResult` is to accumulate results while
/// building the loop tree, in particular, the `Addr`s so far,
/// the incomplete instructions we must complete as we move out,
/// and how many outer loop layers we are forced to reject.
///
/// We parse `Addr`s specifically inside the `TurboLoop` parse block function,
/// and add the appropriate `omega` value then.
///
/// Fields:
/// - `Addr* load`: a linked list giving the loads of the loop tree.
/// These contain ordering information, which is enough for the linear program
/// to deduce the orders of memory accesses, and perform an analysis.
/// Note that adding loads and stores always pushes to the front.
/// Thus, old `TreeResult`s are not invalidated; they just start at the middle
/// of the grown list.
/// - `Addr* stow`: same as load, but for stores.
/// - 'Instruction* incomplete': a linked list giving the nodes that we
/// stopped exploring due to not being inside the loop nest, and thus may need
/// to have their parents filled out.
/// - `size_t rejectDepth` is how many outer loops were rejected, due to to
/// failure to produce an affine representation of the loop or memory
/// accesses. Either because an affine representation is not possible, or
/// because our analysis failed and needs improvement.
///
/// We use setChild for setting the last load/stow/incomplete
/// only the very first is guaranteed to be correct, as we
/// do not update the old when concatenating
struct TreeResult {
  AddrChain addr{};
  Compute *incomplete{nullptr};
  int rejectDepth{0};
  int maxDepth{0};
  [[nodiscard]] constexpr auto reject(int depth) const -> bool {
    return (depth < rejectDepth) || (addr.addr == nullptr);
  }
  [[nodiscard]] constexpr auto accept(int depth) const -> bool {
    // depth >= rejectDepth && stow != nullptr
    return !reject(depth);
  }
  constexpr void addIncomplete(Compute *I) {
    Node *last = incomplete ? incomplete->getChild() : I;
    incomplete = static_cast<Compute *>(I->setNext(incomplete));
    I->setChild(last);
  }
  constexpr void addAddr(Addr *A) { addr.addAddr(A); }
  [[nodiscard]] constexpr auto getAddr() const { return addr.getAddr(); }
  [[nodiscard]] constexpr auto getLoads() const { return addr.getLoads(); }
  [[nodiscard]] constexpr auto getStores() const { return addr.getStores(); }
  void setLoopNest(Valid<poly::Loop> L) const {
    for (Addr *A : getAddr()) A->setLoopNest(L);
  }
  constexpr auto operator*=(TreeResult tr) -> TreeResult & {
    addr *= tr.addr;
    incomplete = concatenate(incomplete, tr.incomplete);
    rejectDepth = std::max(rejectDepth, tr.rejectDepth);
    return *this;
  }

  [[nodiscard]] constexpr auto getLoop() const -> poly::Loop * {
    return (addr.addr) ? addr.addr->getAffineLoop() : nullptr;
  }
  [[nodiscard]] constexpr auto getMaxDepth() const -> int {
    invariant(maxDepth >= rejectDepth);
    return maxDepth - rejectDepth;
  }

private:
  static constexpr auto concatenate(Compute *A, Compute *B) -> Compute * {
    if (!A) return B;
    if (!B) return A;
    A->getChild()->setNext(B);
    A->setChild(B->getChild());
    return A;
  }
};

} // namespace IR