#pragma once
#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Hash.hpp"
#include "Support/Iterators.hpp"
#include <Containers/BitSets.hpp>
#include <Math/Array.hpp>
#include <Utilities/Allocators.hpp>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>

namespace poly::CostModeling {
using math::MutPtrVector, math::DensePtrMatrix;
struct ArrayIndex {
  const llvm::SCEVUnknown *array;
  DensePtrMatrix<int64_t> index;

  ArrayIndex(IR::Addr *a)
    : array{a->getArrayPointer()}, index{a->indexMatrix()} {}
  constexpr auto operator==(const ArrayIndex &) const -> bool = default;
};

} // namespace poly::CostModeling
template <>
struct ankerl::unordered_dense::hash<poly::CostModeling::ArrayIndex> {
  using is_avalanching = void;
  [[nodiscard]] auto
  operator()(poly::CostModeling::ArrayIndex const &x) const noexcept
    -> uint64_t {
    using poly::Hash::combineHash, poly::Hash::getHash;
    uint64_t seed = getHash(x.array);
    seed = combineHash(seed, getHash(ptrdiff_t{x.index.numRow()}));
    seed = combineHash(seed, getHash(ptrdiff_t{x.index.numCol()}));
    // Maybe use a faster hash?
    for (int64_t y : x.index) seed = combineHash(seed, getHash(y));
    return seed;
  }
};
/// It would be best to actually use the tree structure
/// Let's consider a depth first search approach.
/// A(9) --> B(3) --> C(2) --> D(0)
///      \-> E(5) --> F(4) \-> G(1)
///      \-> H(8) --> I(7) --> J(6)
/// At a given level, we do need to decide whether to unroll and vectorize,
/// using context of both upper and lower levels. Thus, it is a non-local
/// decision that doesn't map well to a single traversal.
///
/// Many optimal optimization possibilities however are orthogonal with one
/// another, so it would be wasteful to consider the full cartesian product.
///
/// At each loopnest level, we could consider not unrolling or vectorizing that
/// or any outer level, and then optimize all subtrees independently. When
/// considering these subtrees together, we must always consider them with at
/// least one of this level or an outer level unrolled and/or vectorized.
///
/// One consideration pattern:
/// [!D], [D], [!G, G], [C, !D, !G], [C, D, !G], [C, !D, G], [C, D, G],
/// [B, C, !D, !G], [B, C, D, !G], [B, C, !D, G], [B, C, D, G].
/// [B, !C, !D, !G], [B, !C, D, !G], [B, !C, !D, G], [B, !C, D, G]
/// etc
/// So it doesn't cut down much.
/// Note (of course) that we're not considering all of these possibilities;
/// these are the fast ones we're willing to consider at a particular time.
///
/// We can have contiguous indices giving the range that an optimization
/// possibilities applies to. E.g., if we're at `3` when we reach a loop, we
/// have [3, curr] (i.e., a close-close range).
///
/// We only need to track the best alg of each subtree, and compare that of the
/// current root. The current root then returns the current best alg: either
/// it's opt, or the composition of the subtrees. The recursion continues until
/// we get the best alg for the whole tree.
namespace poly::CostModeling {

class AddrSummary {
  IR::Addr *addr;
  uint64_t minStaticStride;
  uint64_t *data;
  unsigned numLoops;
  [[nodiscard]] constexpr auto numLoopWords() const -> size_t {
    return (numLoops + 63) / 64;
  }

public:
  constexpr AddrSummary(IR::Addr *addr, uint64_t minStaticStride,
                        uint64_t *data, unsigned numLoops)
    : addr(addr), minStaticStride(minStaticStride), data(data),
      numLoops(numLoops) {}
  [[nodiscard]] constexpr auto getAddr() const -> IR::Addr * { return addr; }
  [[nodiscard]] constexpr auto getMinStaticStride() const -> uint64_t {
    return minStaticStride;
  }
  constexpr auto minStaticStrideLoops()
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    math::MutPtrVector<uint64_t> v{data, numLoopWords()};
    return containers::BitSet<math::MutPtrVector<uint64_t>>{v};
  }
  constexpr auto remainingLoops()
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    size_t nL = numLoopWords();
    math::MutPtrVector<uint64_t> v{data + nL, nL};
    return containers::BitSet<math::MutPtrVector<uint64_t>>{v};
  }
  constexpr auto getAddr() -> IR::Addr * { return addr; }
};

