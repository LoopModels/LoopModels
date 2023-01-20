#pragma once

#include "Containers/BumpMap.hpp"
#include <llvm/ADT/DenseSet.h>

template <typename ValueT, typename Alloc = BumpAlloc<>,
          typename ValueInfoT = llvm::DenseMapInfo<ValueT>>
class BumpSet : public llvm::detail::DenseSetImpl<
                  ValueT,
                  BumpMap<ValueT, llvm::detail::DenseSetEmpty, Alloc,
                          ValueInfoT, llvm::detail::DenseSetPair<ValueT>>,
                  ValueInfoT> {
  using BaseT = llvm::detail::DenseSetImpl<
    ValueT,
    BumpMap<ValueT, llvm::detail::DenseSetEmpty, Alloc, ValueInfoT,
            llvm::detail::DenseSetPair<ValueT>>,
    ValueInfoT>;

public:
  using BaseT::BaseT;
};
