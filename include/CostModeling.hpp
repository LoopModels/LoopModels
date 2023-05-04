#pragma once

#include "./ControlFlowMerging.hpp"
#include "./Instruction.hpp"
#include "./LoopBlock.hpp"
#include "./LoopForest.hpp"
#include "./MemoryAccess.hpp"
#include "./Schedule.hpp"
#include "Address.hpp"
#include "DependencyPolyhedra.hpp"
#include "Graphs.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Utilities/Allocators.hpp"
#include <algorithm>
#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <utility>

namespace CostModeling {

struct CPURegisterFile {
  [[no_unique_address]] uint8_t maximumVectorWidth;
  [[no_unique_address]] uint8_t numVectorRegisters;
  [[no_unique_address]] uint8_t numGeneralPurposeRegisters;
  [[no_unique_address]] uint8_t numPredicateRegisters;

  // hacky check for has AVX512
  static inline auto hasAVX512(llvm::LLVMContext &C,
                               llvm::TargetTransformInfo &TTI) -> bool {
    return TTI.isLegalMaskedExpandLoad(
      llvm::FixedVectorType::get(llvm::Type::getDoubleTy(C), 8));
  }

  static auto estimateNumPredicateRegisters(llvm::LLVMContext &C,
                                            llvm::TargetTransformInfo &TTI)
    -> uint8_t {
    if (TTI.supportsScalableVectors()) return 8;
    // hacky check for AVX512
    if (hasAVX512(C, TTI)) return 7; // 7, because k0 is reserved for unmasked
    return 0;
  }
  // returns vector width in bits
  static auto estimateMaximumVectorWidth(llvm::LLVMContext &C,
                                         llvm::TargetTransformInfo &TTI)
    -> uint8_t {
    uint8_t twiceMaxVectorWidth = 2;
    auto *f32 = llvm::Type::getFloatTy(C);
    llvm::InstructionCost prevCost = TTI.getArithmeticInstrCost(
      llvm::Instruction::FAdd,
      llvm::FixedVectorType::get(f32, twiceMaxVectorWidth));
    while (true) {
      llvm::InstructionCost nextCost = TTI.getArithmeticInstrCost(
        llvm::Instruction::FAdd,
        llvm::FixedVectorType::get(f32, twiceMaxVectorWidth *= 2));
      if (nextCost > prevCost) break;
      prevCost = nextCost;
    }
    return 16 * twiceMaxVectorWidth;
  }
  constexpr CPURegisterFile(llvm::LLVMContext &C,
                            llvm::TargetTransformInfo &TTI) {
    maximumVectorWidth = estimateMaximumVectorWidth(C, TTI);
    numVectorRegisters = TTI.getNumberOfRegisters(true);
    numGeneralPurposeRegisters = TTI.getNumberOfRegisters(false);
    numPredicateRegisters = estimateNumPredicateRegisters(C, TTI);
  }
};
// struct CPUExecutionModel {};

// Plan for cost modeling:
// 1. Build Instruction graph
// 2. Iterate over all PredicatedChains, merging instructions across branches
// where possible
// 3. Create a loop tree structure for optimization
// 4. Create InstructionBlocks at each level.

// void pushBlock(llvm::SmallPtrSet<llvm::Instruction *, 32> &trackInstr,
//                llvm::SmallPtrSet<llvm::BasicBlock *, 32> &chainBBs,
//                Predicates &pred, llvm::BasicBlock *BB) {
//     assert(chainBBs.contains(block));
//     chainBBs.erase(BB);
//     // we only want to extract relevant instructions, i.e. parents of
//     stores for (llvm::Instruction &instr : *BB) {
//         if (trackInstr.contains(&instr))
//             instructions.emplace_back(pred, instr);
//     }
//     llvm::Instruction *term = BB->getTerminator();
//     if (!term)
//         return;
//     switch (term->getNumSuccessors()) {
//     case 0:
//         return;
//     case 1:
//         BB = term->getSuccessor(0);
//         if (chainBBs.contains(BB))
//             pushBlock(trackInstr, chainBBs, pred, BB);
//         return;
//     case 2:
//         break;
//     default:
//         assert(false);
//     }
//     auto succ0 = term->getSuccessor(0);
//     auto succ1 = term->getSuccessor(1);
//     if (chainBBs.contains(succ0) && chainBBs.contains(succ1)) {
//         // TODO: we need to fuse these blocks.

//     } else if (chainBBs.contains(succ0)) {
//         pushBlock(trackInstr, chainBBs, pred, succ0);
//     } else if (chainBBs.contains(succ1)) {
//         pushBlock(trackInstr, chainBBs, pred, succ1);
//     }
// }

/// Given: llvm::SmallVector<LoopAndExit> subTrees;
/// subTrees[i].second is the preheader for
/// subTrees[i+1].first, which has exit block
/// subTrees[i+1].second

/// Initialized from a LoopBlock
/// First, all memory accesses are placed.
///  - Topologically sort at the same level
///  - Hoist out as far as posible
/// Then, merge eligible loads.
///  - I.e., merge loads that are in the same block with same address and not
///  aliasing stores in between
/// Finally, place instructions, seeded by stores, hoisted as far out as
/// possible. With this, we can begin cost modeling.
class LoopTreeSchedule {
  template <typename T> using Vec = LinAlg::ResizeableView<T, unsigned>;

public:
  struct AddressGraph {
    using VertexType = Address;
    [[no_unique_address]] MutPtrVector<Address *> addresses;
    [[no_unique_address]] unsigned depth;
    [[nodiscard]] auto getNumVertices() const -> size_t {
      return addresses.size();
    }
    auto inNeighbors(size_t i) { return addresses[i]->inNeighbors(depth); }
    auto outNeighbors(size_t i) { return addresses[i]->outNeighbors(depth); }
    [[nodiscard]] auto inNeighbors(size_t i) const {
      return addresses[i]->inNeighbors(depth);
    }
    [[nodiscard]] auto outNeighbors(size_t i) const {
      return addresses[i]->outNeighbors(depth);
    }
    [[nodiscard]] auto wasVisited(size_t i) const -> bool {
      return addresses[i]->wasVisited();
    }
    void visit(size_t i) { addresses[i]->visit(); }
    void unVisit(size_t i) { addresses[i]->unVisit(); }
    void clearVisited() {
      for (auto *a : addresses) a->unVisit();
    }
  };

private:
  template <typename T>
  static constexpr auto realloc(BumpAlloc<> &alloc, Vec<T> vec, unsigned nc)
    -> Vec<T> {
    return Vec<T>(alloc.reallocate<false>(vec.data(), vec.getCapacity(), nc),
                  vec.size(), nc);
  }
  template <typename T>
  static constexpr auto grow(BumpAlloc<> &alloc, Vec<T> vec, unsigned sz)
    -> Vec<T> {
    if (unsigned C = vec.getCapacity(); C < sz) {
      T *p = alloc.allocate<T>(sz + sz);
      for (unsigned i = 0; i < vec.size(); ++i) p[i] = std::move(vec[i]);
      return Vec<T>(p, sz, sz + sz);
    }
    vec.resizeForOverwrite(sz);
    return vec;
  }

