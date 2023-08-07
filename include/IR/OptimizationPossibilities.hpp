#pragma once
#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Hash.hpp"
#include "Math/GreatestCommonDivisor.hpp"
#include "Support/Iterators.hpp"
#include <Containers/BitSets.hpp>
#include <Math/Array.hpp>
#include <Utilities/Allocators.hpp>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace poly::CostModeling {
using math::DensePtrMatrix;

// Classifies the type of loop dependency
// Possibilities:
// Not nested: this address is not nested inside the loop
// Dynamic: unknown stride
// Static: 0...
// Special values:
// 0: nested inside, but not dependent
// 1: contiguous
// -1: contiguous, but reversed
// From here, small values like `2` might mean we can efficiently shuffle.
// Ideally, we'd be paired with another address offset by 1.
// But even just `A[2*i]`, it may take less micro-ops to shuffle than to
// use a gather.
struct LoopDependency {
  int type_;
  static constexpr int notNested = std::numeric_limits<int>::min();
  static constexpr int dynamic = notNested + 1;
  // NOLINTNEXTLINE(readability-identifier-naming)
  static constexpr auto NotNested() -> LoopDependency { return {notNested}; }
  // NOLINTNEXTLINE(readability-identifier-naming)
  static constexpr auto Dynamic() -> LoopDependency { return {dynamic}; }
  // NOLINTNEXTLINE(readability-identifier-naming)
  static constexpr auto Static(int stride) -> LoopDependency {
    // incredibly unlikely, but we don't want to mistake "notNested"
    // for a static stride with that value. It isn't something we can optimize
    // anyway, so we'll just return dynamic.
    return {stride == notNested ? dynamic : stride};
  }
  [[nodiscard]] constexpr auto stride() const -> std::optional<int> {
    switch (type_) {
    case notNested:
    case dynamic: return std::nullopt;
    default: return type_;
    }
  }
  [[nodiscard]] constexpr auto isDynamic() const -> bool {
    return type_ == dynamic;
  }
  [[nodiscard]] constexpr auto isNotNested() const -> bool {
    return type_ == notNested;
  }
};

// loop subsets are contiguous
//
// comparing addrs; NaN == independent of?
// for (j : J){
//   b = B[j];
//   for (i : I) f(A[i], b);
// }
//
// Lets start with the actual cost:
// Ca and Cb are cost of loading from A and B, respectively.
// Ui and Uj are the unrolling factors for i and j, respectively.
// Vi and Vj are the vectorization factors; only one is allowed to be !=0
// C = Ca * J * I/(Uj*Vj*Vi) + Cb * J/Vj
//
// Vi: C = Ca * J * I/(Uj*Vi) + Cb * J
// Vj: C = Ca * J * I/(Uj*Vj) + Cb * J/Vj
//
// Cost of `A` is Ca * Uj^0 * Ui^1 * I*J / (Uj * Ui)
// = Ca * I * J / Uj
// Cost of `B` is Cb * Uj^1 * Ui^0 * I^0*J / (Uj * Ui^0)
// = Cb * J
// Perhaps easier to work with logarithms?
//
//
//     j    i
// A:  0    1
// B:  1   NaN
//
//
//
//   i     j     k
// NaN   NaN   NaN
class Costs {
  DensePtrMatrix<double> costs;

  [[nodiscard]] constexpr auto numAddr() const -> ptrdiff_t {
    return ptrdiff_t{costs.numRow()};
  }
  [[nodiscard]] constexpr auto numLoops() const -> ptrdiff_t {
    return ptrdiff_t{costs.numCol()};
  }
};

// perhaps should just store costs?
class LoopIndexDependency {
  // for (j : J){
  //   b = B[j];
  //   for (i : I) f(A[i], b);
  // }
  // In this example, if `j` and `i` have the same stride category,
  // we'd want to add the option to unroll and vectorize `j`
  // If `i` has a small static stride and `j` does not, we'd want
  // to add the option to vectorize `i` (while still unrolling `j`)
  //
  // Another thing to consider with optimization options is that we may
  // want to be able to combine separate ones; do we need some
  // indicator of what we would like to discourage?
  //
  // Nested: benefit from unrolling
  // Non-static stride: penalize vectorization
  //
  //
  // Mappings:
  // A[i] in j, B[j] in i => i,j;
  // A[i] in j, B[j] in !i => j;
  // A[i] in !j, B[j] in i => i;
  // A[i] in !j, B[j] in !i => {};

