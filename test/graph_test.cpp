#include "Containers/BitSets.hpp"
#include "Graphs/IndexGraphs.hpp"
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <iostream>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <ranges>
#include <utility>

namespace poly {
using containers::BitSet, math::Range, math::_;

struct MockVertex {
  BitSet<> inNeighbors;
  BitSet<> outNeighbors;
  bool visited{false};
  bool visited2{false};
  [[nodiscard]] auto wasVisited() const -> bool { return visited; }
  void visit() { visited = true; }
  void unVisit() { visited = false; }
  [[nodiscard]] auto wasVisited2() const -> bool { return visited2; }
  void visit2() { visited2 = true; }
  void unVisit2() { visited2 = false; }
};

struct MockGraph {
  llvm::SmallVector<MockVertex> vertices;
  [[nodiscard]] auto getNumVertices() const -> size_t {
    return vertices.size();
  }
  [[nodiscard]] auto maxVertexId() const -> size_t { return vertices.size(); }
  // BitSet vertexIds() const { return BitSet::dense(getNumVertices()); }
  [[nodiscard]] auto vertexIds() const -> Range<ptrdiff_t, ptrdiff_t> {
    return _(0, getNumVertices());
  }
  // BitSet &vertexIds() { return vids; }
  auto inNeighbors(ptrdiff_t i) -> BitSet<> & {
    return vertices[i].inNeighbors;
  }
  auto outNeighbors(ptrdiff_t i) -> BitSet<> & {
    return vertices[i].outNeighbors;
  }
  [[nodiscard]] auto inNeighbors(ptrdiff_t i) const -> const BitSet<> & {
    return vertices[i].inNeighbors;
  }
  [[nodiscard]] auto outNeighbors(ptrdiff_t i) const -> const BitSet<> & {
    return vertices[i].outNeighbors;
  }
  auto begin() { return vertices.begin(); }
  auto end() { return vertices.end(); }
  [[nodiscard]] auto wasVisited(ptrdiff_t i) const -> bool {
    return vertices[i].wasVisited();
  }
  void visit(ptrdiff_t i) { vertices[i].visit(); }
  void unVisit(ptrdiff_t i) { vertices[i].unVisit(); }
  auto operator[](ptrdiff_t i) -> MockVertex & { return vertices[i]; }
  void connect(ptrdiff_t parent, ptrdiff_t child) {
    MockVertex &p{vertices[parent]}, &c{vertices[child]};
    p.outNeighbors.insert(child);
    c.inNeighbors.insert(parent);
  }
};

static_assert(graphs::AbstractIndexGraph<MockGraph>);

// std::ranges::any_of not supported by libc++
auto anyEquals(auto a, std::integral auto y) -> bool {
  return std::ranges::any_of(a, [y](auto x) { return x == y; });
}

// template <typename T> struct Equal {
//     T x;
//     bool operator()(T y) const { return x == y; }
// };
// template <typename T> static Equal<T> equals(T x) { return Equal<T>{x}; }

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(StronglyConnectedComponentsTest, BasicAssertions) {
  // graph
  //      0 -> 1 <---
  //      |    |    |
  //      v    v    |
  // ---> 2 -> 3 -> 4
  // |    |
  // |    v
  // 6 <- 5
  //
  MockGraph G;
  G.vertices.resize(7);
  G.connect(0, 1);
  G.connect(0, 2);
  G.connect(1, 3);
  G.connect(2, 3);
  G.connect(2, 5);
  G.connect(3, 4);
  G.connect(4, 1);
  G.connect(5, 6);
  G.connect(6, 2);
  graphs::print(G);
  auto scc0 = graphs::stronglyConnectedComponents(G);
  auto scc1 = graphs::stronglyConnectedComponents(G);
  EXPECT_EQ(scc0, scc1);
  for (auto &v : scc0) std::cout << "SCC: " << v << "\n";
  // NOTE: currently using inNeighbors instead of outNeighbors, so in
  // topological order.
  EXPECT_EQ(scc0[0].size(), size_t(1));
  EXPECT_EQ(scc0[1].size(), size_t(3));
  EXPECT_EQ(scc0[2].size(), size_t(3));

  EXPECT_TRUE(scc0[0][0]);

  EXPECT_TRUE(anyEquals(scc0[0], ptrdiff_t(0)));

  EXPECT_TRUE(anyEquals(scc0[1], ptrdiff_t(2)));
  EXPECT_TRUE(anyEquals(scc0[1], ptrdiff_t(5)));
  EXPECT_TRUE(anyEquals(scc0[1], ptrdiff_t(6)));

  EXPECT_TRUE(anyEquals(scc0[2], ptrdiff_t(1)));
  EXPECT_TRUE(anyEquals(scc0[2], ptrdiff_t(3)));
  EXPECT_TRUE(anyEquals(scc0[2], ptrdiff_t(4)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[0], equals(0)));

  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(2)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(5)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(6)));

  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(1)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(3)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(4)));
}
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TopologicalSortTest, BasicAssertions) {
  // graph
  //  0 -> 1
  //  |    |
  //  v    v
  //  2 -> 3 -> 4
  MockGraph G;
  G.vertices.resize(7);
  G.connect(0, 1);
  G.connect(0, 2);
  G.connect(1, 3);
  G.connect(2, 3);
  G.connect(3, 4);
  graphs::print(G);
  auto ts = graphs::topologicalSort(G);
  EXPECT_EQ(ts.size(), G.getNumVertices());
  EXPECT_EQ(ts[0], 0);
  if (ts[1] == 1) {
    EXPECT_EQ(ts[2], 2);
  } else {
    EXPECT_EQ(ts[1], 2);
    EXPECT_EQ(ts[2], 1);
  }
  EXPECT_EQ(ts[3], 3);
  EXPECT_EQ(ts[4], 4);
}
} // namespace poly
