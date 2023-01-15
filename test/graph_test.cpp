#include "BitSets.hpp"
#include "Graphs.hpp"
#include "Math/Math.hpp"
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <ranges>
#include <utility>

struct MockVertex {
  BitSet<> inNeighbors;
  BitSet<> outNeighbors;
  bool visited{false};
  [[nodiscard]] auto wasVisited() const -> bool { return visited; }
  void visit() { visited = true; }
  void unVisit() { visited = false; }
};

struct MockGraph {
  llvm::SmallVector<MockVertex> vertices;
  [[nodiscard]] auto getNumVertices() const -> size_t {
    return vertices.size();
  }
  [[nodiscard]] auto maxVertexId() const -> size_t { return vertices.size(); }
  // BitSet vertexIds() const { return BitSet::dense(getNumVertices()); }
  [[nodiscard]] auto vertexIds() const -> Range<size_t, size_t> {
    return _(0, getNumVertices());
  }
  // BitSet &vertexIds() { return vids; }
  auto inNeighbors(size_t i) -> BitSet<> & { return vertices[i].inNeighbors; }
  auto outNeighbors(size_t i) -> BitSet<> & { return vertices[i].outNeighbors; }
  [[nodiscard]] auto inNeighbors(size_t i) const -> const BitSet<> & {
    return vertices[i].inNeighbors;
  }
  [[nodiscard]] auto outNeighbors(size_t i) const -> const BitSet<> & {
    return vertices[i].outNeighbors;
  }
  auto begin() { return vertices.begin(); }
  auto end() { return vertices.end(); }
  [[nodiscard]] auto wasVisited(size_t i) const -> bool {
    return vertices[i].wasVisited();
  }
  void visit(size_t i) { vertices[i].visit(); }
  void unVisit(size_t i) { vertices[i].unVisit(); }
  auto operator[](size_t i) -> MockVertex & { return vertices[i]; }
  void connect(size_t parent, size_t child) {
    MockVertex &p{vertices[parent]}, &c{vertices[child]};
    p.outNeighbors.insert(child);
    c.inNeighbors.insert(parent);
  }
};
template <> struct std::iterator_traits<MockGraph> {
  using difference_type = ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;
  using value_type = MockVertex;
  using reference_type = MockVertex &;
  using pointer_type = MockVertex *;
};

static_assert(Graphs::AbstractGraph<MockGraph>);

// std::ranges::any_of not supported by libc++
auto anyEquals(auto a, std::integral auto y) -> bool {
  for (auto x : a)
    if (x == y) return true;
  return false;
}

// template <typename T> struct Equal {
//     T x;
//     bool operator()(T y) const { return x == y; }
// };
// template <typename T> static Equal<T> equals(T x) { return Equal<T>{x}; }

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(GraphTest, BasicAssertions) {
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
  Graphs::print(G);
  auto scc0 = Graphs::stronglyConnectedComponents(G);
  auto scc1 = Graphs::stronglyConnectedComponents(G);
  EXPECT_EQ(scc0, scc1);
  for (auto &v : scc0) llvm::errs() << "SCC: " << v << "\n";
  // NOTE: currently using inNeighbors instead of outNeighbors, so in
  // topological order.
  EXPECT_EQ(scc0[0].size(), size_t(1));
  EXPECT_EQ(scc0[1].size(), size_t(3));
  EXPECT_EQ(scc0[2].size(), size_t(3));

  EXPECT_TRUE(scc0[0][0]);

  EXPECT_TRUE(anyEquals(scc0[0], size_t(0)));

  EXPECT_TRUE(anyEquals(scc0[1], size_t(2)));
  EXPECT_TRUE(anyEquals(scc0[1], size_t(5)));
  EXPECT_TRUE(anyEquals(scc0[1], size_t(6)));

  EXPECT_TRUE(anyEquals(scc0[2], size_t(1)));
  EXPECT_TRUE(anyEquals(scc0[2], size_t(3)));
  EXPECT_TRUE(anyEquals(scc0[2], size_t(4)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[0], equals(0)));

  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(2)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(5)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(6)));

  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(1)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(3)));
  // EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(4)));
}