  // A[i,j], B[j] in i => i;
  // A[i,j], B[j] in !i => {};
  // A[i] in j, B[j,i] => j;
  // A[i] in !j, B[j,i] => {};
  // A[i,j], B[j,i] => {};

  // An access only benefits from a loop it is nested inside but doesn't depend
  // on being unrolled.
  // How can we do the mapping?
  // Nested and dynamic or small static -> benefit from unrolling
  //
  // For now, we'll use 4 bits per...
  uint64_t *data;
  unsigned words;
  // 4 bits/loop
  // 64 bits/word
  static constexpr unsigned numBits = 4; // wasteful
  static constexpr unsigned numLoopsPerUInt = 64 / numBits;
  [[nodiscard]] static constexpr auto numWords(unsigned numLoops) -> unsigned {
    return (numLoops + numLoopsPerUInt - 1) / numLoopsPerUInt;
  }
  static constexpr auto numberToShift(uint64_t x) -> unsigned {
    return std::countr_zero(x) & ~(numBits - 1);
  }

public:
  constexpr LoopIndexDependency(uint64_t *data, unsigned numLoops)
    : data{data}, words{numWords(numLoops)} {}
  enum class DependencyType {
    Independent = 0, // 000 //
    Nested = 1,      // 001 // cheap to vectorize
    Dynamic = 2,     // 010 // expensive to vectorize
    SmallStatic = 4  // 100 // cheap to vectorize
  };
};

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
using math::end, math::last;
constexpr auto cld64(unsigned x) -> unsigned { return (x + 63) / 64; }

class AddrSummary {
  IR::Addr *addr;
  uint64_t minStaticStride;
  uint64_t *data;
  unsigned words;

public:
  constexpr AddrSummary(IR::Addr *addr, uint64_t minStaticStride,
                        uint64_t *data, unsigned numLoops)
    : addr(addr), minStaticStride(minStaticStride), data(data),
      words(cld64(numLoops)) {}
  [[nodiscard]] constexpr auto getAddr() const -> IR::Addr * { return addr; }
  [[nodiscard]] constexpr auto getMinStaticStride() const -> uint64_t {
    return minStaticStride;
  }
  static constexpr auto minStaticStrideLoops(uint64_t *data, unsigned words)
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    math::MutPtrVector<uint64_t> v{data, words};
    return containers::BitSet<math::MutPtrVector<uint64_t>>{v};
  }
  static constexpr auto remainingLoops(uint64_t *data, unsigned words)
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    math::MutPtrVector<uint64_t> v{data + words, words};
    return containers::BitSet<math::MutPtrVector<uint64_t>>{v};
  }
  constexpr auto minStaticStrideLoops()
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    return minStaticStrideLoops(data, words);
  }
  constexpr auto remainingLoops()
    -> containers::BitSet<math::MutPtrVector<uint64_t>> {
    return remainingLoops(data, words);
  }
  constexpr auto getAddr() -> IR::Addr * { return addr; }
  constexpr void copyTo(char *dst) {
    void *paddr = dst;
    void *stride = dst + sizeof(IR::Addr *);
    void *bits = dst + sizeof(IR::Addr *) + sizeof(uint64_t);
    *static_cast<IR::Addr **>(paddr) = addr;
    *static_cast<uint64_t *>(stride) = minStaticStride;
    std::copy_n(data, 2 * words, static_cast<uint64_t *>(bits));
  }
  constexpr auto setAddr(IR::Addr *a) -> AddrSummary & {
    addr = a;
    return *this;
  }
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
  int32_t maxAddr;
  int32_t numAddr{0};
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
    return (2 * cld64(numLoops) + 1) * sizeof(uint64_t) + sizeof(IR::Addr *);
  }
  struct AddrReference {
    IR::Addr **addr;
    uint64_t *stride;
    uint64_t *bits;
  };
  constexpr auto addrRef(ptrdiff_t i) -> AddrReference {
    utils::invariant(i < numAddr);
    char *base = data + i * bytesPerAddr(numLoops);
    void *addr = base;
    void *stride = base + sizeof(IR::Addr *);
    void *bits = base + sizeof(IR::Addr *) + sizeof(uint64_t);
    return {static_cast<IR::Addr **>(addr), static_cast<uint64_t *>(stride),
            static_cast<uint64_t *>(bits)};
  }

