#ifdef USE_MODULE
module;
#else
#pragma once
#endif
#include <algorithm>
#include <cstddef>

#ifndef USE_MODULE
#include "Containers/Pair.cxx"
#include "Math/ManagedArray.cxx"
#include "Math/ArrayConcepts.cxx"
#else
export module BipartiteMarch;

import ArrayConcepts;
import ManagedArray;
import Pair;
#endif

using math::Matrix, math::Vector, math::MutPtrVector, math::MutPtrMatrix;

#ifdef USE_MODULE
export namespace graph {
#else
namespace graph {
#endif

// NOLINTNEXTLINE(misc-no-recursion)
inline auto bipartiteMatch(MutPtrMatrix<bool> bpGraph, int u,
                           MutPtrVector<bool> seen,
                           MutPtrVector<int> matchR) -> bool {
  // Try every job one by one
  for (int v = 0; v < bpGraph.numRow(); v++) {
    // If applicant u is interested in
    // job v and v is not visited
    if (bpGraph[v, u] && !seen[v]) {
      // Mark v as visited
      seen[v] = true;

      // If job 'v' is not assigned to an
      // applicant OR previously assigned
      // applicant for job v (which is matchR[v])
      // has an alternate job available.
      // Since v is marked as visited in
      // the above line, matchR[v] in the following
      // recursive call will not get job 'v' again
      if (matchR[v] < 0 || bipartiteMatch(bpGraph, matchR[v], seen, matchR)) {
        matchR[v] = u;
        return true;
      }
    }
  }
  return false;
}
/// Returns maximum number
/// of matching from M to N
inline auto maxBipartiteMatch(Matrix<bool> &bpGraph)
  -> containers::Pair<size_t, Vector<int>> {
  // An array to keep track of the
  // applicants assigned to jobs.
  // The value of matchR[i] is the
  // applicant number assigned to job i,
  // the value -1 indicates nobody is
  // assigned.
  auto [N, M] = math::shape(bpGraph);
  containers::Pair<size_t, Vector<int>> res{0, {math::length(N), -1}};
  size_t &result = res.first;
  Vector<int> &matchR{res.second};
  if (M) {
    Vector<bool> seen{math::length(N)};
    // Count of jobs assigned to applicants
    for (int u = 0; u < M; u++) {
      // Mark all jobs as not seen
      // for next applicant.
      std::fill(seen.begin(), seen.end(), false);

      // Find if the applicant 'u' can get a job
      if (bipartiteMatch(bpGraph, u, seen, matchR)) result++;
    }
  }
  return res;
}
} // namespace graph