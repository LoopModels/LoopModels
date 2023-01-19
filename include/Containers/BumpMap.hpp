#pragma once
#include "Utilities/Allocators.hpp"
#include <cstddef>
#include <llvm/ADT/DenseMap.h>
#include <variant>

template <typename KeyT, typename ValueT, typename Alloc = BumpAlloc<>,
          typename KeyInfoT = llvm::DenseMapInfo<KeyT>,
          typename BucketT = llvm::detail::DenseMapPair<KeyT, ValueT>>
struct BumpMap
  : public llvm::DenseMapBase<BumpMap<KeyT, ValueT, Alloc, KeyInfoT, BucketT>,
                              KeyT, ValueT, KeyInfoT, BucketT> {
  friend class llvm::DenseMapBase<BumpMap, KeyT, ValueT, KeyInfoT, BucketT>;

private:
  // Lift some types from the dependent base class into this class for
  // simplicity of referring to them.
  using BaseT = llvm::DenseMapBase<BumpMap, KeyT, ValueT, KeyInfoT, BucketT>;

  BucketT *Buckets;
  unsigned NumEntries;
  unsigned NumTombstones;
  unsigned NumBuckets;
  NotNull<Alloc> Allocator;

public:
  /// Create a BumpMap with an optional \p InitialReserve that guarantee that
  /// this number of elements can be inserted in the map without grow()
  explicit BumpMap(Alloc &A, unsigned InitialReserve = 0) : Allocator(A) {
    init(InitialReserve);
  }

  BumpMap(const BumpMap &other) : BaseT() {
    init(0);
    copyFrom(other);
  }

  BumpMap(BumpMap &&other) noexcept : BaseT() {
    init(0);
    swap(other);
  }

  template <typename InputIt> BumpMap(const InputIt &I, const InputIt &E) {
    init(std::distance(I, E));
    this->insert(I, E);
  }

  BumpMap(std::initializer_list<typename BaseT::value_type> Vals) {
    init(Vals.size());
    this->insert(Vals.begin(), Vals.end());
  }

  ~BumpMap() {
    this->destroyAll();
    Allocator->deallocate(Buckets, NumBuckets);
  }

  void swap(BumpMap &RHS) {
    this->incrementEpoch();
    RHS.incrementEpoch();
    std::swap(Buckets, RHS.Buckets);
    std::swap(NumEntries, RHS.NumEntries);
    std::swap(NumTombstones, RHS.NumTombstones);
    std::swap(NumBuckets, RHS.NumBuckets);
    std::swap(Allocator, RHS.Allocator);
  }

  auto operator=(const BumpMap &other) -> BumpMap & {
    if (&other != this) copyFrom(other);
    return *this;
  }

  auto operator=(BumpMap &&other) noexcept -> BumpMap & {
    this->destroyAll();
    Allocator->deallocate(Buckets, NumBuckets);
    init(0);
    swap(other);
    return *this;
  }

  void copyFrom(const BumpMap &other) {
    this->destroyAll();
    Allocator->deallocate(Buckets, NumBuckets);
    if (allocateBuckets(other.NumBuckets)) {
      this->BaseT::copyFrom(other);
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  void init(unsigned InitNumEntries) {
    auto InitBuckets = BaseT::getMinBucketToReserveForEntries(InitNumEntries);
    if (allocateBuckets(InitBuckets)) {
      this->BaseT::initEmpty();
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  // void grow(unsigned AtLeast) {
  //   BucketT *OldBuckets = Buckets;
  //   reallocateBuckets(std::max<unsigned>(
  //     64, static_cast<unsigned>(llvm::NextPowerOf2(AtLeast - 1))));
  //   assert(Buckets);
  //   if (!OldBuckets) this->BaseT::initEmpty();
  // }
  void grow(unsigned AtLeast) {
    unsigned OldNumBuckets = NumBuckets;
    BucketT *OldBuckets = Buckets;
    auto NewNumBuckets = std::max<unsigned>(
      64, static_cast<unsigned>(llvm::NextPowerOf2(AtLeast - 1)));
    if (!OldBuckets) {
      allocateBuckets(NewNumBuckets);
      assert(Buckets);
      this->BaseT::initEmpty();
      return;
    }
    NumBuckets = NewNumBuckets;
    Buckets = Allocator->template reallocate<false, BucketT>(
      Buckets, OldNumBuckets, NewNumBuckets);
    const KeyT EmptyKey = BaseT::getEmptyKey();
    for (size_t i = OldNumBuckets; i < NewNumBuckets; ++i)
      new (Buckets + i) KeyT(EmptyKey);
    const KeyT TombstoneKey = BaseT::getTombstoneKey();
    for (BucketT *B = Buckets, *E = Buckets + OldNumBuckets; B != E; ++B) {
      if (!KeyInfoT::isEqual(B->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(B->getFirst(), TombstoneKey)) {

        auto &Val = B->getFirst();
        unsigned BucketNo = BaseT::getHashValue(Val) & (NumBuckets - 1);
        unsigned ProbeAmt = 0;
        while (true) {
          BucketT *ThisBucket = Buckets + BucketNo;
          if (ThisBucket == B) break;
          assert(!(KeyInfoT::isEqual(Val, ThisBucket->getFirst())));
          if (KeyInfoT::isEqual(ThisBucket->getFirst(), EmptyKey)) [[likely]] {
            ThisBucket->getFirst() = Val;
            ThisBucket->getSecond() = B->getSecond();
            Val = TombstoneKey;
            setNumTombstones(getNumTombstones() + 1);
            break;
          }
          // If this is a tombstone, remember it.  If Val ends up not in the
          // map, we prefer to return it than something that would require more
          // probing.
          if (KeyInfoT::isEqual(ThisBucket->getFirst(), TombstoneKey)) {
            // setNumTombstones(getNumTombstones() - 1);
            Val = TombstoneKey;
            ThisBucket->getFirst() = Val;
            ThisBucket->getSecond() = B->getSecond();
            break;
          }
          // Hash collision or a tombstone, continue quadratic probing.
          BucketNo += ++ProbeAmt;
          BucketNo &= (NumBuckets - 1);
        }
      }
    }
  }

  void shrink_and_clear() {
    unsigned OldNumBuckets = NumBuckets;
    unsigned OldNumEntries = NumEntries;
    this->destroyAll();

    // Reduce the number of buckets.
    unsigned NewNumBuckets = 0;
    if (OldNumEntries)
      NewNumBuckets =
        std::max(64, 1 << (llvm::Log2_32_Ceil(OldNumEntries) + 1));
    if (NewNumBuckets == NumBuckets) {
      this->BaseT::initEmpty();
      return;
    }
    Allocator->deallocate(Buckets, OldNumBuckets);
    init(NewNumBuckets);
  }

private:
  [[nodiscard]] auto getNumEntries() const -> unsigned { return NumEntries; }

  void setNumEntries(unsigned Num) { NumEntries = Num; }

  [[nodiscard]] auto getNumTombstones() const -> unsigned {
    return NumTombstones;
  }

  void setNumTombstones(unsigned Num) { NumTombstones = Num; }

  auto getBuckets() const -> BucketT * { return Buckets; }

  [[nodiscard]] auto getNumBuckets() const -> unsigned { return NumBuckets; }

  auto allocateBuckets(unsigned Num) -> bool {
    NumBuckets = Num;
    if (NumBuckets == 0) {
      Buckets = nullptr;
      return false;
    }
    Buckets = Allocator->template allocate<BucketT>(NumBuckets);
    return true;
  }
  void reallocateBuckets(unsigned Num) {
    unsigned oldNumBuckets = NumBuckets;
    NumBuckets = Num;
    if (NumBuckets == 0) {
      Buckets = nullptr;
    } else {
      Buckets = Allocator->template reallocate<false, BucketT>(
        Buckets, oldNumBuckets, NumBuckets);
    }
  }
};
