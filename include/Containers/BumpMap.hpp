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
    auto NewNumBuckets = std::max<unsigned>(
      64, static_cast<unsigned>(llvm::NextPowerOf2(AtLeast - 1)));
    BucketT *OldBuckets = allocateBucketsInit(NewNumBuckets);
    assert(Buckets);
    if (!OldBuckets) return;
    moveFromOldBuckets(OldBuckets, OldNumBuckets);
    Allocator->deallocate(OldBuckets, OldNumBuckets);
  }
  void moveFromOldBuckets(BucketT *OldBucketsBegin, unsigned OldNumBuckets) {
    // this->initEmpty();

    // Insert all the old elements.
    const KeyT EmptyKey = BaseT::getEmptyKey();
    const KeyT TombstoneKey = BaseT::getTombstoneKey();
    for (BucketT *B = OldBucketsBegin, *E = B + OldNumBuckets; B != E; ++B) {
      if (!KeyInfoT::isEqual(B->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(B->getFirst(), TombstoneKey)) {
        // Insert the key/value into the new table.
        BucketT *DestBucket;
        KeyT &Val = B->getFirst();

        unsigned BucketNo = BaseT::getHashValue(Val) & (NumBuckets - 1);
        unsigned ProbeAmt = 0;
        while (true) {
          DestBucket = Buckets + BucketNo;
          // Found Val's bucket?  If so, return it.
          assert((!KeyInfoT::isEqual(Val, DestBucket->getFirst())) &&
                 "Key already in new map?");
          if (KeyInfoT::isEqual(DestBucket->getFirst(), EmptyKey) ||
              KeyInfoT::isEqual(DestBucket->getFirst(), TombstoneKey))
            [[likely]] {
            break;
          }
          // Hash collision, continue quadratic probing.
          BucketNo += ++ProbeAmt;
          BucketNo &= (NumBuckets - 1);
        }
        DestBucket->getFirst() = std::move(B->getFirst());
        ::new (&DestBucket->getSecond()) ValueT(std::move(B->getSecond()));
        setNumEntries(getNumEntries() + 1);
        // Free the value.
        B->getSecond().~ValueT();
      }
      B->getFirst().~KeyT();
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
  /// returns a pointer to the old buckets
  /// the basic idea is to try and take advantage of realloc
  /// Semantically, it is the same as `allocateBuckets` followed by calling
  /// this->initEmpty();
  ///
  [[gnu::returns_nonnull]] auto allocateBucketsInit(unsigned Num) -> BucketT * {
    BucketT *OrigBuckets = Buckets;
    unsigned OldNumBuckets = NumBuckets;
    const KeyT EmptyKey = BaseT::getEmptyKey();
    setNumEntries(0);
    setNumTombstones(0);
    NumBuckets = Num;
    Buckets = Buckets
                ? Allocator->tryReallocate(Buckets, OldNumBuckets, NumBuckets)
                : nullptr;
    if (Buckets) {
      // now we'll allocate a temporary array of buckets
      // we'll then copy the old buckets into it, while initializing the new
      // buckets
      BucketT *OldBuckets =
        Allocator->template allocate<BucketT>(OldNumBuckets);
      if constexpr (Alloc::BumpDown) {
        assert(OrigBuckets == Buckets + (Num - OldNumBuckets));
        BucketT *E = OrigBuckets;
        for (BucketT *B = Buckets; B != E; ++B)
          ::new (&B->getFirst()) KeyT(EmptyKey);
        for (size_t i = 0; i < OldNumBuckets; ++i) {
          BucketT *B = E + i;
          OldBuckets[i] = *B;
          ::new (&B->getFirst()) KeyT(EmptyKey);
        }
      } else {
        for (size_t i = 0; i < OldNumBuckets; ++i) {
          BucketT *B = Buckets + i;
          OldBuckets[i] = *B;
          ::new (&B->getFirst()) KeyT(EmptyKey);
        }
        for (BucketT *B = Buckets + OldNumBuckets, *E = Buckets + NumBuckets;
             B != E; ++B)
          ::new (&B->getFirst()) KeyT(EmptyKey);
      }
      return OldBuckets;
    }
    Buckets = Allocator->template allocate<BucketT>(NumBuckets);
    for (BucketT *B = Buckets, *E = B + NumBuckets; B != E; ++B)
      ::new (&B->getFirst()) KeyT(EmptyKey);
    return OrigBuckets;
  }
};
