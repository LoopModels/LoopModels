#pragma once
#include "./MemoryAccess.hpp"
#include "Math.hpp"
#include <cstdint>

/// Represents a memory access that has been rotated according to some affine
/// transform.
// clang-format off
/// Return the memory accesses after applying the Schedule.
/// Let
/// \f{eqnarray*}{
/// D &=& \text{the dimension of the array}\\ %
/// N &=& \text{depth of the loop nest}\\ %
/// V &=& \text{runtime variables}\\ %
/// \textbf{i}\in\mathbb{R}^N &=& \text{the old index vector}\\ %
/// \textbf{j}\in\mathbb{R}^N &=& \text{the new index vector}\\ %
/// \textbf{x}\in\mathbb{R}^D &=& \text{the indices into the array}\\ %
/// \textbf{M}\in\mathbb{R}^{N \times D} &=& \text{map from loop ind vars to array indices}\\ %
/// \boldsymbol{\Phi}\in\mathbb{R}^{N \times N} &=& \text{the schedule matrix}\\ %
/// \boldsymbol{\Phi}_*\in\mathbb{R}^{N \times N} &=& \textbf{E}\boldsymbol{\Phi}\\ %
/// \boldsymbol{\omega}\in\mathbb{R}^N &=& \text{the offset vector}\\ %
/// \textbf{c}\in\mathbb{R}^{N} &=& \text{the constant offset vector}\\ %
/// \textbf{C}\in\mathbb{R}^{N \times V} &=& \text{runtime variable coefficient matrix}\\ %
/// \textbf{s}\in\mathbb{R}^V &=& \text{the symbolic runtime variables}\\ %
/// \f}
/// 
/// Where \f$\textbf{E}\f$ is an [exchange matrix](https://en.wikipedia.org/wiki/Exchange_matrix).
/// The rows of \f$\boldsymbol{\Phi}\f$ are sorted from the outermost loop to
/// the innermost loop, the opposite ordering used elsewhere. \f$\boldsymbol{\Phi}_*\f$
/// corrects this.
/// We have
/// \f{eqnarray*}{
/// \textbf{j} &=& \boldsymbol{\Phi}_*\textbf{i} + \boldsymbol{\omega}\\ %
/// \textbf{i} &=& \boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right)\\ %
/// \textbf{x} &=& \textbf{M}'\textbf{i} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{M}'_* &=& \textbf{M}'\boldsymbol{\Phi}_*^{-1}\\ %
/// \textbf{x} &=& \textbf{M}'_*\left(j - \boldsymbol{\omega}\right) + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{x} &=& \textbf{M}'_*j - \textbf{M}'_*\boldsymbol{\omega} + \textbf{c} + \textbf{Cs} \\ %
/// \textbf{c}_* &=& \textbf{c} - \textbf{M}'_*\boldsymbol{\omega} \\ %
/// \textbf{x} &=& \textbf{M}'_*j + \textbf{c}_* + \textbf{Cs} \\ %
/// \f}
/// Therefore, to update the memory accesses, we must simply compute the updated
/// \f$\textbf{c}_*\f$ and \f$\textbf{M}'_*\f$.
/// We can also test for the case where \f$\boldsymbol{\Phi} = \textbf{E}\f$, or equivalently that $\textbf{E}\boldsymbol{\Phi} = \boldsymbol{\Phi}_* = \textbf{I}$.
// clang-format on
struct ScheduledMemoryAccess {
  [[no_unique_address]] MemoryAccess *access;
  // may be `false` while `access->isStore()==true`
  // which indicates a reload from this address.
  [[no_unique_address]] size_t denominator{1};
  [[no_unique_address]] bool isStore;
  ScheduledMemoryAccess(MemoryAccess *access, PtrMatrix<int64_t> Pinv,
                        int64_t denominator, PtrVector<int64_t> omega,
                        bool isStore)
    : access(access), isStore(isStore) {
    IntMatrix MStarT = access->indexMatrix().transpose() * Pinv;
    Vector<int64_t> omegaStar = access->offsetMatrix()(_, 0) - MStarT * omega;
  }
};
