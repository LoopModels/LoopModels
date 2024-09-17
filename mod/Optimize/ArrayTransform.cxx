#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include "Containers/Pair.cxx"
#include "Math/Array.cxx"
#include "Math/Indexing.cxx"
#include <cstddef>
#include <cstdint>
#else
export module ArrayTransform;
import Array;
import Pair;
import STL;
#endif

#ifndef USE_MODULE
namespace CostModeling {
#else
export namespace CostModeling {
#endif

// The mapping of Array access to loops is itself a graph.
// We may have an array accessed many times at different places in the tree, and
// grouped with other arrays in orth or conv dims that have different patterns.
// Thus, we may want to apply different transforms at different places.
// We may wish to reuse transforms, which reduces their cost.
// TODO: how can we get reuse with different cache blocking factors?
// We may want to specify integer stride multiples within cache optimization.
// Consider simple NN:
// B = f.(A*W .+ a);
// C = g.(B*X .+ b);
// We may wish to reuse the `B` pack between both. If `B` is a local
// non-escaping temporary, we'd like to change the data layout altogether.
//
// TODO: The most difficult aspect of this is that it requires joint
// optimization. We can potentially joint optimize sub-loops...

struct ArrayTransform {
  uint8_t vectorized_ : 1;     ///< Vector or matrix load/stores?
  uint8_t packed_ : 1;         ///< Do we pack the array?
  uint8_t pack_l2_stride_ : 6; ///< If packed, what is the stride between
                               ///< successive element accesses? `Stride=1`
                               ///< means that they're contiguous, `Stride=2`
                               ///< that they're two apart, etc. The usefulness
                               ///< of this is that we can place successive
                               ///< accesses in separate cache lines, and then
                               ///< repeatedly stripe across an array to keep it
                               ///< in the most recently used position.
};
}
