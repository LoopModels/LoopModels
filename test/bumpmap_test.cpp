#include "Dicts/BumpMapSet.hpp"
#include <cstdint>
#include <gtest/gtest.h>

// // NOLINTNEXTLINE(modernize-use-trailing-return-type)
// TEST(BumpUpMapTest, BasicAssertions) {
//   OwningArena<16384, true> alloc;
//   for (int i = 0; i < 100; ++i) {
//     amap<uint64_t, uint64_t, decltype(alloc)> map(alloc);
//     for (int j = 0; j < 100; ++j) map.insert({j, j});
//     for (int j = 0; j < 100; ++j) EXPECT_EQ(map.find(j)->second, j);
//     alloc.reset();
//   }
// }
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BumpDownMapTest, BasicAssertions) {
  using M = poly::dict::amap<uint64_t, uint64_t>;
  static_assert(
    std::same_as<
      M::value_container_type,
      poly::math::BumpPtrVector<containers::Pair<uint64_t, uint64_t>>>);
  static_assert(
    std::same_as<
      M::allocator_type,
      poly::alloc::WArena<containers::Pair<uint64_t, uint64_t>, 16384, true>>);

  poly::alloc::OwningArena<> alloc;
  M::allocator_type walloc{&alloc};
  M::value_container_type mvals{walloc};
  // poly::math::BumpPtrVector<int> vec{&alloc};
  for (int i = 0; i < 100; ++i) {
    M map{&alloc};
    for (int j = 0; j < 100; ++j) map.insert({j, j});
    for (int j = 0; j < 100; ++j) EXPECT_EQ(map.find(j)->second, j);
    alloc.reset();
  }
}