public:
  LoopDependencies(Arena<> *alloc, unsigned numLoops, unsigned numAddr)
    : addrMap(alloc), numLoops(numLoops), numAddr(int32_t(numAddr)) {}

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
    AddrReference ref = addrRef(i);
    return {*(ref.addr), *(ref.stride), ref.bits, numLoops};
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
  constexpr void push(AddrSummary s) {
    invariant(numAddr < maxAddr);
    s.copyTo(data + (numAddr++) * bytesPerAddr(numLoops));
  }

  // adding an Addr should adds unroll options
  inline constexpr void
  addAddr(utils::Arena<> *,
          math::ResizeableView<OptimizationOptions, unsigned> &, IR::Addr *);
  inline constexpr void
  addOptOption(utils::Arena<> *,
               math::ResizeableView<OptimizationOptions, unsigned> &,
               AddrSummary);
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
  int32_t id = numAddr;
  // something to think about here is how to handle the mix of `Addr` reference
  // as pointers, vs `Addr` references as indices into a vector.
  auto *f = findShared(a);
  size_t bpa = bytesPerAddr(numLoops);
  if (f) {
    AddrSummary s = (*this)[f->second].setAddr(a);
    push(s);
    // FIXME: not okay!!! We may have different loops
    std::copy_n(data + bpa * f->second, bpa, data + bpa * id);
    if (f && f->second >= offset) return;
    addOptOption(alloc, optops, s);
    return;
  }
  ++numAddr;
  AddrReference ref = addrRef(id);
  *ref.addr = a;
  // calc minStaticStride
  DensePtrMatrix<int64_t> indMat = a->indexMatrix(); // dim x loop
  // we want mapping from `indMat` index to loop index
  uint64_t minStaticStride = std::numeric_limits<uint64_t>::max();
  auto minStaticStrideLoops =
    AddrSummary::minStaticStrideLoops(ref.bits, cld64(numLoops));
  auto remainingLoops = AddrSummary::remainingLoops(ref.bits, cld64(numLoops));
  IR::Loop *L = a->getLoop();
  for (ptrdiff_t l = ptrdiff_t{indMat.numCol()}; l--; L = L->getLoop()) {
    uint32_t lid = L->getID();
    for (ptrdiff_t j = 0; j < ptrdiff_t{indMat.numRow()} - 1; ++j)
      if (indMat(j, l)) remainingLoops.insert(lid);
    if (int64_t x = indMat(last, l)) {
      uint64_t absx = math::constexpr_abs(x);
      if (minStaticStride > absx) {
        minStaticStride = absx;
        remainingLoops |= minStaticStrideLoops;
        minStaticStrideLoops.clear();
        minStaticStrideLoops.insert(lid);
      } else if (minStaticStride == absx) {
        minStaticStrideLoops.insert(lid);
      } else {
        remainingLoops.insert(lid);
      }
    }
  }
  *ref.stride = minStaticStride;
  addOptOption(alloc, optops, {a, minStaticStride, ref.bits, numLoops});
}

inline constexpr void LoopDependencies::addOptOption(
  utils::Arena<> *alloc,
  math::ResizeableView<OptimizationOptions, unsigned> &optops, AddrSummary s) {
  // scan older `AddrSummary`s, compare with `s`
  for (ptrdiff_t i = 0; i < numAddr - 1; ++i) {
    AddrSummary o = (*this)[i];
    // to identify tiling opportunities...we need to know which loops `s` and
    // `o` are actually both nested inside.
    //
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