class OptimizationOptions;
/// 3-d array, numAddr x (2 x cld(numLoops,64) + 1)
/// The `1` is a static multiplier (e.g. 2) on strides
/// We store the minimum-static rank dependency of each addr,
/// the static rank, and the remaining dependencies.
///
/// We also want to collect addrs corresponding to arrays, to find
/// unroll possibilities.
class LoopDependencies {
  dict::amap<ArrayIndex, int32_t> addrMap;
  /// the chain is for mappings of indices w/in LoopDependencies
  unsigned numLoops;
  unsigned maxAddr;
  unsigned numAddr{0};
  int32_t offset{0};
  // data is an array of `AddrSummary`, and then a chain of length `numAddr`
  // giving the position among `AddrSummary` of the next in the chain
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#endif
  char data[]; // NOLINT(modernize-avoid-c-arrays)
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

  static constexpr auto bytesPerAddr(unsigned numLoops) -> size_t {
    return (2 * ((numLoops + 63) / 64) + 1) * sizeof(uint64_t) +
           sizeof(IR::Addr *);
  }

public:
  LoopDependencies(Arena<> *alloc, unsigned numLoops, unsigned numAddr)
    : addrMap(alloc), numLoops(numLoops), numAddr(numAddr) {}

  LoopDependencies(const LoopDependencies &) = default;
  static auto create(utils::Arena<> *alloc, unsigned numLoops, unsigned numAddr)
    -> LoopDependencies * {

    size_t size = size_t(numAddr) * (bytesPerAddr(numLoops) + sizeof(int32_t)) +
                  sizeof(LoopDependencies);
    void *data = alloc->allocate(size);
    char *p = static_cast<char *>(data);
    std::fill(p + sizeof(LoopDependencies), p + size, 0);
    auto *ldp = static_cast<LoopDependencies *>(data);
    return std::construct_at(ldp, alloc, numLoops, numAddr);
  }
  constexpr auto subTree() -> int32_t { return std::exchange(offset, numAddr); }
  constexpr void resetTree(int32_t newOffset) { offset = newOffset; }
  constexpr auto operator[](ptrdiff_t i) -> AddrSummary {
    utils::invariant(i < numAddr);
    char *base = data + i * bytesPerAddr(numLoops);
    void *addr = base;
    void *stride = base + sizeof(IR::Addr *);
    void *bits = base + sizeof(IR::Addr *) + sizeof(uint64_t);
    return {*static_cast<IR::Addr **>(addr), *static_cast<uint64_t *>(stride),
            static_cast<uint64_t *>(bits), numLoops};
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t { return numAddr; }
  class Iterator {
    LoopDependencies *deps;
    ptrdiff_t i;

  public:
    constexpr Iterator(LoopDependencies *deps, ptrdiff_t i)
      : deps(deps), i(i) {}
    constexpr auto operator*() -> AddrSummary { return (*deps)[i]; }

    constexpr auto operator++() -> Iterator & {
      ++i;
      return *this;
    }
    constexpr auto operator!=(Iterator other) const -> bool {
      return i != other.i;
    }
    constexpr auto operator++(int) -> Iterator {
      Iterator tmp = *this;
      ++*this;
      return tmp;
    }
    constexpr auto operator-(Iterator other) const -> ptrdiff_t {
      return i - other.i;
    }
    constexpr auto operator--() -> Iterator & {
      --i;
      return *this;
    }
    constexpr auto operator--(int) -> Iterator {
      Iterator tmp = *this;
      --*this;
      return tmp;
    }
  };
  constexpr auto begin() -> Iterator { return {this, offset}; }
  constexpr auto end() -> Iterator { return {this, size()}; }
  constexpr auto findShared(IR::Addr *a) -> std::pair<ArrayIndex, int32_t> * {
    return addrMap.find(ArrayIndex{a});
  }
  // constexpr auto getShared(IR::Addr *a) -> int32_t & {
  //   return addrMap[ArrayIndex{a}];
  // }
  [[nodiscard]] constexpr auto sharedChain() -> int32_t * {
    void *p = data + maxAddr * bytesPerAddr(numLoops);
    return static_cast<int32_t *>(p);
  }
  /// calls `f` with `this` and an iterator over a set of
  /// array pointers that share `indexMatrix`
  void evalCollections(const auto &f) {
    int32_t *p = sharedChain();
    for (auto [s, i] : addrMap) {
      if ((i < offset) || (p[i] < offset)) continue;
      f(this, utils::VForwardRange{p, i});
    }
  }
  // constexpr auto hasMultiple(IR::Addr *a) -> bool {
  //   auto *f = findShared(a);
  //   if (!f) return false;

  // }
  constexpr auto sharedIndex(IR::Addr *a) -> utils::VForwardRange {
    auto *f = findShared(a);
    if (f == addrMap.end()) return utils::VForwardRange{nullptr, -1};

    return utils::VForwardRange{sharedChain(), f->second};
  }
  constexpr auto commonIndices(IR::Addr *a) {
    return sharedIndex(a) |
           std::views::filter([=](int32_t i) -> bool { return i >= offset; }) |
           std::views::transform(
             [this](int32_t i) -> AddrSummary { return (*this)[i]; });
  }
  // adding an Addr should adds unroll options
  inline constexpr void
  addAddr(utils::Arena<> *,
          math::ResizeableView<OptimizationOptions, unsigned> &, IR::Addr *);
};

/// What we want is a map from {array,indexMatrix()} pairs to
/// all Addrs with that `indexMatrix()`;
/// the `all Addrs with that `indexMatrix()`` can be achieved via
/// an integer index into a vector representing chains.
///
/// We can carry an offset for use in filtering `Addr`s when
/// restricting ourselves to a particular subtree.
///
class ArrayCollection {

public:
  // iterate over...
};

class UnrollOptions {
  uint32_t options; // bitmask
public:
  constexpr UnrollOptions(uint32_t options) : options(options) {}
  static constexpr auto atMost(uint32_t x) -> UnrollOptions {
    return {(uint32_t{1} << x) - 1};
  }
  [[nodiscard]] constexpr auto allowed(uint32_t x) const -> bool {
    return (options & (uint32_t{1} << x));
  }
  [[nodiscard]] constexpr auto isDense() const -> bool {
    return options == std::numeric_limits<uint32_t>::max();
  }
  [[nodiscard]] constexpr auto getOptions() const -> uint32_t {
    return options;
  }
  [[nodiscard]] constexpr auto operator&(UnrollOptions other) const
    -> UnrollOptions {
    return {options & other.options};
  }
  constexpr auto operator&=(UnrollOptions other) -> UnrollOptions & {
    options &= other.options;
    return *this;
  }
};

// the accumulated set of unroll and vectorization options
// that we can search later.
class OptimizationOptions {
  std::array<UnrollOptions, 4> unrollOptions;
  std::array<uint16_t, 4> loopIDs;
  // bounds on the applicable region
  uint16_t lower;
  uint16_t upper;
  uint16_t vecid;

public:
  [[nodiscard]] constexpr auto vectorize() const -> bool {
    return vecid != std::numeric_limits<uint16_t>::max();
  }
  [[nodiscard]] constexpr auto getVecID() const -> uint16_t { return vecid; }
  [[nodiscard]] constexpr auto getRange() const
    -> math::Range<unsigned, unsigned> {
    return {lower, upper};
  }
};

// Here, we scan the addr so far,
// I think we should have sub-tree ref sets.
inline constexpr void LoopDependencies::addAddr(
  utils::Arena<> *alloc,
  math::ResizeableView<OptimizationOptions, unsigned> &optops, IR::Addr *a) {
  auto *f = findShared(a);
  if (f && f->second >= offset) return; // already added

  bool foundMatchInd = false;
  for (AddrSummary s : commonIndices(a)) {
    IR::Addr *b = s.getAddr();
    // we share the same array and IndexMatrix
    // check for patterns like `A[i,2*j], A[i,2*j + 1]`
    // if we're not part of the same BB
    if ((a->getParent() != b->getParent()) || (a->isLoad() != b->isLoad()))
      continue;
    // we can check top positions
    invariant(a->getTopPosition() > b->getTopPosition());
    // note that outputEdgeIDs are sorted; can we use this to check for
    // no intervening edges?
  }
  if (foundMatchInd) return; // already added this `indexMatrix`
  for (AddrSummary s : *this) {
    if (s.getAddr() == a) {
      // check for patterns like `A[i,2*j], A[i,2*j + 1]`
    }
  }
}

class ContigSummary {
  uint32_t nonContiguous; // bitmask
  uint32_t contiguous;    // bitmask, A[i+j,k+l]; multiple may be set
public:
  constexpr ContigSummary(uint32_t nonContiguous, uint32_t contiguous)
    : nonContiguous(nonContiguous), contiguous(contiguous) {}
  [[nodiscard]] constexpr auto getNonContiguous() const -> uint32_t {
    return nonContiguous;
  }
  [[nodiscard]] constexpr auto getContiguous() const -> uint32_t {
    return contiguous;
  }
  [[nodiscard]] constexpr auto operator&(ContigSummary other) const
    -> ContigSummary {
    uint32_t nonContig = nonContiguous | other.nonContiguous;
    return {nonContig, (contiguous & other.contiguous) & ~nonContig};
  }
};

/// When we unroll, we have an ordering of the unrolled dimensions.
class RegisterTile {
  std::array<uint8_t, 3> unroll; //
  uint8_t vector;                //
  uint32_t unrollMask;           // bitmask
public:
  RegisterTile(std::array<uint8_t, 3> unroll, uint8_t vector)
    : unroll(unroll), vector(vector) {
    unrollMask = (1 << unroll[0]) | (1 << unroll[1]) | (1 << unroll[2]);
  }
};
} // namespace poly::CostModeling
