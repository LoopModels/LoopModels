#pragma once

#include "./BitSets.hpp"
#include "./Instruction.hpp"
#include "./Loops.hpp"
#include "./MemoryAccess.hpp"
#include <cstddef>
#include <iterator>
#include <limits>
#include <llvm/ADT/DenseMap.h>
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
  [[no_unique_address]] llvm::Loop *loop;
  [[no_unique_address]] llvm::SmallVector<LoopTree *> subLoops;
  // length number of sub loops + 1
  // - this loop's header to first loop preheader
  // - first loop's exit to next loop's preheader...
  // - etc
  // - last loop's exit to this loop's latch

  // in addition to requiring simplify form, we require a single exit block
  [[no_unique_address]] llvm::SmallVector<Predicate::Map> paths;
  [[no_unique_address]] AffineLoopNest<true> affineLoop;
  [[no_unique_address]] LoopTree *parentLoop{nullptr};
  [[no_unique_address]] llvm::SmallVector<MemoryAccess, 0> memAccesses{};

  auto getPaths() -> llvm::MutableArrayRef<Predicate::Map> { return paths; }
  auto getPaths() const -> llvm::ArrayRef<Predicate::Map> { return paths; }
  auto getSubLoops() -> llvm::MutableArrayRef<LoopTree *> { return subLoops; }
  auto getSubLoops() const -> llvm::ArrayRef<LoopTree *> { return subLoops; }
  [[nodiscard]] auto isLoopSimplifyForm() const -> bool {
    return loop->isLoopSimplifyForm();
  }
  // mostly to get a loop to print
  [[nodiscard]] auto getOuterLoop() const -> llvm::Loop * {
    if (loop) return loop;
    for (auto *subLoop : subLoops)
      if (auto *L = subLoop->getOuterLoop()) return L;
    return nullptr;
  }
  LoopTree(const LoopTree &) = default;
  LoopTree(LoopTree &&) = default;
  auto operator=(const LoopTree &) -> LoopTree & = default;
  auto operator=(LoopTree &&) -> LoopTree & = default;
  LoopTree(llvm::SmallVector<LoopTree *> sL,
           llvm::SmallVector<Predicate::Map> paths)
    : loop(nullptr), subLoops(std::move(sL)), paths(std::move(paths)) {}

  LoopTree(llvm::Loop *L, const llvm::SCEV *BT, llvm::ScalarEvolution &SE,
           Predicate::Map paths)
    : loop(L), paths({std::move(paths)}), affineLoop(L, BT, SE) {}

  LoopTree(llvm::Loop *L, AffineLoopNest<true> aln,
           llvm::SmallVector<LoopTree *> sL,
           llvm::SmallVector<Predicate::Map> paths)
    : loop(L), subLoops(std::move(sL)), paths(std::move(paths)),
      affineLoop(std::move(aln)) {
#ifndef NDEBUG
    if (loop)
      for (auto &&chain : paths)
        for (auto &&pbb : chain) assert(loop->contains(pbb.first));
#endif
  }
  [[nodiscard]] auto getNumLoops() const -> size_t {
    return affineLoop.getNumLoops();
  }

  friend inline auto operator<<(llvm::raw_ostream &os, const LoopTree &tree)
    -> llvm::raw_ostream & {
    if (tree.loop) os << (*tree.loop) << "\n" << tree.affineLoop << "\n";
    else os << "top-level:\n";
    for (auto branch : tree.subLoops) os << *branch;
    return os << "\n";
  }
  // NOLINTNEXTLINE(*-nodiscard)
  auto dump() const -> llvm::raw_ostream & { return llvm::errs() << *this; }
  void addZeroLowerBounds(llvm::DenseMap<llvm::Loop *, LoopTree *> &loopMap) {
    affineLoop.addZeroLowerBounds();
    for (auto tree : subLoops) {
      tree->addZeroLowerBounds(loopMap);
      tree->parentLoop = this;
    }
    if (loop) loopMap.insert(std::make_pair(loop, this));
  }
  auto begin() { return subLoops.begin(); }
  auto end() { return subLoops.end(); }
  [[nodiscard]] auto begin() const { return subLoops.begin(); }
  [[nodiscard]] auto end() const { return subLoops.end(); }
  [[nodiscard]] auto size() const -> size_t { return subLoops.size(); }

  static void split(llvm::BumpPtrAllocator &alloc,
                    llvm::SmallVectorImpl<LoopTree *> &trees,
                    llvm::SmallVectorImpl<Predicate::Map> &paths,
                    llvm::SmallVectorImpl<LoopTree *> &subTree) {
    if (subTree.size()) {
      assert(1 + subTree.size() == paths.size());
      auto *newTree =
        new (alloc) LoopTree{std::move(subTree), std::move(paths)};
      trees.push_back(newTree);
      subTree.clear();
    }
    paths.clear();
  }
  void dumpAllMemAccess() const {
    llvm::errs() << "dumpAllMemAccess for ";
    if (loop) llvm::errs() << *loop << "\n";
    else llvm::errs() << "toplevel\n";
    for (auto &mem : memAccesses) llvm::errs() << "mem = " << mem << "\n";
    for (auto sL : subLoops) sL->dumpAllMemAccess();
  }
};
