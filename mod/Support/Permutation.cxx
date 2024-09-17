#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>

#ifndef USE_MODULE
#include "Containers/TinyVector.cxx"
#include "Math/Ranges.cxx"
#include "Math/MatrixDimensions.cxx"
#include "Utilities/Invariant.cxx"
#include "Numbers/Int8.cxx"
#include "Graphs/IndexGraphs.cxx"
#include "Containers/BitSets.cxx"
#include "Math/ArrayConcepts.cxx"
#include "Math/Array.cxx"
#else
export module Permutation;
import Array;
import ArrayConcepts;
import BitSet;
import IndexGraph;
import Int8;
import Invariant;
import MatDim;
import Range;
import TinyVector;
#endif

#ifdef USE_MODULE
export namespace utils {
#else
namespace utils {
#endif
using math::_;
using ::numbers::i8;

// Supports loop nests up to 15 deep
// Assumes 1-based indexing for loops; 0 refers to top-level
// Assumed order outer
struct LoopPermutation {
  uint64_t data{0};
  struct Iterator {
    uint64_t data;
    constexpr auto operator==(Iterator other) const -> bool {
      return data == other.data;
    }
    constexpr auto operator==(math::End) const -> bool { return data == 0; }
    constexpr auto operator++() -> Iterator & {
      data >>= 4;
      return *this;
    }
    constexpr auto operator++(int) -> Iterator {
      uint64_t old{data};
      data >>= 4;
      return {old};
    }
    constexpr auto operator*() const -> uint64_t { return data & 0x0f; }
  };
  [[nodiscard]] constexpr auto size() const -> size_t {
    return size_t(16) - (std::countl_zero(data) >> 2);
  }
  constexpr void push_first(uint64_t x) {
    utils::invariant(x < 16);
    data <<= 4;
    data |= x;
  }
  [[nodiscard]] constexpr auto begin() const -> Iterator { return {data}; }
  static constexpr auto end() -> math::End { return {}; }
  struct Reference {
    uint64_t &d;
    ptrdiff_t i;
    constexpr operator uint64_t() const { return (d >> (4 * i)) & 0x0f; }
    constexpr auto operator=(uint64_t x) -> Reference & {
      d = (d ^ (0x0f << (4 * i))) | ((x & 0x0f) << (4 * i));
      return *this;
    }
  };
  constexpr auto operator[](ptrdiff_t i) const -> uint64_t {
    return (data >> (4 * i)) & 0x0f;
  }
  constexpr auto operator[](ptrdiff_t i) -> Reference { return {data, i}; }
};

// Permutation iterator using Heap's algorithm
// https://en.wikipedia.org/wiki/Heap%27s_algorithm
// This is the non-recursive variant, with the `while` loop moved
// into the iterator increment.
template <math::LinearlyIndexable V = containers::TinyVector<i8, 15, int8_t>>
struct PermutationIterator {
  V v_{};
  V c_{};
  ptrdiff_t i_{1};
  constexpr PermutationIterator(i8 len) {
    utils::invariant(len < 16);
    for (i8 j = i8(0); j < len; ++j) {
      v_.push_back(j);
      c_.push_back(i8(0));
    }
  }
  constexpr PermutationIterator(V v, V c) : v_(v), c_(c) {
    utils::invariant(v_.size() == c_.size());
  }
  constexpr auto operator*() const -> const V & { return v_; }
  constexpr auto operator++() -> PermutationIterator & {
    auto sz = v_.size();
    invariant(c_.size() == sz);
    while ((i_ < sz) && (c_[i_] >= i_)) c_[i_++] = i8(0);
    if (i_ < sz) {
      if (i_ & 1) std::swap(v_[std::ptrdiff_t(c_[i_])], v_[i_]);
      else std::swap(v_[0], v_[i_]);
      ++c_[i_];
      i_ = 1;
    }
    return *this;
  }
  constexpr auto operator==(math::End) const -> bool { return i_ >= v_.size(); }
};
struct Permutations {
  i8 len_;
  constexpr Permutations(ptrdiff_t x) : len_(i8(x)) { invariant(x < 16); }
  [[nodiscard]] constexpr auto begin() const -> PermutationIterator<> {
    return {len_};
  }
  static constexpr auto end() -> math::End { return {}; }
};

using LoopSet = containers::BitSet<std::array<uint16_t, 1>>;

template <std::unsigned_integral U> constexpr auto flipMask(U u, U count) -> U {
  U on = (U(1) << count) - U(1);
  return (~u) & on;
}

struct IndexRelationGraph {
  containers::TinyVector<LoopSet, 15, int16_t> data_;

