#pragma once

#include "./Instruction.hpp"
#include "./Loops.hpp"
#include "./MemoryAccess.hpp"
#include "Containers/BitSets.hpp"
#include "Utilities/Valid.hpp"
#include <cstddef>
#include <iterator>
#include <limits>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>
#include <vector>

struct LoopTree {
  [[no_unique_address]] llvm::Loop *loop{nullptr};
  [[no_unique_address]] llvm::SmallVector<NotNull<LoopTree>> subLoops;
  // length number of sub loops + 1
  // - this loop's header to first loop preheader
  // - first loop's exit to next loop's preheader...
  // - etc
  // - last loop's exit to this loop's latch

  // in addition to requiring simplify form, we require a single exit block
  [[no_unique_address]] llvm::SmallVector<Predicate::Map> paths;
  [[no_unique_address]] AffineLoopNest<true> *affineLoop{nullptr};
  [[no_unique_address]] Optional<LoopTree *> parentLoop{nullptr};
  [[no_unique_address]] llvm::SmallVector<NotNull<MemoryAccess>> memAccesses{};

  ~LoopTree() { // NOLINT(misc-no-recursion)
    for (auto subLoop : subLoops) subLoop->~LoopTree();
  }

  auto getPaths() -> llvm::MutableArrayRef<Predicate::Map> { return paths; }
  auto getPaths() const -> llvm::ArrayRef<Predicate::Map> { return paths; }
  auto getSubLoops() -> llvm::MutableArrayRef<NotNull<LoopTree>> {
    return subLoops;
  }
  auto getSubLoops() const -> llvm::ArrayRef<NotNull<LoopTree>> {
    return subLoops;
  }
  [[nodiscard]] auto isLoopSimplifyForm() const -> bool {
    return loop->isLoopSimplifyForm();
  }
  // mostly to get a loop to print
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto getOuterLoop() const -> llvm::Loop * {
    if (loop) return loop;
    for (auto subLoop : subLoops)
      if (auto *L = subLoop->getOuterLoop()) return L;
    return nullptr;
  }
  // LoopTree(const LoopTree &) = default;
  // LoopTree(LoopTree &&) = default;
  auto operator=(const LoopTree &) -> LoopTree & = delete;
  auto operator=(LoopTree &&) -> LoopTree & = delete;
  LoopTree(llvm::SmallVector<NotNull<LoopTree>> sL,
           llvm::SmallVector<Predicate::Map> pth)
    : subLoops(std::move(sL)), paths(std::move(pth)) {}

  LoopTree(BumpAlloc<> &alloc, llvm::Loop *L, const llvm::SCEV *BT,
           llvm::ScalarEvolution &SE, Predicate::Map pth)
    : loop(L), affineLoop{AffineLoopNest<true>::construct(alloc, L, BT, SE)} {
    paths.push_back(std::move(pth));
  }

  LoopTree(BumpAlloc<> &alloc, llvm::Loop *L, NotNull<AffineLoopNest<true>> aln,
           llvm::SmallVector<NotNull<LoopTree>> sL,
           llvm::SmallVector<Predicate::Map> pth)
    : loop(L), subLoops(std::move(sL)), paths(std::move(pth)),
      affineLoop(aln->copy(alloc)) {
#ifndef NDEBUG
    if (loop)
      for (auto &&chain : pth)
        for (auto &&pbb : chain) assert(loop->contains(pbb.first));
#endif
  }
  [[nodiscard]] auto getNumLoops() const -> size_t {
    return affineLoop->getNumLoops();
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  friend inline auto operator<<(llvm::raw_ostream &os, const LoopTree &tree)
    -> llvm::raw_ostream & {
    if (tree.loop) os << (*tree.loop) << "\n" << *tree.affineLoop << "\n";
    else os << "top-level:\n";
    for (auto branch : tree.subLoops) os << *branch;
    return os << "\n";
  }
#ifndef NDEBUG
  [[gnu::used]] void dump() const { llvm::errs() << *this; }
#endif
  // NOLINTNEXTLINE(misc-no-recursion)
  void addZeroLowerBounds(BumpAlloc<> &alloc,
                          map<llvm::Loop *, LoopTree *> &loopMap) {
    if (affineLoop) affineLoop->addZeroLowerBounds(alloc);
    for (auto tree : subLoops) {
      tree->addZeroLowerBounds(alloc, loopMap);
      tree->parentLoop = this;
    }
    if (loop) loopMap.insert(std::make_pair(loop, this));
  }
  [[nodiscard]] auto begin() { return subLoops.begin(); }
  [[nodiscard]] auto end() { return subLoops.end(); }
  [[nodiscard]] auto begin() const { return subLoops.begin(); }
  [[nodiscard]] auto end() const { return subLoops.end(); }
  [[nodiscard]] auto size() const -> size_t { return subLoops.size(); }

  static void split(BumpAlloc<> &alloc,
                    llvm::SmallVector<NotNull<LoopTree>> &trees,
                    llvm::SmallVector<Predicate::Map> &paths,
                    llvm::SmallVector<NotNull<LoopTree>> &subTree) {
    if (!subTree.empty()) {
      assert(1 + subTree.size() == paths.size());
      auto *newTree =
        new (alloc) LoopTree{std::move(subTree), std::move(paths)};
      trees.push_back(newTree);
      subTree.clear();
    }
    paths.clear();
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  void dumpAllMemAccess() const {
    llvm::errs() << "dumpAllMemAccess for ";
    if (loop) llvm::errs() << *loop << "\n";
    else llvm::errs() << "toplevel\n";
    for (const auto &mem : memAccesses) llvm::errs() << "mem = " << mem << "\n";
    for (auto sL : subLoops) sL->dumpAllMemAccess();
  }
};
