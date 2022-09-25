#include "../include/BitSets.hpp"
#include "../include/Graphs.hpp"
#include "../include/Math.hpp"
#include "Macro.hpp"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <ranges>
#include <utility>

struct MockVertex {
    BitSet inNeighbors;
    BitSet outNeighbors;
    bool visited{false};
    bool wasVisited() const { return visited; }
    void visit() { visited = true; }
    void unVisit() { visited = false; }
};

struct MockGraph {
    llvm::SmallVector<MockVertex> vertices;
    size_t getNumVertices() const { return vertices.size(); }
    size_t maxVertexId() const { return vertices.size(); }
    // BitSet vertexIds() const { return BitSet::dense(getNumVertices()); }
    Range<size_t, size_t> vertexIds() const { return _(0, getNumVertices()); }
    // BitSet &vertexIds() { return vids; }
    BitSet &inNeighbors(size_t i) { return vertices[i].inNeighbors; }
    BitSet &outNeighbors(size_t i) { return vertices[i].outNeighbors; }
    const BitSet &inNeighbors(size_t i) const {
        return vertices[i].inNeighbors;
    }
    const BitSet &outNeighbors(size_t i) const {
        return vertices[i].outNeighbors;
    }
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }
    bool wasVisited(size_t i) const { return vertices[i].wasVisited(); }
    void visit(size_t i) { vertices[i].visit(); }
    void unVisit(size_t i) { vertices[i].unVisit(); }
    MockVertex &operator[](size_t i) { return vertices[i]; }
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

static_assert(Graph::Graph<MockGraph>);

template <typename T> struct Equal {
    T x;
    bool operator()(T y) const { return x == y; }
};
template <typename T> static Equal<T> equals(T x) { return Equal<T>{x}; }

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
    Graph::print(G);
    auto scc0 = Graph::stronglyConnectedComponents(G);
    auto scc1 = Graph::stronglyConnectedComponents(G);
    EXPECT_EQ(scc0, scc1);
    SHOWLN(scc0.size());
    for (auto &v : scc0)
        std::cout << "SCC: " << v << std::endl;
    // NOTE: currently using inNeighbors instead of outNeighbors, so in
    // topological order.
    EXPECT_EQ(scc0[0].size(), 1);
    EXPECT_EQ(scc0[1].size(), 3);
    EXPECT_EQ(scc0[2].size(), 3);

    EXPECT_TRUE(scc0[0][0]);
    EXPECT_TRUE(std::ranges::any_of(scc0[0], equals(0)));

    EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(2)));
    EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(5)));
    EXPECT_TRUE(std::ranges::any_of(scc0[1], equals(6)));

    EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(1)));
    EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(3)));
    EXPECT_TRUE(std::ranges::any_of(scc0[2], equals(4)));
}