  IndexRelationGraph(int16_t numLoops) { data_.resize(numLoops); };

  void add_edge(ptrdiff_t i, ptrdiff_t j) { data_[i].insert(j); }
  void add_edges(ptrdiff_t i, LoopSet j) { data_[i] |= j; }
  auto inNeighbors(ptrdiff_t i) -> LoopSet & { return data_[i]; }
  [[nodiscard]] auto inNeighbors(ptrdiff_t i) const -> LoopSet {
    return data_[i];
  }
  [[nodiscard]] auto getNumVertices() const -> unsigned { return data_.size(); }
  [[nodiscard]] auto maxVertexId() const -> unsigned {
    return getNumVertices() - 1;
  }
  [[nodiscard]] auto vertexIds() const { return _(0, data_.size()); }
};

struct LoopPermutations {
  using SubPerms = containers::TinyVector<LoopSet, 15, int16_t>;
  SubPerms subperms_;
  // To iterate, we're imagining a nested loop, with nesting depth equal to
  // `subperms.size()`. Each level of the loop nest uses Heap's algorithm to
  // iterate over all permutations of the corresponding element `subperms`.
  struct Iterator {
    using State = containers::TinyVector<i8, 15, int8_t>;
    State state_;              // `v` field in `PermutationIterator`
    State iterator_positions_; // `c` field in `PermutationIterator`
    SubPerms subperms_;
    bool done_{false};
    // return `State` by value to avoid modification risk, and because it is
    // trivially copyable
    constexpr Iterator(SubPerms sp) : subperms_(sp) {
      for (LoopSet ls : sp) {
        for (ptrdiff_t i : ls) {
          invariant(i < 16);
          state_.push_back(i8(i));
          iterator_positions_.push_back(i8(0));
        }
      }
    }
    constexpr auto operator*() const -> State { return state_; }
    constexpr auto operator++() -> Iterator & {
      // lvl is the level we're incrementing. Here, 0 refers to the deepest
      // level. If a perm is at its end, we increment to ascend.
      if (done_) return *this;
      ptrdiff_t lvl{0}, offset{0}, n_perms = subperms_.size();
      while (true) {
        if (++PermutationIterator<math::MutPtrVector<i8>>{
              permIterator(lvl, offset)} == math::End{}) {
          ptrdiff_t prev_lvl = lvl++;
          done_ = lvl == n_perms;
          if (done_) return *this;
          offset = resetLevel(prev_lvl, offset);
        } else return *this;
      }
    }
    constexpr auto operator==(math::End) const -> bool { return done_; }

  private:
    constexpr auto permIterator(ptrdiff_t lvl, ptrdiff_t offset)
      -> PermutationIterator<math::MutPtrVector<i8>> {
      ptrdiff_t L = subperms_[lvl].size();
      return {math::MutPtrVector<i8>{state_.begin() + offset, math::length(L)},
              math::MutPtrVector<i8>{iterator_positions_.begin() + offset,
                                     math::length(L)}};
    }
    constexpr auto resetLevel(ptrdiff_t lvl, ptrdiff_t offset) -> ptrdiff_t {
      // when reseting the level, we don't actually need to reset the state
      // we can use the last ending state as the initial state, iterating
      // through its permutations from there.
      ptrdiff_t sz = subperms_[lvl].size();
      for (ptrdiff_t i = 0; i < sz; ++i)
        iterator_positions_[i + offset] = i8(0);
      return sz + offset;
      // for (ptrdiff_t i : subperms[lvl]) {
      //   invariant(i < 16);
      //   state[offset] = int8_t(i);
      //   iterator_positions[offset++] = 0;
      // }
      // return offset;
    }
  };
  [[nodiscard]] constexpr auto empty() const -> bool {
    return subperms_.empty();
  }
  [[nodiscard]] constexpr auto size() const -> ptrdiff_t {
    return subperms_.size();
  }
  [[nodiscard]] constexpr auto begin() const -> Iterator { return {subperms_}; }
  static constexpr auto end() -> math::End { return {}; }
};

// struct LoopPerm {
//   using data_type = containers::TinyVector<LoopSet, 15, int16_t>;
//   data_type data{};

//   static constexpr auto onLoopMask(uint16_t loopDepth) -> uint16_t {
//     return (uint16_t(1) << loopDepth) - uint16_t(1);
//   }
// };

} // namespace utils