  class InstructionBlock {
    Address **addresses{nullptr};
    unsigned numAddr{0};
    unsigned capacity{0};
    // [[no_unique_address]] Vec<Address *> memAccesses{};
  public:
    [[nodiscard]] constexpr auto isInitialized() const -> bool {
      return addresses;
    }
    [[nodiscard]] constexpr auto getAddr() -> Vec<Address *> {
      return {addresses, numAddr, capacity};
    }
    [[nodiscard]] constexpr auto size() const -> unsigned { return numAddr; }
    [[nodiscard]] constexpr auto getCapacity() const -> unsigned {
      return capacity;
    }
    [[nodiscard]] constexpr auto operator[](unsigned i) const -> Address * {
      invariant(i < numAddr);
      invariant(i < capacity);
      return addresses[i];
    }
    /// add space for `i` extra slots
    constexpr void reserveExtra(BumpAlloc<> &alloc, unsigned i) {
      unsigned oldCapacity = std::exchange(capacity, capacity + i);
      addresses = alloc.reallocate<false>(addresses, oldCapacity, capacity);
    }
    constexpr void initialize(BumpAlloc<> &alloc) {
      addresses = alloc.allocate<Address *>(capacity);
    }
    constexpr void push_back(Address *addr) {
      invariant(numAddr < capacity);
      addresses[numAddr++] = addr;
    }
    constexpr void push_back(BumpAlloc<> &alloc, Address *addr) {
      if (numAddr >= capacity)
        reserveExtra(alloc, std::max<unsigned>(4, numAddr));
      addresses[numAddr++] = addr;
    }
    constexpr void incNumAddr(unsigned x) { numAddr += x; }
    [[nodiscard]] constexpr auto try_delete(Address *adr) -> bool {
      for (size_t i = 0; i < numAddr; ++i) {
        if (addresses[i] == adr) {
          addresses[i] = addresses[--numAddr];
          return true;
        }
      }
      return false;
    }
  };
  struct LoopAndExit {
    [[no_unique_address]] LoopTreeSchedule *subTree;
    [[no_unique_address]] InstructionBlock exit{};
    constexpr LoopAndExit(LoopTreeSchedule *subTree) : subTree(subTree) {}
    static constexpr auto construct(BumpAlloc<> &alloc, LoopTreeSchedule *L,
                                    uint8_t d) {
      return LoopAndExit(alloc.create<LoopTreeSchedule>(L, d));
    }
  };
  /// Header of the loop.
  [[no_unique_address]] InstructionBlock header{};
  /// Variable number of sub loops and their associated exits.
  /// For the inner most loop, `subTrees.empty()`.
  [[no_unique_address]] Vec<LoopAndExit> subTrees{};
  [[no_unique_address]] LoopTreeSchedule *parent{nullptr};
  [[no_unique_address]] uint8_t depth;
  [[no_unique_address]] uint8_t vectorizationFactor{1};
  [[no_unique_address]] uint8_t unrollFactor{1};
  [[no_unique_address]] uint8_t unrollPredcedence{1};
  // i = 0 means self, i = 1 (default) returns parent, i = 2 parent's parent...
  constexpr auto try_delete(Address *adr) -> bool { // NOLINT(misc-no-recursion)
    if (header.try_delete(adr)) return true;
    for (auto &[subTree, exit] : subTrees) {
      if (exit.try_delete(adr)) return true;
      if (subTree->try_delete(adr)) return true;
    }
    return false;
  }

