#include "../include/BitSets.hpp"
#include "../include/Graphs.hpp"
#include "../include/Math.hpp"
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
    BitSet vids;
    size_t getNumVertices() const { return vertices.size(); }
    size_t maxVertexId() const { return vertices.size(); }
    // BitSet vertexIds() const { return BitSet::dense(getNumVertices()); }
    Range<size_t, size_t> vertexIds() const { return _(0, getNumVertices()); }
    // BitSet &vertexIds() { return vids; }
    BitSet &inNeighbors(size_t i) { return vertices[i].inNeighbors; }
    BitSet &outNeighbors(size_t i) { return vertices[i].outNeighbors; }
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }
    bool wasVisited(size_t i) const { return vertices[i].wasVisited(); }
    void visit(size_t i) { vertices[i].visit(); }
    void unVisit(size_t i) { vertices[i].unVisit(); }
};
template <> struct std::iterator_traits<MockGraph> {
    using difference_type = ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using value_type = MockVertex;
    using reference_type = MockVertex &;
    using pointer_type = MockVertex *;
};

static_assert(Graph::Graph<MockGraph>);

TEST(GraphTest, BasicAssertions) {}
