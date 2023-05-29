#pragma once
#include "ArrayIndex.hpp"
#include "Containers/BitSets.hpp"
#include "Loops.hpp"
#include "Math/Array.hpp"
#include "Math/Math.hpp"
#include "Utilities/Valid.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/Support/raw_ostream.h>

// refactor to use GraphTraits.h ??
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
/// A particular memory access instance
struct MemoryAccess {
  using BitSet = ::BitSet<std::array<uint64_t, 2>>;

private:
  NotNull<ArrayIndex> arrayRef;
  // TODO: bail out gracefully if we need more than 128 elements
  // TODO: better yet, have some mechanism for allocating more space.
  // llvm::ArrayRef<const llvm::SCEV *> sizes;
  // llvm::ArrayRef<const llvm::SCEV *> symbolicOffsets;
  // llvm::SmallVector<const llvm::SCEV *, 3> sizes;
  // llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
  // unsigned (instead of ptr) as we build up edges
  // and I don't want to relocate pointers when resizing vector
  BitSet edgesIn{};
  BitSet edgesOut{};
  Addr *addrs{nullptr};
  unsigned nodeIndex{std::numeric_limits<unsigned>::max()};
  bool load_;
  // This is a flexible length array, declared as a length-1 array
  // I wish there were some way to opt into "I'm using a c99 extension"
  // so that I could use `mem[]` or `mem[0]` instead of `mem[1]`. See:
  // https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
  // https://developers.redhat.com/articles/2022/09/29/benefits-limitations-flexible-array-members#flexible_array_members_vs__pointer_implementation
public:
  MemoryAccess(NotNull<ArrayIndex> ref)
    : arrayRef(ref), load_(ref->getLoad()) {}
  constexpr MemoryAccess(NotNull<ArrayIndex> ref, bool load)
    : arrayRef(ref), load_(load) {}

