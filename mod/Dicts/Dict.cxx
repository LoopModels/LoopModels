#ifdef USE_MODULE
module;
#else
#pragma once
#endif
// includes needed for explicit instantiation
// #include <boost/unordered/concurrent_flat_map.hpp>
// #include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Value.h>

#ifndef USE_MODULE
#include "Alloc/Mallocator.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#else
export module IR:Dict;

import Allocator;
import :Node;
import :Instruction;
#endif

#ifdef USE_MODULE
export namespace dict {
#else
namespace dict {
#endif

template <typename K>
using set = boost::unordered_flat_set<K, boost::hash<K>, std::equal_to<K>,
                                      alloc::Mallocator<K>>;
template <typename K, typename V>
using map = boost::unordered_flat_map<K, V, boost::hash<K>, std::equal_to<K>,
                                      alloc::Mallocator<std::pair<const K, V>>>;

} // namespace dict

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
struct LLVMIRBuilder {
  dict::map<llvm::Value *, Value *> *llvmToInternalMap_;
  // boost::unordered_flat_map<llvm::Value *, Value *> *llvmToInternalMap_;
  llvm::LoopInfo *LI_;
  llvm::ScalarEvolution *SE_;
  auto operator[](llvm::Value *v) const -> Value * {
    auto f = llvmToInternalMap_->find(v);
    if (f != llvmToInternalMap_->end()) return f->second;
    return nullptr;
  }
};
} // namespace IR
// template class dict::map<llvm::Value*,IR::Value*>;

/*
template class boost::unordered_flat_map<
  llvm::Value *, IR::Value *, boost::hash<llvm::Value *>,
  std::equal_to<llvm::Value *>,
  alloc::Mallocator<std::pair<llvm::Value *const, IR::Value *>>>;
template class boost::unordered_flat_map<
  IR::Value *, ptrdiff_t, boost::hash<IR::Value *>, std::equal_to<IR::Value *>,
  alloc::Mallocator<std::pair<IR::Value *const, ptrdiff_t>>>;
template class boost::unordered_flat_map<
  IR::Instruction *, IR::Instruction *, boost::hash<IR::Instruction *>,
  std::equal_to<IR::Instruction *>,
  alloc::Mallocator<std::pair<IR::Instruction *const, IR::Instruction *>>>;
template class boost::unordered_flat_map<
  IR::InstByValue, IR::Compute *, boost::hash<IR::InstByValue>,
  std::equal_to<IR::InstByValue>,
  alloc::Mallocator<std::pair<const IR::InstByValue, IR::Compute *>>>;
template class boost::unordered_flat_map<
  IR::LoopInvariant::Identifier, IR::LoopInvariant *,
  boost::hash<IR::LoopInvariant::Identifier>,
  std::equal_to<IR::LoopInvariant::Identifier>,
  alloc::Mallocator<
    std::pair<const IR::LoopInvariant::Identifier, IR::LoopInvariant *>>>;

template class boost::unordered_flat_set<
  llvm::BasicBlock *, boost::hash<llvm::BasicBlock *>,
  std::equal_to<llvm::BasicBlock *>, alloc::Mallocator<llvm::BasicBlock *>>;
template class boost::unordered_flat_set<
  llvm::CallBase *, boost::hash<llvm::CallBase *>,
  std::equal_to<llvm::CallBase *>, alloc::Mallocator<llvm::CallBase *>>;
static_assert(
  std::same_as<boost::unordered_flat_map<
                 llvm::Value *, IR::Value *, boost::hash<llvm::Value *>,
                 std::equal_to<llvm::Value *>,
                 alloc::Mallocator<std::pair<llvm::Value *const, IR::Value *>>>,
               dict::map<llvm::Value *, IR::Value *>>);
*/
#ifdef USE_MODULE
export namespace boost {
#else
namespace boost {
#endif
inline void throw_exception(std::exception const &) { __builtin_trap(); }
};