  constexpr auto getParent(size_t i) -> LoopTreeSchedule * {
    invariant(i <= depth);
    LoopTreeSchedule *p = this;
    while (i--) p = p->parent;
    return p;
  }
  constexpr void incNumAddr(unsigned x) { header.incNumAddr(x); }
  constexpr auto getParent() -> LoopTreeSchedule * { return parent; }
  [[nodiscard]] constexpr auto getNumSubTrees() const -> unsigned {
    return subTrees.size();
  }
  [[nodiscard]] constexpr auto numBlocks() const -> unsigned {
    return getNumSubTrees() + 1;
  }
  constexpr auto getLoopAndExit(BumpAlloc<> &alloc, size_t i, size_t d)
    -> LoopAndExit & {
    if (i >= subTrees.size()) {
      subTrees = grow(alloc, subTrees, i + 1);
      for (size_t j = subTrees.size(); j <= i; ++j)
        subTrees[j] = LoopAndExit::construct(alloc, this, d);
    }
    return subTrees[i];
  }
  auto getLoopTripple(size_t i)
    -> std::tuple<InstructionBlock &, LoopTreeSchedule *, InstructionBlock &> {
    auto loopAndExit = subTrees[i];
    if (i) return {subTrees[i - 1].exit, loopAndExit.subTree, loopAndExit.exit};
    return {header, loopAndExit.subTree, loopAndExit.exit};
  }
  auto getLoop(BumpAlloc<> &alloc, size_t i, uint8_t d) -> LoopTreeSchedule * {
    return getLoopAndExit(alloc, i, d).subTree;
  }
  auto getLoop(size_t i) -> LoopTreeSchedule * { return subTrees[i].subTree; }
  [[nodiscard]] auto getLoop(size_t i) const -> const LoopTreeSchedule * {
    return subTrees[i].subTree;
  }
  auto getBlock(size_t i) -> InstructionBlock & {
    if (i) return subTrees[i - 1].exit;
    return header;
  }
  [[nodiscard]] auto getBlock(size_t i) const -> const InstructionBlock & {
    if (i) return subTrees[i - 1].exit;
    return header;
  }
  /// Adds the schedule corresponding for the innermost loop.

