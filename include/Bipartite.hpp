#pragma once
#include "./Math.hpp"

bool bipartiteMatch(Matrix<bool, 0, 0> &bpGraph, size_t u,
                    llvm::SmallVectorImpl<bool> &seen,
                    llvm::SmallVectorImpl<int> &matchR) {
    // Try every job one by one
    for (size_t v = 0; v < bpGraph.numRow(); v++) {
        // If applicant u is interested in
        // job v and v is not visited
        if (bpGraph(v, u) && !seen[v]) {
            // Mark v as visited
            seen[v] = true;

            // If job 'v' is not assigned to an
            // applicant OR previously assigned
            // applicant for job v (which is matchR[v])
            // has an alternate job available.
            // Since v is marked as visited in
            // the above line, matchR[v] in the following
            // recursive call will not get job 'v' again
            if (matchR[v] < 0 ||
                bipartiteMatch(bpGraph, matchR[v], seen, matchR)) {
                matchR[v] = u;
                return true;
            }
        }
    }
    return false;
}
// Returns maximum number
// of matching from M to N
std::pair<size_t, llvm::SmallVector<int>>
maxBipartiteMatch(Matrix<bool, 0, 0> &bpGraph) {
    // An array to keep track of the
    // applicants assigned to jobs.
    // The value of matchR[i] is the
    // applicant number assigned to job i,
    // the value -1 indicates nobody is
    // assigned.
    auto [N, M] = bpGraph.size();
    llvm::SmallVector<int> matchR(N, -1);
    size_t result = 0;
    if (M){
        llvm::SmallVector<bool> seen(N);
        // Count of jobs assigned to applicants
        for (size_t u = 0; u < M; u++) {
            // Mark all jobs as not seen
            // for next applicant.
            std::fill(seen.begin(), seen.end(), false);

            // Find if the applicant 'u' can get a job
            if (bipartiteMatch(bpGraph, u, seen, matchR))
                result++;
        }
    }
    return std::make_pair(result, matchR);
}
