#include "Dicts/Trie.hpp"
#include <Alloc/Arena.hpp>
#include <ankerl/unordered_dense.h>
#include <gtest/gtest.h>
#include <random>

using poly::dict::TrieMap, poly::dict::InlineTrie;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TrieTest, BasicAssertions) {
  std::mt19937_64 rng;
  poly::alloc::OwningArena<> alloc{};

  TrieMap<true, int, int> d;
  EXPECT_FALSE(d.find(3));
  d[&alloc, 3] = 11;
  EXPECT_EQ(d.find(3)->second, 11);
  d[&alloc, 3] += 11;
  EXPECT_EQ(d.find(3)->second, 22);

  InlineTrie<int, int> t;
  EXPECT_FALSE(t.find(7));
  t[alloc, 7] = 13;
  EXPECT_TRUE(t.find(7));
  EXPECT_EQ(*t.find(7), 13);
  t[alloc, 7] += 14;
  EXPECT_EQ(*t.find(7), 27);
  //// More thorough test:
  TrieMap<true, void *, uint64_t> tm;
  InlineTrie<void *, uint64_t> it;
  ankerl::unordered_dense::map<void *, uint64_t> m;

  // uint64_t mask = ((1ULL << 5) - 1) << 4ULL;
  uint64_t mask = ((1ULL << 10) - 1) << 4ULL;
  bool found = false;
  // static constexpr auto debugval = 0xc38;
  static constexpr auto debugval = 0x3c00;
  // static constexpr auto debugval = 0x1358;
  // static constexpr auto debugval = 0x12e8;
  for (uint64_t i = 0; i < 512;) {
    void *x = reinterpret_cast<void *>(rng() & mask);
    if (!x) continue;
    void *y = reinterpret_cast<void *>(rng() & mask);
    if (!y) continue;
    if (reinterpret_cast<uintptr_t>(x) == debugval) {
      found = true;
      auto *tmf = tm.find(y);
      auto itf = it.find(y);
      auto *tmfx = tm.find(x);
      auto itfx = it.find(x);
      std::cout << "i = " << i + 1 << "; m[y] = " << m[y] << "\n"
                << "tm.find(y) = " << (tmf ? tmf->second : 0)
                << "\nit.find(y) = " << (itf ? *itf : 0)
                << "\ntm.find(x) = " << (tmfx ? tmfx->second : -1)
                << "\nit.find(x) = " << (itfx ? *itfx : -1)
                << "\ntm[a, x] = " << tm[&alloc, x]
                << "\nit[a, x] = " << it[&alloc, x]
                << "\ntm.find(x) = " << tm.find(x)->second
                << "\nit.find(x) = " << *it.find(x) << "\n";
    }
    if (found) {
      void *p = reinterpret_cast<void *>(debugval);
      EXPECT_EQ(m[p], tm.find(p)->second);
      EXPECT_EQ(m[p], *it.find(p));
      ASSERT(m[p] == tm.find(p)->second);
      ASSERT(m[p] == *it.find(p));
    }
    m[x] += (++i) + m[y];
    tm[&alloc, x] += i + tm[&alloc, y];
    it[&alloc, x] += i + it[&alloc, y];
    if (reinterpret_cast<uintptr_t>(x) == debugval) {
      auto *tmf = tm.find(x);
      auto itf = it.find(x);
      std::cout << "i = " << i << "; m[x] = " << m[x] << "\n"
                << "tm.find(x) = " << (tmf ? tmf->second : -1)
                << "\nit.find(x) = " << (itf ? *itf : -1) << "\n";
    }
    EXPECT_TRUE(tm.find(x));
    EXPECT_TRUE(it.find(x));
    if (tm.find(x)->second != m[x]) std::cout << "x = " << x << "\n";
    if (*it.find(x) != m[x]) std::cout << "x = " << x << "\n";
    EXPECT_EQ(tm.find(x)->second, m[x]);
    EXPECT_EQ(*it.find(x), m[x]);
    void *z = reinterpret_cast<void *>(rng() & mask);
    if (!z) continue;
    // std::cout << "i = " << i << "\n";
    if (found) {
      void *p = reinterpret_cast<void *>(debugval);
      EXPECT_EQ(m[p], tm.find(p)->second);
      EXPECT_EQ(m[p], *it.find(p));
      ASSERT(m[p] == tm.find(p)->second);
      ASSERT(m[p] == *it.find(p));
    }
    if (void *p = reinterpret_cast<void *>(debugval); p == z) {
      auto *tmf = tm.find(z);
      auto itf = it.find(z);
      std::cout << "i = " << i << "; m[z] = " << m[z] << "\n"
                << "tm.find(z) = " << (tmf ? tmf->second : -1)
                << "\nit.find(z) = " << (itf ? *itf : -1) << "\n";
    }
    m.erase(z);
    tm.erase(z);
    it.erase(z);
    EXPECT_FALSE(tm.find(z));
    EXPECT_FALSE(it.find(z));
    if (reinterpret_cast<void *>(debugval) == z) found = false;
    if (found) {
      void *p = reinterpret_cast<void *>(debugval);
      EXPECT_EQ(m[p], tm.find(p)->second);
      EXPECT_EQ(m[p], *it.find(p));
      ASSERT(m[p] == tm.find(p)->second);
      ASSERT(m[p] == *it.find(p));
    }
  }
  for (auto [k, v] : m) {
    // std::cout << "k = " << k << "; v = " << v << "\n";
    EXPECT_TRUE(tm.find(k));
    EXPECT_TRUE(it.find(k));
    EXPECT_EQ(tm.find(k)->second, v);
    EXPECT_EQ(*it.find(k), v);
  }
}