  // this method descends
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto allocLoopNodes(BumpAlloc<> &alloc, AffineSchedule sch,
                             LoopTreeSchedule *L) -> LoopTreeSchedule * {
    auto fO = sch.getFusionOmega();
    unsigned numLoops = sch.getNumLoops();
    invariant(fO.size() - 1, numLoops);
    for (size_t i = 0; i < numLoops; ++i) L = L->getLoop(alloc, fO[i], i + 1);
    return L;
  }
  void place(BumpAlloc<> &alloc, Address *adr, unsigned to) {
    InstructionBlock *block;
    if (adr->isStore()) block = &subTrees[to].exit;
    else if (to) block = &subTrees[to - 1].exit;
    else block = &header;
    block->push_back(alloc, adr);
  }
  void hoist(BumpAlloc<> &alloc, Address *adr, LoopTreeSchedule *from,
             unsigned to) {
    place(alloc, adr, to);
    invariant(from->try_delete(adr));
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto placeAddr(BumpAlloc<> &alloc, LinearProgramLoopBlock &LB,
                 MutPtrVector<Address *> addr) -> unsigned {
    // we sort via repeatedly calculating the strongly connected components
    // of the address graph. The SCCs are in topological order.
    // If a load or store are isolated within a SCC from a sub-loop, we hoist if
    // it's indices do not depend on that loop.
    //
    // We will eventually insert all addr at this level and within sub-loops
    // into `addr`. We process them in batches, iterating based on `omega`
    // values. We only need to force separation here for those that are not
    // already separated.
    //
    unsigned numAddr = header.size(), subTreeInd{0};
    addr[_(0, numAddr)] << header.getAddr();
    for (auto &L : subTrees)
      numAddr += L.subTree->placeAddr(alloc, LB, addr[_(numAddr, end)]);
    addr = addr[_(0, numAddr)];
    auto sccs =
      Graphs::stronglyConnectedComponents(AddressGraph{addr, getDepth()});
    // sccs are in topological order
    // we need to track progress of sccs w/ respect to loops
    // are we before, w/in, between, or after a loop?
    for (auto &&scc : sccs) {
      if (scc.size() == 1) {
        Address *adr = scc[0];
        if (adr->wasPlaced()) {
          if (allZero(adr->indexMatrix()(_, getDepth())))
            hoist(alloc, adr, adr->getLoopTreeSchedule(), subTreeInd);
          ++subTreeInd;
        } else {
        }
      } else {
#ifndef NDEBUG
        for (auto &&adr : scc) assert(adr->wasPlaced());
#endif
      }
    }
    return numAddr;
  }
  template <typename T>
  static constexpr auto get(llvm::SmallVectorImpl<T> &x, size_t i) -> T & {
    if (i >= x.size()) x.resize(i + 1);
    return x[i];
  }
#ifndef NDEBUG
  void validate() { // NOLINT(misc-no-recursion)
    for (auto &subTree : subTrees) {
      assert(subTree.subTree->parent == this);
      assert(subTree.subTree->getDepth() == getDepth() + 1);
      subTree.subTree->validate();
    }
  }
#endif
  static auto init(BumpAlloc<> &alloc, LinearProgramLoopBlock &LB)
    -> LoopTreeSchedule * {
    // TODO: can we shorten the life span of the instructions we
    // allocate here to `lalloc`? I.e., do we need them to live on after
    // this forest is scheduled?

    // we first add all memory operands
    // then, we licm
    // the only kind of replication that occur are store reloads
    // size_t maxDepth = 0;
    PtrVector<ScheduledNode> lnodes = LB.getNodes();
    Vector<LoopTreeSchedule *> loops{lnodes.size()};
    LoopTreeSchedule *root{alloc.create<LoopTreeSchedule>(nullptr, 0)};
    unsigned numAddr = 0;
    for (size_t i = 0; i < lnodes.size(); ++i) {
      auto &node = lnodes[i];
      LoopTreeSchedule *L = loops[i] =
        allocLoopNodes(alloc, node.getSchedule(), root);
      unsigned numMem = node.getNumMem();
      L->incNumAddr(numMem);
      numAddr += numMem;
      node.incrementReplicationCounts(LB.getMemoryAccesses());
    }
#ifndef NDEBUG
    root->validate();
#endif
    for (size_t i = 0, j = 0; i < lnodes.size(); ++i) {
      auto &node = lnodes[i];
      auto *L = loops[i];
      size_t k = j + node.getNumMem();
      node.insertMemAccesses(alloc, LB.getMemoryAccesses(), LB.getEdges(), L);
      j = k;
    }
    // we now have a vector of addrs
    for (auto &edge : LB.getEdges()) {
      // here we add all connections to the addrs
      // edges -> MA -> Addr
      for (Address *in : edge.input()->getAddresses())
        for (Address *out : edge.output()->getAddresses())
          in->addOut(out, edge.getSatLvl()[0]);
    }
    MutPtrVector<Address *> addr{alloc.allocate<Address *>(numAddr), numAddr};
    root->placeAddr(alloc, LB, addr);
    return root;
  }
  // void initializeInstrGraph(BumpAlloc<> &alloc, Instruction::Cache &cache,
  //                           BumpAlloc<> &tAlloc, LoopTree *loopForest,
  //                           LinearProgramLoopBlock &LB,
  //                           llvm::TargetTransformInfo &TTI,
  //                           unsigned int vectorBits) {

  //   // buidInstructionGraph(alloc, cache);
  //   mergeInstructions(alloc, cache, loopForest, TTI, tAlloc, vectorBits);
  // }

public:
  [[nodiscard]] constexpr auto getInitAddr(BumpAlloc<> &alloc)
    -> Vec<Address *> {
    if (!header.isInitialized()) header.initialize(alloc);
    return header.getAddr();
  }
  [[nodiscard]] constexpr auto getDepth() const -> unsigned { return depth; }
  constexpr LoopTreeSchedule(LoopTreeSchedule *L, uint8_t d)
    : parent(L), depth(d) {}
};
constexpr auto getDepth(LoopTreeSchedule *L) -> unsigned {
  return L->getDepth();
}
constexpr auto getInitAddr(LoopTreeSchedule *L, BumpAlloc<> &alloc)
  -> LinAlg::ResizeableView<Address *, unsigned> {
  return L->getInitAddr(alloc);
}

static_assert(Graphs::AbstractPtrGraph<LoopTreeSchedule::AddressGraph>);
// class LoopForestSchedule : LoopTreeSchedule {
//   [[no_unique_address]] BumpAlloc<> &allocator;
// };
} // namespace CostModeling