  constexpr auto operator==(const MemoryAccess &ma) const -> bool {
    return *arrayRef == *ma.arrayRef && load_ == ma.load_;
  }
  [[nodiscard]] static auto construct(BumpAlloc<> &alloc,
                                      NotNull<ArrayIndex> arrayRef, bool load)
    -> NotNull<MemoryAccess> {
    return alloc.create<MemoryAccess>(arrayRef, load);
  }
  [[nodiscard]] static auto construct(BumpAlloc<> &alloc,
                                      NotNull<ArrayIndex> arrayRef)
    -> NotNull<MemoryAccess> {
    return alloc.create<MemoryAccess>(arrayRef, arrayRef->isLoad());
  }
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest &loopRef, llvm::Instruction *user,
            PtrMatrix<int64_t> indMatT,
            std::array<llvm::SmallVector<const llvm::SCEV *, 3>, 2> szOff,
            PtrMatrix<int64_t> offsets, PtrVector<unsigned> o)
    -> NotNull<MemoryAccess> {
    return alloc.create<MemoryAccess>(ArrayIndex::construct(
      alloc, arrayPtr, loopRef, user, indMatT, std::move(szOff), offsets, o));
  }
  [[nodiscard]] static auto
  construct(BumpAlloc<> &alloc, const llvm::SCEVUnknown *arrayPtr,
            AffineLoopNest &loopRef, llvm::Instruction *user,
            PtrVector<unsigned> o) -> NotNull<MemoryAccess> {
    return alloc.create<MemoryAccess>(
      ArrayIndex::construct(alloc, arrayPtr, loopRef, user, o));
  }

  void peelLoops(size_t n) { arrayRef->peelLoops(n); }
  [[nodiscard]] constexpr auto isLoad() const -> bool { return load_; }
  [[nodiscard]] constexpr auto isStore() const -> bool { return !load_; }
  [[nodiscard]] constexpr auto getArrayRef() -> NotNull<ArrayIndex> {
    return arrayRef;
  }
  [[nodiscard]] constexpr auto getArrayRef() const
    -> NotNull<const ArrayIndex> {
    return arrayRef;
  }
  constexpr void setAddress(Addr *addr) { addrs = addr; }
  // [[nodiscard]] constexpr auto repCount() const -> size_t {
  //   return addrReplications + 1;
  // }
  // void replicateAddr() { ++addrReplications; }
  // void push_back(BumpAlloc<> &alloc, Address *addr) {
  //   if (addrReplications) {
  //     if (numAddr == 0)
  //       addrss = alloc.allocate<Address *>(addrReplications + 1);
  //     invariant(numAddr <= addrReplications);
  //     addrss[numAddr++] = addr;
  //   } else addrs = addr;
  // }
  // auto getAddresses() -> MutPtrVector<Address *> {
  //   return {addrReplications ? addrss : &addrs, addrReplications ? numAddr :
  //   1};
  // }
  [[nodiscard]] auto getInstruction() -> llvm::Instruction * {
    return arrayRef->getInstruction();
  }
  [[nodiscard]] auto getInstruction() const -> const llvm::Instruction * {
    return arrayRef->getInstruction();
  }
  [[nodiscard]] constexpr auto getLoop() const
    -> NotNull<const AffineLoopNest> {
    return arrayRef->getLoop();
  }
  [[nodiscard]] constexpr auto getLoop() -> NotNull<AffineLoopNest> {
    return arrayRef->getLoop();
  }
  [[nodiscard]] constexpr auto getArrayDim() const -> size_t {
    return arrayRef->getArrayDim();
  }
  [[nodiscard]] auto sizesMatch(const MemoryAccess &ma) const -> bool {
    return arrayRef->sizesMatch(ma.getArrayRef());
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  constexpr auto indexMatrix() -> MutDensePtrMatrix<int64_t> {
    return arrayRef->indexMatrix();
  }
  /// indexMatrix() -> arrayDim() x getNumLoops()
  [[nodiscard]] constexpr auto indexMatrix() const -> DensePtrMatrix<int64_t> {
    return arrayRef->indexMatrix();
  }
  constexpr auto offsetMatrix() -> MutDensePtrMatrix<int64_t> {
    return arrayRef->offsetMatrix();
  }
  [[nodiscard]] constexpr auto offsetMatrix() const -> DensePtrMatrix<int64_t> {
    return arrayRef->offsetMatrix();
  }
  constexpr auto getFusionOmega() -> MutPtrVector<int64_t> {
    return arrayRef->getFusionOmega();
  }
  [[nodiscard]] constexpr auto getFusionOmega() const -> PtrVector<int64_t> {
    return arrayRef->getFusionOmega();
  }
  [[nodiscard]] constexpr auto getArrayPointer() const -> const llvm::SCEV * {
    return arrayRef->getArrayPointer();
  }
  constexpr auto getAddress() -> NotNull<Addr> { return addrs; }
  [[nodiscard]] constexpr auto getNumLoops() const -> unsigned {
    return arrayRef->getNumLoops();
  }
  [[nodiscard]] constexpr auto getAddress() const -> NotNull<const Addr> {
    return addrs;
  }
  [[nodiscard]] constexpr auto inputEdges() const -> const BitSet & {
    return edgesIn;
  }
  [[nodiscard]] constexpr auto outputEdges() const -> const BitSet & {
    return edgesOut;
  }
  [[nodiscard]] constexpr auto getNode() const -> unsigned { return nodeIndex; }
  constexpr void addEdgeIn(size_t i) { edgesIn.insert(i); }
  constexpr void addEdgeOut(size_t i) { edgesOut.insert(i); }
  /// add a node index

  [[nodiscard]] auto getLoad() -> llvm::LoadInst * {
    return arrayRef->getLoad();
  }
  [[nodiscard]] auto getStore() -> llvm::StoreInst * {
    return arrayRef->getStore();
  }
  constexpr void addNodeIndex(unsigned i) { nodeIndex = i; }
};

static_assert(std::is_trivially_copyable_v<MemoryAccess>);
